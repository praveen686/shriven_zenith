#include "trading/market_data/zerodha/zerodha_instrument_fetcher.h"
#include "config/config.h"
#include "common/logging.h"
#include <curl/curl.h>
#include <cstring>
#include <cstdio>
#include <ctime>

namespace Trading::MarketData::Zerodha {

// Static member definition
Instrument ZerodhaInstrumentFetcher::instruments_[MAX_INSTRUMENTS];

// Static callback for CURL
static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    char* buffer = static_cast<char*>(userp);
    
    // Find current length (assuming null-terminated)
    size_t current_len = strlen(buffer);
    
    // Check if we have space (assuming 10MB buffer)
    const size_t max_size = 10 * 1024 * 1024;
    if (current_len + total_size >= max_size - 1) {
        // Truncate if necessary
        total_size = max_size - current_len - 1;
    }
    
    if (total_size > 0) {
        memcpy(buffer + current_len, contents, total_size);
        buffer[current_len + total_size] = '\0';
    }
    
    return size * nmemb;  // Return original size to keep CURL happy
}

// Implementation of fetchAllInstruments
auto ZerodhaInstrumentFetcher::fetchAllInstruments(
    char* buffer, 
    size_t buffer_size
) noexcept -> bool {
    
    if (!buffer || buffer_size == 0) {
        LOG_ERROR("Invalid buffer provided");
        return false;
    }
    
    // Clear buffer
    memset(buffer, 0, buffer_size);
    
    LOG_INFO("Fetching instruments from Zerodha...");
    
    // Initialize CURL
    CURL* curl = curl_easy_init();  // AUDIT_IGNORE: Init-time only
    if (!curl) {
        LOG_ERROR("Failed to initialize CURL");
        return false;
    }
    
    // Zerodha instruments URL (public, no auth needed)
    const char* url = "https://api.kite.trade/instruments";
    
    // Set CURL options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    // Get HTTP response code
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    // Cleanup
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("CURL failed: %s", curl_easy_strerror(res));
        return false;
    }
    
    if (http_code != 200) {
        LOG_ERROR("HTTP error: %ld", http_code);
        return false;
    }
    
    // Parse the CSV data
    size_t parsed = parseInstruments(buffer, strlen(buffer));
    LOG_INFO("Fetched and parsed %zu instruments", parsed);
    
    // Update timestamp
    last_update_time_ns_ = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
    
    return parsed > 0;
}

// Helper function to fetch and cache instruments - exported for use in tests
auto fetchAndCacheInstruments(ZerodhaInstrumentFetcher* fetcher) noexcept -> bool {
    if (!fetcher) return false;
    
    // Allocate buffer for CSV data (10MB for ~90k instruments)
    constexpr size_t BUFFER_SIZE = 10 * 1024 * 1024;
    char* csv_buffer = new char[BUFFER_SIZE];  // AUDIT_IGNORE: Init-time only
    memset(csv_buffer, 0, BUFFER_SIZE);
    
    bool success = false;
    
    // Try to fetch from API
    if (fetcher->fetchAllInstruments(csv_buffer, BUFFER_SIZE)) {
        // Save to cache file with today's date
        char filename[512];
        time_t now = time(nullptr);
        struct tm* tm_info = localtime(&now);
        
        snprintf(filename, sizeof(filename), "%s/instruments_%04d%02d%02d.csv",
                Trading::ConfigManager::getConfig().paths.instruments_dir,
                tm_info->tm_year + 1900,
                tm_info->tm_mon + 1,
                tm_info->tm_mday);
        
        if (fetcher->saveToCSV(filename)) {
            LOG_INFO("Saved instruments to cache: %s", filename);
            success = true;
        }
    } else {
        LOG_WARN("Failed to fetch from API, trying to load from cache");
        
        // Try to load from most recent cache file
        char filename[512];
        time_t now = time(nullptr);
        struct tm* tm_info = localtime(&now);
        
        // Try today's file first
        snprintf(filename, sizeof(filename), "%s/instruments_%04d%02d%02d.csv",
                Trading::ConfigManager::getConfig().paths.instruments_dir,
                tm_info->tm_year + 1900,
                tm_info->tm_mon + 1,
                tm_info->tm_mday);
        
        if (fetcher->loadFromCSV(filename)) {
            LOG_INFO("Loaded instruments from cache: %s", filename);
            success = true;
        } else {
            // Try yesterday's file
            now -= 86400;  // Subtract one day
            tm_info = localtime(&now);
            
            snprintf(filename, sizeof(filename), "%s/instruments_%04d%02d%02d.csv",
                    Trading::ConfigManager::getConfig().paths.instruments_dir,
                    tm_info->tm_year + 1900,
                    tm_info->tm_mon + 1,
                    tm_info->tm_mday);
            
            if (fetcher->loadFromCSV(filename)) {
                LOG_INFO("Loaded instruments from yesterday's cache: %s", filename);
                success = true;
            }
        }
    }
    
    delete[] csv_buffer;  // AUDIT_IGNORE: Init-time only
    return success;
}


} // namespace Trading::MarketData::Zerodha