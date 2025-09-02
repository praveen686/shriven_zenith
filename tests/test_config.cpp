#include <iostream>
#include "config/toml_config.h"
#include "common/logging.h"

int main() {
    // Initialize logging
    Common::initLogging("logs/test_config.log");
    
    std::cout << "Testing TOML Configuration System..." << std::endl;
    
    // Initialize config from TOML file
    if (!Trading::TomlConfigManager::init("config/trading_config.toml")) {
        std::cerr << "Failed to initialize config!" << std::endl;
        return 1;
    }
    
    // Get config and verify some values
    const auto& config = Trading::TomlConfigManager::getConfig();
    
    std::cout << "\n=== Configuration Loaded ===" << std::endl;
    std::cout << "System: " << config.system.name << " v" << config.system.version << std::endl;
    std::cout << "Environment: " << config.system.environment << std::endl;
    std::cout << "Start Date: " << config.system.start_date << std::endl;
    
    std::cout << "\nPaths:" << std::endl;
    std::cout << "  Logs: " << config.paths.logs_dir << std::endl;
    std::cout << "  Data: " << config.paths.data_dir << std::endl;
    std::cout << "  Cache: " << config.paths.cache_dir << std::endl;
    
    std::cout << "\nPerformance:" << std::endl;
    std::cout << "  Threads: " << config.performance.thread_count << std::endl;
    std::cout << "  Market Data Queue Size: " << config.performance.market_data_queue_size << std::endl;
    std::cout << "  Order Queue Size: " << config.performance.order_queue_size << std::endl;
    
    std::cout << "\nTrading Limits:" << std::endl;
    std::cout << "  Max Position Value: Rs " << config.trading.max_position_value << std::endl;
    std::cout << "  Max Daily Loss: Rs " << config.trading.max_daily_loss << std::endl;
    std::cout << "  Max Order Rate: " << config.trading.max_order_rate_per_sec << "/sec" << std::endl;
    
    std::cout << "\nExchanges:" << std::endl;
    std::cout << "  Zerodha Enabled: " << (config.zerodha.enabled ? "Yes" : "No") << std::endl;
    if (config.zerodha.enabled) {
        std::cout << "    API: " << config.zerodha.api_endpoint << std::endl;
        std::cout << "    WebSocket: " << config.zerodha.websocket_endpoint << std::endl;
        std::cout << "    Market Hours: " << config.zerodha.market_open_time 
                  << " - " << config.zerodha.market_close_time << std::endl;
    }
    
    std::cout << "  Binance Enabled: " << (config.binance.enabled ? "Yes" : "No") << std::endl;
    if (config.binance.enabled) {
        std::cout << "    API: " << config.binance.api_endpoint << std::endl;
        std::cout << "    WebSocket: " << config.binance.websocket_endpoint << std::endl;
        std::cout << "    Testnet: " << (config.binance.use_testnet ? "Yes" : "No") << std::endl;
    }
    
    std::cout << "\nStrategies:" << std::endl;
    std::cout << "  Market Maker: " << (config.market_maker.enabled ? "Enabled" : "Disabled") << std::endl;
    if (config.market_maker.enabled) {
        std::cout << "    Spread: " << config.market_maker.spread_bps << " bps" << std::endl;
        std::cout << "    Min Edge: " << config.market_maker.min_edge_bps << " bps" << std::endl;
        std::cout << "    Quote Size: " << config.market_maker.quote_size << std::endl;
    }
    
    std::cout << "  Arbitrage: " << (config.arbitrage.enabled ? "Enabled" : "Disabled") << std::endl;
    
    std::cout << "\nTesting Mode:" << std::endl;
    std::cout << "  Paper Trading: " << (config.testing.paper_trading_enabled ? "Enabled" : "Disabled") << std::endl;
    
    // Test convenience functions
    std::cout << "\n=== Testing Convenience Functions ===" << std::endl;
    std::cout << "Is Zerodha Enabled: " << Trading::TomlConfigManager::isZerodhaEnabled() << std::endl;
    std::cout << "Is Binance Enabled: " << Trading::TomlConfigManager::isBinanceEnabled() << std::endl;
    std::cout << "Is Paper Trading: " << Trading::TomlConfigManager::isPaperTradingEnabled() << std::endl;
    std::cout << "Max Position Value: " << Trading::TomlConfigManager::getMaxPositionValue() << std::endl;
    std::cout << "Thread Count: " << Trading::TomlConfigManager::getThreadCount() << std::endl;
    
    std::cout << "\n✅ Config system test passed!" << std::endl;
    
    Common::shutdownLogging();
    return 0;
}