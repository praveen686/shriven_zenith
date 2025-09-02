#include "config_manager.h"
#include <cstdlib>
#include <unistd.h>

namespace Trading {

// Static member definitions
MasterConfig ConfigManager::config_{};
bool ConfigManager::initialized_ = false;

auto ConfigManager::init(const char* config_file) noexcept -> bool {
    if (initialized_) {
        return true;
    }
    
    // Default config file path
    const char* config_path = config_file ? config_file : 
        "/home/isoula/om/shriven_zenith/config/master_config.txt";
    
    FILE* fp = fopen(config_path, "r");  // AUDIT_IGNORE: Init-time only
    if (!fp) {
        fprintf(stderr, "Failed to open config file: %s\n", config_path);
        return false;
    }
    
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        
        parseLine(line);
    }
    
    fclose(fp);
    
    // Validate and create directories
    if (!validatePaths()) {
        fprintf(stderr, "Failed to validate/create directories\n");
        return false;
    }
    
    config_.is_valid = true;
    initialized_ = true;
    
    // Load environment variables
    if (!loadEnvFile()) {
        fprintf(stderr, "Warning: Failed to load .env file from %s\n", config_.env_file);
    }
    
    return true;
}

auto ConfigManager::parseLine(const char* line) noexcept -> void {
    char key[128];
    char value[384];
    
    // Parse key=value format
    if (sscanf(line, "%127[^=]=%383s", key, value) != 2) {
        return;
    }
    
    // Map keys to config fields
    if (strcmp(key, "ENV_FILE") == 0) {
        strncpy(config_.env_file, value, sizeof(config_.env_file) - 1);
    } else if (strcmp(key, "LOGS_DIR") == 0) {
        strncpy(config_.logs_dir, value, sizeof(config_.logs_dir) - 1);
    } else if (strcmp(key, "DATA_DIR") == 0) {
        strncpy(config_.data_dir, value, sizeof(config_.data_dir) - 1);
    } else if (strcmp(key, "AUTH_LOGS_DIR") == 0) {
        strncpy(config_.auth_logs_dir, value, sizeof(config_.auth_logs_dir) - 1);
    } else if (strcmp(key, "MARKET_DATA_LOGS_DIR") == 0) {
        strncpy(config_.market_data_logs_dir, value, sizeof(config_.market_data_logs_dir) - 1);
    } else if (strcmp(key, "ORDER_GW_LOGS_DIR") == 0) {
        strncpy(config_.order_gw_logs_dir, value, sizeof(config_.order_gw_logs_dir) - 1);
    } else if (strcmp(key, "STRATEGY_LOGS_DIR") == 0) {
        strncpy(config_.strategy_logs_dir, value, sizeof(config_.strategy_logs_dir) - 1);
    } else if (strcmp(key, "CACHE_DIR") == 0) {
        strncpy(config_.cache_dir, value, sizeof(config_.cache_dir) - 1);
    } else if (strcmp(key, "SESSION_DIR") == 0) {
        strncpy(config_.session_dir, value, sizeof(config_.session_dir) - 1);
    } else if (strcmp(key, "MARKET_DATA_DIR") == 0) {
        strncpy(config_.market_data_dir, value, sizeof(config_.market_data_dir) - 1);
    } else if (strcmp(key, "MAX_LOG_FILE_SIZE_MB") == 0) {
        config_.max_log_file_size_mb = static_cast<uint32_t>(atoi(value));
    } else if (strcmp(key, "LOG_ROTATION_COUNT") == 0) {
        config_.log_rotation_count = static_cast<uint32_t>(atoi(value));
    } else if (strcmp(key, "LOG_LEVEL") == 0) {
        strncpy(config_.log_level, value, sizeof(config_.log_level) - 1);
    } else if (strcmp(key, "INSTRUMENTS_CONFIG_FILE") == 0) {
        strncpy(config_.instruments_config_file, value, sizeof(config_.instruments_config_file) - 1);
    } else if (strcmp(key, "INSTRUMENTS_DATA_DIR") == 0) {
        strncpy(config_.instruments_data_dir, value, sizeof(config_.instruments_data_dir) - 1);
    } else if (strcmp(key, "FETCH_INTERVAL_MINUTES") == 0) {
        config_.fetch_interval_minutes = static_cast<uint32_t>(atoi(value));
    } else if (strcmp(key, "CACHE_EXPIRY_HOURS") == 0) {
        config_.cache_expiry_hours = static_cast<uint32_t>(atoi(value));
    }
}

auto ConfigManager::createDirectory(const char* path) noexcept -> bool {
    struct stat st{};
    
    // Check if directory exists
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;  // Directory already exists
        }
        fprintf(stderr, "Path exists but is not a directory: %s\n", path);
        return false;
    }
    
    // Create directory with parent directories
    char temp[512];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    
    // Create parent directories if needed
    for (char* p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
                fprintf(stderr, "Failed to create directory %s: %s\n", temp, strerror(errno));
                return false;
            }
            *p = '/';
        }
    }
    
    // Create the final directory
    if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create directory %s: %s\n", temp, strerror(errno));
        return false;
    }
    
    return true;
}

auto ConfigManager::validatePaths() noexcept -> bool {
    // Check if .env file exists
    if (access(config_.env_file, R_OK) != 0) {
        fprintf(stderr, "Warning: .env file not accessible: %s\n", config_.env_file);
        // Not a fatal error - can still use environment variables
    }
    
    // Create directories if they don't exist
    bool success = true;
    
    success &= createDirectory(config_.logs_dir);
    success &= createDirectory(config_.data_dir);
    success &= createDirectory(config_.auth_logs_dir);
    success &= createDirectory(config_.market_data_logs_dir);
    success &= createDirectory(config_.order_gw_logs_dir);
    success &= createDirectory(config_.strategy_logs_dir);
    success &= createDirectory(config_.cache_dir);
    success &= createDirectory(config_.session_dir);
    success &= createDirectory(config_.market_data_dir);
    success &= createDirectory(config_.instruments_data_dir);
    
    return success;
}

auto ConfigManager::loadEnvFile() noexcept -> bool {
    if (!config_.env_file[0]) {
        return false;
    }
    
    return EnvLoader::loadFromFile(config_.env_file);
}

auto ConfigManager::getLogFilePath(char* buffer, size_t len, const char* component) noexcept -> bool {
    if (!initialized_ || !buffer || len == 0 || !component) {
        return false;
    }
    
    // Get timestamp for log file name
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);
    
    // Determine which log directory to use based on component prefix
    const char* log_dir = config_.logs_dir;
    if (strncmp(component, "auth", 4) == 0) {
        log_dir = config_.auth_logs_dir;
    } else if (strncmp(component, "market_data", 11) == 0) {
        log_dir = config_.market_data_logs_dir;
    } else if (strncmp(component, "order_gw", 8) == 0) {
        log_dir = config_.order_gw_logs_dir;
    } else if (strncmp(component, "strategy", 8) == 0) {
        log_dir = config_.strategy_logs_dir;
    }
    
    // Format: /path/to/logs/component/component_YYYYMMDD_HHMMSS.log
    int written = snprintf(buffer, len, "%s/%s_%s.log", log_dir, component, timestamp);
    
    return written > 0 && static_cast<size_t>(written) < len;
}

// ============================================================================
// EnvLoader Implementation
// ============================================================================

auto EnvLoader::loadFromFile(const char* filepath) noexcept -> bool {
    FILE* fp = fopen(filepath, "r");  // AUDIT_IGNORE: Init-time only
    if (!fp) {
        return false;
    }
    
    char line[1024];
    int loaded_count = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        
        if (setEnvVar(line)) {
            loaded_count++;
        }
    }
    
    fclose(fp);
    return loaded_count > 0;
}

auto EnvLoader::setEnvVar(const char* line) noexcept -> bool {
    char key[128];
    char value[896];
    
    // Parse KEY=value format (value may contain spaces)
    const char* equals = strchr(line, '=');
    if (!equals) {
        return false;
    }
    
    // Extract key
    size_t key_len = static_cast<size_t>(equals - line);
    if (key_len >= sizeof(key)) {
        return false;
    }
    
    strncpy(key, line, key_len);
    key[key_len] = '\0';
    
    // Extract value (skip '=' and trim newline)
    const char* value_start = equals + 1;
    strncpy(value, value_start, sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';
    
    // Trim trailing newline/carriage return
    size_t value_len = strlen(value);
    while (value_len > 0 && (value[value_len - 1] == '\n' || value[value_len - 1] == '\r')) {
        value[--value_len] = '\0';
    }
    
    // Set environment variable
    if (setenv(key, value, 1) != 0) {  // AUDIT_IGNORE: Init-time only
        return false;
    }
    
    return true;
}

auto EnvLoader::getEnv(const char* key, const char* default_val) noexcept -> const char* {
    const char* value = getenv(key);
    return value ? value : default_val;
}

} // namespace Trading