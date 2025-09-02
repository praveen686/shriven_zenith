#include "zerodha_auth.h"
#include "common/time_utils.h"
#include "trading/config_manager.h"
#include <curl/curl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <chrono>

namespace Trading::Zerodha {

// ============================================================================
// TOTPGenerator Implementation
// ============================================================================

auto TOTPGenerator::init(const char* base32_secret) noexcept -> bool {
    if (!base32_secret || strlen(base32_secret) == 0) {
        return false;
    }
    
    // Decode base32 secret
    secret_len_ = base32Decode(base32_secret, decoded_secret_, MAX_SECRET_SIZE);
    return secret_len_ > 0;
}

auto TOTPGenerator::generate(char* output, size_t len) noexcept -> bool {
    if (len < 7) return false;  // Need space for 6 digits + null
    
    uint64_t current_time = static_cast<uint64_t>(std::time(nullptr));
    return generateAt(current_time, output, len);
}

auto TOTPGenerator::generateAt(uint64_t timestamp_sec, char* output, size_t len) noexcept -> bool {
    if (len < 7 || secret_len_ == 0) return false;
    
    // Calculate 30-second counter
    uint64_t counter = timestamp_sec / 30;
    
    // Convert to big-endian bytes
    uint8_t counter_bytes[8];
    for (int i = 7; i >= 0; --i) {
        counter_bytes[i] = counter & 0xFF;
        counter >>= 8;
    }
    
    // Generate HMAC-SHA1
    uint8_t hash[SHA1_SIZE];
    hmacSha1(decoded_secret_, secret_len_, counter_bytes, 8, hash);
    
    // Dynamic truncation (RFC 4226)
    int offset = hash[SHA1_SIZE - 1] & 0x0F;
    uint32_t truncated = 
        ((hash[offset] & 0x7F) << 24) |
        ((hash[offset + 1] & 0xFF) << 16) |
        ((hash[offset + 2] & 0xFF) << 8) |
        (hash[offset + 3] & 0xFF);
    
    // Generate 6-digit code
    uint32_t otp = truncated % 1000000;
    
    // Format with leading zeros
    snprintf(output, len, "%06u", otp);
    return true;
}

auto TOTPGenerator::base32Decode(const char* encoded, uint8_t* output, size_t max_len) noexcept -> size_t {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    size_t output_len = 0;
    int bits = 0;
    int value = 0;
    
    for (const char* p = encoded; *p && output_len < max_len; ++p) {
        if (*p == '=' || *p == ' ') continue;
        
        // Convert to uppercase if needed
        char c = (*p >= 'a' && *p <= 'z') ? (*p - 'a' + 'A') : *p;
        
        // Find position in alphabet
        const char* pos = strchr(alphabet, c);
        if (!pos) continue;
        
        value = (value << 5) | static_cast<int>(pos - alphabet);
        bits += 5;
        
        if (bits >= 8) {
            output[output_len++] = static_cast<uint8_t>((value >> (bits - 8)) & 0xFF);
            bits -= 8;
        }
    }
    
    return output_len;
}

// Simplified HMAC-SHA1 implementation
auto TOTPGenerator::hmacSha1(const uint8_t* key, size_t key_len,
                             const uint8_t* data, size_t data_len,
                             uint8_t* output) noexcept -> void {
    constexpr size_t BLOCK_SIZE = 64;
    uint8_t k_ipad[BLOCK_SIZE], k_opad[BLOCK_SIZE];
    uint8_t key_buf[BLOCK_SIZE] = {0};
    
    // If key is longer than block size, hash it
    if (key_len > BLOCK_SIZE) {
        sha1(key, key_len, key_buf);
        key_len = SHA1_SIZE;
    } else {
        memcpy(key_buf, key, key_len);
    }
    
    // XOR key with ipad and opad
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        k_ipad[i] = key_buf[i] ^ 0x36;
        k_opad[i] = key_buf[i] ^ 0x5C;
    }
    
    // Inner hash: SHA1(k_ipad || data)
    uint8_t inner_data[BLOCK_SIZE + 256];  // Assuming data_len < 256
    memcpy(inner_data, k_ipad, BLOCK_SIZE);
    memcpy(inner_data + BLOCK_SIZE, data, data_len);
    
    uint8_t inner_hash[SHA1_SIZE];
    sha1(inner_data, BLOCK_SIZE + data_len, inner_hash);
    
    // Outer hash: SHA1(k_opad || inner_hash)
    uint8_t outer_data[BLOCK_SIZE + SHA1_SIZE];
    memcpy(outer_data, k_opad, BLOCK_SIZE);
    memcpy(outer_data + BLOCK_SIZE, inner_hash, SHA1_SIZE);
    
    sha1(outer_data, BLOCK_SIZE + SHA1_SIZE, output);
}

// SHA1 implementation without external dependencies
auto TOTPGenerator::sha1(const uint8_t* data, size_t len, uint8_t* hash) noexcept -> void {
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;
    
    // Calculate padded length
    size_t bit_len = len * 8;
    size_t padded_len = ((len + 8) / 64 + 1) * 64;
    
    // Use stack buffer for reasonable sizes
    uint8_t padded[1024];
    if (padded_len > sizeof(padded)) {
        // Too large, return zeros
        memset(hash, 0, SHA1_SIZE);
        return;
    }
    
    // Copy data and add padding
    memcpy(padded, data, len);
    padded[len] = 0x80;
    memset(padded + len + 1, 0, padded_len - len - 9);
    
    // Append bit length as big-endian 64-bit
    for (int i = 0; i < 8; ++i) {
        padded[padded_len - 1 - static_cast<size_t>(i)] = static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF);
    }
    
    // Process 512-bit chunks
    for (size_t chunk = 0; chunk < padded_len; chunk += 64) {
        uint32_t w[80];
        
        // Break chunk into 16 32-bit words (big-endian)
        for (int i = 0; i < 16; ++i) {
            size_t idx = chunk + static_cast<size_t>(i * 4);
            w[i] = (static_cast<uint32_t>(padded[idx]) << 24) |
                   (static_cast<uint32_t>(padded[idx + 1]) << 16) |
                   (static_cast<uint32_t>(padded[idx + 2]) << 8) |
                   static_cast<uint32_t>(padded[idx + 3]);
        }
        
        // Extend to 80 words
        for (int i = 16; i < 80; ++i) {
            uint32_t temp = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
            w[i] = (temp << 1) | (temp >> 31); // Rotate left 1
        }
        
        // Initialize working variables
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        
        // Main loop
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            
            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2); // Rotate left 30
            b = a;
            a = temp;
        }
        
        // Add to hash
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }
    
    // Produce final hash (big-endian)
    for (int i = 0; i < 4; ++i) {
        hash[i] = static_cast<uint8_t>((h0 >> (24 - i * 8)) & 0xFF);
        hash[i + 4] = static_cast<uint8_t>((h1 >> (24 - i * 8)) & 0xFF);
        hash[i + 8] = static_cast<uint8_t>((h2 >> (24 - i * 8)) & 0xFF);
        hash[i + 12] = static_cast<uint8_t>((h3 >> (24 - i * 8)) & 0xFF);
        hash[i + 16] = static_cast<uint8_t>((h4 >> (24 - i * 8)) & 0xFF);
    }
}

// ============================================================================
// ZerodhaAuth Implementation
// ============================================================================

ZerodhaAuth::ZerodhaAuth() noexcept {
    // Initialize CURL handle once
    curl_global_init(CURL_GLOBAL_DEFAULT);  // AUDIT_IGNORE: Init-time only
    curl_handle_ = curl_easy_init();
    
    // Use ConfigManager paths if available
    if (Trading::ConfigManager::isInitialized()) {
        snprintf(cookie_jar_, sizeof(cookie_jar_), "%s/zerodha_cookies", 
                Trading::ConfigManager::getCacheDir());
        snprintf(session_file_, sizeof(session_file_), "%s/zerodha_session", 
                Trading::ConfigManager::getSessionDir());
    } else {
        // Fallback to home directory
        const char* home = getenv("HOME");
        if (home) {
            snprintf(cookie_jar_, sizeof(cookie_jar_), "%s/.zerodha_cookies", home);
            snprintf(session_file_, sizeof(session_file_), "%s/.zerodha_session", home);
        }
    }
}

ZerodhaAuth::~ZerodhaAuth() noexcept {
    if (curl_handle_) {
        curl_easy_cleanup(curl_handle_);
        curl_handle_ = nullptr;
    }
    curl_global_cleanup();
}

auto ZerodhaAuth::init(const Credentials& creds) noexcept -> bool {
    memcpy(&credentials_, &creds, sizeof(Credentials));
    
    // Initialize TOTP generator
    if (!totp_gen_.init(credentials_.totp_secret)) {
        LOG_ERROR("Failed to initialize TOTP generator");
        return false;
    }
    
    return true;
}

auto ZerodhaAuth::loadFromEnv() noexcept -> bool {
    const char* api_key = getenv("ZERODHA_API_KEY");
    const char* api_secret = getenv("ZERODHA_API_SECRET");
    const char* user_id = getenv("ZERODHA_USER_ID");
    const char* password = getenv("ZERODHA_PASSWORD");
    const char* totp_secret = getenv("ZERODHA_TOTP_SECRET");
    
    if (!api_key || !api_secret || !user_id || !password || !totp_secret) {
        LOG_ERROR("Missing required environment variables");
        return false;
    }
    
    Credentials creds{};
    strncpy(creds.api_key, api_key, sizeof(creds.api_key) - 1);
    strncpy(creds.api_secret, api_secret, sizeof(creds.api_secret) - 1);
    strncpy(creds.user_id, user_id, sizeof(creds.user_id) - 1);
    strncpy(creds.password, password, sizeof(creds.password) - 1);
    strncpy(creds.totp_secret, totp_secret, sizeof(creds.totp_secret) - 1);
    
    return init(creds);
}

auto ZerodhaAuth::loadFromFile(const char* path) noexcept -> bool {
    FILE* fp = fopen(path, "r");  // AUDIT_IGNORE: Init-time only
    if (!fp) {
        LOG_ERROR("Failed to open credentials file: %s", path);
        return false;
    }
    
    Credentials creds{};
    char line[256];
    
    while (fgets(line, sizeof(line), fp)) {
        // Remove newline
        line[strcspn(line, "\n")] = 0;
        
        // Parse key=value pairs
        char* eq = strchr(line, '=');
        if (!eq) continue;
        
        *eq = '\0';
        const char* key = line;
        const char* value = eq + 1;
        
        if (strcmp(key, "api_key") == 0) {
            strncpy(creds.api_key, value, sizeof(creds.api_key) - 1);
        } else if (strcmp(key, "api_secret") == 0) {
            strncpy(creds.api_secret, value, sizeof(creds.api_secret) - 1);
        } else if (strcmp(key, "user_id") == 0) {
            strncpy(creds.user_id, value, sizeof(creds.user_id) - 1);
        } else if (strcmp(key, "password") == 0) {
            strncpy(creds.password, value, sizeof(creds.password) - 1);
        } else if (strcmp(key, "totp_secret") == 0) {
            strncpy(creds.totp_secret, value, sizeof(creds.totp_secret) - 1);
        }
    }
    
    fclose(fp);
    return init(creds);
}

auto ZerodhaAuth::authenticate() noexcept -> bool {
    // Try cached session first
    if (loadCachedSession()) {
        if (current_token_.isValid()) {
            LOG_INFO("Using cached session");
            return true;
        }
    }
    
    // Perform fresh login
    return performLogin();
}

auto ZerodhaAuth::loadCachedSession() noexcept -> bool {
    // Ensure we're using the cache directory
    char token_file[512];
    if (Trading::ConfigManager::isInitialized()) {
        snprintf(token_file, sizeof(token_file), "%s/zerodha_access_token.cache", 
                Trading::ConfigManager::getCacheDir());
    } else {
        strncpy(token_file, session_file_, sizeof(token_file) - 1);
    }
    
    FILE* fp = fopen(token_file, "r");  // AUDIT_IGNORE: Init-time only
    if (!fp) {
        LOG_INFO("No cached session found at: %s", token_file);
        return false;
    }
    
    // Read token data
    char line[512];
    if (fgets(line, sizeof(line), fp)) {
        // Parse token (format: access_token|public_token|refresh_token|expiry_timestamp)
        // Note: public_token and refresh_token may be empty
        
        // Find pipe positions manually to handle empty fields
        char* pipe1 = strchr(line, '|');
        char* pipe2 = pipe1 ? strchr(pipe1 + 1, '|') : nullptr;
        char* pipe3 = pipe2 ? strchr(pipe2 + 1, '|') : nullptr;
        
        if (pipe1 && pipe2 && pipe3) {
            // Extract access_token (from start to first pipe)
            size_t access_len = static_cast<size_t>(pipe1 - line);
            if (access_len > 0 && access_len < sizeof(current_token_.access_token)) {
                memcpy(current_token_.access_token, line, access_len);
                current_token_.access_token[access_len] = '\0';
            }
            
            // Extract public_token (from pipe1+1 to pipe2)
            size_t public_len = static_cast<size_t>(pipe2 - pipe1 - 1);
            if (public_len > 0 && public_len < sizeof(current_token_.public_token)) {
                memcpy(current_token_.public_token, pipe1 + 1, public_len);
                current_token_.public_token[public_len] = '\0';
            }
            
            // Extract refresh_token (from pipe2+1 to pipe3)
            size_t refresh_len = static_cast<size_t>(pipe3 - pipe2 - 1);
            if (refresh_len > 0 && refresh_len < sizeof(current_token_.refresh_token)) {
                memcpy(current_token_.refresh_token, pipe2 + 1, refresh_len);
                current_token_.refresh_token[refresh_len] = '\0';
            }
            
            // Extract expiry (from pipe3+1 to end)
            current_token_.expiry_timestamp_ns = strtoull(pipe3 + 1, nullptr, 10);
            
            LOG_INFO("Loaded cached token: access=%s..., expiry=%llu", 
                    current_token_.access_token[0] ? "present" : "empty",
                    static_cast<unsigned long long>(current_token_.expiry_timestamp_ns));
        } else {
            LOG_ERROR("Invalid cache format");
        }
    }
    
    fclose(fp);
    
    bool valid = current_token_.isValid();
    if (valid) {
        LOG_INFO("Cached token is valid");
    } else {
        LOG_INFO("Cached token is invalid or expired");
    }
    
    return valid;
}

auto ZerodhaAuth::saveCachedSession() const noexcept -> bool {
    // Ensure we're using the cache directory
    char token_file[512];
    if (Trading::ConfigManager::isInitialized()) {
        snprintf(token_file, sizeof(token_file), "%s/zerodha_access_token.cache", 
                Trading::ConfigManager::getCacheDir());
    } else {
        strncpy(token_file, session_file_, sizeof(token_file) - 1);
    }
    
    FILE* fp = fopen(token_file, "w");  // AUDIT_IGNORE: Init-time only
    if (!fp) {
        LOG_ERROR("Failed to open cache file for writing: %s", token_file);
        return false;
    }
    
    // Save all token components
    fprintf(fp, "%s|%s|%s|%llu\n", 
            current_token_.access_token,
            current_token_.public_token,
            current_token_.refresh_token,
            static_cast<unsigned long long>(current_token_.expiry_timestamp_ns));
    
    fclose(fp);
    LOG_INFO("Saved session to cache: %s", token_file);
    return true;
}

auto ZerodhaAuth::performLogin() noexcept -> bool {
    LOG_INFO("Performing Zerodha login for user: %s", credentials_.user_id);
    
    char request_id[128];  // Zerodha request_ids can be 64+ chars
    char request_token[256];
    
    // Step 1: Initial authentication
    if (!step1_InitialAuth(request_id, sizeof(request_id))) {
        LOG_ERROR("Step 1 failed: Initial auth");
        return false;
    }
    LOG_INFO("Got request_id: %s", request_id);
    
    // Step 2: Submit TOTP
    if (!step2_SubmitTOTP(request_id)) {
        LOG_ERROR("Step 2 failed: TOTP submission");
        return false;
    }
    LOG_INFO("TOTP submitted successfully");
    
    // Step 3: Get request_token from redirect
    if (!getRequestTokenFromRedirect(request_id, request_token, sizeof(request_token))) {
        LOG_ERROR("Failed to get request_token from redirect");
        return false;
    }
    LOG_INFO("Got request_token: %s", request_token);
    
    // Step 4: Exchange request_token for access_token
    if (!step3_GetToken(request_token)) {
        LOG_ERROR("Step 4 failed: Token exchange");
        return false;
    }
    
    // Save session for next time
    saveCachedSession();
    
    LOG_INFO("Authentication successful");
    return true;
}

// HTTP callback for CURL
static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    char* response = static_cast<char*>(userp);
    
    // Find current length of response
    size_t current_len = strlen(response);
    
    // Check if we have space (assuming 8192 buffer size)
    const size_t max_size = 8192;
    if (current_len + total_size >= max_size - 1) {
        // Truncate if necessary
        total_size = max_size - current_len - 1;
    }
    
    if (total_size > 0) {
        memcpy(response + current_len, contents, total_size);
        response[current_len + total_size] = '\0';
    }
    
    return size * nmemb;  // Always return original size to avoid curl error
}

auto ZerodhaAuth::httpPost(const char* url, const char* data,
                           char* response, size_t max_response) noexcept -> int {
    (void)max_response;  // Unused in this implementation
    if (!curl_handle_) return -1;
    
    CURL* curl = static_cast<CURL*>(curl_handle_);
    
    // Copy URL and data to persistent buffers
    strncpy(http_url_buffer_, url, sizeof(http_url_buffer_) - 1);
    strncpy(http_post_buffer_, data, sizeof(http_post_buffer_) - 1);
    
    response[0] = '\0';
    
    curl_easy_setopt(curl, CURLOPT_URL, http_url_buffer_);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, http_post_buffer_);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookie_jar_);
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookie_jar_);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        LOG_ERROR("CURL error: %s", curl_easy_strerror(res));
        return -1;
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    return static_cast<int>(http_code);
}

auto ZerodhaAuth::step1_InitialAuth(char* request_id, size_t len) noexcept -> bool {
    // Use the correct Zerodha API endpoint
    const char* url = "https://kite.zerodha.com/api/login";
    
    char post_data[512];
    snprintf(post_data, sizeof(post_data), "user_id=%s&password=%s", 
             credentials_.user_id, credentials_.password);
    
    char response[8192];
    int status = httpPost(url, post_data, response, sizeof(response));
    
    LOG_INFO("Login response status: %d", status);
    LOG_INFO("Login response (first 500 chars): %.500s", response);
    
    if (status != 200) {
        LOG_ERROR("Login failed with status: %d", status);
        LOG_ERROR("Full response: %s", response);
        return false;
    }
    
    // Extract request_id from JSON response
    return extractRequestId(response, request_id, len);
}

auto ZerodhaAuth::step2_SubmitTOTP(const char* request_id) noexcept -> bool {
    char totp_code[8];
    if (!totp_gen_.generate(totp_code, sizeof(totp_code))) {
        LOG_ERROR("Failed to generate TOTP");
        return false;
    }
    
    LOG_INFO("Generated TOTP: %s", totp_code);
    
    // Use the correct TOTP endpoint
    const char* url = "https://kite.zerodha.com/api/twofa";
    
    char post_data[512];
    snprintf(post_data, sizeof(post_data), "user_id=%s&request_id=%s&twofa_value=%s", 
             credentials_.user_id, request_id, totp_code);
    
    char response[8192];
    int status = httpPost(url, post_data, response, sizeof(response));
    
    LOG_INFO("TOTP submission status: %d", status);
    LOG_INFO("TOTP response: %.500s", response);
    
    if (status != 200) {
        LOG_ERROR("TOTP submission failed with status: %d", status);
        LOG_ERROR("Full response: %s", response);
        return false;
    }
    
    // Check if response contains success status
    return strstr(response, "\"status\":\"success\"") != nullptr;
}

auto ZerodhaAuth::getRequestTokenFromRedirect(const char* request_id, char* request_token, size_t len) noexcept -> bool {
    (void)request_id;  // May not be needed for redirect
    
    // Create a NEW CURL handle for this request (like reference does)
    CURL* curl = curl_easy_init();  // AUDIT_IGNORE: Init-time only
    if (!curl) {
        LOG_ERROR("Failed to create CURL handle for redirect");
        return false;
    }
    
    // Use member buffer for URL
    snprintf(http_url_buffer_, sizeof(http_url_buffer_), 
             "https://kite.zerodha.com/connect/login?v=3&api_key=%s", credentials_.api_key);
    
    // Clear response buffer
    memset(http_response_buffer_, 0, sizeof(http_response_buffer_));
    
    curl_easy_setopt(curl, CURLOPT_URL, http_url_buffer_);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, http_response_buffer_);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookie_jar_);
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookie_jar_);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        LOG_ERROR("Failed to get redirect: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return false;
    }
    
    // Get the final URL after redirect
    char* final_url = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &final_url);
    
    bool found = false;
    if (final_url) {
        LOG_INFO("Redirect final URL: %s", final_url);
        
        // Extract request_token from URL parameters (like reference does with regex)
        const char* token_start = strstr(final_url, "request_token=");
        
        if (token_start) {
            token_start += 14;  // Skip "request_token="
            const char* token_end = strchr(token_start, '&');
            
            size_t token_len = token_end ? static_cast<size_t>(token_end - token_start) : strlen(token_start);
            if (token_len < len) {
                strncpy(request_token, token_start, token_len);
                request_token[token_len] = '\0';
                LOG_INFO("Extracted request_token from redirect URL: %s", request_token);
                found = true;
            }
        } else {
            LOG_ERROR("No request_token found in redirect URL");
            LOG_INFO("Response body (first 500 chars): %.500s", http_response_buffer_);
        }
    }
    
    curl_easy_cleanup(curl);
    return found;
}

auto ZerodhaAuth::step3_GetToken(const char* request_token) noexcept -> bool {
    const char* url = "https://api.kite.trade/session/token";
    
    // Create checksum: SHA256(api_key + request_token + api_secret)
    char checksum_data[512];
    snprintf(checksum_data, sizeof(checksum_data), "%s%s%s",
             credentials_.api_key, request_token, credentials_.api_secret);
    
    // Generate SHA256 checksum
    uint8_t hash[32];
    sha256(reinterpret_cast<const uint8_t*>(checksum_data), strlen(checksum_data), hash);
    
    char checksum[65];
    for (int i = 0; i < 32; ++i) {
        sprintf(&checksum[i*2], "%02x", hash[i]);
    }
    checksum[64] = '\0';
    
    char post_data[1024];
    snprintf(post_data, sizeof(post_data), 
             "api_key=%s&request_token=%s&checksum=%s",
             credentials_.api_key, request_token, checksum);
    
    char response[8192];
    int status = httpPost(url, post_data, response, sizeof(response));
    
    if (status != 200) {
        LOG_ERROR("Token exchange failed with status: %d", status);
        LOG_ERROR("Response: %.200s", response);
        return false;
    }
    
    LOG_INFO("Token exchange response: %.200s", response);
    
    return extractToken(response, current_token_);
}

auto ZerodhaAuth::extractRequestId(const char* response, char* request_id, size_t len) noexcept -> bool {
    // Parse JSON response for request_id
    // Expected format: {"status":"success","data":{"request_id":"..."}}
    const char* pattern = "\"request_id\":\"";
    const char* start = strstr(response, pattern);
    
    if (!start) {
        LOG_ERROR("No request_id found in response");
        return false;
    }
    
    // Move past the pattern to the actual value
    start += strlen(pattern);
    
    // Find the closing quote
    const char* end = strchr(start, '"');
    if (!end) {
        LOG_ERROR("No closing quote for request_id");
        return false;
    }
    
    size_t id_len = static_cast<size_t>(end - start);
    if (id_len >= len) {
        LOG_ERROR("request_id too long: %zu", id_len);
        return false;
    }
    
    strncpy(request_id, start, id_len);
    request_id[id_len] = '\0';
    
    LOG_INFO("Extracted request_id: %s", request_id);
    
    return true;
}

auto ZerodhaAuth::extractToken(const char* response, AuthToken& token) noexcept -> bool {
    // Parse JSON response (simplified - no external JSON library)
    // Format: {"status":"success","data":{"access_token":"...","..."}}
    
    const char* token_pattern = "access_token\":\"";
    const char* token_start = strstr(response, token_pattern);
    if (!token_start) return false;
    
    token_start += strlen(token_pattern);
    const char* token_end = strchr(token_start, '"');
    if (!token_end) return false;
    
    size_t token_len = static_cast<size_t>(token_end - token_start);
    if (token_len >= sizeof(token.access_token)) return false;
    
    strncpy(token.access_token, token_start, token_len);
    token.access_token[token_len] = '\0';
    
    // Set expiry (8 hours from now)
    // Set expiry to 8 hours from now
    uint64_t now_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    token.expiry_timestamp_ns = now_ns + (8ULL * 60 * 60 * 1000000000ULL);
    
    return true;
}

// SHA256 implementation without external dependencies
auto ZerodhaAuth::sha256(const uint8_t* data, size_t len, uint8_t* hash) const noexcept -> void {
    // SHA256 constants
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
        0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
        0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    
    // Initial hash values
    uint32_t h0 = 0x6a09e667;
    uint32_t h1 = 0xbb67ae85;
    uint32_t h2 = 0x3c6ef372;
    uint32_t h3 = 0xa54ff53a;
    uint32_t h4 = 0x510e527f;
    uint32_t h5 = 0x9b05688c;
    uint32_t h6 = 0x1f83d9ab;
    uint32_t h7 = 0x5be0cd19;
    
    // Calculate padded length
    size_t bit_len = len * 8;
    size_t padded_len = ((len + 8) / 64 + 1) * 64;
    
    // Use stack buffer for reasonable sizes
    uint8_t padded[1024];
    if (padded_len > sizeof(padded)) {
        memset(hash, 0, 32);
        return;
    }
    
    // Copy data and add padding
    memcpy(padded, data, len);
    padded[len] = 0x80;
    memset(padded + len + 1, 0, padded_len - len - 9);
    
    // Append bit length as big-endian 64-bit
    for (int i = 0; i < 8; ++i) {
        padded[padded_len - 1 - static_cast<size_t>(i)] = static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF);
    }
    
    // Process 512-bit chunks
    for (size_t chunk = 0; chunk < padded_len; chunk += 64) {
        uint32_t w[64];
        
        // Copy chunk into first 16 words (big-endian)
        for (int i = 0; i < 16; ++i) {
            size_t idx = chunk + static_cast<size_t>(i * 4);
            w[i] = (static_cast<uint32_t>(padded[idx]) << 24) |
                   (static_cast<uint32_t>(padded[idx + 1]) << 16) |
                   (static_cast<uint32_t>(padded[idx + 2]) << 8) |
                   static_cast<uint32_t>(padded[idx + 3]);
        }
        
        // Extend to 64 words
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = ((w[i-15] >> 7) | (w[i-15] << 25)) ^
                         ((w[i-15] >> 18) | (w[i-15] << 14)) ^
                         (w[i-15] >> 3);
            uint32_t s1 = ((w[i-2] >> 17) | (w[i-2] << 15)) ^
                         ((w[i-2] >> 19) | (w[i-2] << 13)) ^
                         (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        
        // Initialize working variables
        uint32_t a = h0, b = h1, c = h2, d = h3;
        uint32_t e = h4, f = h5, g = h6, h = h7;
        
        // Main loop
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = ((e >> 6) | (e << 26)) ^
                         ((e >> 11) | (e << 21)) ^
                         ((e >> 25) | (e << 7));
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + K[i] + w[i];
            uint32_t S0 = ((a >> 2) | (a << 30)) ^
                         ((a >> 13) | (a << 19)) ^
                         ((a >> 22) | (a << 10));
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;
            
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        
        // Add to hash
        h0 += a; h1 += b; h2 += c; h3 += d;
        h4 += e; h5 += f; h6 += g; h7 += h;
    }
    
    // Produce final hash (big-endian)
    for (int i = 0; i < 4; ++i) {
        hash[i] = static_cast<uint8_t>((h0 >> (24 - i * 8)) & 0xFF);
        hash[i + 4] = static_cast<uint8_t>((h1 >> (24 - i * 8)) & 0xFF);
        hash[i + 8] = static_cast<uint8_t>((h2 >> (24 - i * 8)) & 0xFF);
        hash[i + 12] = static_cast<uint8_t>((h3 >> (24 - i * 8)) & 0xFF);
        hash[i + 16] = static_cast<uint8_t>((h4 >> (24 - i * 8)) & 0xFF);
        hash[i + 20] = static_cast<uint8_t>((h5 >> (24 - i * 8)) & 0xFF);
        hash[i + 24] = static_cast<uint8_t>((h6 >> (24 - i * 8)) & 0xFF);
        hash[i + 28] = static_cast<uint8_t>((h7 >> (24 - i * 8)) & 0xFF);
    }
}

// ============================================================================
// API Methods Implementation
// ============================================================================

auto ZerodhaAuth::makeAPICall(const char* endpoint, char* response_buffer, size_t buffer_size) noexcept -> bool {
    if (!isAuthenticated()) {
        LOG_ERROR("Not authenticated");
        return false;
    }
    
    // Build URL with authorization header
    char url[512];
    snprintf(url, sizeof(url), "https://api.kite.trade%s", endpoint);
    
    // Setup CURL for API call
    CURL* curl = curl_easy_init();  // AUDIT_IGNORE: Init-time only
    if (!curl) {
        LOG_ERROR("Failed to init CURL for API call");
        return false;
    }
    
    // Clear response buffer
    memset(response_buffer, 0, buffer_size);
    
    // Set headers with authorization
    struct curl_slist* headers = nullptr;
    char auth_header[512];  // Increased buffer size for API key + access token
    snprintf(auth_header, sizeof(auth_header), "Authorization: token %s:%s", 
             credentials_.api_key, current_token_.access_token);
    headers = curl_slist_append(headers, auth_header);  // AUDIT_IGNORE: Init-time only
    headers = curl_slist_append(headers, "X-Kite-Version: 3");  // AUDIT_IGNORE: Init-time only
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("CURL failed for %s: %s", endpoint, curl_easy_strerror(res));
        return false;
    }
    
    if (http_code != 200) {
        LOG_ERROR("API call failed for %s: HTTP %ld", endpoint, http_code);
        LOG_ERROR("Response: %s", response_buffer);
        return false;
    }
    
    return true;
}

auto ZerodhaAuth::fetchProfile(char* buffer, size_t buffer_size) noexcept -> bool {
    return makeAPICall("/user/profile", buffer, buffer_size);
}

auto ZerodhaAuth::fetchPositions(char* buffer, size_t buffer_size) noexcept -> bool {
    return makeAPICall("/portfolio/positions", buffer, buffer_size);
}

auto ZerodhaAuth::fetchHoldings(char* buffer, size_t buffer_size) noexcept -> bool {
    return makeAPICall("/portfolio/holdings", buffer, buffer_size);
}

auto ZerodhaAuth::fetchOrders(char* buffer, size_t buffer_size) noexcept -> bool {
    return makeAPICall("/orders", buffer, buffer_size);
}

auto ZerodhaAuth::fetchFunds(char* buffer, size_t buffer_size) noexcept -> bool {
    return makeAPICall("/user/margins", buffer, buffer_size);
}

auto ZerodhaAuth::fetchInstruments(const char* exchange, char* buffer, size_t buffer_size) noexcept -> bool {
    char endpoint[128];
    snprintf(endpoint, sizeof(endpoint), "/instruments/%s", exchange);
    return makeAPICall(endpoint, buffer, buffer_size);
}

// ============================================================================
// ZerodhaAuthManager Implementation
// ============================================================================

// Static member definitions
ZerodhaAuth ZerodhaAuthManager::instance_{};
bool ZerodhaAuthManager::initialized_ = false;

auto ZerodhaAuthManager::init(const Credentials& creds) noexcept -> bool {
    if (initialized_) {
        LOG_INFO("ZerodhaAuthManager already initialized, reinitializing");
    }
    
    if (!instance_.init(creds)) {
        LOG_ERROR("Failed to initialize ZerodhaAuth");
        initialized_ = false;
        return false;
    }
    
    if (!instance_.authenticate()) {
        LOG_ERROR("Failed to authenticate with Zerodha");
        initialized_ = false;
        return false;
    }
    
    initialized_ = true;
    return true;
}

auto ZerodhaAuthManager::getInstance() noexcept -> ZerodhaAuth* {
    if (!initialized_) {
        LOG_ERROR("ZerodhaAuthManager not initialized");
        return nullptr;
    }
    return &instance_;
}

auto ZerodhaAuthManager::shutdown() noexcept -> void {
    initialized_ = false;
    // Destructor will be called automatically for static instance
}

auto ZerodhaAuth::refreshToken() noexcept -> bool {
    LOG_INFO("Refreshing Zerodha token");
    
    // For Zerodha, we need to re-authenticate completely
    // There's no simple token refresh endpoint
    return performLogin();
}

auto ZerodhaAuth::signRequest(const char* data, char* signature, size_t sig_len) const noexcept -> bool {
    if (!data || !signature || sig_len < 65) {  // SHA256 produces 64 hex chars + null
        return false;
    }
    
    // Create data to sign: api_key + data + api_secret
    char sign_data[1024];
    snprintf(sign_data, sizeof(sign_data), "%s%s%s", 
             credentials_.api_key, data, credentials_.api_secret);
    
    // Generate SHA256 hash
    uint8_t hash[32];
    sha256(reinterpret_cast<const uint8_t*>(sign_data), strlen(sign_data), hash);
    
    // Convert to hex string
    for (int i = 0; i < 32; ++i) {
        sprintf(&signature[i*2], "%02x", hash[i]);
    }
    signature[64] = '\0';
    
    return true;
}

} // namespace Trading::Zerodha