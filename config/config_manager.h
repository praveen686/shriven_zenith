#pragma once

#include "common/macros.h"
#include "common/types.h"
#include "common/logging.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <errno.h>

namespace Trading {

// Fixed-size configuration structure - no dynamic allocation
struct MasterConfig {
    // File paths
    char env_file[256]{};
    
    // Directory paths
    char logs_dir[256]{};
    char data_dir[256]{};
    char auth_logs_dir[256]{};
    char market_data_logs_dir[256]{};
    char order_gw_logs_dir[256]{};
    char strategy_logs_dir[256]{};
    char cache_dir[256]{};
    char session_dir[256]{};
    char market_data_dir[256]{};
    
    // Runtime settings
    uint32_t max_log_file_size_mb{100};
    uint32_t log_rotation_count{10};
    char log_level[16]{"INFO"};
    
    // Instrument fetching config
    char instruments_config_file[256]{};
    char instruments_data_dir[256]{};
    uint32_t fetch_interval_minutes{60};
    uint32_t cache_expiry_hours{24};
    
    // Binance API config
    char binance_api_endpoint[256]{};
    
    // Validation flags
    bool is_valid{false};
};

class alignas(CACHE_LINE_SIZE) ConfigManager {
private:
    static MasterConfig config_;
    static bool initialized_;
    
    // Create directory if it doesn't exist
    static auto createDirectory(const char* path) noexcept -> bool;
    
    // Parse a single line from config file
    static auto parseLine(const char* line) noexcept -> void;
    
    // Validate all paths exist/can be created
    static auto validatePaths() noexcept -> bool;
    
public:
    // Initialize from master config file
    [[nodiscard]] static auto init(const char* config_file = nullptr) noexcept -> bool;
    
    // Get configuration
    [[nodiscard]] static auto getConfig() noexcept -> const MasterConfig& {
        return config_;
    }
    
    // Get specific paths (convenience methods)
    [[nodiscard]] static auto getEnvFile() noexcept -> const char* {
        return config_.env_file;
    }
    
    [[nodiscard]] static auto getLogsDir() noexcept -> const char* {
        return config_.logs_dir;
    }
    
    [[nodiscard]] static auto getAuthLogsDir() noexcept -> const char* {
        return config_.auth_logs_dir;
    }
    
    [[nodiscard]] static auto getDataDir() noexcept -> const char* {
        return config_.data_dir;
    }
    
    [[nodiscard]] static auto getSessionDir() noexcept -> const char* {
        return config_.session_dir;
    }
    
    [[nodiscard]] static auto getCacheDir() noexcept -> const char* {
        return config_.cache_dir;
    }
    
    [[nodiscard]] static auto getInstrumentsConfigFile() noexcept -> const char* {
        return config_.instruments_config_file;
    }
    
    [[nodiscard]] static auto getInstrumentsDataDir() noexcept -> const char* {
        return config_.instruments_data_dir;
    }
    
    [[nodiscard]] static auto getFetchIntervalMinutes() noexcept -> uint32_t {
        return config_.fetch_interval_minutes;
    }
    
    [[nodiscard]] static auto getBinanceApiEndpoint() noexcept -> const char* {
        return config_.binance_api_endpoint;
    }
    
    // Check if initialized
    [[nodiscard]] static auto isInitialized() noexcept -> bool {
        return initialized_ && config_.is_valid;
    }
    
    // Load environment variables from .env file
    [[nodiscard]] static auto loadEnvFile() noexcept -> bool;
    
    // Get formatted log file path
    static auto getLogFilePath(char* buffer, size_t len, const char* component) noexcept -> bool;
};

// Helper class to load .env file
class EnvLoader {
public:
    // Load environment variables from file
    [[nodiscard]] static auto loadFromFile(const char* filepath) noexcept -> bool;
    
    // Get environment variable with fallback
    [[nodiscard]] static auto getEnv(const char* key, const char* default_val = nullptr) noexcept -> const char*;
    
private:
    // Parse and set a single environment variable
    static auto setEnvVar(const char* line) noexcept -> bool;
};

} // namespace Trading