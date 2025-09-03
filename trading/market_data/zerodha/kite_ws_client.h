#pragma once

#include "common/types.h"
#include "common/lf_queue.h"
#include "common/mem_pool.h"
#include "common/logging.h"
#include "common/thread_utils.h"
#include "trading/market_data/market_data_consumer.h"

#include <atomic>
#include <thread>
#include <array>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

namespace Trading::MarketData::Zerodha {

using namespace Common;

// Kite binary protocol packet structures
#pragma pack(push, 1)

// Packet modes as per Kite protocol
enum class KiteMode : uint8_t {
    MODE_LTP = 1,      // 8 bytes
    MODE_QUOTE = 2,    // 44 bytes  
    MODE_FULL = 3      // 184 bytes
};

// Binary packet header
struct KitePacketHeader {
    uint16_t num_packets;
    uint16_t packet_length;
};

// LTP packet (8 bytes)
struct KiteLTPPacket {
    uint32_t instrument_token;
    uint32_t last_price;  // Price * 100
};

// Quote packet (44 bytes)
struct KiteQuotePacket {
    uint32_t instrument_token;
    uint32_t last_price;
    uint32_t last_quantity;
    uint32_t average_price;
    uint32_t volume;
    uint32_t buy_quantity;
    uint32_t sell_quantity;
    uint32_t open;
    uint32_t high;
    uint32_t low;
    uint32_t close;
};

// Full packet (184 bytes) - includes market depth
struct KiteFullPacket {
    uint32_t instrument_token;
    uint32_t last_price;
    uint32_t last_quantity;
    uint32_t average_price;
    uint32_t volume;
    uint32_t buy_quantity;
    uint32_t sell_quantity;
    uint32_t open;
    uint32_t high;
    uint32_t low;
    uint32_t close;
    uint32_t last_traded_time;
    uint32_t oi;
    uint32_t oi_day_high;
    uint32_t oi_day_low;
    uint32_t timestamp;
    
    // Market depth - 5 levels each side
    struct DepthItem {
        uint32_t quantity;
        uint32_t price;
        uint16_t orders;
        uint16_t padding;
    } bid[5], ask[5];
};

#pragma pack(pop)

// Processed tick data for internal use
struct alignas(CACHE_LINE_SIZE) KiteTickData {
    TickerId ticker_id{TickerId_INVALID};
    uint32_t instrument_token{0};
    Price last_price{Price_INVALID};
    Qty last_qty{0};
    Qty volume{0};
    Qty oi{0};  // Open Interest for F&O
    Price open{Price_INVALID};
    Price high{Price_INVALID};
    Price low{Price_INVALID};
    Price close{Price_INVALID};
    uint64_t exchange_timestamp_ns{0};
    uint64_t local_timestamp_ns{0};
    
    void reset() noexcept {
        ticker_id = TickerId_INVALID;
        instrument_token = 0;
        last_price = Price_INVALID;
        last_qty = 0;
        volume = 0;
        oi = 0;
        open = Price_INVALID;
        high = Price_INVALID;
        low = Price_INVALID;
        close = Price_INVALID;
        exchange_timestamp_ns = 0;
        local_timestamp_ns = 0;
    }
};

// Market depth update
struct alignas(CACHE_LINE_SIZE) KiteDepthUpdate {
    TickerId ticker_id{TickerId_INVALID};
    uint32_t instrument_token{0};
    uint64_t local_timestamp_ns{0};
    
    static constexpr size_t MAX_DEPTH = 5;  // Kite provides 5 levels
    Price bid_prices[MAX_DEPTH]{};
    Qty bid_qtys[MAX_DEPTH]{};
    uint16_t bid_orders[MAX_DEPTH]{};
    Price ask_prices[MAX_DEPTH]{};
    Qty ask_qtys[MAX_DEPTH]{};
    uint16_t ask_orders[MAX_DEPTH]{};
    uint8_t bid_count{0};
    uint8_t ask_count{0};
    
    void reset() noexcept {
        ticker_id = TickerId_INVALID;
        instrument_token = 0;
        local_timestamp_ns = 0;
        std::memset(bid_prices, 0, sizeof(bid_prices));
        std::memset(bid_qtys, 0, sizeof(bid_qtys));
        std::memset(bid_orders, 0, sizeof(bid_orders));
        std::memset(ask_prices, 0, sizeof(ask_prices));
        std::memset(ask_qtys, 0, sizeof(ask_qtys));
        std::memset(ask_orders, 0, sizeof(ask_orders));
        bid_count = 0;
        ask_count = 0;
    }
};

// WebSocket client for Kite
class KiteWSClient : public IMarketDataConsumer {
public:
    struct Config {
        const char* access_token = nullptr;
        const char* api_key = nullptr;
        const char* ws_endpoint = "wss://ws.kite.trade";
        uint32_t reconnect_interval_ms = 5000;
        uint32_t ping_interval_s = 3;  // Kite requires ping every 3 seconds
        int cpu_affinity = -1;
        bool persist_ticks = false;
        bool persist_orderbook = false;
    };
    
    explicit KiteWSClient(Common::LFQueue<Common::MarketUpdate, 262144>* output_queue, 
                         const Config& config);
    ~KiteWSClient() override;
    
    // IMarketDataConsumer interface
    auto start() -> void override;
    auto stop() -> void override;
    auto connect() -> bool override;
    auto disconnect() -> void override;
    auto subscribe(Common::TickerId ticker_id) -> bool override;
    auto unsubscribe(Common::TickerId ticker_id) -> bool override;
    
    // Kite-specific methods
    auto subscribeTokens(const uint32_t* tokens, size_t count, KiteMode mode) -> bool;
    auto unsubscribeTokens(const uint32_t* tokens, size_t count) -> bool;
    auto setMode(const uint32_t* tokens, size_t count, KiteMode mode) -> bool;
    
    // Map instrument token to internal ticker ID
    auto mapTokenToTicker(uint32_t token, TickerId ticker_id) -> void {
        if (token < MAX_INSTRUMENTS) {
            token_to_ticker_[token] = ticker_id;
        }
    }
    
private:
    // Memory pools - zero allocation design
    static constexpr size_t TICK_POOL_SIZE = 100000;
    static constexpr size_t DEPTH_POOL_SIZE = 10000;
    static constexpr size_t MAX_INSTRUMENTS = 100000;  // Max instrument token value
    static constexpr size_t RECV_BUFFER_SIZE = 65536;
    
    MemoryPool<sizeof(KiteTickData), TICK_POOL_SIZE> tick_pool_;
    MemoryPool<sizeof(KiteDepthUpdate), DEPTH_POOL_SIZE> depth_pool_;
    
    // WebSocket connection
    SSL_CTX* ssl_ctx_{nullptr};
    SSL* ssl_{nullptr};
    int socket_fd_{-1};
    std::atomic<bool> connected_{false};
    
    // Configuration
    Config config_;
    
    // Subscription management
    std::array<std::atomic<bool>, MAX_INSTRUMENTS> subscribed_tokens_{};
    std::array<std::atomic<KiteMode>, MAX_INSTRUMENTS> token_modes_{};
    std::array<TickerId, MAX_INSTRUMENTS> token_to_ticker_{};
    
    // Receive buffer
    alignas(CACHE_LINE_SIZE) uint8_t recv_buffer_[RECV_BUFFER_SIZE];
    size_t recv_buffer_pos_{0};
    
    // Thread management
    std::thread ws_thread_;
    std::thread heartbeat_thread_;
    std::atomic<uint64_t> last_ping_ns_{0};
    std::atomic<uint64_t> last_pong_ns_{0};
    
    // Statistics
    std::atomic<uint64_t> ticks_received_{0};
    std::atomic<uint64_t> ticks_dropped_{0};
    std::atomic<uint64_t> reconnect_count_{0};
    
    // Internal methods
    auto wsThreadMain() -> void;
    auto heartbeatThreadMain() -> void;
    auto processReceivedData(const uint8_t* data, size_t len) -> void;
    auto parseBinaryPacket(const uint8_t* data, size_t len) -> bool;
    auto parseLTPPacket(const KiteLTPPacket* packet) -> void;
    auto parseQuotePacket(const KiteQuotePacket* packet) -> void;
    auto parseFullPacket(const KiteFullPacket* packet) -> void;
    auto sendWebSocketFrame(const uint8_t* data, size_t len, uint8_t opcode = 0x02) -> bool;  // 0x02 = binary frame
    auto sendPing() -> bool;
    auto handleReconnect() -> void;
    auto initSSL() -> bool;
    auto cleanupSSL() -> void;
    auto performWebSocketHandshake() -> bool;
    auto buildSubscribeMessage(const uint32_t* tokens, size_t count, KiteMode mode, uint8_t* buffer) -> size_t;
    
    // Convert Kite price format (price * 100) to internal format
    [[gnu::always_inline]] inline auto convertPrice(uint32_t kite_price) const noexcept -> Price {
        return static_cast<Price>(kite_price);  // Keep as paise for now
    }
    
    [[gnu::always_inline]] inline auto convertQty(uint32_t qty) const noexcept -> Qty {
        return static_cast<Qty>(qty);
    }
};

} // namespace Trading::MarketData::Zerodha