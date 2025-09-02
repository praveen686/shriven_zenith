// ============================================================================
// binance_auth.cpp - Binance API Authentication Implementation
// ============================================================================

#include "trading/auth/binance/binance_auth.h"
#include "common/logging.h"
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <chrono>

namespace Trading::Binance {

// Static member initialization
BinanceAuth* BinanceAuth::instance_ = nullptr;

// ============================================================================
// Helper Functions
// ============================================================================

// Buffer tracking structure
struct BufferContext {
    char* data;
    size_t size;
    size_t max_size;
};

// CURL write callback for tracked buffers
static size_t writeCallbackTracked(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    BufferContext* ctx = static_cast<BufferContext*>(userp);
    
    // Check buffer space
    if (ctx->size + total_size >= ctx->max_size - 1) {
        LOG_WARN("Response truncated: %zu + %zu >= %zu", ctx->size, total_size, ctx->max_size);
        total_size = ctx->max_size - ctx->size - 1;
    }
    
    if (total_size > 0) {
        memcpy(ctx->data + ctx->size, contents, total_size);
        ctx->size += total_size;
        ctx->data[ctx->size] = '\0';
    }
    
    return size * nmemb;  // Always return original size to keep CURL happy
}

// CURL write callback for simple buffers (backward compatibility)
static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    char* buffer = static_cast<char*>(userp);
    
    // Find current length (for small buffers only)
    size_t current_len = strlen(buffer);
    
    // Safety limit
    const size_t max_size = 65536;  // 64KB for simple responses
    if (current_len + total_size >= max_size - 1) {
        total_size = max_size - current_len - 1;
    }
    
    if (total_size > 0) {
        memcpy(buffer + current_len, contents, total_size);
        buffer[current_len + total_size] = '\0';
    }
    
    return size * nmemb;
}

// ============================================================================
// BinanceAuth Implementation
// ============================================================================

bool BinanceAuth::init(const Credentials& creds, const char* api_endpoint) noexcept {
    if (instance_ != nullptr) {
        LOG_WARN("BinanceAuth already initialized");
        return true;
    }
    
    instance_ = new BinanceAuth();  // AUDIT_IGNORE: Init-time only
    
    // Copy credentials
    strncpy(instance_->credentials_.api_key, creds.api_key, 
            sizeof(instance_->credentials_.api_key) - 1);
    strncpy(instance_->credentials_.api_secret, creds.api_secret, 
            sizeof(instance_->credentials_.api_secret) - 1);
    
    // Set API endpoint (default to production if not specified)
    if (api_endpoint && api_endpoint[0] != '\0') {
        strncpy(instance_->api_endpoint_, api_endpoint, 
                sizeof(instance_->api_endpoint_) - 1);
    } else {
        // Default to production
        strncpy(instance_->api_endpoint_, "https://api.binance.com", 
                sizeof(instance_->api_endpoint_) - 1);
    }
    
    instance_->initialized_.store(true);
    
    LOG_INFO("BinanceAuth initialized (endpoint=%s)", instance_->api_endpoint_);
    
    // Test connectivity and authenticate
    if (instance_->testConnectivity()) {
        LOG_INFO("Binance API connectivity test passed");
        instance_->authenticated_.store(true);
        return true;
    }
    
    LOG_ERROR("Binance API connectivity test failed");
    return false;
}

void BinanceAuth::shutdown() noexcept {
    if (instance_) {
        LOG_INFO("Shutting down BinanceAuth");
        delete instance_;  // AUDIT_IGNORE: Shutdown only
        instance_ = nullptr;
    }
}

bool BinanceAuth::createSignature(const char* query_string, char* signature, size_t sig_size) const noexcept {
    if (!query_string || !signature || sig_size < 65) {
        return false;
    }
    
    unsigned char hash[32];
    unsigned int hash_len = 0;
    
    HMAC(EVP_sha256(), 
         credentials_.api_secret, 
         static_cast<int>(strlen(credentials_.api_secret)),
         reinterpret_cast<const unsigned char*>(query_string), 
         strlen(query_string),
         hash, 
         &hash_len);
    
    // Convert to hex string
    for (unsigned int i = 0; i < hash_len; ++i) {
        snprintf(signature + (i * 2), 3, "%02x", hash[i]);
    }
    signature[hash_len * 2] = '\0';
    
    return true;
}

bool BinanceAuth::getServerTime(int64_t* server_time_ms) noexcept {
    if (!server_time_ms) return false;
    
    CURL* curl = curl_easy_init();  // AUDIT_IGNORE: Init-time only
    if (!curl) {
        LOG_ERROR("Failed to initialize CURL");
        return false;
    }
    
    char url[256];
    snprintf(url, sizeof(url), "%s/api/v3/time", getBaseUrl());
    
    char response[1024];
    memset(response, 0, sizeof(response));
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("CURL request failed: %s", curl_easy_strerror(res));
        return false;
    }
    
    // Parse JSON response {"serverTime":1234567890123}
    char* time_str = strstr(response, "\"serverTime\":");
    if (time_str) {
        time_str += 13;
        *server_time_ms = strtoll(time_str, nullptr, 10);
        return true;
    }
    
    return false;
}

void BinanceAuth::enforceRateLimit() noexcept {
    auto now_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
    
    uint64_t last_request = last_request_time_ns_.load();
    if (now_ns - last_request < MIN_REQUEST_INTERVAL_NS) {
        uint64_t sleep_ns = MIN_REQUEST_INTERVAL_NS - (now_ns - last_request);
        struct timespec ts;
        ts.tv_sec = static_cast<time_t>(sleep_ns / 1000000000);
        ts.tv_nsec = static_cast<long>(sleep_ns % 1000000000);
        nanosleep(&ts, nullptr);
    }
    
    last_request_time_ns_.store(now_ns);
}

bool BinanceAuth::authenticate() noexcept {
    if (!initialized_.load()) {
        LOG_ERROR("BinanceAuth not initialized");
        return false;
    }
    
    // For Binance, authentication is just having valid API keys
    // We test by making a signed request
    char response[8192];
    if (fetchAccountInfo(response, sizeof(response))) {
        authenticated_.store(true);
        LOG_INFO("Binance authentication successful");
        return true;
    }
    
    LOG_ERROR("Binance authentication failed");
    return false;
}

bool BinanceAuth::testConnectivity() noexcept {
    CURL* curl = curl_easy_init();  // AUDIT_IGNORE: Init-time only
    if (!curl) {
        LOG_ERROR("Failed to initialize CURL");
        return false;
    }
    
    char url[256];
    snprintf(url, sizeof(url), "%s/api/v3/ping", getBaseUrl());
    
    char response[256];
    memset(response, 0, sizeof(response));
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("Connectivity test failed: %s", curl_easy_strerror(res));
        return false;
    }
    
    return http_code == 200;
}

bool BinanceAuth::fetchAccountInfo(char* response, size_t response_size) noexcept {
    if (!response || response_size == 0) return false;
    
    enforceRateLimit();
    
    CURL* curl = curl_easy_init();  // AUDIT_IGNORE: API call
    if (!curl) {
        LOG_ERROR("Failed to initialize CURL");
        return false;
    }
    
    // Get current timestamp
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // Build query string
    char query[512];
    snprintf(query, sizeof(query), "timestamp=%lld&recvWindow=5000", 
             static_cast<long long>(now_ms));
    
    // Create signature
    char signature[65];
    if (!createSignature(query, signature, sizeof(signature))) {
        curl_easy_cleanup(curl);
        return false;
    }
    
    // Build full URL with query and signature
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/v3/account?%s&signature=%s", 
             getBaseUrl(), query, signature);
    
    // Set headers
    struct curl_slist* headers = nullptr;
    char api_key_header[256];
    snprintf(api_key_header, sizeof(api_key_header), "X-MBX-APIKEY: %s", credentials_.api_key);
    headers = curl_slist_append(headers, api_key_header);
    
    memset(response, 0, response_size);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("Account info request failed: %s", curl_easy_strerror(res));
        return false;
    }
    
    if (http_code != 200) {
        LOG_ERROR("Account info request failed with HTTP %ld: %s", http_code, response);
        return false;
    }
    
    return true;
}

bool BinanceAuth::fetchBalances(char* response, size_t response_size) noexcept {
    // Balances are part of account info
    return fetchAccountInfo(response, response_size);
}

bool BinanceAuth::fetchLargeResponse(const char* url, char* response, size_t response_size) noexcept {
    if (!url || !response || response_size == 0) return false;
    
    CURL* curl = curl_easy_init();  // AUDIT_IGNORE: API call
    if (!curl) {
        LOG_ERROR("Failed to initialize CURL");
        return false;
    }
    
    // Clear response buffer
    memset(response, 0, response_size);
    
    // Setup buffer context for tracking
    BufferContext ctx;
    ctx.data = response;
    ctx.size = 0;
    ctx.max_size = response_size;
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallbackTracked);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);  // Longer timeout for large responses
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("Request failed: %s", curl_easy_strerror(res));
        return false;
    }
    
    if (http_code != 200) {
        LOG_ERROR("Request failed with HTTP %ld", http_code);
        return false;
    }
    
    LOG_INFO("Fetched %zu bytes", ctx.size);
    return true;
}

bool BinanceAuth::fetchExchangeInfo(char* response, size_t response_size) noexcept {
    if (!response || response_size == 0) return false;
    
    enforceRateLimit();
    
    char url[256];
    snprintf(url, sizeof(url), "%s/api/v3/exchangeInfo", getBaseUrl());
    
    // Use large response handler for exchange info
    return fetchLargeResponse(url, response, response_size);
}

bool BinanceAuth::fetchTicker24hr(const char* symbol, char* response, size_t response_size) noexcept {
    if (!response || response_size == 0) return false;
    
    enforceRateLimit();
    
    char url[512];
    if (symbol && symbol[0] != '\0') {
        snprintf(url, sizeof(url), "%s/api/v3/ticker/24hr?symbol=%s", getBaseUrl(), symbol);
    } else {
        snprintf(url, sizeof(url), "%s/api/v3/ticker/24hr", getBaseUrl());
    }
    
    // Use large response handler for all tickers (can be several MB)
    if (!symbol || symbol[0] == '\0') {
        return fetchLargeResponse(url, response, response_size);
    }
    
    // Single symbol - use simple handler
    CURL* curl = curl_easy_init();  // AUDIT_IGNORE: API call
    if (!curl) {
        LOG_ERROR("Failed to initialize CURL");
        return false;
    }
    
    memset(response, 0, response_size);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("Ticker request failed: %s", curl_easy_strerror(res));
        return false;
    }
    
    if (http_code != 200) {
        LOG_ERROR("Ticker request failed with HTTP %ld", http_code);
        return false;
    }
    
    return true;
}

bool BinanceAuth::fetchOrderBook(const char* symbol, int limit, char* response, size_t response_size) noexcept {
    if (!symbol || !response || response_size == 0) return false;
    
    enforceRateLimit();
    
    CURL* curl = curl_easy_init();  // AUDIT_IGNORE: API call
    if (!curl) {
        LOG_ERROR("Failed to initialize CURL");
        return false;
    }
    
    char url[512];
    snprintf(url, sizeof(url), "%s/api/v3/depth?symbol=%s&limit=%d", 
             getBaseUrl(), symbol, limit);
    
    memset(response, 0, response_size);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("Order book request failed: %s", curl_easy_strerror(res));
        return false;
    }
    
    if (http_code != 200) {
        LOG_ERROR("Order book request failed with HTTP %ld", http_code);
        return false;
    }
    
    return true;
}

bool BinanceAuth::fetchOpenOrders(const char* symbol, char* response, size_t response_size) noexcept {
    if (!response || response_size == 0) return false;
    
    enforceRateLimit();
    
    CURL* curl = curl_easy_init();  // AUDIT_IGNORE: API call
    if (!curl) {
        LOG_ERROR("Failed to initialize CURL");
        return false;
    }
    
    // Get current timestamp
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // Build query string
    char query[512];
    if (symbol && symbol[0] != '\0') {
        snprintf(query, sizeof(query), "symbol=%s&timestamp=%lld&recvWindow=5000", 
                 symbol, static_cast<long long>(now_ms));
    } else {
        snprintf(query, sizeof(query), "timestamp=%lld&recvWindow=5000", 
                 static_cast<long long>(now_ms));
    }
    
    // Create signature
    char signature[65];
    if (!createSignature(query, signature, sizeof(signature))) {
        curl_easy_cleanup(curl);
        return false;
    }
    
    // Build full URL
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/v3/openOrders?%s&signature=%s", 
             getBaseUrl(), query, signature);
    
    // Set headers
    struct curl_slist* headers = nullptr;
    char api_key_header[256];
    snprintf(api_key_header, sizeof(api_key_header), "X-MBX-APIKEY: %s", credentials_.api_key);
    headers = curl_slist_append(headers, api_key_header);
    
    memset(response, 0, response_size);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("Open orders request failed: %s", curl_easy_strerror(res));
        return false;
    }
    
    if (http_code != 200) {
        LOG_ERROR("Open orders request failed with HTTP %ld: %s", http_code, response);
        return false;
    }
    
    return true;
}

bool BinanceAuth::fetchAllOrders(const char* symbol, char* response, size_t response_size) noexcept {
    if (!symbol || !response || response_size == 0) return false;
    
    enforceRateLimit();
    
    CURL* curl = curl_easy_init();  // AUDIT_IGNORE: API call
    if (!curl) {
        LOG_ERROR("Failed to initialize CURL");
        return false;
    }
    
    // Get current timestamp
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // Build query string
    char query[512];
    snprintf(query, sizeof(query), "symbol=%s&timestamp=%lld&recvWindow=5000", 
             symbol, static_cast<long long>(now_ms));
    
    // Create signature
    char signature[65];
    if (!createSignature(query, signature, sizeof(signature))) {
        curl_easy_cleanup(curl);
        return false;
    }
    
    // Build full URL
    char url[1024];
    snprintf(url, sizeof(url), "%s/api/v3/allOrders?%s&signature=%s", 
             getBaseUrl(), query, signature);
    
    // Set headers
    struct curl_slist* headers = nullptr;
    char api_key_header[256];
    snprintf(api_key_header, sizeof(api_key_header), "X-MBX-APIKEY: %s", credentials_.api_key);
    headers = curl_slist_append(headers, api_key_header);
    
    memset(response, 0, response_size);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("All orders request failed: %s", curl_easy_strerror(res));
        return false;
    }
    
    if (http_code != 200) {
        LOG_ERROR("All orders request failed with HTTP %ld: %s", http_code, response);
        return false;
    }
    
    return true;
}

} // namespace Trading::Binance