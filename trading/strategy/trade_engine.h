#pragma once

#include "common/types.h"
#include "common/lf_queue.h"
#include "common/mem_pool.h"
#include "common/logging.h"
#include "common/thread_utils.h"
#include "common/time_utils.h"

#include "trading/market_data/order_book.h"
#include "order_manager.h"
#include "risk_manager.h"
#include "position_keeper.h"
#include "feature_engine.h"
#include "market_maker.h"
#include "liquidity_taker.h"

namespace Trading {

using namespace Common;

// Forward declarations
class OrderManager;
class RiskManager;
class PositionKeeper;

/// Main trading engine - zero allocation design following CLAUDE.md principles
class TradeEngine {
public:
    /// Client request structure (from trading strategies to exchange)
    struct ClientRequest {
        enum Type : uint8_t {
            NEW_ORDER = 1,
            CANCEL_ORDER = 2,
            MODIFY_ORDER = 3
        };
        
        Type type{NEW_ORDER};
        ClientId client_id{ClientId_INVALID};
        TickerId ticker_id{TickerId_INVALID};
        OrderId order_id{OrderId_INVALID};
        Side side{0};
        Price price{Price_INVALID};
        Qty quantity{0};
        uint64_t timestamp_ns{0};
    };
    
    /// Client response structure (from exchange back to strategies)
    struct ClientResponse {
        enum Type : uint8_t {
            ORDER_ACK = 1,
            ORDER_FILL = 2,
            ORDER_CANCEL = 3,
            ORDER_REJECT = 4
        };
        
        Type type{ORDER_ACK};
        ClientId client_id{ClientId_INVALID};
        TickerId ticker_id{TickerId_INVALID};
        OrderId order_id{OrderId_INVALID};
        Side side{0};
        Price price{Price_INVALID};
        Qty quantity{0};
        Qty leaves_qty{0};
        uint64_t timestamp_ns{0};
    };
    
    /// Market update structure
    struct MarketUpdate {
        enum Type : uint8_t {
            TRADE = 1,
            BID_UPDATE = 2,
            ASK_UPDATE = 3
        };
        
        Type type{TRADE};
        TickerId ticker_id{TickerId_INVALID};
        Price price{Price_INVALID};
        Qty quantity{0};
        Side side{0};
        uint64_t timestamp_ns{0};
    };
    
    // Queue types using Common infrastructure
    using ClientRequestQueue = SPSCLFQueue<ClientRequest*, 65536>;
    using ClientResponseQueue = SPSCLFQueue<ClientResponse*, 65536>;
    using MarketUpdateQueue = SPSCLFQueue<MarketUpdate*, 262144>;
    
    TradeEngine(ClientId client_id,
                ClientRequestQueue* order_requests_out,
                ClientResponseQueue* order_responses_in,
                MarketUpdateQueue* market_updates_in);
    
    ~TradeEngine();
    
    /// Start the trade engine thread
    bool start();
    
    /// Stop the trade engine
    void stop();
    
    /// Main event loop - processes market data and order responses
    void run() noexcept;
    
    /// Send order request to exchange
    void sendOrderRequest(const ClientRequest* request) noexcept;
    
    /// Send a new order (used by strategies)
    void sendOrder(TickerId ticker_id, Side side, Price price, Qty quantity) noexcept;
    
    /// Process market data update
    void onMarketUpdate(const MarketUpdate* update) noexcept;
    
    /// Process order response from exchange
    void onOrderResponse(const ClientResponse* response) noexcept;
    
    /// Get current position for a symbol
    int64_t getPosition(TickerId ticker_id) const noexcept;
    
    /// Get current P&L
    int64_t getTotalPnL() const noexcept;
    
    // Delete copy/move constructors per CLAUDE.md
    TradeEngine(const TradeEngine&) = delete;
    TradeEngine& operator=(const TradeEngine&) = delete;
    TradeEngine(TradeEngine&&) = delete;
    TradeEngine& operator=(TradeEngine&&) = delete;
    
private:
    // Core identifiers
    const ClientId client_id_;
    
    // Lock-free queues for communication
    ClientRequestQueue* order_requests_out_{nullptr};
    ClientResponseQueue* order_responses_in_{nullptr};
    MarketUpdateQueue* market_updates_in_{nullptr};
    
    // Memory pools - pre-allocated, no dynamic allocation
    // Size must be at least 64 bytes for cache-line alignment
    MemoryPool<64, 10000> request_pool_;  // Padded to 64 bytes
    MemoryPool<64, 10000> response_pool_; // Padded to 64 bytes
    MemoryPool<64, 100000> update_pool_;  // Padded to 64 bytes
    
    // Core components
    std::unique_ptr<OrderManager> order_manager_;
    std::unique_ptr<RiskManager> risk_manager_;
    std::unique_ptr<PositionKeeper> position_keeper_;
    std::unique_ptr<FeatureEngine> feature_engine_;
    std::unique_ptr<MarketMaker> market_maker_;
    std::unique_ptr<LiquidityTaker> liquidity_taker_;
    
    // Order books for each symbol
    std::array<MarketData::OrderBook<100>, ME_MAX_TICKERS> order_books_;
    
    // Thread control
    std::atomic<bool> running_{false};
    std::thread engine_thread_;
    
    // Performance tracking
    std::atomic<uint64_t> messages_processed_{0};
    std::atomic<uint64_t> orders_sent_{0};
    std::atomic<uint64_t> last_event_time_ns_{0};
    
    // Internal helper methods
    void processMarketQueue() noexcept;
    void processOrderQueue() noexcept;
    void updateOrderBook(const MarketUpdate* update) noexcept;
    void checkSignals(TickerId ticker_id) noexcept;
};

} // namespace Trading