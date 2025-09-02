// Debug program to check raw instrument response from Zerodha

#include <cstdio>
#include <cstring>
#include <curl/curl.h>
#include "trading/auth/zerodha/zerodha_auth.h"

// Write callback for CURL
static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    char* buffer = static_cast<char*>(userp);
    
    // Find current position in buffer
    size_t current_len = strlen(buffer);
    
    // Copy new data
    memcpy(buffer + current_len, contents, realsize);
    buffer[current_len + realsize] = '\0';
    
    return realsize;
}

int main() {
    printf("=== Debug Zerodha Instruments ===\n\n");
    
    // Initialize auth
    Trading::Zerodha::Credentials creds{};
    const char* user_id = getenv("ZERODHA_USER_ID");
    const char* password = getenv("ZERODHA_PASSWORD");
    const char* totp_secret = getenv("ZERODHA_TOTP_SECRET");
    const char* api_key = getenv("ZERODHA_API_KEY");
    const char* api_secret = getenv("ZERODHA_API_SECRET");
    
    if (!user_id || !password || !totp_secret || !api_key || !api_secret) {
        printf("Missing credentials\n");
        return 1;
    }
    
    strncpy(creds.user_id, user_id, sizeof(creds.user_id) - 1);
    strncpy(creds.password, password, sizeof(creds.password) - 1);
    strncpy(creds.totp_secret, totp_secret, sizeof(creds.totp_secret) - 1);
    strncpy(creds.api_key, api_key, sizeof(creds.api_key) - 1);
    strncpy(creds.api_secret, api_secret, sizeof(creds.api_secret) - 1);
    
    if (!Trading::Zerodha::ZerodhaAuthManager::init(creds)) {
        printf("Failed to init auth\n");
        return 1;
    }
    
    auto* auth = Trading::Zerodha::ZerodhaAuthManager::getInstance();
    if (!auth || !auth->isAuthenticated()) {
        printf("Not authenticated\n");
        return 1;
    }
    
    printf("Authenticated with token: %.20s...\n\n", auth->getAccessToken());
    
    // Fetch raw instruments
    CURL* curl = curl_easy_init();
    if (!curl) {
        printf("CURL init failed\n");
        return 1;
    }
    
    // Allocate buffer for response
    constexpr size_t BUFFER_SIZE = 10 * 1024 * 1024;  // 10MB
    char* buffer = new char[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    
    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.kite.trade/instruments");
    
    // Set auth header
    struct curl_slist* headers = nullptr;
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: token %s", auth->getAccessToken());
    headers = curl_slist_append(headers, auth_header);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Set write callback
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
    
    // Perform request
    printf("Fetching instruments from https://api.kite.trade/instruments...\n");
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        printf("CURL failed: %s\n", curl_easy_strerror(res));
    } else {
        printf("Response received, length = %zu bytes\n\n", strlen(buffer));
        
        // Print first 5000 chars to see format
        printf("First 5000 characters:\n");
        printf("====================\n");
        char preview[5001];
        strncpy(preview, buffer, 5000);
        preview[5000] = '\0';
        printf("%s\n", preview);
        printf("====================\n\n");
        
        // Count lines
        int line_count = 0;
        char* line = strtok(buffer, "\n");
        while (line) {
            line_count++;
            
            // Print some NSE/NFO lines
            if (line_count <= 10 || 
                (strstr(line, "NSE") && line_count <= 20) ||
                (strstr(line, "NFO") && line_count <= 30) ||
                (strstr(line, "NIFTY") && line_count <= 40)) {
                printf("Line %d: %s\n", line_count, line);
            }
            
            line = strtok(nullptr, "\n");
        }
        
        printf("\nTotal lines: %d\n", line_count);
        
        // Search for specific symbols
        printf("\nSearching for NIFTY instruments...\n");
        char* ptr = buffer;
        int nifty_count = 0;
        while ((ptr = strstr(ptr, "NIFTY")) != nullptr) {
            nifty_count++;
            ptr++;
        }
        printf("Found %d occurrences of 'NIFTY'\n", nifty_count);
    }
    
    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    delete[] buffer;
    
    Trading::Zerodha::ZerodhaAuthManager::shutdown();
    
    return 0;
}