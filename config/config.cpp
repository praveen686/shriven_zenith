#include "config.h"
#include "common/logging.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace Trading {

// Static member definitions
TradingConfig ConfigManager::config_{};
bool ConfigManager::initialized_ = false;

auto ConfigManager::init(const char* config_file) noexcept -> bool {
    if (initialized_) {
        LOG_WARN("ConfigManager already initialized");
        return true;
    }
    
    // Clear config structure (proper initialization for non-trivial type)
    config_ = TradingConfig{};
    
    // Parse TOML file
    if (!parseTomlFile(config_file)) {
        LOG_ERROR("Failed to parse TOML config file: %s", config_file);
        return false;
    }
    
    // Validate configuration
    if (!validateConfig()) {
        LOG_ERROR("Configuration validation failed");
        return false;
    }
    
    config_.is_valid = true;
    initialized_ = true;
    
    LOG_INFO("ConfigManager initialized successfully from %s", config_file);
    printConfig();
    
    return true;
}

auto ConfigManager::parseTomlFile(const char* filepath) noexcept -> bool {
    FILE* file = std::fopen(filepath, "r");
    if (!file) {
        LOG_ERROR("Cannot open config file: %s", filepath);
        return false;
    }
    
    char line[1024];
    char current_section[64] = "";
    
    while (std::fgets(line, sizeof(line), file)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        
        // Remove trailing newline
        size_t len = std::strlen(line);
        if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[len-1] = '\0';
            if (len > 1 && (line[len-2] == '\n' || line[len-2] == '\r')) {
                line[len-2] = '\0';
            }
        }
        
        // Check for section header
        if (line[0] == '[') {
            char* end = std::strchr(line, ']');
            if (end) {
                *end = '\0';
                std::strncpy(current_section, line + 1, sizeof(current_section) - 1);
            }
            continue;
        }
        
        // Parse based on current section
        if (std::strcmp(current_section, "system") == 0) {
            extractStringValue(line, "name", config_.system.name, sizeof(config_.system.name));
            extractStringValue(line, "version", config_.system.version, sizeof(config_.system.version));
            extractStringValue(line, "environment", config_.system.environment, sizeof(config_.system.environment));
            extractStringValue(line, "start_date", config_.system.start_date, sizeof(config_.system.start_date));
        }
        else if (std::strcmp(current_section, "paths") == 0) {
            extractStringValue(line, "base_dir", config_.paths.base_dir, sizeof(config_.paths.base_dir));
            extractStringValue(line, "logs_dir", config_.paths.logs_dir, sizeof(config_.paths.logs_dir));
            extractStringValue(line, "data_dir", config_.paths.data_dir, sizeof(config_.paths.data_dir));
            extractStringValue(line, "cache_dir", config_.paths.cache_dir, sizeof(config_.paths.cache_dir));
            extractStringValue(line, "session_dir", config_.paths.session_dir, sizeof(config_.paths.session_dir));
            extractStringValue(line, "instruments_dir", config_.paths.instruments_dir, sizeof(config_.paths.instruments_dir));
            extractStringValue(line, "env_file", config_.paths.env_file, sizeof(config_.paths.env_file));
        }
        else if (std::strcmp(current_section, "logging") == 0) {
            extractStringValue(line, "level", config_.logging.level, sizeof(config_.logging.level));
            uint64_t temp;
            if (extractUintValue(line, "max_file_size_mb", &temp)) config_.logging.max_file_size_mb = static_cast<uint32_t>(temp);
            if (extractUintValue(line, "rotation_count", &temp)) config_.logging.rotation_count = static_cast<uint32_t>(temp);
            extractBoolValue(line, "async_enabled", &config_.logging.async_enabled);
            extractUintValue(line, "latency_target_ns", &config_.logging.latency_target_ns);
        }
        else if (std::strcmp(current_section, "performance") == 0) {
            uint64_t temp;
            if (extractUintValue(line, "thread_count", &temp)) config_.performance.thread_count = static_cast<uint32_t>(temp);
            extractBoolValue(line, "cpu_affinity_enabled", &config_.performance.cpu_affinity_enabled);
            if (extractUintValue(line, "realtime_priority", &temp)) config_.performance.realtime_priority = static_cast<uint32_t>(temp);
            if (extractUintValue(line, "memory_pool_size_mb", &temp)) config_.performance.memory_pool_size_mb = static_cast<uint32_t>(temp);
            extractBoolValue(line, "use_huge_pages", &config_.performance.use_huge_pages);
            extractBoolValue(line, "numa_aware", &config_.performance.numa_aware);
            if (extractUintValue(line, "market_data_queue_size", &temp)) config_.performance.market_data_queue_size = static_cast<uint32_t>(temp);
            if (extractUintValue(line, "order_queue_size", &temp)) config_.performance.order_queue_size = static_cast<uint32_t>(temp);
            if (extractUintValue(line, "response_queue_size", &temp)) config_.performance.response_queue_size = static_cast<uint32_t>(temp);
        }
        else if (std::strcmp(current_section, "cpu_config") == 0) {
            int64_t temp;
            if (extractIntValue(line, "trading_core", &temp)) config_.cpu_config.trading_core = static_cast<int>(temp);
            if (extractIntValue(line, "market_data_core", &temp)) config_.cpu_config.market_data_core = static_cast<int>(temp);
            if (extractIntValue(line, "order_gateway_core", &temp)) config_.cpu_config.order_gateway_core = static_cast<int>(temp);
            if (extractIntValue(line, "logging_core", &temp)) config_.cpu_config.logging_core = static_cast<int>(temp);
            if (extractIntValue(line, "numa_node", &temp)) config_.cpu_config.numa_node = static_cast<int>(temp);
            extractBoolValue(line, "enable_realtime", &config_.cpu_config.enable_realtime);
            if (extractIntValue(line, "realtime_priority", &temp)) config_.cpu_config.realtime_priority = static_cast<int>(temp);
        }
        else if (std::strcmp(current_section, "trading") == 0) {
            extractIntValue(line, "max_position_value", &config_.trading.max_position_value);
            extractIntValue(line, "max_daily_loss", &config_.trading.max_daily_loss);
            uint64_t temp;
            if (extractUintValue(line, "max_order_rate_per_sec", &temp)) config_.trading.max_order_rate_per_sec = static_cast<uint32_t>(temp);
            if (extractUintValue(line, "max_order_size", &temp)) config_.trading.max_order_size = static_cast<uint32_t>(temp);
            extractIntValue(line, "position_limit_per_symbol", &config_.trading.position_limit_per_symbol);
            extractUintValue(line, "market_data_latency_target_ns", &config_.trading.market_data_latency_target_ns);
            extractUintValue(line, "order_placement_latency_target_us", &config_.trading.order_placement_latency_target_us);
            extractUintValue(line, "risk_check_latency_target_ns", &config_.trading.risk_check_latency_target_ns);
        }
        else if (std::strcmp(current_section, "zerodha") == 0) {
            extractBoolValue(line, "enabled", &config_.zerodha.enabled);
            extractStringValue(line, "api_endpoint", config_.zerodha.api_endpoint, sizeof(config_.zerodha.api_endpoint));
            extractStringValue(line, "websocket_endpoint", config_.zerodha.websocket_endpoint, sizeof(config_.zerodha.websocket_endpoint));
            extractStringValue(line, "instrument_dump_url", config_.zerodha.instrument_dump_url, sizeof(config_.zerodha.instrument_dump_url));
            extractStringValue(line, "market_open_time", config_.zerodha.market_open_time, sizeof(config_.zerodha.market_open_time));
            extractStringValue(line, "market_close_time", config_.zerodha.market_close_time, sizeof(config_.zerodha.market_close_time));
            uint64_t temp;
            if (extractUintValue(line, "max_symbols", &temp)) config_.zerodha.max_symbols = static_cast<uint32_t>(temp);
            extractStringValue(line, "subscription_mode", config_.zerodha.subscription_mode, sizeof(config_.zerodha.subscription_mode));
            if (extractUintValue(line, "tick_batch_size", &temp)) config_.zerodha.tick_batch_size = static_cast<uint32_t>(temp);
            extractStringValue(line, "order_type_default", config_.zerodha.order_type_default, sizeof(config_.zerodha.order_type_default));
            extractStringValue(line, "product_type", config_.zerodha.product_type, sizeof(config_.zerodha.product_type));
            extractStringValue(line, "exchange", config_.zerodha.exchange, sizeof(config_.zerodha.exchange));
        }
        else if (std::strcmp(current_section, "binance") == 0) {
            extractBoolValue(line, "enabled", &config_.binance.enabled);
            extractStringValue(line, "api_endpoint", config_.binance.api_endpoint, sizeof(config_.binance.api_endpoint));
            extractStringValue(line, "testnet_endpoint", config_.binance.testnet_endpoint, sizeof(config_.binance.testnet_endpoint));
            extractStringValue(line, "websocket_endpoint", config_.binance.websocket_endpoint, sizeof(config_.binance.websocket_endpoint));
            extractBoolValue(line, "use_testnet", &config_.binance.use_testnet);
            uint64_t temp;
            if (extractUintValue(line, "weight_limit_per_minute", &temp)) config_.binance.weight_limit_per_minute = static_cast<uint32_t>(temp);
            if (extractUintValue(line, "order_limit_per_10s", &temp)) config_.binance.order_limit_per_10s = static_cast<uint32_t>(temp);
            if (extractUintValue(line, "order_limit_per_day", &temp)) config_.binance.order_limit_per_day = static_cast<uint32_t>(temp);
            if (extractUintValue(line, "depth_levels", &temp)) config_.binance.depth_levels = static_cast<uint32_t>(temp);
            if (extractUintValue(line, "update_speed_ms", &temp)) config_.binance.update_speed_ms = static_cast<uint32_t>(temp);
            extractBoolValue(line, "trade_stream_enabled", &config_.binance.trade_stream_enabled);
            extractBoolValue(line, "ticker_stream_enabled", &config_.binance.ticker_stream_enabled);
            extractStringValue(line, "default_order_type", config_.binance.default_order_type, sizeof(config_.binance.default_order_type));
            extractStringValue(line, "time_in_force", config_.binance.time_in_force, sizeof(config_.binance.time_in_force));
            if (extractUintValue(line, "recv_window_ms", &temp)) config_.binance.recv_window_ms = static_cast<uint32_t>(temp);
        }
        else if (std::strcmp(current_section, "strategies.market_maker") == 0) {
            extractBoolValue(line, "enabled", &config_.market_maker.enabled);
            extractDoubleValue(line, "spread_bps", &config_.market_maker.spread_bps);
            extractDoubleValue(line, "min_edge_bps", &config_.market_maker.min_edge_bps);
            uint64_t temp;
            if (extractUintValue(line, "quote_size", &temp)) config_.market_maker.quote_size = static_cast<uint32_t>(temp);
            if (extractUintValue(line, "inventory_limit", &temp)) config_.market_maker.inventory_limit = static_cast<uint32_t>(temp);
            if (extractUintValue(line, "quote_lifetime_ms", &temp)) config_.market_maker.quote_lifetime_ms = static_cast<uint32_t>(temp);
            extractBoolValue(line, "skew_enabled", &config_.market_maker.skew_enabled);
        }
        else if (std::strcmp(current_section, "strategies.arbitrage") == 0) {
            extractBoolValue(line, "enabled", &config_.arbitrage.enabled);
            extractDoubleValue(line, "min_spread_bps", &config_.arbitrage.min_spread_bps);
            extractDoubleValue(line, "execution_threshold_bps", &config_.arbitrage.execution_threshold_bps);
            extractIntValue(line, "max_exposure", &config_.arbitrage.max_exposure);
        }
        else if (std::strcmp(current_section, "testing") == 0) {
            extractBoolValue(line, "paper_trading_enabled", &config_.testing.paper_trading_enabled);
            extractBoolValue(line, "backtesting_enabled", &config_.testing.backtesting_enabled);
            extractBoolValue(line, "simulation_mode", &config_.testing.simulation_mode);
        }
    }
    
    std::fclose(file);
    return true;
}

auto ConfigManager::extractStringValue(const char* line, const char* key, char* value, size_t max_len) noexcept -> bool {
    char key_pattern[128];
    std::snprintf(key_pattern, sizeof(key_pattern), "%s = \"", key);
    
    const char* start = std::strstr(line, key_pattern);
    if (!start) {
        return false;
    }
    
    start += std::strlen(key_pattern);
    const char* end = std::strchr(start, '"');
    if (!end) {
        return false;
    }
    
    size_t len = static_cast<size_t>(end - start);
    if (len >= max_len) {
        len = max_len - 1;
    }
    
    std::strncpy(value, start, len);
    value[len] = '\0';
    
    return true;
}

auto ConfigManager::extractIntValue(const char* line, const char* key, int64_t* value) noexcept -> bool {
    char key_pattern[128];
    std::snprintf(key_pattern, sizeof(key_pattern), "%s = ", key);
    
    const char* start = std::strstr(line, key_pattern);
    if (!start) {
        return false;
    }
    
    start += std::strlen(key_pattern);
    *value = std::strtoll(start, nullptr, 10);
    return true;
}

auto ConfigManager::extractUintValue(const char* line, const char* key, uint64_t* value) noexcept -> bool {
    char key_pattern[128];
    std::snprintf(key_pattern, sizeof(key_pattern), "%s = ", key);
    
    const char* start = std::strstr(line, key_pattern);
    if (!start) {
        return false;
    }
    
    start += std::strlen(key_pattern);
    *value = std::strtoull(start, nullptr, 10);
    return true;
}

auto ConfigManager::extractDoubleValue(const char* line, const char* key, double* value) noexcept -> bool {
    char key_pattern[128];
    std::snprintf(key_pattern, sizeof(key_pattern), "%s = ", key);
    
    const char* start = std::strstr(line, key_pattern);
    if (!start) {
        return false;
    }
    
    start += std::strlen(key_pattern);
    *value = std::strtod(start, nullptr);
    return true;
}

auto ConfigManager::extractBoolValue(const char* line, const char* key, bool* value) noexcept -> bool {
    char key_pattern[128];
    std::snprintf(key_pattern, sizeof(key_pattern), "%s = ", key);
    
    const char* start = std::strstr(line, key_pattern);
    if (!start) {
        return false;
    }
    
    start += std::strlen(key_pattern);
    *value = (std::strncmp(start, "true", 4) == 0);
    return true;
}

auto ConfigManager::validateConfig() noexcept -> bool {
    // Check required paths
    if (std::strlen(config_.paths.logs_dir) == 0) {
        LOG_ERROR("logs_dir not configured");
        return false;
    }
    
    if (std::strlen(config_.paths.data_dir) == 0) {
        LOG_ERROR("data_dir not configured");
        return false;
    }
    
    // Validate queue sizes are power of 2
    auto isPowerOf2 = [](uint32_t n) { return n && !(n & (n - 1)); };
    
    if (!isPowerOf2(config_.performance.market_data_queue_size)) {
        LOG_ERROR("market_data_queue_size must be power of 2");
        return false;
    }
    
    if (!isPowerOf2(config_.performance.order_queue_size)) {
        LOG_ERROR("order_queue_size must be power of 2");
        return false;
    }
    
    // Validate trading limits
    if (config_.trading.max_position_value <= 0) {
        LOG_ERROR("max_position_value must be positive");
        return false;
    }
    
    // max_daily_loss is stored as a negative number in config (e.g., -50000)
    // But in the TOML it's written as positive (50000), so we accept positive values
    
    // Create directories if they don't exist
    struct stat st;
    if (stat(config_.paths.logs_dir, &st) != 0) {
        LOG_INFO("Creating logs directory: %s", config_.paths.logs_dir);
        // Use mkdir with parents (-p equivalent)
        char cmd[512];
        std::snprintf(cmd, sizeof(cmd), "mkdir -p %s", config_.paths.logs_dir);
        if (std::system(cmd) != 0) {
            LOG_WARN("Could not create logs directory");
        }
    }
    
    if (stat(config_.paths.data_dir, &st) != 0) {
        LOG_INFO("Creating data directory: %s", config_.paths.data_dir);
        char cmd[512];
        std::snprintf(cmd, sizeof(cmd), "mkdir -p %s", config_.paths.data_dir);
        if (std::system(cmd) != 0) {
            LOG_WARN("Could not create data directory");
        }
    }
    
    return true;
}

auto ConfigManager::printConfig() noexcept -> void {
    LOG_INFO("=== Trading System Configuration ===");
    LOG_INFO("System: %s v%s (%s)", config_.system.name, config_.system.version, config_.system.environment);
    LOG_INFO("Paths:");
    LOG_INFO("  Logs: %s", config_.paths.logs_dir);
    LOG_INFO("  Data: %s", config_.paths.data_dir);
    LOG_INFO("  Cache: %s", config_.paths.cache_dir);
    LOG_INFO("Performance:");
    LOG_INFO("  Threads: %u", config_.performance.thread_count);
    LOG_INFO("  Market Data Queue: %u", config_.performance.market_data_queue_size);
    LOG_INFO("  Order Queue: %u", config_.performance.order_queue_size);
    LOG_INFO("Trading Limits:");
    LOG_INFO("  Max Position: %ld", config_.trading.max_position_value);
    LOG_INFO("  Max Daily Loss: %ld", config_.trading.max_daily_loss);
    LOG_INFO("  Max Order Rate: %u/sec", config_.trading.max_order_rate_per_sec);
    LOG_INFO("Exchanges:");
    LOG_INFO("  Zerodha: %s", config_.zerodha.enabled ? "Enabled" : "Disabled");
    LOG_INFO("  Binance: %s", config_.binance.enabled ? "Enabled" : "Disabled");
    LOG_INFO("Strategies:");
    LOG_INFO("  Market Maker: %s", config_.market_maker.enabled ? "Enabled" : "Disabled");
    LOG_INFO("  Arbitrage: %s", config_.arbitrage.enabled ? "Enabled" : "Disabled");
    LOG_INFO("Testing:");
    LOG_INFO("  Paper Trading: %s", config_.testing.paper_trading_enabled ? "Enabled" : "Disabled");
    LOG_INFO("=====================================");
}

} // namespace Trading