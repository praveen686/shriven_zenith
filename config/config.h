#pragma once

#include "common/types.h"
#include "common/macros.h"
#include <cstdint>
#include <cstring>

namespace Trading {

// Complete trading system configuration
struct TradingConfig {
    // System info
    struct System {
        char name[64];
        char version[32];
        char environment[32];  // development, staging, production
        char start_date[32];
    } system;
    
    // File paths
    struct Paths {
        char base_dir[256];
        char logs_dir[256];
        char data_dir[256];
        char cache_dir[256];
        char session_dir[256];
        char instruments_dir[256];
        char env_file[256];
    } paths;
    
    // Logging config
    struct Logging {
        char level[16];
        uint32_t max_file_size_mb;
        uint32_t rotation_count;
        bool async_enabled;
        uint64_t latency_target_ns;
    } logging;
    
    // Performance settings
    struct Performance {
        uint32_t thread_count;
        bool cpu_affinity_enabled;
        uint32_t realtime_priority;
        uint32_t memory_pool_size_mb;
        bool use_huge_pages;
        bool numa_aware;
        uint32_t market_data_queue_size;
        uint32_t order_queue_size;
        uint32_t response_queue_size;
    } performance;
    
    // Trading limits and targets
    struct Trading {
        int64_t max_position_value;
        int64_t max_daily_loss;
        uint32_t max_order_rate_per_sec;
        uint32_t max_order_size;
        int64_t position_limit_per_symbol;
        uint64_t market_data_latency_target_ns;
        uint64_t order_placement_latency_target_us;
        uint64_t risk_check_latency_target_ns;
    } trading;
    
    // Zerodha configuration
    struct Zerodha {
        bool enabled;
        char api_endpoint[256];
        char websocket_endpoint[256];
        char instrument_dump_url[256];
        char market_open_time[16];
        char market_close_time[16];
        
        // Index configuration
        char indices[10][32];  // Support up to 10 indices
        uint32_t num_indices;
        bool fetch_spot;
        bool fetch_futures;
        bool fetch_options;
        uint32_t option_strikes;
        char futures_expiry[16];  // current, next, far
        
        // Subscriptions
        uint32_t max_symbols;
        char subscription_mode[32];
        uint32_t tick_batch_size;
        
        // Data persistence
        bool persist_ticks;
        bool persist_orderbook;
        uint32_t tick_file_rotation_mb;
        uint32_t orderbook_snapshot_interval_s;
        
        // Order configuration
        char order_type_default[32];
        char product_type[16];
        char exchange[16];
    } zerodha;
    
    // Binance configuration
    struct Binance {
        bool enabled;
        char api_endpoint[256];
        char testnet_endpoint[256];
        char websocket_endpoint[256];
        bool use_testnet;
        uint32_t weight_limit_per_minute;
        uint32_t order_limit_per_10s;
        uint32_t order_limit_per_day;
        uint32_t depth_levels;
        uint32_t update_speed_ms;
        bool trade_stream_enabled;
        bool ticker_stream_enabled;
        char default_order_type[32];
        char time_in_force[16];
        uint32_t recv_window_ms;
    } binance;
    
    // CPU configuration for thread affinity
    struct CPUConfig {
        int trading_core;        // Main trading thread CPU core (-1 = no affinity)
        int market_data_core;    // Market data thread CPU core
        int order_gateway_core;  // Order gateway thread CPU core
        int logging_core;        // Logging thread CPU core
        int numa_node;          // NUMA node for memory allocation (-1 = default)
        bool enable_realtime;   // Enable real-time scheduling (SCHED_FIFO)
        int realtime_priority;  // Real-time priority (1-99)
    } cpu_config;
    
    // Strategy configuration
    struct MarketMaker {
        bool enabled;
        double spread_bps;
        double min_edge_bps;
        uint32_t quote_size;
        uint32_t inventory_limit;
        uint32_t quote_lifetime_ms;
        bool skew_enabled;
    } market_maker;
    
    struct Arbitrage {
        bool enabled;
        double min_spread_bps;
        double execution_threshold_bps;
        int64_t max_exposure;
    } arbitrage;
    
    // Monitoring
    struct Monitoring {
        bool prometheus_enabled;
        uint32_t prometheus_port;
        uint32_t health_check_port;
        uint32_t metrics_interval_seconds;
    } monitoring;
    
    // Alerts
    struct Alerts {
        bool enabled;
        bool email_enabled;
        bool slack_enabled;
        bool telegram_enabled;
        int64_t pnl_alert_threshold;
        uint64_t latency_alert_threshold_us;
        double error_rate_threshold;
    } alerts;
    
    // Testing
    struct Testing {
        bool paper_trading_enabled;
        bool backtesting_enabled;
        bool simulation_mode;
    } testing;
    
    // Validation
    bool is_valid{false};
};

// Configuration manager for the trading system
class ConfigManager {
private:
    static TradingConfig config_;
    static bool initialized_;
    
    // Parse TOML file (simple parser for our needs)
    static auto parseTomlFile(const char* filepath) noexcept -> bool;
    
    // Parse individual sections
    static auto parseSystemSection(const char* content) noexcept -> bool;
    static auto parsePathsSection(const char* content) noexcept -> bool;
    static auto parseLoggingSection(const char* content) noexcept -> bool;
    static auto parsePerformanceSection(const char* content) noexcept -> bool;
    static auto parseTradingSection(const char* content) noexcept -> bool;
    static auto parseZerodhaSection(const char* content) noexcept -> bool;
    static auto parseBinanceSection(const char* content) noexcept -> bool;
    static auto parseStrategiesSection(const char* content) noexcept -> bool;
    static auto parseMonitoringSection(const char* content) noexcept -> bool;
    static auto parseAlertsSection(const char* content) noexcept -> bool;
    static auto parseTestingSection(const char* content) noexcept -> bool;
    
    // Helper to extract value from key=value line
    static auto extractStringValue(const char* line, const char* key, char* value, size_t max_len) noexcept -> bool;
    static auto extractIntValue(const char* line, const char* key, int64_t* value) noexcept -> bool;
    static auto extractUintValue(const char* line, const char* key, uint64_t* value) noexcept -> bool;
    static auto extractDoubleValue(const char* line, const char* key, double* value) noexcept -> bool;
    static auto extractBoolValue(const char* line, const char* key, bool* value) noexcept -> bool;
    
public:
    // Initialize from TOML config file
    [[nodiscard]] static auto init(const char* config_file = "config/config.toml") noexcept -> bool;
    
    // Get configuration
    [[nodiscard]] static auto getConfig() noexcept -> const TradingConfig& {
        return config_;
    }
    
    // Convenience accessors
    [[nodiscard]] static auto isZerodhaEnabled() noexcept -> bool {
        return config_.zerodha.enabled;
    }
    
    [[nodiscard]] static auto isBinanceEnabled() noexcept -> bool {
        return config_.binance.enabled;
    }
    
    [[nodiscard]] static auto isPaperTradingEnabled() noexcept -> bool {
        return config_.testing.paper_trading_enabled;
    }
    
    [[nodiscard]] static auto getMaxPositionValue() noexcept -> int64_t {
        return config_.trading.max_position_value;
    }
    
    [[nodiscard]] static auto getMaxDailyLoss() noexcept -> int64_t {
        return config_.trading.max_daily_loss;
    }
    
    [[nodiscard]] static auto getMarketDataQueueSize() noexcept -> uint32_t {
        return config_.performance.market_data_queue_size;
    }
    
    [[nodiscard]] static auto getOrderQueueSize() noexcept -> uint32_t {
        return config_.performance.order_queue_size;
    }
    
    [[nodiscard]] static auto getThreadCount() noexcept -> uint32_t {
        return config_.performance.thread_count;
    }
    
    [[nodiscard]] static auto getLogsDir() noexcept -> const char* {
        return config_.paths.logs_dir;
    }
    
    [[nodiscard]] static auto getDataDir() noexcept -> const char* {
        return config_.paths.data_dir;
    }
    
    [[nodiscard]] static auto getCacheDir() noexcept -> const char* {
        return config_.paths.cache_dir;
    }
    
    [[nodiscard]] static auto getZerodhaWebSocketEndpoint() noexcept -> const char* {
        return config_.zerodha.websocket_endpoint;
    }
    
    [[nodiscard]] static auto getBinanceWebSocketEndpoint() noexcept -> const char* {
        return config_.binance.websocket_endpoint;
    }
    
    [[nodiscard]] static auto isInitialized() noexcept -> bool {
        return initialized_ && config_.is_valid;
    }
    
    // Validate configuration
    [[nodiscard]] static auto validateConfig() noexcept -> bool;
    
    // Print configuration (for debugging)
    static auto printConfig() noexcept -> void;
};

// Global accessor function
[[nodiscard]] inline auto getTradingConfig() noexcept -> const TradingConfig& {
    return ConfigManager::getConfig();
}

} // namespace Trading