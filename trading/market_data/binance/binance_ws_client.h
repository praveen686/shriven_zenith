#pragma once

#include "common/types.h"
#include "common/lf_queue.h"
#include "common/mem_pool.h"
#include "common/logging.h"
#include "common/time_utils.h"
#include "common/thread_utils.h"

#include <libwebsockets.h>
#include <atomic>
#include <thread>
#include <array>
#include <cstring>
#include <functional>

namespace Trading::MarketData::Binance {

using namespace Common;

// ============================================================================
// Market Data Message Types - Zero Allocation Design
// ============================================================================

// Define invalid constants if not already defined
constexpr Qty Qty_INVALID = std::numeric_limits<Qty>::max();
constexpr double PRICE_MULTIPLIER = 100000.0;  // 5 decimal places
constexpr double QTY_MULTIPLIER = 100000000.0;  // 8 decimal places for crypto

struct alignas(CACHE_LINE_SIZE) BinanceTickData {
    TickerId ticker_id{TickerId_INVALID};
    Price price{Price_INVALID};
    Qty qty{Qty_INVALID};
    uint64_t exchange_timestamp_ns{0};
    uint64_t local_timestamp_ns{0};
    bool is_buyer_maker{false};
    char symbol[16]{};  // Fixed size for symbol
    
    void reset() noexcept {
        ticker_id = TickerId_INVALID;
        price = Price_INVALID;
        qty = Qty_INVALID;
        exchange_timestamp_ns = 0;
        local_timestamp_ns = 0;
        is_buyer_maker = false;
        std::memset(symbol, 0, sizeof(symbol));
    }
};

struct alignas(CACHE_LINE_SIZE) BinanceDepthUpdate {
    TickerId ticker_id{TickerId_INVALID};
    uint64_t last_update_id{0};
    uint64_t local_timestamp_ns{0};
    
    // Fixed-size arrays for bids/asks (top 10 levels)
    static constexpr size_t MAX_DEPTH = 10;
    Price bid_prices[MAX_DEPTH]{};
    Qty bid_qtys[MAX_DEPTH]{};
    Price ask_prices[MAX_DEPTH]{};
    Qty ask_qtys[MAX_DEPTH]{};
    uint8_t bid_count{0};
    uint8_t ask_count{0};
    
    void reset() noexcept {
        ticker_id = TickerId_INVALID;
        last_update_id = 0;
        local_timestamp_ns = 0;
        std::memset(bid_prices, 0, sizeof(bid_prices));
        std::memset(bid_qtys, 0, sizeof(bid_qtys));
        std::memset(ask_prices, 0, sizeof(ask_prices));
        std::memset(ask_qtys, 0, sizeof(ask_qtys));
        bid_count = 0;
        ask_count = 0;
    }
};

// ============================================================================
// Binance WebSocket Client - Ultra Low Latency Implementation
// ============================================================================

class BinanceWSClient {
public:
    // Configuration
    struct Config {
        const char* ws_url = "wss://stream.binance.com:9443/ws";
        const char* testnet_url = "wss://stream.testnet.binance.vision:9443/ws";
        bool use_testnet = false;
        uint32_t reconnect_interval_ms = 5000;
        uint32_t ping_interval_s = 30;
        int cpu_affinity = -1;  // -1 = no affinity
    };
    
    // Callbacks for market data
    using TickCallback = std::function<void(const BinanceTickData*)>;
    using DepthCallback = std::function<void(const BinanceDepthUpdate*)>;
    
private:
    // Memory pools - pre-allocated, zero dynamic allocation
    static constexpr size_t TICK_POOL_SIZE = 100000;
    static constexpr size_t DEPTH_POOL_SIZE = 10000;
    static constexpr size_t QUEUE_SIZE = 262144;  // 256K, power of 2
    
    MemoryPool<sizeof(BinanceTickData), TICK_POOL_SIZE> tick_pool_;
    MemoryPool<sizeof(BinanceDepthUpdate), DEPTH_POOL_SIZE> depth_pool_;
    
    // Lock-free queues for message passing
    SPSCLFQueue<BinanceTickData*, QUEUE_SIZE> tick_queue_;
    SPSCLFQueue<BinanceDepthUpdate*, QUEUE_SIZE> depth_queue_;
    
    // WebSocket context
    struct lws_context* ws_context_{nullptr};
    std::atomic<struct lws*> ws_connection_{nullptr};
    
    // Receive buffer - fixed size, no allocation
    static constexpr size_t RX_BUFFER_SIZE = 65536;  // 64KB
    alignas(CACHE_LINE_SIZE) char rx_buffer_[RX_BUFFER_SIZE];
    size_t rx_buffer_pos_{0};
    
    // JSON parser buffer - for zero-copy parsing
    static constexpr size_t JSON_BUFFER_SIZE = 8192;
    alignas(CACHE_LINE_SIZE) char json_buffer_[JSON_BUFFER_SIZE];
    
    // Connection state
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> messages_dropped_{0};
    std::atomic<uint64_t> reconnect_count_{0};
    std::atomic<uint64_t> last_ping_time_{0};
    std::atomic<uint64_t> messages_rate_limited_{0};
    
    // Subscribed symbols
    static constexpr size_t MAX_SYMBOLS = 100;
    std::array<const char*, MAX_SYMBOLS> symbols_;
    size_t symbol_count_{0};
    
    // Threading
    std::thread ws_thread_;
    std::thread processor_thread_;
    
    // Configuration
    Config config_;
    
    // Rate limiting
    static constexpr uint32_t MAX_MESSAGES_PER_SECOND = 10000;
    std::atomic<uint32_t> messages_this_second_{0};
    std::atomic<uint64_t> current_second_{0};
    
    // Callbacks
    TickCallback tick_callback_;
    DepthCallback depth_callback_;
    
public:
    BinanceWSClient() = default;
    ~BinanceWSClient() { stop(); }
    
    // Delete copy/move for safety
    BinanceWSClient(const BinanceWSClient&) = delete;
    BinanceWSClient& operator=(const BinanceWSClient&) = delete;
    BinanceWSClient(BinanceWSClient&&) = delete;
    BinanceWSClient& operator=(BinanceWSClient&&) = delete;
    
    // Initialize with configuration
    bool init(const Config& config);
    
    // Start/stop WebSocket connection
    bool start();
    void stop();
    
    // Subscribe to market data streams
    bool subscribeTicker(const char* symbol);
    bool subscribeDepth(const char* symbol, int levels = 10);
    bool subscribeAllTickers();
    
    // Set callbacks
    void setTickCallback(TickCallback cb) { tick_callback_ = std::move(cb); }
    void setDepthCallback(DepthCallback cb) { depth_callback_ = std::move(cb); }
    
    // Statistics
    uint64_t getMessagesReceived() const { return messages_received_.load(std::memory_order_relaxed); }
    uint64_t getMessagesDropped() const { return messages_dropped_.load(std::memory_order_relaxed); }
    uint64_t getReconnectCount() const { return reconnect_count_.load(std::memory_order_relaxed); }
    uint64_t getMessagesRateLimited() const { return messages_rate_limited_.load(std::memory_order_relaxed); }
    bool isConnected() const { return connected_.load(std::memory_order_acquire); }
    
    // WebSocket callback - must be public for C callback
    static int wsCallback(struct lws* wsi, enum lws_callback_reasons reason,
                         void* user, void* in, size_t len);
    
private:
    // WebSocket thread - handles network I/O
    void wsThreadFunc();
    
    // Processor thread - processes messages from queues
    void processorThreadFunc();
    
    // Message parsing - zero allocation
    bool parseTickMessage(const char* json, size_t len, BinanceTickData* tick);
    bool parseDepthMessage(const char* json, size_t len, BinanceDepthUpdate* depth);
    
    // Fast JSON parsing helpers - no allocation
    bool parseDouble(const char* str, double& value);
    bool parseLong(const char* str, uint64_t& value);
    bool extractJsonValue(const char* json, const char* key, char* out, size_t out_len);
    
    // Subscribe helper
    bool sendSubscribeMessage(const char* stream);
};

// ============================================================================
// Inline Performance-Critical Functions
// ============================================================================

inline bool BinanceWSClient::parseDouble(const char* str, double& value) {
    char* end;
    value = strtod(str, &end);
    return end != str;
}

inline bool BinanceWSClient::parseLong(const char* str, uint64_t& value) {
    char* end;
    value = strtoull(str, &end, 10);
    return end != str;
}

} // namespace Trading::MarketData::Binance