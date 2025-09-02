#pragma once

#include "common/macros.h"
#include "common/types.h"
#include "common/logging.h"
#include <cstdint>
#include <cstring>
#include <chrono>

namespace Trading::Zerodha {

// Fixed-size structures for Zerodha API
struct Credentials {
    char api_key[64]{};
    char api_secret[128]{};
    char user_id[32]{};
    char password[64]{};
    char totp_secret[64]{};
};

struct AuthToken {
    char access_token[256]{};
    char public_token[256]{};
    char refresh_token[256]{};
    uint64_t expiry_timestamp_ns{0};
    
    [[nodiscard]] auto isValid() const noexcept -> bool {
        uint64_t now_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        return now_ns < expiry_timestamp_ns && access_token[0] != '\0';
    }
    
    [[nodiscard]] auto needsRefresh() const noexcept -> bool {
        // Refresh if less than 2 hours remaining
        constexpr uint64_t TWO_HOURS_NS = 2ULL * 60 * 60 * 1000000000ULL;
        uint64_t now_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        return (expiry_timestamp_ns - now_ns) < TWO_HOURS_NS;
    }
};

// TOTP Generator without external dependencies
class TOTPGenerator {
private:
    static constexpr size_t MAX_SECRET_SIZE = 64;
    static constexpr size_t SHA1_SIZE = 20;
    
    uint8_t decoded_secret_[MAX_SECRET_SIZE]{};
    size_t secret_len_{0};
    
public:
    TOTPGenerator() = default;
    
    // Initialize with base32 encoded secret
    [[nodiscard]] auto init(const char* base32_secret) noexcept -> bool;
    
    // Generate 6-digit TOTP code
    [[nodiscard]] auto generate(char* output, size_t len) noexcept -> bool;
    
    // Generate at specific time (for testing)
    [[nodiscard]] auto generateAt(uint64_t timestamp_sec, char* output, size_t len) noexcept -> bool;
    
private:
    // Base32 decode helper
    [[nodiscard]] auto base32Decode(const char* encoded, uint8_t* output, size_t max_len) noexcept -> size_t;
    
    // HMAC-SHA1 implementation (no OpenSSL)
    auto hmacSha1(const uint8_t* key, size_t key_len,
                  const uint8_t* data, size_t data_len,
                  uint8_t* output) noexcept -> void;
    
    // SHA1 implementation
    auto sha1(const uint8_t* data, size_t len, uint8_t* hash) noexcept -> void;
};

// Main authentication class - shared by market_data and order_gw
class alignas(CACHE_LINE_SIZE) ZerodhaAuth {
private:
    Credentials credentials_{};
    AuthToken current_token_{};
    TOTPGenerator totp_gen_{};
    
    // Session file path for caching
    char session_file_[256]{};
    
    // HTTP client state (reusable)
    void* curl_handle_{nullptr};  // CURL* but avoiding include
    char cookie_jar_[256]{};
    
    // HTTP buffers - persist for CURL operations
    char http_url_buffer_[512]{};
    char http_post_buffer_[1024]{};
    char http_response_buffer_[8192]{};
    
    // Rate limiting
    std::atomic<uint32_t> requests_count_{0};
    std::atomic<uint64_t> rate_limit_reset_ns_{0};
    
    // Thread safety for token access
    alignas(CACHE_LINE_SIZE) mutable std::atomic<uint64_t> token_version_{0};
    
public:
    ZerodhaAuth() noexcept;
    ~ZerodhaAuth() noexcept;
    
    // Delete copy/move
    ZerodhaAuth(const ZerodhaAuth&) = delete;
    ZerodhaAuth& operator=(const ZerodhaAuth&) = delete;
    ZerodhaAuth(ZerodhaAuth&&) = delete;
    ZerodhaAuth& operator=(ZerodhaAuth&&) = delete;
    
    // Initialize with credentials
    [[nodiscard]] auto init(const Credentials& creds) noexcept -> bool;
    
    // Load credentials from environment variables
    [[nodiscard]] auto loadFromEnv() noexcept -> bool;
    
    // Load credentials from file
    [[nodiscard]] auto loadFromFile(const char* path) noexcept -> bool;
    
    // Authenticate with Zerodha (blocking - init time only)
    [[nodiscard]] auto authenticate() noexcept -> bool;  // AUDIT_IGNORE: Init-time only
    
    // Try to load cached session first
    [[nodiscard]] auto loadCachedSession() noexcept -> bool;
    
    // Save session to cache
    auto saveCachedSession() const noexcept -> bool;
    
    // Get current access token (fast - for hot path)
    [[nodiscard]] auto getAccessToken() const noexcept -> const char* {
        return current_token_.access_token;
    }
    
    // Check if authenticated
    [[nodiscard]] auto isAuthenticated() const noexcept -> bool {
        return current_token_.isValid();
    }
    
    // Check if token needs refresh
    [[nodiscard]] auto needsRefresh() const noexcept -> bool {
        return current_token_.needsRefresh();
    }
    
    // Refresh token (called by background thread)
    [[nodiscard]] auto refreshToken() noexcept -> bool;
    
    // Get API key for WebSocket connection
    [[nodiscard]] auto getApiKey() const noexcept -> const char* {
        return credentials_.api_key;
    }
    
    // Sign request for REST API
    auto signRequest(const char* data, char* signature, size_t sig_len) const noexcept -> bool;
    
    // API Methods - fetch various data (init-time only, not for hot path)
    [[nodiscard]] auto fetchProfile(char* buffer, size_t buffer_size) noexcept -> bool;  // AUDIT_IGNORE
    [[nodiscard]] auto fetchPositions(char* buffer, size_t buffer_size) noexcept -> bool;  // AUDIT_IGNORE
    [[nodiscard]] auto fetchHoldings(char* buffer, size_t buffer_size) noexcept -> bool;  // AUDIT_IGNORE
    [[nodiscard]] auto fetchOrders(char* buffer, size_t buffer_size) noexcept -> bool;  // AUDIT_IGNORE
    [[nodiscard]] auto fetchFunds(char* buffer, size_t buffer_size) noexcept -> bool;  // AUDIT_IGNORE
    [[nodiscard]] auto fetchInstruments(const char* exchange, char* buffer, size_t buffer_size) noexcept -> bool;  // AUDIT_IGNORE
    
private:
    // Generic API call helper
    [[nodiscard]] auto makeAPICall(const char* endpoint, char* response_buffer, size_t buffer_size) noexcept -> bool;
    // Authentication flow steps
    [[nodiscard]] auto performLogin() noexcept -> bool;
    [[nodiscard]] auto step1_InitialAuth(char* request_id, size_t len) noexcept -> bool;
    [[nodiscard]] auto step2_SubmitTOTP(const char* request_id) noexcept -> bool;
    [[nodiscard]] auto getRequestTokenFromRedirect(const char* request_id, char* request_token, size_t len) noexcept -> bool;
    [[nodiscard]] auto step3_GetToken(const char* request_token) noexcept -> bool;
    
    // HTTP utilities (using CURL internally)
    [[nodiscard]] auto httpPost(const char* url, const char* data,
                                char* response, size_t max_response) noexcept -> int;
    [[nodiscard]] auto httpGet(const char* url,
                               char* response, size_t max_response) noexcept -> int;
    
    // Response parsing (no JSON library)
    [[nodiscard]] auto extractRequestId(const char* html, char* request_id, size_t len) noexcept -> bool;
    [[nodiscard]] auto extractToken(const char* response, AuthToken& token) noexcept -> bool;
    
    // SHA256 for API signing
    auto sha256(const uint8_t* data, size_t len, uint8_t* hash) const noexcept -> void;
};

// Global instance for the application
class ZerodhaAuthManager {
private:
    static ZerodhaAuth instance_;  // Static instance, no dynamic allocation
    static bool initialized_;
    
public:
    static auto init(const Credentials& creds) noexcept -> bool;
    static auto getInstance() noexcept -> ZerodhaAuth*;
    static auto shutdown() noexcept -> void;
};

} // namespace Trading::Zerodha