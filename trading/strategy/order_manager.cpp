#include "order_manager.h"
#include "trade_engine.h"
#include "risk_manager.h"
#include "common/time_utils.h"

namespace Trading {

using namespace Common;

OrderManager::OrderManager(TradeEngine* trade_engine, RiskManager* risk_manager)
    : trade_engine_(trade_engine),
      risk_manager_(risk_manager) {
    
    // Initialize all order entries
    for (auto& cached_entry : orders_) {
        auto& entry = static_cast<OrderEntry&>(cached_entry);
        entry.order.reset();
        entry.active.store(false, std::memory_order_relaxed);
    }
}

Order* OrderManager::createOrder(TickerId ticker_id, Side side, Price price, Qty quantity) noexcept {
    // Find free slot
    const size_t slot = findFreeSlot();
    if (slot == MAX_ORDERS) {
        LOG_WARN("Order pool exhausted - cannot create order");
        return nullptr;
    }
    
    auto& entry = static_cast<OrderEntry&>(orders_[slot]);
    auto& order = entry.order;
    
    // Generate order ID
    const OrderId order_id = next_order_id_.fetch_add(1, std::memory_order_relaxed);
    
    // Initialize order
    order.order_id = order_id;
    order.client_id = ClientId_INVALID; // Set by caller if needed
    order.ticker_id = ticker_id;
    order.side = side;
    order.price = price;
    order.original_qty = quantity;
    order.filled_qty = 0;
    order.leaves_qty = quantity;
    order.state = OrderState::PENDING_NEW;
    order.timestamp_ns = Common::getNanosSinceEpoch();
    order.last_update_ns = order.timestamp_ns;
    
    // Mark as active
    entry.active.store(true, std::memory_order_release);
    
    total_orders_created_.fetch_add(1, std::memory_order_relaxed);
    
    LOG_DEBUG("Created order: id=%lu, ticker=%u, side=%u, px=%lu, qty=%u",
             order_id, ticker_id, side, price, quantity);
    
    return &order;
}

bool OrderManager::cancelOrder(OrderId order_id) noexcept {
    Order* order = getOrder(order_id);
    if (!order) {
        LOG_WARN("Cannot cancel order %lu - not found", order_id);
        return false;
    }
    
    if (!order->isActive()) {
        LOG_WARN("Cannot cancel order %lu - not active (state=%u)", 
                order_id, static_cast<uint8_t>(order->state));
        return false;
    }
    
    // Update state
    order->state = OrderState::PENDING_CANCEL;
    order->last_update_ns = Common::getNanosSinceEpoch();
    
    total_orders_canceled_.fetch_add(1, std::memory_order_relaxed);
    
    LOG_DEBUG("Canceling order: id=%lu", order_id);
    return true;
}

bool OrderManager::modifyOrder(OrderId order_id, Price new_price, Qty new_qty) noexcept {
    Order* order = getOrder(order_id);
    if (!order) {
        LOG_WARN("Cannot modify order %lu - not found", order_id);
        return false;
    }
    
    if (order->state != OrderState::LIVE) {
        LOG_WARN("Cannot modify order %lu - not live (state=%u)",
                order_id, static_cast<uint8_t>(order->state));
        return false;
    }
    
    // Validate new quantity
    if (new_qty <= order->filled_qty) {
        LOG_WARN("Cannot modify order %lu - requested qty %u <= filled %u",
                order_id, new_qty, order->filled_qty);
        return false;
    }
    
    // Update order
    order->price = new_price;
    order->original_qty = new_qty;
    order->leaves_qty = new_qty - order->filled_qty;
    order->state = OrderState::PENDING_MODIFY;
    order->last_update_ns = Common::getNanosSinceEpoch();
    
    LOG_DEBUG("Modifying order: id=%lu, new_px=%lu, new_qty=%u",
             order_id, new_price, new_qty);
    return true;
}

void OrderManager::onOrderUpdate(OrderId order_id, OrderState new_state,
                                Qty filled_qty, Qty leaves_qty) noexcept {
    Order* order = getOrder(order_id);
    if (!order) {
        LOG_WARN("Order update for unknown order: id=%lu", order_id);
        return;
    }
    
    const uint64_t now_ns = Common::getNanosSinceEpoch();
    
    // Update order state
    order->state = new_state;
    order->filled_qty += filled_qty;
    order->leaves_qty = leaves_qty;
    order->last_update_ns = now_ns;
    
    // Handle terminal states
    if (new_state == OrderState::FILLED) {
        total_orders_filled_.fetch_add(1, std::memory_order_relaxed);
        LOG_INFO("Order filled: id=%lu, total_filled=%u", order_id, order->filled_qty);
        
        // Mark as inactive after fill
        const size_t idx = order_id % MAX_ORDERS;
        static_cast<OrderEntry&>(orders_[idx]).active.store(false, std::memory_order_release);
    } else if (new_state == OrderState::CANCELED || new_state == OrderState::REJECTED) {
        // Mark as inactive
        const size_t idx = order_id % MAX_ORDERS;
        static_cast<OrderEntry&>(orders_[idx]).active.store(false, std::memory_order_release);
    }
}

size_t OrderManager::getActiveOrders(TickerId ticker_id, Order** output, size_t max_orders) noexcept {
    size_t count = 0;
    
    for (size_t i = 0; i < MAX_ORDERS && count < max_orders; ++i) {
        auto& entry = static_cast<OrderEntry&>(orders_[i]);
        if (entry.active.load(std::memory_order_acquire)) {
            Order* order = &entry.order;
            if (order->ticker_id == ticker_id && order->isActive()) {
                output[count++] = order;
            }
        }
    }
    
    return count;
}

void OrderManager::cancelAllOrders(TickerId ticker_id) noexcept {
    size_t canceled = 0;
    
    for (size_t i = 0; i < MAX_ORDERS; ++i) {
        auto& entry = static_cast<OrderEntry&>(orders_[i]);
        if (entry.active.load(std::memory_order_acquire)) {
            Order* order = &entry.order;
            if (order->ticker_id == ticker_id && order->isActive()) {
                if (cancelOrder(order->order_id)) {
                    canceled++;
                }
            }
        }
    }
    
    LOG_INFO("Canceled %zu orders for ticker %u", canceled, ticker_id);
}

void OrderManager::moveOrders(TickerId ticker_id, Price bid_price, Price ask_price, Qty clip) noexcept {
    // Get active orders for this symbol
    Order* active_orders[100];
    const size_t count = getActiveOrders(ticker_id, active_orders, 100);
    
    for (size_t i = 0; i < count; ++i) {
        Order* order = active_orders[i];
        if (!order || order->state != OrderState::LIVE) continue;
        
        // Check if order needs to be moved
        bool needs_move = false;
        Price new_price = order->price;
        
        if (order->side == 1) { // BUY
            // Buy order should be at or below best bid
            if (order->price > bid_price) {
                new_price = bid_price;
                needs_move = true;
            }
        } else { // SELL
            // Sell order should be at or above best ask
            if (order->price < ask_price) {
                new_price = ask_price;
                needs_move = true;
            }
        }
        
        if (needs_move) {
            // Adjust quantity if needed
            Qty new_qty = order->original_qty;
            if (clip > 0 && order->leaves_qty > clip) {
                new_qty = order->filled_qty + clip;
            }
            
            modifyOrder(order->order_id, new_price, new_qty);
        }
    }
}

size_t OrderManager::findFreeSlot() noexcept {
    // Linear search for free slot (can be optimized with free list)
    for (size_t i = 0; i < MAX_ORDERS; ++i) {
        auto& entry = static_cast<OrderEntry&>(orders_[i]);
        bool expected = false;
        if (entry.active.compare_exchange_weak(expected, true, 
                                               std::memory_order_acq_rel)) {
            return i;
        }
    }
    return MAX_ORDERS; // No free slot
}

} // namespace Trading