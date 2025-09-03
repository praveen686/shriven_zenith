// ============================================================================
// trader_main.cpp - Main Entry Point for Ultra-Low Latency Trading System
// ============================================================================

#include "common/logging.h"
#include "common/time_utils.h"
#include "common/types.h"
#include "common/thread_utils.h"

#include "config/config.h"
#include "trading/auth/zerodha/zerodha_auth.h"
#include "trading/market_data/zerodha/zerodha_instrument_fetcher.h"
#include "trading/market_data/zerodha/kite_ws_client.h"
#include "trading/market_data/zerodha/kite_symbol_resolver.h"
#include "trading/market_data/binance/binance_ws_client.h"
#include "trading/market_data/order_book.h"
#include "common/lf_queue.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

// Global shutdown flag
static std::atomic<bool> g_shutdown{false};

// Global market data queue and WebSocket clients
static Common::LFQueue<Trading::MarketData::MarketUpdate, 262144>* g_market_queue = nullptr;
static Trading::MarketData::Zerodha::KiteWSClient* g_kite_client = nullptr;
static Trading::MarketData::Binance::BinanceWSClient* g_binance_client = nullptr;
static Trading::MarketData::OrderBookManager<1000>* g_book_manager = nullptr;

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
    
    // Display instrument type counts
    printf("\n[INSTRUMENT TYPES]\n");
    auto counts = fetcher->countByType();
    printf("Equity:      %6zu\n", counts.equity);
    printf("Futures:     %6zu\n", counts.futures);
    printf("Call Options:%6zu\n", counts.option_calls);
    printf("Put Options: %6zu\n", counts.option_puts);
    printf("Currency:    %6zu\n", counts.currency);
    printf("Commodity:   %6zu\n", counts.commodity);
    printf("Index:       %6zu\n", counts.index);
    if (counts.unknown > 0) {
        printf("Unknown:     %6zu\n", counts.unknown);
    }
    printf("---------------------\n");
    printf("TOTAL:       %6zu\n", counts.total);
    
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
    const auto& cfg = Trading::ConfigManager::getConfig();
    if (cfg.cpu_config.trading_core >= 0) {
        if (Common::setThreadCore(cfg.cpu_config.trading_core)) {
            LOG_INFO("Main trading thread pinned to CPU core %d", cfg.cpu_config.trading_core);
        } else {
            LOG_WARN("Failed to set thread affinity to core %d", cfg.cpu_config.trading_core);
        }
    }
    
    // Set real-time scheduling priority for low latency
    if (cfg.cpu_config.enable_realtime) {
        struct sched_param param;
        param.sched_priority = cfg.cpu_config.realtime_priority;
        if (sched_setscheduler(0, SCHED_FIFO, &param) == 0) {
            LOG_INFO("Set real-time scheduling (SCHED_FIFO) with priority %d", cfg.cpu_config.realtime_priority);
        } else {
            LOG_WARN("Failed to set real-time scheduling (may need sudo/CAP_SYS_NICE)");
        }
    }
    
    // Step 2: ConfigManager already initialized in main()
    LOG_INFO("ConfigManager already initialized: env_file=%s", cfg.paths.env_file);
    
    // Step 3: Load environment variables from .env file
    FILE* env_file = fopen(cfg.paths.env_file, "r");  // AUDIT_IGNORE: Init-time only
    if (env_file) {
        char line[1024];
        while (fgets(line, sizeof(line), env_file)) {
            // Skip comments and empty lines
            if (line[0] == '#' || line[0] == '\n') continue;
            
            // Remove trailing newline
            size_t len = strlen(line);
            if (len > 0 && line[len-1] == '\n') {
                line[len-1] = '\0';
            }
            
            // Find the = separator
            char* equals = strchr(line, '=');
            if (equals) {
                *equals = '\0';
                const char* key = line;
                const char* value = equals + 1;
                
                // Set the environment variable
                setenv(key, value, 1);
                LOG_INFO("Loaded env var: %s", key);
            }
        }
        fclose(env_file);
        LOG_INFO("Environment variables loaded from %s", cfg.paths.env_file);
    } else {
        LOG_WARN("Could not open env file: %s", cfg.paths.env_file);
    }
    
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
            cfg.paths.data_dir,
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
        
        char* buffer = new char[10 * 1024 * 1024];  // AUDIT_IGNORE: Init-time only, 10MB for instruments
        if (fetcher->fetchAllInstruments(buffer, 10 * 1024 * 1024)) {
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
    
    // Step 7: Initialize market data connection
    LOG_INFO("Initializing market data connection...");
    printf("   Initializing WebSocket connection...\n");
    
    // Create market update queue
    g_market_queue = new Common::LFQueue<Trading::MarketData::MarketUpdate, 262144>();  // AUDIT_IGNORE: Init-time only
    
    // Initialize Kite WebSocket client
    Trading::MarketData::Zerodha::KiteWSClient::Config ws_config;
    ws_config.api_key = api_key;
    ws_config.access_token = auth->getAccessToken();
    ws_config.ws_endpoint = cfg.zerodha.websocket_endpoint;
    ws_config.persist_ticks = cfg.zerodha.persist_ticks;
    ws_config.persist_orderbook = cfg.zerodha.persist_orderbook;
    
    g_kite_client = new Trading::MarketData::Zerodha::KiteWSClient(g_market_queue, ws_config);  // AUDIT_IGNORE: Init-time only
    
    // Initialize symbol resolver
    Trading::MarketData::Zerodha::KiteSymbolResolver resolver(fetcher);
    
    // Get subscription list based on config
    LOG_INFO("Resolving %s components...", cfg.zerodha.indices[0]);
    auto subscription = resolver.getSubscriptionList(cfg);
    LOG_INFO("Subscription list: %zu tokens", subscription.count);
    printf("   ✓ Resolved %zu instruments for subscription\n", subscription.count);
    
    // Initialize order book manager
    g_book_manager = new Trading::MarketData::OrderBookManager<1000>();  // AUDIT_IGNORE: Init-time only
    
    // Map tokens to order books
    for (size_t i = 0; i < subscription.count && i < 100; ++i) {  // Limit to 100 for initial setup
        g_book_manager->registerInstrument(subscription.tokens[i], static_cast<Common::TickerId>(i));
        g_kite_client->mapTokenToTicker(subscription.tokens[i], static_cast<Common::TickerId>(i));
    }
    
    // Start WebSocket client
    LOG_INFO("Starting WebSocket client...");
    g_kite_client->start();
    
    // Connect to Kite
    LOG_INFO("Connecting to Kite WebSocket...");
    if (!g_kite_client->connect()) {
        LOG_ERROR("Failed to connect to Kite WebSocket");
        printf("   ✗ Failed to connect to WebSocket\n");
        delete fetcher;  // AUDIT_IGNORE: Init-time only
        return false;
    }
    printf("   ✓ Connected to Kite WebSocket\n");
    
    // Subscribe to tokens
    LOG_INFO("Subscribing to market data...");
    size_t subscribe_count = std::min(subscription.count, static_cast<size_t>(100));  // Start with 100 instruments
    g_kite_client->subscribeTokens(subscription.tokens, subscribe_count, 
                                Trading::MarketData::Zerodha::KiteMode::MODE_FULL);
    printf("   ✓ Subscribed to %zu instruments\n", subscribe_count);
    
    delete fetcher;  // AUDIT_IGNORE: Init-time only
    
    // Step 8: Initialize Binance WebSocket client
    LOG_INFO("Initializing Binance WebSocket client...");
    printf("   Initializing Binance connection...\n");
    
    g_binance_client = new Trading::MarketData::Binance::BinanceWSClient();  // AUDIT_IGNORE: Init-time only
    
    Trading::MarketData::Binance::BinanceWSClient::Config binance_config;
    binance_config.use_testnet = false;  // Use live data
    binance_config.reconnect_interval_ms = 5000;
    binance_config.cpu_affinity = cfg.cpu_config.market_data_core;
    
    if (!g_binance_client->init(binance_config)) {
        LOG_ERROR("Failed to initialize Binance WebSocket client");
        printf("   ✗ Failed to initialize Binance client\n");
        delete g_binance_client;  // AUDIT_IGNORE: Init-time only
        g_binance_client = nullptr;
    } else {
        // Set up callbacks for Binance data
        g_binance_client->setTickCallback([](const Trading::MarketData::Binance::BinanceTickData* /* tick */) {
            static uint64_t binance_tick_count = 0;
            binance_tick_count++;
            
            if (binance_tick_count % 1000 == 1) {
                LOG_INFO("[BINANCE] Processed %lu ticks", binance_tick_count);
            }
        });
        
        g_binance_client->setDepthCallback([](const Trading::MarketData::Binance::BinanceDepthUpdate* /* depth */) {
            static uint64_t binance_depth_count = 0;
            binance_depth_count++;
            
            if (binance_depth_count % 1000 == 1) {
                LOG_INFO("[BINANCE] Processed %lu depth updates", binance_depth_count);
            }
        });
        
        // Start Binance client
        if (g_binance_client->start()) {
            LOG_INFO("Binance WebSocket client started");
            printf("   ✓ Binance client started\n");
            
            // Wait a bit for connection
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            // Subscribe to major crypto pairs
            if (g_binance_client->isConnected()) {
                g_binance_client->subscribeTicker("btcusdt");
                g_binance_client->subscribeTicker("ethusdt");
                g_binance_client->subscribeDepth("btcusdt", 5);
                LOG_INFO("Subscribed to BTCUSDT and ETHUSDT");
                printf("   ✓ Subscribed to BTC and ETH data\n");
            }
        } else {
            LOG_WARN("Failed to start Binance WebSocket client");
            printf("   ⚠ Binance client not started\n");
        }
    }
    
    LOG_INFO("=== SYSTEM INITIALIZATION COMPLETE ===");
    return true;
}

// Shutdown system components
static void shutdownSystem() {
    LOG_INFO("=== SYSTEM SHUTDOWN STARTED ===");
    
    // Shutdown Kite WebSocket client
    if (g_kite_client) {
        LOG_INFO("Shutting down Kite WebSocket client...");
        g_kite_client->stop();
        delete g_kite_client;  // AUDIT_IGNORE: Shutdown-time only
        g_kite_client = nullptr;
    }
    
    // Shutdown Binance WebSocket client
    if (g_binance_client) {
        LOG_INFO("Shutting down Binance WebSocket client...");
        g_binance_client->stop();
        delete g_binance_client;  // AUDIT_IGNORE: Shutdown-time only
        g_binance_client = nullptr;
    }
    
    // Cleanup order book manager
    if (g_book_manager) {
        delete g_book_manager;  // AUDIT_IGNORE: Shutdown-time only
        g_book_manager = nullptr;
    }
    
    // Cleanup market queue
    if (g_market_queue) {
        delete g_market_queue;  // AUDIT_IGNORE: Shutdown-time only
        g_market_queue = nullptr;
    }
    
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
    uint64_t tick_count = 0;
    Common::Price last_bid = 0;
    Common::Price last_ask = 0;
    auto last_status_time = std::chrono::steady_clock::now();
    
    while (!g_shutdown.load()) {
        auto now = std::chrono::steady_clock::now();
        
        // Process market data from queue
        if (g_market_queue) {
            const Trading::MarketData::MarketUpdate* update = g_market_queue->getNextToRead();
            if (update) {
                tick_count++;
                
                // Update order book
                if (update->ticker_id != Common::TickerId_INVALID && g_book_manager) {
                    auto* book = g_book_manager->getOrderBook(update->ticker_id);
                    if (book) {
                        // Update bid/ask based on update type
                        if (update->update_type == Trading::MarketData::MessageType::MARKET_DATA) {
                            book->updateBid(update->bid_price, update->bid_qty, 1, 0);
                            book->updateAsk(update->ask_price, update->ask_qty, 1, 0);
                            book->updateTimestamp(update->timestamp);
                            
                            last_bid = update->bid_price;
                            last_ask = update->ask_price;
                        }
                    }
                }
                
                g_market_queue->updateReadIndex();
                
                // Log every 1000 ticks
                if (tick_count % 1000 == 0) {
                    LOG_INFO("Processed %llu ticks, Last: Bid=%ld Ask=%ld Spread=%ld",
                            static_cast<unsigned long long>(tick_count),
                            last_bid,
                            last_ask,
                            last_ask - last_bid);
                }
            }
        }
        
        // Print status every 30 seconds
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_status_time).count() >= 30) {
            LOG_INFO("Trading loop status: iterations=%llu, ticks=%llu, authenticated=%s",
                    static_cast<unsigned long long>(loop_count),
                    static_cast<unsigned long long>(tick_count),
                    auth->isAuthenticated() ? "true" : "false");
            
            // Print order book summary
            if (g_book_manager) {
                Trading::MarketData::OrderBookManager<1000>::ActiveBooks active;
                size_t book_count = g_book_manager->getActiveBooks(active);
                LOG_INFO("Active order books: %zu", book_count);
            }
            
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
        // - Signal generation based on order books
        // - Risk checks
        // - Order placement
        
        loop_count++;
        
        // High-frequency loop - minimal sleep
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    LOG_INFO("=== TRADING LOOP STOPPED === (iterations=%llu)", 
            static_cast<unsigned long long>(loop_count));
}

int main(int argc, char* argv[]) {
    // Display startup banner
    displayBanner();
    
    // Initialize ConfigManager first (needed for log directory)
    const char* config_file = "/home/isoula/om/shriven_zenith/config/config.toml";
    if (!Trading::ConfigManager::init(config_file)) {
        fprintf(stderr, "Failed to initialize ConfigManager\n");
        return 1;
    }
    
    // Initialize logging using ConfigManager's log path
    char log_file[512];
    snprintf(log_file, sizeof(log_file), "%s/trader_main.log", Trading::ConfigManager::getConfig().paths.logs_dir);
    
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