// ============================================================================
// binance_auth.h - Binance API Authentication
// ============================================================================

#pragma once

#include "common/types.h"
#include "common/macros.h"
#include <atomic>
#include <chrono>
#include <cstring>

namespace Trading::Binance {

// ============================================================================
// Binance API Credentials
// ============================================================================
struct Credentials {
    char api_key[128];
    char api_secret[256];
    
    Credentials() {
        memset(api_key, 0, sizeof(api_key));
        memset(api_secret, 0, sizeof(api_secret));
    }
    
    // Delete copy operations for security
    Credentials(const Credentials&) = delete;
    Credentials& operator=(const Credentials&) = delete;
    
    // Allow move operations
    Credentials(Credentials&&) noexcept = default;
    Credentials& operator=(Credentials&&) noexcept = default;
    
    ~Credentials() {
        // Clear sensitive data
        memset(api_key, 0, sizeof(api_key));
        memset(api_secret, 0, sizeof(api_secret));
    }
};

// ============================================================================
// Binance Authentication Manager
// ============================================================================
class BinanceAuth {
private:
    // Static instance for singleton pattern
    static BinanceAuth* instance_;
    
    // Credentials
    Credentials credentials_;
    
    // Authentication state
    std::atomic<bool> initialized_{false};
    std::atomic<bool> authenticated_{false};
    
    // Rate limiting
    std::atomic<uint64_t> last_request_time_ns_{0};
    static constexpr uint64_t MIN_REQUEST_INTERVAL_NS = 100000000; // 100ms
    
    // API endpoint (configurable)
    char api_endpoint_[256];
    
    // Internal helper for large responses
    bool fetchLargeResponse(const char* url, char* response, size_t response_size) noexcept;
    
    // Private constructor for singleton
    BinanceAuth() = default;
    
    // Helper to create HMAC signature
    bool createSignature(const char* query_string, char* signature, size_t sig_size) const noexcept;
    
    // Helper to get server time for synchronization
    bool getServerTime(int64_t* server_time_ms) noexcept;
    
    // Rate limiting helper
    void enforceRateLimit() noexcept;
    
public:
    // Delete copy operations
    BinanceAuth(const BinanceAuth&) = delete;
    BinanceAuth& operator=(const BinanceAuth&) = delete;
    
    // Singleton access
    static bool init(const Credentials& creds, const char* api_endpoint = nullptr) noexcept;
    static BinanceAuth* getInstance() noexcept { return instance_; }
    static void shutdown() noexcept;
    
    // Authentication methods
    bool authenticate() noexcept;
    bool isAuthenticated() const noexcept { return authenticated_.load(); }
    
    // API Key access (for signed requests)
    const char* getApiKey() const noexcept { return credentials_.api_key; }
    
    // Test connectivity
    bool testConnectivity() noexcept;
    
    // Account information
    bool fetchAccountInfo(char* response, size_t response_size) noexcept;
    bool fetchBalances(char* response, size_t response_size) noexcept;
    
    // Market data (public endpoints - no auth needed)
    bool fetchExchangeInfo(char* response, size_t response_size) noexcept;
    bool fetchTicker24hr(const char* symbol, char* response, size_t response_size) noexcept;
    bool fetchOrderBook(const char* symbol, int limit, char* response, size_t response_size) noexcept;
    
    // Trading endpoints (require signature)
    bool fetchOpenOrders(const char* symbol, char* response, size_t response_size) noexcept;
    bool fetchAllOrders(const char* symbol, char* response, size_t response_size) noexcept;
    
    // Get base URL
    const char* getBaseUrl() const noexcept { 
        return api_endpoint_; 
    }
};

// ============================================================================
// Binance Auth Manager (Singleton Manager)
// ============================================================================
class BinanceAuthManager {
public:
    static bool init(const Credentials& creds, const char* api_endpoint = nullptr) noexcept {
        return BinanceAuth::init(creds, api_endpoint);
    }
    
    static BinanceAuth* getInstance() noexcept {
        return BinanceAuth::getInstance();
    }
    
    static void shutdown() noexcept {
        BinanceAuth::shutdown();
    }
};

} // namespace Trading::Binance