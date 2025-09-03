#include "trading/market_data/binance/binance_ws_client.h"
#include "common/logging.h"
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <signal.h>

using namespace Trading::MarketData::Binance;

// Global shutdown flag
std::atomic<bool> g_shutdown{false};

static void signal_handler(int sig) {
    if (sig == SIGINT) {
        std::cout << "\nShutting down..." << std::endl;
        g_shutdown.store(true);
    }
}

int main() {
    // Initialize logging
    Common::initLogging("/tmp/binance_ws_test.log");
    
    std::cout << "==================================" << std::endl;
    std::cout << "Binance WebSocket Client Test" << std::endl;
    std::cout << "==================================" << std::endl;
    
    // Install signal handler
    signal(SIGINT, signal_handler);
    
    // Create WebSocket client
    BinanceWSClient ws_client;
    
    // Configure for live stream
    BinanceWSClient::Config config;
    config.use_testnet = false;  // Use live stream (testnet seems down)
    config.reconnect_interval_ms = 5000;
    config.cpu_affinity = -1;  // No CPU pinning for test
    
    // Initialize client
    if (!ws_client.init(config)) {
        std::cerr << "Failed to initialize WebSocket client" << std::endl;
        return 1;
    }
    
    std::cout << "WebSocket client initialized" << std::endl;
    
    // Set up callbacks
    std::atomic<uint64_t> tick_count{0};
    std::atomic<uint64_t> depth_count{0};
    
    ws_client.setTickCallback([&tick_count](const BinanceTickData* tick) {
        tick_count.fetch_add(1);
        
        // Print first few ticks
        if (tick_count.load() <= 5) {
            std::cout << "TICK: " << tick->symbol 
                     << " Price: " << tick->price 
                     << " Qty: " << tick->qty
                     << " Time: " << tick->exchange_timestamp_ns 
                     << std::endl;
        }
    });
    
    ws_client.setDepthCallback([&depth_count](const BinanceDepthUpdate* depth) {
        depth_count.fetch_add(1);
        
        // Print first few depth updates
        if (depth_count.load() <= 5) {
            std::cout << "DEPTH: Update ID: " << depth->last_update_id
                     << " Bids: " << static_cast<int>(depth->bid_count)
                     << " Asks: " << static_cast<int>(depth->ask_count)
                     << std::endl;
        }
    });
    
    // Start WebSocket client
    if (!ws_client.start()) {
        std::cerr << "Failed to start WebSocket client" << std::endl;
        return 1;
    }
    
    std::cout << "WebSocket client started" << std::endl;
    
    // Subscribe to BTCUSDT trades
    std::this_thread::sleep_for(std::chrono::seconds(2));  // Wait for connection
    
    if (ws_client.isConnected()) {
        std::cout << "Connected! Subscribing to BTCUSDT..." << std::endl;
        ws_client.subscribeTicker("btcusdt");
        ws_client.subscribeDepth("btcusdt", 10);
    } else {
        std::cout << "Not connected yet..." << std::endl;
    }
    
    // Run for a while and print statistics
    auto start_time = std::chrono::steady_clock::now();
    auto last_print = start_time;
    
    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_print).count();
        
        if (elapsed >= 5) {
            std::cout << "\n[STATS] "
                     << "Connected: " << (ws_client.isConnected() ? "YES" : "NO")
                     << " | Ticks: " << tick_count.load()
                     << " | Depth: " << depth_count.load()
                     << " | Received: " << ws_client.getMessagesReceived()
                     << " | Dropped: " << ws_client.getMessagesDropped()
                     << " | Reconnects: " << ws_client.getReconnectCount()
                     << std::endl;
            last_print = now;
        }
        
        // Auto-shutdown after 30 seconds for testing
        auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
        if (total_elapsed >= 30) {
            std::cout << "\nTest duration reached (30s), shutting down..." << std::endl;
            break;
        }
    }
    
    // Stop client
    ws_client.stop();
    
    // Final statistics
    std::cout << "\n==================================" << std::endl;
    std::cout << "Final Statistics:" << std::endl;
    std::cout << "Ticks received: " << tick_count.load() << std::endl;
    std::cout << "Depth updates: " << depth_count.load() << std::endl;
    std::cout << "Total messages: " << ws_client.getMessagesReceived() << std::endl;
    std::cout << "Dropped messages: " << ws_client.getMessagesDropped() << std::endl;
    std::cout << "==================================" << std::endl;
    
    Common::shutdownLogging();
    
    return 0;
}