// ============================================================================
// trader_main.cpp - Main Entry Point for Ultra-Low Latency Trading System
// ============================================================================

#include "common/logging.h"
#include "common/time_utils.h"
#include "common/types.h"

#include "trading/config_manager.h"
#include "trading/auth/zerodha/zerodha_auth.h"
#include "trading/market_data/zerodha/zerodha_instrument_fetcher.h"

#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

// Global shutdown flag
static std::atomic<bool> g_shutdown{false};

// Signal handler for graceful shutdown
static void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        LOG_INFO("Received shutdown signal %d", signal);
        g_shutdown.store(true);
    }
}

// Display system startup banner
static void displayBanner() {
    printf("\n");
    printf("=======================================================\n");
    printf("     SHRIVEN ZENITH - Ultra-Low Latency Trading       \n");
    printf("=======================================================\n");
    printf("Version: 1.0.0\n");
    printf("Build:   %s %s\n", __DATE__, __TIME__);
    printf("Mode:    PRODUCTION\n");
    printf("=======================================================\n\n");
}

// Display instruments summary
static void displayInstrumentsSummary(Trading::MarketData::Zerodha::ZerodhaInstrumentFetcher* fetcher) {
    if (!fetcher) return;
    
    printf("\n[INSTRUMENTS SUMMARY]\n");
    printf("---------------------\n");
    printf("Total instruments:   %zu\n", fetcher->getInstrumentCount());
    
    // Try to find key instruments
    Trading::MarketData::Instrument inst;
    
    // NIFTY
    if (fetcher->findNearestFuture("NIFTY", inst)) {
        printf("NIFTY Future:       %s (Exp: %d days)\n", 
               inst.trading_symbol, inst.getDaysToExpiry());
    }
    
    // BANKNIFTY
    if (fetcher->findNearestFuture("BANKNIFTY", inst)) {
        printf("BANKNIFTY Future:   %s (Exp: %d days)\n", 
               inst.trading_symbol, inst.getDaysToExpiry());
    }
    
    // TODO: Add instrument type counting when iterator methods are available
    
    printf("---------------------\n\n");
}

// Display account information after authentication
static void displayAccountInfo(Trading::Zerodha::ZerodhaAuth* auth) {
    printf("\n[ACCOUNT INFORMATION]\n");
    printf("---------------------\n");
    
    char response[8192];
    
    // Fetch and display profile
    if (auth->fetchProfile(response, sizeof(response))) {
        // Parse key fields
        char* user_id = strstr(response, "\"user_id\":\"");
        if (user_id) {
            user_id += 11;
            char* end = strchr(user_id, '"');
            if (end) {
                *end = '\0';
                printf("User ID:     %s\n", user_id);
                *end = '"';
            }
        }
        
        char* email = strstr(response, "\"email\":\"");
        if (email) {
            email += 9;
            char* end = strchr(email, '"');
            if (end) {
                *end = '\0';
                printf("Email:       %s\n", email);
                *end = '"';
            }
        }
        
        char* user_name = strstr(response, "\"user_name\":\"");
        if (user_name) {
            user_name += 13;
            char* end = strchr(user_name, '"');
            if (end) {
                *end = '\0';
                printf("Name:        %s\n", user_name);
                *end = '"';
            }
        }
        
        char* broker = strstr(response, "\"broker\":\"");
        if (broker) {
            broker += 10;
            char* end = strchr(broker, '"');
            if (end) {
                *end = '\0';
                printf("Broker:      %s\n", broker);
                *end = '"';
            }
        }
    }
    
    // Fetch and display funds
    if (auth->fetchFunds(response, sizeof(response))) {
        char* cash = strstr(response, "\"cash\":");
        if (cash) {
            cash += 7;
            double available_cash = strtod(cash, nullptr);
            printf("Cash:        ₹%.2f\n", available_cash);
        }
        
        char* margin = strstr(response, "\"available\":{\"adhoc_margin\":");
        if (margin) {
            margin += 29;
            double available_margin = strtod(margin, nullptr);
            printf("Margin:      ₹%.2f\n", available_margin);
        }
    }
    
    // Fetch and display positions
    if (auth->fetchPositions(response, sizeof(response))) {
        int position_count = 0;
        char* ptr = response;
        while ((ptr = strstr(ptr, "\"tradingsymbol\":")) != nullptr) {
            position_count++;
            ptr++;
        }
        printf("Positions:   %d\n", position_count);
    }
    
    // Fetch and display holdings
    if (auth->fetchHoldings(response, sizeof(response))) {
        int holdings_count = 0;
        char* ptr = response;
        while ((ptr = strstr(ptr, "\"tradingsymbol\":")) != nullptr) {
            holdings_count++;
            ptr++;
        }
        printf("Holdings:    %d\n", holdings_count);
    }
    
    // Fetch and display orders
    if (auth->fetchOrders(response, sizeof(response))) {
        int orders_count = 0;
        char* ptr = response;
        while ((ptr = strstr(ptr, "\"order_id\":")) != nullptr) {
            orders_count++;
            ptr++;
        }
        printf("Orders:      %d (today)\n", orders_count);
    }
    
    printf("---------------------\n\n");
}

// Initialize system components
static bool initializeSystem() {
    LOG_INFO("=== SYSTEM INITIALIZATION STARTED ===");
    
    // Step 1: Set thread affinity and priority for main thread
    // TODO: Add thread affinity and priority when ThreadUtils is available
    
    // Step 2: ConfigManager already initialized in main()
    LOG_INFO("ConfigManager already initialized: env_file=%s", Trading::ConfigManager::getEnvFile());
    
    // Step 3: Load environment variables
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "source %s 2>/dev/null", Trading::ConfigManager::getEnvFile());
    system(cmd);  // AUDIT_IGNORE: Init-time only
    
    // Step 4: Initialize Zerodha authentication
    LOG_INFO("Initializing Zerodha authentication...");
    
    // Load credentials from environment
    Trading::Zerodha::Credentials creds{};
    const char* user_id = getenv("ZERODHA_USER_ID");
    const char* password = getenv("ZERODHA_PASSWORD");
    const char* totp_secret = getenv("ZERODHA_TOTP_SECRET");
    const char* api_key = getenv("ZERODHA_API_KEY");
    const char* api_secret = getenv("ZERODHA_API_SECRET");
    
    if (!user_id || !password || !totp_secret || !api_key || !api_secret) {
        LOG_ERROR("Missing Zerodha credentials in environment");
        return false;
    }
    
    strncpy(creds.user_id, user_id, sizeof(creds.user_id) - 1);
    strncpy(creds.password, password, sizeof(creds.password) - 1);
    strncpy(creds.totp_secret, totp_secret, sizeof(creds.totp_secret) - 1);
    strncpy(creds.api_key, api_key, sizeof(creds.api_key) - 1);
    strncpy(creds.api_secret, api_secret, sizeof(creds.api_secret) - 1);
    
    if (!Trading::Zerodha::ZerodhaAuthManager::init(creds)) {
        LOG_ERROR("Failed to initialize ZerodhaAuthManager");
        return false;
    }
    
    auto* auth = Trading::Zerodha::ZerodhaAuthManager::getInstance();
    if (!auth) {
        LOG_ERROR("Failed to get ZerodhaAuth instance");
        return false;
    }
    
    // Step 5: Authenticate with Zerodha
    LOG_INFO("Authenticating with Zerodha...");
    if (!auth->isAuthenticated()) {
        LOG_ERROR("Authentication failed");
        return false;
    }
    
    LOG_INFO("Zerodha authentication successful");
    LOG_INFO("Access token: %.20s...", auth->getAccessToken());
    
    // Display account information
    displayAccountInfo(auth);
    
    // Step 6: Initialize and fetch instruments
    LOG_INFO("Fetching market instruments...");
    printf("   Fetching market instruments...\n");
    
    // Create instrument fetcher
    auto* fetcher = new Trading::MarketData::Zerodha::ZerodhaInstrumentFetcher(auth);  // AUDIT_IGNORE: Init-time only
    
    // Try to load from cache or fetch from API
    char cache_file[512];
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    snprintf(cache_file, sizeof(cache_file), "%s/instruments_%04d%02d%02d.csv",
            Trading::ConfigManager::getInstrumentsDataDir(),
            tm_info->tm_year + 1900,
            tm_info->tm_mon + 1,
            tm_info->tm_mday);
    
    bool instruments_loaded = false;
    
    // First try to load from today's cache
    if (fetcher->loadFromCSV(cache_file)) {
        LOG_INFO("Loaded %zu instruments from cache", fetcher->getInstrumentCount());
        printf("   ✓ Loaded %zu instruments from cache\n", fetcher->getInstrumentCount());
        instruments_loaded = true;
    } else {
        // Fetch from API
        LOG_INFO("Cache not found, fetching from API...");
        printf("   Fetching from Zerodha API...\n");
        
        char* buffer = new char[1024 * 1024];  // AUDIT_IGNORE: Init-time only
        if (fetcher->fetchAllInstruments(buffer, 1024 * 1024)) {
            LOG_INFO("Fetched %zu instruments from API", fetcher->getInstrumentCount());
            printf("   ✓ Fetched %zu instruments\n", fetcher->getInstrumentCount());
            
            // Save to cache
            if (fetcher->saveToCSV(cache_file)) {
                LOG_INFO("Saved instruments to cache");
                printf("   ✓ Cached for future use\n");
            }
            instruments_loaded = true;
        } else {
            LOG_ERROR("Failed to fetch instruments");
            printf("   ✗ Failed to fetch instruments\n");
        }
        delete[] buffer;  // AUDIT_IGNORE: Init-time only
    }
    
    if (instruments_loaded) {
        displayInstrumentsSummary(fetcher);
    }
    
    // Store fetcher globally (would be better in a context object)
    // For now, we'll just delete it as we don't use it in the trading loop yet
    delete fetcher;  // AUDIT_IGNORE: Init-time only
    
    LOG_INFO("=== SYSTEM INITIALIZATION COMPLETE ===");
    return true;
}

// Shutdown system components
static void shutdownSystem() {
    LOG_INFO("=== SYSTEM SHUTDOWN STARTED ===");
    
    // Shutdown Zerodha authentication
    LOG_INFO("Shutting down Zerodha authentication...");
    Trading::Zerodha::ZerodhaAuthManager::shutdown();
    
    // ConfigManager doesn't need explicit shutdown
    
    LOG_INFO("=== SYSTEM SHUTDOWN COMPLETE ===");
}

// Main trading loop
static void runTradingLoop() {
    LOG_INFO("=== TRADING LOOP STARTED ===");
    
    auto* auth = Trading::Zerodha::ZerodhaAuthManager::getInstance();
    if (!auth) {
        LOG_ERROR("No auth instance available");
        return;
    }
    
    uint64_t loop_count = 0;
    auto last_status_time = std::chrono::steady_clock::now();
    
    while (!g_shutdown.load()) {
        auto now = std::chrono::steady_clock::now();
        
        // Print status every 30 seconds
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_status_time).count() >= 30) {
            LOG_INFO("Trading loop status: iterations=%llu, authenticated=%s",
                    static_cast<unsigned long long>(loop_count),
                    auth->isAuthenticated() ? "true" : "false");
            
            // Check if token needs refresh
            if (auth->needsRefresh()) {
                LOG_INFO("Token needs refresh, refreshing...");
                if (auth->refreshToken()) {
                    LOG_INFO("Token refreshed successfully");
                } else {
                    LOG_ERROR("Token refresh failed");
                }
            }
            
            last_status_time = now;
        }
        
        // TODO: Add actual trading logic here
        // - Market data processing
        // - Signal generation
        // - Order management
        // - Risk checks
        
        loop_count++;
        
        // Sleep briefly to avoid spinning
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    LOG_INFO("=== TRADING LOOP STOPPED === (iterations=%llu)", 
            static_cast<unsigned long long>(loop_count));
}

int main(int argc, char* argv[]) {
    // Display startup banner
    displayBanner();
    
    // Initialize ConfigManager first (needed for log directory)
    const char* master_config = "/home/isoula/om/shriven_zenith/config/master_config.txt";
    if (!Trading::ConfigManager::init(master_config)) {
        fprintf(stderr, "Failed to initialize ConfigManager\n");
        return 1;
    }
    
    // Initialize logging using ConfigManager's log path
    char log_file[512];
    Trading::ConfigManager::getLogFilePath(log_file, sizeof(log_file), "trader");
    
    Common::initLogging(log_file);
    
    LOG_INFO("========================================");
    LOG_INFO("SHRIVEN ZENITH TRADER STARTING");
    LOG_INFO("========================================");
    LOG_INFO("Version: 1.0.0");
    LOG_INFO("Build: %s %s", __DATE__, __TIME__);
    LOG_INFO("PID: %d", getpid());
    LOG_INFO("Command line: argc=%d", argc);
    for (int i = 0; i < argc; ++i) {
        LOG_INFO("  argv[%d]: %s", i, argv[i]);
    }
    
    // Install signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    LOG_INFO("Signal handlers installed");
    
    // Initialize system
    if (!initializeSystem()) {
        LOG_ERROR("System initialization failed");
        printf("\n[ERROR] System initialization failed\n");
        printf("Check log file for details: %s\n\n", log_file);
        shutdownSystem();
        Common::shutdownLogging();
        return 1;
    }
    
    printf("[SYSTEM] Initialization complete\n");
    printf("[SYSTEM] Starting trading loop...\n");
    printf("[SYSTEM] Press Ctrl+C to shutdown\n\n");
    
    // Run main trading loop
    runTradingLoop();
    
    // Shutdown system
    printf("\n[SYSTEM] Shutting down...\n");
    shutdownSystem();
    
    LOG_INFO("========================================");
    LOG_INFO("SHRIVEN ZENITH TRADER STOPPED");
    LOG_INFO("========================================");
    
    // Shutdown logging
    Common::shutdownLogging();
    
    printf("[SYSTEM] Shutdown complete\n");
    printf("Log file: %s\n\n", log_file);
    
    return 0;
}