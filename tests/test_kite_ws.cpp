#include <iostream>
#include <fstream>
#include <cstring>
#include <thread>
#include <chrono>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common/types.h"
#include "common/logging.h"
#include "common/lf_queue.h"
#include "common/time_utils.h"
#include "config/config.h"
#include "trading/market_data/zerodha/kite_ws_client.h"
#include "trading/market_data/zerodha/kite_symbol_resolver.h"
#include "trading/market_data/zerodha/zerodha_instrument_fetcher.h"
#include "trading/market_data/order_book.h"
#include "trading/auth/zerodha/zerodha_auth.h"

using namespace Trading::MarketData;
using namespace Trading::MarketData::Zerodha;
using namespace Common;


// Simple tick writer for persistence
class TickPersister {
public:
    TickPersister(const char* data_dir) {
        // Create data directory if it doesn't exist
        mkdir(data_dir, 0755);
        
        // Create tick data file with timestamp
        auto now = std::chrono::system_clock::now();
        auto time_val = std::chrono::system_clock::to_time_t(now);
        
        char filename[512];
        std::snprintf(filename, sizeof(filename), "%s/ticks_%ld.dat", data_dir, time_val);
        
        tick_file_ = std::fopen(filename, "wb");
        if (tick_file_) {
            std::cout << "Opened tick file: " << filename << std::endl;
        } else {
            std::cerr << "Failed to open tick file: " << filename << std::endl;
        }
        
        // Create order book snapshot file
        std::snprintf(filename, sizeof(filename), "%s/orderbook_%ld.dat", data_dir, time_val);
        orderbook_file_ = std::fopen(filename, "wb");
        if (orderbook_file_) {
            std::cout << "Opened orderbook file: " << filename << std::endl;
        }
    }
    
    ~TickPersister() {
        if (tick_file_) {
            std::fclose(tick_file_);
            std::cout << "Closed tick file. Wrote " << tick_count_ << " ticks" << std::endl;
        }
        if (orderbook_file_) {
            std::fclose(orderbook_file_);
            std::cout << "Closed orderbook file. Wrote " << snapshot_count_ << " snapshots" << std::endl;
        }
    }
    
    void writeTick(const MarketUpdate& update) {
        if (!tick_file_) return;
        
        // Simple binary format for speed
        struct TickRecord {
            uint64_t timestamp_ns;
            TickerId ticker_id;
            Price bid_price;
            Price ask_price;
            Qty bid_qty;
            Qty ask_qty;
            MessageType type;
        } __attribute__((packed));
        
        TickRecord record;
        record.timestamp_ns = getNanosSinceEpoch();
        record.ticker_id = update.ticker_id;
        record.bid_price = update.bid_price;
        record.ask_price = update.ask_price;
        record.bid_qty = update.bid_qty;
        record.ask_qty = update.ask_qty;
        record.type = update.update_type;
        
        std::fwrite(&record, sizeof(record), 1, tick_file_);
        tick_count_++;
        
        // Flush periodically
        if (tick_count_ % 100 == 0) {
            std::fflush(tick_file_);
        }
    }
    
    void writeOrderBookSnapshot(const OrderBook<20>& book) {
        if (!orderbook_file_) return;
        
        auto snapshot = book.getSnapshot();
        std::fwrite(&snapshot, sizeof(snapshot), 1, orderbook_file_);
        snapshot_count_++;
        
        // Flush after each snapshot
        std::fflush(orderbook_file_);
    }
    
    size_t getTickCount() const { return tick_count_; }
    size_t getSnapshotCount() const { return snapshot_count_; }
    
private:
    FILE* tick_file_ = nullptr;
    FILE* orderbook_file_ = nullptr;
    size_t tick_count_ = 0;
    size_t snapshot_count_ = 0;
};

int main() {
    std::cout << "==================================" << std::endl;
    std::cout << "Zerodha WebSocket & Persistence Test" << std::endl;
    std::cout << "==================================" << std::endl;
    
    // Initialize config
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        std::cout << "Current working directory: " << cwd << std::endl;
    }
    
    // Initialize logging first with a temp file
    initLogging("/tmp/test_kite_ws.log");
    
    // Try relative path first, then absolute
    if (!Trading::ConfigManager::init("config/config.toml")) {
        LOG_ERROR("Relative path failed");
        std::cout << "Relative path failed, trying absolute path..." << std::endl;
        if (!Trading::ConfigManager::init("/home/isoula/om/shriven_zenith/config/config.toml")) {
            LOG_ERROR("Absolute path also failed");
            std::cerr << "Failed to load config from both paths" << std::endl;
            return 1;
        }
    }
    const auto& cfg = Trading::ConfigManager::getConfig();
    
    LOG_INFO("Starting Kite WebSocket test");
    
    // Get Zerodha credentials - REQUIRED for production
    char* api_key = std::getenv("ZERODHA_API_KEY");
    char* access_token = std::getenv("ZERODHA_ACCESS_TOKEN");
    
    if (!api_key || !access_token) {
        std::cerr << "\nERROR: Zerodha credentials NOT FOUND!" << std::endl;
        std::cerr << "This is a PRODUCTION system - credentials are REQUIRED" << std::endl;
        std::cerr << "Set ZERODHA_API_KEY and ZERODHA_ACCESS_TOKEN environment variables" << std::endl;
        return 1;  // EXIT - no mock allowed
    }
    
    // Create market update queue
    LFQueue<MarketUpdate, 262144> market_queue;
    
    // Initialize Kite WebSocket client
    KiteWSClient::Config ws_config;
    ws_config.api_key = api_key;
    ws_config.access_token = access_token;
    ws_config.ws_endpoint = cfg.zerodha.websocket_endpoint;
    ws_config.persist_ticks = cfg.zerodha.persist_ticks;
    ws_config.persist_orderbook = cfg.zerodha.persist_orderbook;
    
    KiteWSClient ws_client(&market_queue, ws_config);
    
    // Initialize components
    Trading::Zerodha::ZerodhaAuth auth;
    // For testing, we assume access token is already available from environment
    // In production, would use full auth flow with credentials
    ZerodhaInstrumentFetcher fetcher(&auth);
    KiteSymbolResolver resolver(&fetcher);
    
    // Fetch instruments
    std::cout << "\nFetching Zerodha instruments..." << std::endl;
    // Allocate sufficient buffer for instruments CSV (typically ~5-10MB)
    constexpr size_t BUFFER_SIZE = 10 * 1024 * 1024;  // 10MB
    char* csv_buffer = new char[BUFFER_SIZE];  // AUDIT_IGNORE: Test-time only
    memset(csv_buffer, 0, BUFFER_SIZE);
    
    if (!fetcher.fetchAllInstruments(csv_buffer, BUFFER_SIZE)) {
        std::cerr << "Failed to fetch instruments" << std::endl;
        delete[] csv_buffer;  // AUDIT_IGNORE: Test-time only
        return 1;
    }
    delete[] csv_buffer;  // AUDIT_IGNORE: Test-time only
    std::cout << "Fetched " << fetcher.getInstrumentCount() << " instruments" << std::endl;
    
    // Get subscription list based on config
    std::cout << "\nResolving " << cfg.zerodha.indices[0] << " components..." << std::endl;
    auto subscription = resolver.getSubscriptionList(cfg);
    std::cout << "Subscription list: " << subscription.count << " tokens" << std::endl;
    
    // Initialize order book manager
    OrderBookManager<1000> book_manager;
    
    // Map tokens to order books
    for (size_t i = 0; i < subscription.count; ++i) {
        book_manager.registerInstrument(subscription.tokens[i], static_cast<TickerId>(i));
        ws_client.mapTokenToTicker(subscription.tokens[i], static_cast<TickerId>(i));
    }
    
    // Initialize persistence
    TickPersister persister(cfg.paths.data_dir);
    
    // Start WebSocket client
    std::cout << "\nStarting WebSocket client..." << std::endl;
    ws_client.start();
    
    // Connect to Kite
    std::cout << "Connecting to Kite WebSocket..." << std::endl;
    if (!ws_client.connect()) {
        std::cerr << "Failed to connect to Kite WebSocket" << std::endl;
        return 1;
    }
    
    // Subscribe to tokens
    std::cout << "Subscribing to tokens..." << std::endl;
    ws_client.subscribeTokens(subscription.tokens, 
                             std::min(subscription.count, static_cast<size_t>(10)),  // Limit for testing
                             KiteMode::MODE_FULL);
    
    // Process market updates
    std::cout << "\nProcessing market data for 30 seconds..." << std::endl;
    std::cout << "Data will be persisted to: " << cfg.paths.data_dir << std::endl;
    
    auto start_time = std::chrono::steady_clock::now();
    auto last_snapshot = start_time;
    size_t update_count = 0;
    
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(30)) {
        // Process market updates
        const MarketUpdate* update = market_queue.getNextToRead();
        if (update) {
            update_count++;
            
            // Persist tick data
            if (cfg.zerodha.persist_ticks) {
                persister.writeTick(*update);
            }
            
            // Update order book
            if (update->ticker_id != TickerId_INVALID) {
                // This is simplified - real implementation would update order book properly
                // based on update type (ADD, MODIFY, DELETE)
                
                if (update_count % 100 == 0) {
                    std::cout << "Processed " << update_count << " updates, "
                             << "Persisted " << persister.getTickCount() << " ticks" << std::endl;
                }
            }
            
            market_queue.updateReadIndex();
        }
        
        // Take order book snapshots periodically
        auto now = std::chrono::steady_clock::now();
        if (cfg.zerodha.persist_orderbook && 
            std::chrono::duration_cast<std::chrono::seconds>(now - last_snapshot).count() >= 5) {
            
            // Snapshot all active order books
            OrderBookManager<1000>::ActiveBooks active;
            size_t book_count = book_manager.getActiveBooks(active);
            
            for (size_t i = 0; i < book_count; ++i) {
                persister.writeOrderBookSnapshot(*active.books[i]);
            }
            
            std::cout << "Saved " << book_count << " order book snapshots" << std::endl;
            last_snapshot = now;
        }
        
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    // Stop client
    std::cout << "\nStopping WebSocket client..." << std::endl;
    ws_client.stop();
    
    // Final stats
    std::cout << "\n==================================" << std::endl;
    std::cout << "Test Complete!" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "Total updates processed: " << update_count << std::endl;
    std::cout << "Ticks persisted: " << persister.getTickCount() << std::endl;
    std::cout << "Order book snapshots: " << persister.getSnapshotCount() << std::endl;
    std::cout << "Data saved to: " << cfg.paths.data_dir << std::endl;
    
    shutdownLogging();
    return 0;
}

// REMOVED - NO MOCK TESTS IN PRODUCTION SYSTEM