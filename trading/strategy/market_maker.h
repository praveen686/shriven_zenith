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

/// Configuration for market making per symbol
struct MarketMakerConfig {
    Qty clip{100};              // Order size
    double threshold{0.0001};   // Minimum edge required (10 bps)
    Price tick_size{100};       // Minimum price increment
    Qty min_size{10};          // Minimum order size
    Qty max_position{10000};   // Maximum position size
    bool enabled{true};         // Enable/disable for this symbol
};

/// Market Maker Strategy - provides liquidity by placing passive orders
class MarketMaker {
public:
    MarketMaker(OrderManager* order_manager,
                FeatureEngine* feature_engine,
                RiskManager* risk_manager,
                PositionKeeper* position_keeper);
    
    ~MarketMaker() = default;
    
    /// Configure market making for a symbol
    void configureSymbol(TickerId ticker_id, const MarketMakerConfig& config) {
        if (ticker_id < ME_MAX_TICKERS) {
            ticker_configs_[ticker_id] = config;
        }
    }
    
    /// Process order book update - main market making logic
    void onOrderBookUpdate(TickerId ticker_id, const MarketData::OrderBook<100>* book) noexcept {
        if (ticker_id >= ME_MAX_TICKERS || !book) return;
        
        const auto& config = ticker_configs_[ticker_id];
        if (!config.enabled) return;
        
        // Get market features
        const auto* features = feature_engine_->getFeatures(ticker_id);
        if (!features || !features->isValid()) return;
        
        const Price best_bid = book->getBestBid();
        const Price best_ask = book->getBestAsk();
        
        if (best_bid == Price_INVALID || best_ask == Price_INVALID) return;
        
        // Check if spread is wide enough to make market
        if (features->spread_bps < config.threshold * 10000) {
            LOG_DEBUG("MM: Spread too tight for ticker %u: %.2f bps", 
                     ticker_id, features->spread_bps);
            return;
        }
        
        // Calculate our quotes based on fair value
        const auto fair_price = static_cast<Price>(features->fair_price);
        
        // Check current position to adjust quotes
        const int64_t position = position_keeper_->getPosition(ticker_id);
        
        // Adjust quote prices based on position (inventory management)
        Price our_bid = best_bid;
        Price our_ask = best_ask;
        
        if (position > 0) {
            // Long position - be more aggressive on ask side to reduce inventory
            our_bid = best_bid;  // Keep at best bid
            our_ask = best_ask - config.tick_size; // Improve ask
        } else if (position < 0) {
            // Short position - be more aggressive on bid side
            our_bid = best_bid + config.tick_size; // Improve bid
            our_ask = best_ask; // Keep at best ask
        } else {
            // Neutral position - quote symmetrically around fair value
            const Price edge = static_cast<Price>(features->spread * config.threshold);
            our_bid = fair_price - edge;
            our_ask = fair_price + edge;
            
            // Make sure we're at least at the BBO
            our_bid = std::max(our_bid, best_bid);
            our_ask = std::min(our_ask, best_ask);
        }
        
        // Calculate order sizes based on position limits
        Qty bid_size = config.clip;
        Qty ask_size = config.clip;
        
        // Reduce size if approaching position limits
        const auto max_pos = static_cast<int64_t>(config.max_position);
        const int64_t half_max_pos = max_pos / 2;
        if (position > half_max_pos) {
            bid_size = std::max(config.min_size, bid_size / 2);
        }
        if (position < -half_max_pos) {
            ask_size = std::max(config.min_size, ask_size / 2);
        }
        
        // Move or place orders
        order_manager_->moveOrders(ticker_id, our_bid, our_ask, config.clip);
        
        // Update statistics
        quotes_updated_++;
        last_update_ns_ = Common::getNanosSinceEpoch();
        
        LOG_DEBUG("MM: Updated quotes for ticker %u: bid=%ld@%lu, ask=%ld@%lu, pos=%ld",
                 ticker_id, our_bid, bid_size, our_ask, ask_size, position);
    }
    
    /// Process trade update - adjust quotes after trades
    void onTradeUpdate(TickerId ticker_id, Side side, Price price, Qty quantity) noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return;
        
        const auto& config = ticker_configs_[ticker_id];
        if (!config.enabled) return;
        
        // Track market trades for momentum detection
        // Could trigger quote adjustments based on trade flow
        trades_observed_++;
        
        LOG_DEBUG("MM: Trade observed ticker=%u, side=%u, px=%ld, qty=%lu",
                 ticker_id, side, price, quantity);
    }
    
    /// Process order updates for our orders
    void onOrderUpdate(OrderId order_id, OrderState state, Qty filled_qty) noexcept {
        switch (state) {
            case OrderState::FILLED:
                orders_filled_++;
                LOG_INFO("MM: Order filled id=%lu, qty=%lu", order_id, filled_qty);
                break;
                
            case OrderState::CANCELED:
                orders_canceled_++;
                break;
                
            case OrderState::REJECTED:
                orders_rejected_++;
                LOG_WARN("MM: Order rejected id=%lu", order_id);
                break;
                
            default:
                break;
        }
    }
    
    /// Get strategy statistics
    void getStats(uint64_t& quotes_updated, uint64_t& orders_filled, 
                  uint64_t& orders_canceled, uint64_t& orders_rejected) const noexcept {
        quotes_updated = quotes_updated_.load(std::memory_order_relaxed);
        orders_filled = orders_filled_.load(std::memory_order_relaxed);
        orders_canceled = orders_canceled_.load(std::memory_order_relaxed);
        orders_rejected = orders_rejected_.load(std::memory_order_relaxed);
    }
    
    // Delete copy/move constructors
    MarketMaker(const MarketMaker&) = delete;
    MarketMaker& operator=(const MarketMaker&) = delete;
    MarketMaker(MarketMaker&&) = delete;
    MarketMaker& operator=(MarketMaker&&) = delete;
    
private:
    // Strategy components
    OrderManager* order_manager_;
    FeatureEngine* feature_engine_;
    RiskManager* risk_manager_;
    PositionKeeper* position_keeper_;
    
    // Configuration per symbol
    std::array<MarketMakerConfig, ME_MAX_TICKERS> ticker_configs_;
    
    // Statistics
    std::atomic<uint64_t> quotes_updated_{0};
    std::atomic<uint64_t> trades_observed_{0};
    std::atomic<uint64_t> orders_filled_{0};
    std::atomic<uint64_t> orders_canceled_{0};
    std::atomic<uint64_t> orders_rejected_{0};
    std::atomic<uint64_t> last_update_ns_{0};
};

} // namespace Trading