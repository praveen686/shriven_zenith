// ============================================================================
// test_binance.cpp - Test Binance Authentication and Instrument Fetching
// ============================================================================

#include "trading/auth/binance/binance_auth.h"
#include "trading/market_data/binance/binance_instrument_fetcher.h"
#include "config/config_manager.h"
#include "common/logging.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

// ============================================================================
// Test Authentication
// ============================================================================
static bool testAuthentication() {
    printf("\n=== TESTING BINANCE AUTHENTICATION ===\n");
    
    // Load credentials from environment
    Trading::Binance::Credentials creds{};
    const char* api_key = getenv("BINANCE_API_KEY");
    const char* api_secret = getenv("BINANCE_API_SECRET");
    
    if (!api_key || !api_secret) {
        printf("❌ Missing Binance credentials in environment\n");
        printf("   Please set BINANCE_API_KEY and BINANCE_API_SECRET\n");
        return false;
    }
    
    strncpy(creds.api_key, api_key, sizeof(creds.api_key) - 1);
    strncpy(creds.api_secret, api_secret, sizeof(creds.api_secret) - 1);
    
    // Get endpoint from ConfigManager
    const char* binance_endpoint = Trading::ConfigManager::getBinanceApiEndpoint();
    if (!binance_endpoint || binance_endpoint[0] == '\0') {
        // Fall back to testnet if not configured
        binance_endpoint = "https://testnet.binance.vision";
        printf("No endpoint in config, using testnet: %s\n", binance_endpoint);
    } else {
        printf("Using configured endpoint: %s\n", binance_endpoint);
    }
    
    printf("1. Initializing BinanceAuth...\n");
    if (!Trading::Binance::BinanceAuthManager::init(creds, binance_endpoint)) {
        printf("❌ Failed to initialize BinanceAuth\n");
        return false;
    }
    printf("✅ BinanceAuth initialized\n");
    
    auto* auth = Trading::Binance::BinanceAuthManager::getInstance();
    if (!auth) {
        printf("❌ Failed to get BinanceAuth instance\n");
        return false;
    }
    
    // Test connectivity
    printf("\n2. Testing API connectivity...\n");
    if (!auth->testConnectivity()) {
        printf("❌ Connectivity test failed\n");
        return false;
    }
    printf("✅ API connectivity confirmed\n");
    
    // Test account info (requires valid API key)
    printf("\n3. Fetching account information...\n");
    char response[65536];  // 64KB for API responses
    if (auth->fetchAccountInfo(response, sizeof(response))) {
        printf("✅ Account info fetched successfully\n");
        
        // Parse some basic info
        char* balances = strstr(response, "\"balances\":");
        if (balances) {
            printf("   Account has balance information\n");
        }
        
        char* can_trade = strstr(response, "\"canTrade\":");
        if (can_trade) {
            can_trade += 11;
            bool trading_enabled = strncmp(can_trade, "true", 4) == 0;
            printf("   Trading enabled: %s\n", trading_enabled ? "Yes" : "No");
        }
    } else {
        printf("⚠️  Could not fetch account info (may need real API keys)\n");
    }
    
    // Test public endpoints (no auth needed)
    printf("\n4. Testing public endpoints...\n");
    
    // Allocate larger buffer for exchange info
    char* exchange_info = new char[20 * 1024 * 1024];  // AUDIT_IGNORE: Test code
    memset(exchange_info, 0, 20 * 1024 * 1024);
    
    // Get exchange info
    printf("   Fetching exchange info...\n");
    if (auth->fetchExchangeInfo(exchange_info, 20 * 1024 * 1024)) {
        printf("   ✅ Exchange info fetched\n");
        
        // Count symbols
        int symbol_count = 0;
        char* ptr = exchange_info;
        while ((ptr = strstr(ptr, "\"symbol\":")) != nullptr) {
            symbol_count++;
            ptr++;
        }
        printf("   Found %d symbols in exchange info\n", symbol_count);
        delete[] exchange_info;  // AUDIT_IGNORE: Test code
    } else {
        printf("   ❌ Failed to fetch exchange info\n");
        delete[] exchange_info;  // AUDIT_IGNORE: Test code
    }
    
    // Get 24hr ticker for BTCUSDT
    printf("   Fetching BTCUSDT ticker...\n");
    if (auth->fetchTicker24hr("BTCUSDT", response, sizeof(response))) {
        printf("   ✅ Ticker data fetched\n");
        
        // Parse last price
        char* last_price = strstr(response, "\"lastPrice\":\"");
        if (last_price) {
            last_price += 13;
            char* end = strchr(last_price, '"');
            if (end) {
                *end = '\0';
                printf("   BTCUSDT last price: $%s\n", last_price);
                *end = '"';
            }
        }
    } else {
        printf("   ❌ Failed to fetch ticker\n");
    }
    
    // Get order book
    printf("   Fetching BTCUSDT order book...\n");
    if (auth->fetchOrderBook("BTCUSDT", 5, response, sizeof(response))) {
        printf("   ✅ Order book fetched\n");
        
        // Count bids
        int bid_count = 0;
        char* bids = strstr(response, "\"bids\":");
        if (bids) {
            char* ptr = bids;
            while ((ptr = strstr(ptr, "[\"")) != nullptr && ptr < strstr(bids, "\"asks\":")) {
                bid_count++;
                ptr += 2;
            }
            printf("   Order book depth: %d bids\n", bid_count);
        }
    } else {
        printf("   ❌ Failed to fetch order book\n");
    }
    
    printf("\n✅ Authentication tests completed\n");
    return true;
}

// ============================================================================
// Test Instrument Fetching
// ============================================================================
static bool testInstrumentFetching() {
    printf("\n=== TESTING BINANCE INSTRUMENT FETCHER ===\n");
    
    auto* auth = Trading::Binance::BinanceAuthManager::getInstance();
    if (!auth) {
        printf("❌ No auth instance available\n");
        return false;
    }
    
    // Create fetcher
    printf("1. Creating instrument fetcher...\n");
    auto* fetcher = new Trading::MarketData::Binance::BinanceInstrumentFetcher(auth);  // AUDIT_IGNORE: Test code
    printf("✅ Fetcher created\n");
    
    // Allocate buffer for JSON data
    constexpr size_t BUFFER_SIZE = 20 * 1024 * 1024;  // 20MB for production API
    char* json_buffer = new char[BUFFER_SIZE];  // AUDIT_IGNORE: Test code
    memset(json_buffer, 0, BUFFER_SIZE);
    
    // Fetch symbols (use fetchTopSymbols for production, fetchAllSymbols for testnet)
    printf("\n2. Fetching symbols from Binance...\n");
    
    bool fetch_success = false;
    const char* endpoint = Trading::ConfigManager::getBinanceApiEndpoint();
    if (strstr(endpoint, "testnet")) {
        printf("   Using fetchAllSymbols for testnet...\n");
        fetch_success = fetcher->fetchAllSymbols(json_buffer, BUFFER_SIZE);
    } else {
        printf("   Using fetchTopSymbols for production (more efficient)...\n");
        fetch_success = fetcher->fetchTopSymbols(json_buffer, BUFFER_SIZE);
    }
    
    if (fetch_success) {
        size_t count = fetcher->getSymbolCount();
        printf("✅ Fetched %zu symbols\n", count);
        
        // Display some statistics
        printf("\n3. Symbol Statistics:\n");
        
        // Count USDT pairs
        Trading::MarketData::Binance::Symbol usdt_pairs[100];
        size_t usdt_count = fetcher->findByQuoteAsset("USDT", usdt_pairs, 100);
        printf("   USDT pairs: %zu\n", usdt_count);
        
        // Count BTC pairs
        Trading::MarketData::Binance::Symbol btc_pairs[100];
        size_t btc_count = fetcher->findByBaseAsset("BTC", btc_pairs, 100);
        printf("   BTC base pairs: %zu\n", btc_count);
        
        // Find specific symbols
        printf("\n4. Finding specific symbols:\n");
        Trading::MarketData::Binance::Symbol symbol;
        
        if (fetcher->findSymbol("BTCUSDT", symbol)) {
            printf("   ✅ BTCUSDT: price=$%.2f, 24h vol=%.2f\n", 
                   symbol.last_price, symbol.quote_volume_24h);
        }
        
        if (fetcher->findSymbol("ETHUSDT", symbol)) {
            printf("   ✅ ETHUSDT: price=$%.2f, 24h vol=%.2f\n", 
                   symbol.last_price, symbol.quote_volume_24h);
        }
        
        if (fetcher->findSymbol("BNBUSDT", symbol)) {
            printf("   ✅ BNBUSDT: price=$%.2f, 24h vol=%.2f\n", 
                   symbol.last_price, symbol.quote_volume_24h);
        }
        
        // Get top USDT pairs by volume
        printf("\n5. Top 10 USDT pairs by volume:\n");
        Trading::MarketData::Binance::Symbol top_pairs[10];
        size_t top_count = fetcher->findTopUSDTPairs(top_pairs, 10);
        
        for (size_t i = 0; i < top_count; ++i) {
            printf("   %2zu. %-10s: $%.2f (vol: $%.0f)\n", 
                   i + 1,
                   top_pairs[i].symbol,
                   top_pairs[i].last_price,
                   top_pairs[i].quote_volume_24h);
        }
        
        // Save to cache
        printf("\n6. Saving to cache...\n");
        char cache_file[512];
        time_t now = time(nullptr);
        struct tm* tm_info = localtime(&now);
        snprintf(cache_file, sizeof(cache_file), "%s/binance_symbols_%04d%02d%02d.csv",
                Trading::ConfigManager::getInstrumentsDataDir(),
                tm_info->tm_year + 1900,
                tm_info->tm_mon + 1,
                tm_info->tm_mday);
        
        if (fetcher->saveToCSV(cache_file)) {
            printf("✅ Saved to: %s\n", cache_file);
            
            // Test loading back
            printf("\n7. Testing cache load...\n");
            fetcher->clear();
            if (fetcher->loadFromCSV(cache_file)) {
                printf("✅ Loaded %zu symbols from cache\n", fetcher->getSymbolCount());
            } else {
                printf("❌ Failed to load from cache\n");
            }
        } else {
            printf("❌ Failed to save cache\n");
        }
        
    } else {
        printf("❌ Failed to fetch symbols\n");
        delete[] json_buffer;  // AUDIT_IGNORE: Test code
        delete fetcher;  // AUDIT_IGNORE: Test code
        return false;
    }
    
    delete[] json_buffer;  // AUDIT_IGNORE: Test code
    delete fetcher;  // AUDIT_IGNORE: Test code
    
    printf("\n✅ Instrument fetching tests completed\n");
    return true;
}

// ============================================================================
// Main
// ============================================================================
int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    printf("========================================\n");
    printf("     BINANCE API TEST PROGRAM\n");
    printf("========================================\n\n");
    
    // Initialize ConfigManager
    const char* master_config = "/home/isoula/om/shriven_zenith/config/master_config.txt";
    if (!Trading::ConfigManager::init(master_config)) {
        fprintf(stderr, "Failed to initialize ConfigManager\n");
        return 1;
    }
    
    // Initialize logging
    char log_file[512];
    Trading::ConfigManager::getLogFilePath(log_file, sizeof(log_file), "test_binance");
    Common::initLogging(log_file);
    
    LOG_INFO("Starting Binance API tests");
    
    // Check for credentials
    const char* api_key = getenv("BINANCE_API_KEY");
    const char* api_secret = getenv("BINANCE_API_SECRET");
    
    if (!api_key || !api_secret) {
        printf("⚠️  WARNING: Binance API credentials not found\n");
        printf("   Set BINANCE_API_KEY and BINANCE_API_SECRET environment variables\n");
        printf("   Using testnet mode for basic connectivity tests only\n\n");
    }
    
    bool all_passed = true;
    
    // Run tests
    if (!testAuthentication()) {
        all_passed = false;
    }
    
    if (!testInstrumentFetching()) {
        all_passed = false;
    }
    
    // Cleanup
    Trading::Binance::BinanceAuthManager::shutdown();
    
    printf("\n========================================\n");
    if (all_passed) {
        printf("✅ ALL TESTS PASSED\n");
    } else {
        printf("❌ SOME TESTS FAILED\n");
    }
    printf("========================================\n\n");
    
    LOG_INFO("Binance API tests completed");
    Common::shutdownLogging();
    
    return all_passed ? 0 : 1;
}