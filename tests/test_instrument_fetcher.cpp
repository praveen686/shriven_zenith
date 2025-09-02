#include "common/logging.h"
#include "trading/config_manager.h"
#include "trading/auth/zerodha/zerodha_auth.h"
#include "trading/market_data/zerodha/zerodha_instrument_fetcher.h"

#include <cstdio>
#include <cstdlib>

using namespace Trading::MarketData::Zerodha;
using namespace Trading::MarketData;

int main(int /*argc*/, char* /*argv*/[]) {
    printf("=== Instrument Fetcher Test ===\n");
    
    // Step 1: Initialize ConfigManager
    printf("1. Initializing ConfigManager...\n");
    const char* master_config = "/home/isoula/om/shriven_zenith/config/master_config.txt";
    if (!Trading::ConfigManager::init(master_config)) {
        fprintf(stderr, "   Failed to initialize ConfigManager\n");
        return 1;
    }
    printf("   ConfigManager initialized successfully\n");
    printf("   Instruments config: %s\n", Trading::ConfigManager::getInstrumentsConfigFile());
    printf("   Instruments data dir: %s\n", Trading::ConfigManager::getInstrumentsDataDir());
    
    // Step 2: Initialize logging
    char log_file[512];
    Trading::ConfigManager::getLogFilePath(log_file, sizeof(log_file), "instrument_test");
    printf("2. Initializing logging to: %s\n", log_file);
    Common::initLogging(log_file);
    
    // Step 3: Load credentials and authenticate
    printf("3. Loading credentials from environment...\n");
    Trading::Zerodha::Credentials creds{};
    const char* user_id = getenv("ZERODHA_USER_ID");
    const char* password = getenv("ZERODHA_PASSWORD");
    const char* totp_secret = getenv("ZERODHA_TOTP_SECRET");
    const char* api_key = getenv("ZERODHA_API_KEY");
    const char* api_secret = getenv("ZERODHA_API_SECRET");
    
    if (!user_id || !password || !totp_secret || !api_key || !api_secret) {
        fprintf(stderr, "   Missing Zerodha credentials in environment\n");
        Common::shutdownLogging();
        return 1;
    }
    
    strncpy(creds.user_id, user_id, sizeof(creds.user_id) - 1);
    strncpy(creds.password, password, sizeof(creds.password) - 1);
    strncpy(creds.totp_secret, totp_secret, sizeof(creds.totp_secret) - 1);
    strncpy(creds.api_key, api_key, sizeof(creds.api_key) - 1);
    strncpy(creds.api_secret, api_secret, sizeof(creds.api_secret) - 1);
    
    printf("   Loaded credentials for user: %s\n", creds.user_id);
    
    // Step 4: Initialize ZerodhaAuthManager
    printf("4. Initializing ZerodhaAuthManager...\n");
    if (!Trading::Zerodha::ZerodhaAuthManager::init(creds)) {
        fprintf(stderr, "   Failed to initialize ZerodhaAuthManager\n");
        Common::shutdownLogging();
        return 1;
    }
    
    auto* auth = Trading::Zerodha::ZerodhaAuthManager::getInstance();
    if (!auth || !auth->isAuthenticated()) {
        fprintf(stderr, "   Authentication failed\n");
        Common::shutdownLogging();
        return 1;
    }
    printf("   ✓ Authentication successful\n");
    
    // Step 5: Create instrument fetcher
    printf("5. Creating instrument fetcher...\n");
    ZerodhaInstrumentFetcher fetcher(auth);
    
    // Step 6: Try to fetch instruments (or load from cache)
    printf("6. Fetching instruments from Zerodha...\n");
    
    // Allocate buffer for CSV data
    constexpr size_t BUFFER_SIZE = 10 * 1024 * 1024;  // 10MB for ~90k instruments
    char* csv_buffer = new char[BUFFER_SIZE];  // AUDIT_IGNORE: Test code
    memset(csv_buffer, 0, BUFFER_SIZE);
    
    bool fetched = false;
    
    // First try to load from today's cache
    char cache_file[512];
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    snprintf(cache_file, sizeof(cache_file), "%s/instruments_%04d%02d%02d.csv",
            Trading::ConfigManager::getInstrumentsDataDir(),
            tm_info->tm_year + 1900,
            tm_info->tm_mon + 1,
            tm_info->tm_mday);
    
    printf("   Checking for cached file: %s\n", cache_file);
    if (fetcher.loadFromCSV(cache_file)) {
        printf("   ✓ Loaded %zu instruments from cache\n", fetcher.getInstrumentCount());
        fetched = true;
    } else {
        printf("   Cache not found, fetching from API...\n");
        if (fetcher.fetchAllInstruments(csv_buffer, BUFFER_SIZE)) {
            printf("   ✓ Fetched %zu instruments from API\n", fetcher.getInstrumentCount());
            
            // Save to cache
            if (fetcher.saveToCSV(cache_file)) {
                printf("   ✓ Saved to cache: %s\n", cache_file);
            }
            fetched = true;
        } else {
            printf("   ✗ Failed to fetch instruments\n");
        }
    }
    
    delete[] csv_buffer;  // AUDIT_IGNORE: Test code
    
    if (!fetched) {
        fprintf(stderr, "Failed to get instruments\n");
        Trading::Zerodha::ZerodhaAuthManager::shutdown();
        Common::shutdownLogging();
        return 1;
    }
    
    // Step 7: Find NIFTY instruments
    printf("\n7. Finding NIFTY instruments...\n");
    
    // Find NIFTY spot
    Instrument nifty_spot;
    if (fetcher.findSpot("NIFTY", nifty_spot)) {
        printf("   NIFTY Spot:\n");
        printf("     Symbol: %s\n", nifty_spot.trading_symbol);
        printf("     Price: ₹%.2f\n", static_cast<double>(nifty_spot.last_price) / 100.0);
    } else {
        printf("   NIFTY spot not found\n");
    }
    
    // Find NIFTY future
    Instrument nifty_future;
    if (fetcher.findNearestFuture("NIFTY", nifty_future)) {
        printf("   NIFTY Future:\n");
        printf("     Symbol: %s\n", nifty_future.trading_symbol);
        printf("     Expiry: %d days\n", nifty_future.getDaysToExpiry());
        printf("     Lot size: %lu\n", nifty_future.lot_size);
        printf("     Price: ₹%.2f\n", static_cast<double>(nifty_future.last_price) / 100.0);
    } else {
        printf("   NIFTY future not found\n");
    }
    
    // Find NIFTY options (±5 strikes)
    printf("   NIFTY Options (±5 strikes):\n");
    Instrument options[100];
    size_t option_count = fetcher.findOptionChain("NIFTY", 5, options, 100);
    printf("   Found %zu options\n", option_count);
    
    // Display first few options
    for (size_t i = 0; i < option_count && i < 10; ++i) {
        const auto& opt = options[i];
        const char* type = (opt.type == InstrumentType::OPTION_CALL) ? "CALL" : "PUT";
        printf("     %s: Strike ₹%.0f %s, %d days to expiry\n",
               opt.trading_symbol,
               static_cast<double>(opt.strike_price) / 100.0,
               type,
               opt.getDaysToExpiry());
    }
    
    // Step 8: Find BANKNIFTY instruments
    printf("\n8. Finding BANKNIFTY instruments...\n");
    
    Instrument bnf_future;
    if (fetcher.findNearestFuture("BANKNIFTY", bnf_future)) {
        printf("   BANKNIFTY Future:\n");
        printf("     Symbol: %s\n", bnf_future.trading_symbol);
        printf("     Expiry: %d days\n", bnf_future.getDaysToExpiry());
        printf("     Lot size: %lu\n", bnf_future.lot_size);
    } else {
        printf("   BANKNIFTY future not found\n");
    }
    
    // Step 9: Find stock instruments
    printf("\n9. Finding stock instruments...\n");
    
    const char* stocks[] = {"RELIANCE", "TCS", "INFY"};
    for (const char* stock : stocks) {
        Instrument stock_instruments[50];
        size_t count = fetcher.findByUnderlying(stock, stock_instruments, 50);
        printf("   %s: Found %zu instruments\n", stock, count);
        
        // Show spot and nearest future
        for (size_t i = 0; i < count && i < 3; ++i) {
            const auto& inst = stock_instruments[i];
            const char* type_str = "UNKNOWN";
            switch (inst.type) {
                case InstrumentType::EQUITY: type_str = "EQUITY"; break;
                case InstrumentType::FUTURE: type_str = "FUTURE"; break;
                case InstrumentType::OPTION_CALL: type_str = "CALL"; break;
                case InstrumentType::OPTION_PUT: type_str = "PUT"; break;
                default: break;
            }
            printf("     - %s (%s)\n", inst.trading_symbol, type_str);
        }
    }
    
    // Step 10: Summary
    printf("\n10. Summary:\n");
    printf("   Total instruments: %zu\n", fetcher.getInstrumentCount());
    
    uint64_t last_update = fetcher.getLastUpdateTime();
    if (last_update > 0) {
        auto update_time = std::chrono::system_clock::time_point(
            std::chrono::nanoseconds(last_update)
        );
        auto time_t_update = std::chrono::system_clock::to_time_t(update_time);
        printf("   Last updated: %s", ctime(&time_t_update));
    }
    
    // Cleanup
    printf("\n11. Cleaning up...\n");
    Trading::Zerodha::ZerodhaAuthManager::shutdown();
    LOG_INFO("=== Instrument Fetcher Test Complete ===");
    Common::shutdownLogging();
    
    printf("\n=== Test Complete ===\n");
    printf("Check log file for details: %s\n", log_file);
    
    return 0;
}