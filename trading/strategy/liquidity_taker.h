#pragma once

#include "common/types.h"
#include "common/logging.h"
#include "common/macros.h"
#include "order_manager.h"
#include "feature_engine.h"
#include "risk_manager.h"
#include "position_keeper.h"
#include <unordered_map>

namespace Trading {

using namespace Common;

/// Configuration for liquidity taking per symbol
struct LiquidityTakerConfig {
    Qty clip{100};              // Order size for aggressive orders
    double threshold{0.5};      // Aggressive trade ratio threshold (0.5 = 50% aggressive buys)
    Price max_slippage{500};    // Maximum slippage allowed (5 ticks)
    Qty min_size{10};          // Minimum order size
    Qty max_size{1000};        // Maximum order size per trade
    uint32_t cooldown_ms{100}; // Cooldown between aggressive orders
    bool enabled{true};         // Enable/disable for this symbol
};

/// Liquidity Taker Strategy - takes liquidity when detecting momentum/order flow imbalance
class LiquidityTaker {
public:
    LiquidityTaker(OrderManager* order_manager,
                   FeatureEngine* feature_engine,
                   RiskManager* risk_manager,
                   PositionKeeper* position_keeper);
    
    ~LiquidityTaker() = default;
    
    /// Configure liquidity taking for a symbol
    void configureSymbol(TickerId ticker_id, const LiquidityTakerConfig& config) {
        if (ticker_id < ME_MAX_TICKERS) {
            ticker_configs_[ticker_id] = config;
        }
    }
    
    /// Process order book update - monitor for taking opportunities
    void onOrderBookUpdate(TickerId ticker_id, const MarketData::OrderBook<100>* book) noexcept {
        if (ticker_id >= ME_MAX_TICKERS || !book) return;
        
        const auto& config = ticker_configs_[ticker_id];
        if (!config.enabled) return;
        
        // Check for immediate taking opportunities based on book imbalance
        const auto* features = feature_engine_->getFeatures(ticker_id);
        if (!features || !features->isValid()) return;
        
        // Strong imbalance might trigger immediate aggressive order
        if (std::abs(features->imbalance) > 0.7) { // 70% imbalance
            const Price best_bid = book->getBestBid();
            const Price best_ask = book->getBestAsk();
            
            if (best_bid == Price_INVALID || best_ask == Price_INVALID) return;
            
            // Check cooldown
            const uint64_t now_ns = Common::getNanosSinceEpoch();
            const uint64_t last_order_ns = last_order_time_[ticker_id].load(std::memory_order_relaxed);
            if ((now_ns - last_order_ns) < static_cast<uint64_t>(config.cooldown_ms) * 1000000) {
                return; // Still in cooldown
            }
            
            if (features->imbalance > 0.7) {
                // Heavy buying pressure - join the buying
                LOG_INFO("LT: Imbalance detected ticker=%u, imbalance=%.2f - aggressive BUY",
                        ticker_id, features->imbalance);
                
                // Send aggressive buy order at ask
                auto* order = order_manager_->createOrder(ticker_id, 1, best_ask, config.clip);
                if (order) {
                    aggressive_orders_sent_++;
                    last_order_time_[ticker_id].store(now_ns, std::memory_order_relaxed);
                }
            } else if (features->imbalance < -0.7) {
                // Heavy selling pressure - join the selling
                LOG_INFO("LT: Imbalance detected ticker=%u, imbalance=%.2f - aggressive SELL",
                        ticker_id, features->imbalance);
                
                // Send aggressive sell order at bid
                auto* order = order_manager_->createOrder(ticker_id, 2, best_bid, config.clip);
                if (order) {
                    aggressive_orders_sent_++;
                    last_order_time_[ticker_id].store(now_ns, std::memory_order_relaxed);
                }
            }
        }
    }
    
    /// Process trade update - main liquidity taking logic based on trade flow
    void onTradeUpdate(TickerId ticker_id, Side side, Price price, Qty quantity) noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return;
        
        const auto& config = ticker_configs_[ticker_id];
        if (!config.enabled) return;
        
        // Get features to check aggressive trade ratio
        const auto* features = feature_engine_->getFeatures(ticker_id);
        if (!features || !features->isValid()) return;
        
        // Check if aggressive trade ratio exceeds threshold
        if (features->agg_trade_ratio >= config.threshold) {
            // Check cooldown
            const uint64_t now_ns = Common::getNanosSinceEpoch();
            const uint64_t last_order_ns = last_order_time_[ticker_id].load(std::memory_order_relaxed);
            if ((now_ns - last_order_ns) < static_cast<uint64_t>(config.cooldown_ms) * 1000000) {
                return; // Still in cooldown
            }
            
            LOG_INFO("LT: Aggressive ratio %.2f exceeds threshold %.2f for ticker %u",
                    features->agg_trade_ratio, config.threshold, ticker_id);
            
            // Follow the aggressive flow
            if (side == 1) { // Aggressive buying detected
                // Place aggressive buy order
                const Price target_price = price + config.max_slippage;
                auto* order = order_manager_->createOrder(ticker_id, 1, target_price, config.clip);
                if (order) {
                    aggressive_orders_sent_++;
                    last_order_time_[ticker_id].store(now_ns, std::memory_order_relaxed);
                    LOG_DEBUG("LT: Sent aggressive BUY at %ld for ticker %u", target_price, ticker_id);
                }
            } else { // Aggressive selling detected
                // Place aggressive sell order
                const Price target_price = price - config.max_slippage;
                auto* order = order_manager_->createOrder(ticker_id, 2, target_price, config.clip);
                if (order) {
                    aggressive_orders_sent_++;
                    last_order_time_[ticker_id].store(now_ns, std::memory_order_relaxed);
                    LOG_DEBUG("LT: Sent aggressive SELL at %ld for ticker %u", target_price, ticker_id);
                }
            }
        }
        
        // Track momentum
        updateMomentum(ticker_id, side, quantity);
    }
    
    /// Process order updates for our orders
    void onOrderUpdate(OrderId order_id, OrderState state, Qty filled_qty) noexcept {
        switch (state) {
            case OrderState::FILLED:
                aggressive_fills_++;
                total_filled_qty_ += filled_qty;
                LOG_INFO("LT: Aggressive order filled id=%lu, qty=%lu", order_id, filled_qty);
                break;
                
            case OrderState::CANCELED:
                orders_canceled_++;
                break;
                
            case OrderState::REJECTED:
                orders_rejected_++;
                LOG_WARN("LT: Order rejected id=%lu", order_id);
                break;
                
            default:
                break;
        }
    }
    
    /// Get strategy statistics
    void getStats(uint64_t& orders_sent, uint64_t& fills, 
                  uint64_t& total_qty, uint64_t& rejected) const noexcept {
        orders_sent = aggressive_orders_sent_.load(std::memory_order_relaxed);
        fills = aggressive_fills_.load(std::memory_order_relaxed);
        total_qty = total_filled_qty_.load(std::memory_order_relaxed);
        rejected = orders_rejected_.load(std::memory_order_relaxed);
    }
    
    // Delete copy/move constructors
    LiquidityTaker(const LiquidityTaker&) = delete;
    LiquidityTaker& operator=(const LiquidityTaker&) = delete;
    LiquidityTaker(LiquidityTaker&&) = delete;
    LiquidityTaker& operator=(LiquidityTaker&&) = delete;
    
private:
    // Strategy components
    OrderManager* order_manager_;
    FeatureEngine* feature_engine_;
    RiskManager* risk_manager_;
    PositionKeeper* position_keeper_;
    
    // Configuration per symbol
    std::array<LiquidityTakerConfig, ME_MAX_TICKERS> ticker_configs_;
    
    // Cooldown tracking per symbol
    std::array<std::atomic<uint64_t>, ME_MAX_TICKERS> last_order_time_;
    
    // Momentum tracking
    struct MomentumTracker {
        std::atomic<uint64_t> buy_volume{0};
        std::atomic<uint64_t> sell_volume{0};
        std::atomic<uint64_t> last_reset_ns{0};
    };
    std::array<MomentumTracker, ME_MAX_TICKERS> momentum_;
    
    // Statistics
    std::atomic<uint64_t> aggressive_orders_sent_{0};
    std::atomic<uint64_t> aggressive_fills_{0};
    std::atomic<uint64_t> total_filled_qty_{0};
    std::atomic<uint64_t> orders_canceled_{0};
    std::atomic<uint64_t> orders_rejected_{0};
    
    /// Update momentum tracking
    void updateMomentum(TickerId ticker_id, Side side, Qty quantity) noexcept {
        auto& mom = momentum_[ticker_id];
        
        // Reset if more than 1 second has passed
        const uint64_t now_ns = Common::getNanosSinceEpoch();
        const uint64_t last_reset = mom.last_reset_ns.load(std::memory_order_relaxed);
        if ((now_ns - last_reset) > 1000000000) { // 1 second
            mom.buy_volume.store(0, std::memory_order_relaxed);
            mom.sell_volume.store(0, std::memory_order_relaxed);
            mom.last_reset_ns.store(now_ns, std::memory_order_relaxed);
        }
        
        // Update volume
        if (side == 1) {
            mom.buy_volume.fetch_add(quantity, std::memory_order_relaxed);
        } else {
            mom.sell_volume.fetch_add(quantity, std::memory_order_relaxed);
        }
    }
};

} // namespace Trading