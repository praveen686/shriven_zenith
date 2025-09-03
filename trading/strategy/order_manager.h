#pragma once

#include "common/types.h"
#include "common/logging.h"
#include "common/macros.h"

namespace Trading {

using namespace Common;

// Forward declaration
class TradeEngine;
class RiskManager;

/// Order state tracking
enum class OrderState : uint8_t {
    INVALID = 0,
    PENDING_NEW = 1,
    LIVE = 2,
    PENDING_CANCEL = 3,
    PENDING_MODIFY = 4,
    FILLED = 5,
    CANCELED = 6,
    REJECTED = 7,
    DEAD = 8
};

/// Order structure - zero allocation design
struct Order {
    OrderId order_id{OrderId_INVALID};
    ClientId client_id{ClientId_INVALID};
    TickerId ticker_id{TickerId_INVALID};
    Side side{0};
    Price price{Price_INVALID};
    Qty original_qty{0};
    Qty filled_qty{0};
    Qty leaves_qty{0};
    OrderState state{OrderState::INVALID};
    uint64_t timestamp_ns{0};
    uint64_t last_update_ns{0};
    
    void reset() noexcept {
        order_id = OrderId_INVALID;
        client_id = ClientId_INVALID;
        ticker_id = TickerId_INVALID;
        side = 0;
        price = Price_INVALID;
        original_qty = 0;
        filled_qty = 0;
        leaves_qty = 0;
        state = OrderState::INVALID;
        timestamp_ns = 0;
        last_update_ns = 0;
    }
    
    bool isActive() const noexcept {
        return state == OrderState::PENDING_NEW ||
               state == OrderState::LIVE ||
               state == OrderState::PENDING_CANCEL ||
               state == OrderState::PENDING_MODIFY;
    }
};

/// Order Manager - manages order lifecycle without dynamic allocation
class OrderManager {
public:
    OrderManager(TradeEngine* trade_engine, RiskManager* risk_manager);
    ~OrderManager() = default;
    
    /// Create new order (returns nullptr if pool exhausted)
    Order* createOrder(TickerId ticker_id, Side side, Price price, Qty quantity) noexcept;
    
    /// Cancel existing order
    bool cancelOrder(OrderId order_id) noexcept;
    
    /// Modify existing order
    bool modifyOrder(OrderId order_id, Price new_price, Qty new_qty) noexcept;
    
    /// Update order state from exchange response
    void onOrderUpdate(OrderId order_id, OrderState new_state, 
                      Qty filled_qty, Qty leaves_qty) noexcept;
    
    /// Get order by ID (O(1) lookup)
    Order* getOrder(OrderId order_id) noexcept {
        const size_t idx = order_id % MAX_ORDERS;
        auto& entry = static_cast<OrderEntry&>(orders_[idx]);
        if (entry.active && entry.order.order_id == order_id) {
            return &entry.order;
        }
        return nullptr;
    }
    
    /// Get all active orders for a symbol
    size_t getActiveOrders(TickerId ticker_id, Order** output, size_t max_orders) noexcept;
    
    /// Cancel all orders for a symbol
    void cancelAllOrders(TickerId ticker_id) noexcept;
    
    /// Move orders to maintain best bid/ask
    void moveOrders(TickerId ticker_id, Price bid_price, Price ask_price, Qty clip) noexcept;
    
    // Delete copy/move constructors
    OrderManager(const OrderManager&) = delete;
    OrderManager& operator=(const OrderManager&) = delete;
    OrderManager(OrderManager&&) = delete;
    OrderManager& operator=(OrderManager&&) = delete;
    
private:
    // Fixed-size order storage - NO std::map, NO dynamic allocation
    static constexpr size_t MAX_ORDERS = 10000;
    
    struct OrderEntry {
        Order order;  // Not cache-aligned here, whole entry will be aligned
        std::atomic<bool> active{false};
    };
    
    // Direct indexed access for O(1) lookup  
    std::array<CacheAligned<OrderEntry>, MAX_ORDERS> orders_;
    
    // Next order ID counter
    std::atomic<OrderId> next_order_id_{1};
    
    // Statistics
    std::atomic<uint64_t> total_orders_created_{0};
    std::atomic<uint64_t> total_orders_canceled_{0};
    std::atomic<uint64_t> total_orders_filled_{0};
    
    // Parent components
    TradeEngine* trade_engine_{nullptr};
    RiskManager* risk_manager_{nullptr};
    
    // Helper to find free slot
    size_t findFreeSlot() noexcept;
};

} // namespace Trading