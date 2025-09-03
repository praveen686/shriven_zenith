#pragma once

#include "common/types.h"
#include "common/logging.h"
#include "common/macros.h"
#include "trading/market_data/order_book.h"
#include <atomic>
#include <array>
#include <cmath>
// #include <sstream>  // Commented out to avoid std::string usage

namespace Trading {

using namespace Common;

/// Feature values for trading signals
struct MarketFeatures {
    double fair_price{std::numeric_limits<double>::quiet_NaN()};        // Fair market price (mid-weighted)
    double spread{std::numeric_limits<double>::quiet_NaN()};            // Bid-ask spread
    double spread_bps{std::numeric_limits<double>::quiet_NaN()};        // Spread in basis points
    double imbalance{std::numeric_limits<double>::quiet_NaN()};         // Order book imbalance
    double micro_price{std::numeric_limits<double>::quiet_NaN()};       // Micro price (size-weighted)
    double vwap{std::numeric_limits<double>::quiet_NaN()};              // Volume weighted average price
    double agg_trade_ratio{std::numeric_limits<double>::quiet_NaN()};   // Aggressive trade qty ratio
    double momentum{std::numeric_limits<double>::quiet_NaN()};          // Price momentum
    double volatility{std::numeric_limits<double>::quiet_NaN()};        // Rolling volatility
    uint64_t last_update_ns{0};                                         // Last update timestamp
    
    bool isValid() const noexcept {
        return !std::isnan(fair_price) && !std::isnan(spread);
    }
};

/// Feature Engine - calculates trading signals and market microstructure features
class FeatureEngine {
public:
    FeatureEngine();
    ~FeatureEngine() = default;
    
    /// Update features on order book change
    void onOrderBookUpdate(TickerId ticker_id, const MarketData::OrderBook<100>* book) noexcept {
        if (ticker_id >= ME_MAX_TICKERS || !book) return;
        
        auto& features = features_[ticker_id];
        const uint64_t now_ns = Common::getNanosSinceEpoch();
        
        // Get best bid/ask
        Price best_bid = Price_INVALID;
        Price best_ask = Price_INVALID;
        Qty bid_qty = 0;
        Qty ask_qty = 0;
        
        // Get bid/ask from level 0
        const auto [bid_price, bid_quantity, bid_orders] = book->getBidLevel(0);
        if (bid_price != Price_INVALID) {
            best_bid = bid_price;
            bid_qty = bid_quantity;
        }
        
        const auto [ask_price, ask_quantity, ask_orders] = book->getAskLevel(0);
        if (ask_price != Price_INVALID) {
            best_ask = ask_price;
            ask_qty = ask_quantity;
        }
        
        if (best_bid != Price_INVALID && best_ask != Price_INVALID) {
            // Calculate spread
            features.spread = static_cast<double>(best_ask - best_bid);
            
            // Fair price (mid-weighted by size)
            const double total_qty = static_cast<double>(bid_qty + ask_qty);
            if (total_qty > 0) {
                features.fair_price = (static_cast<double>(best_bid) * static_cast<double>(ask_qty) + 
                                       static_cast<double>(best_ask) * static_cast<double>(bid_qty)) / total_qty;
                
                // Spread in basis points
                features.spread_bps = (features.spread / features.fair_price) * 10000.0;
            }
            
            // Micro price (simple mid for now)
            features.micro_price = (static_cast<double>(best_bid) + static_cast<double>(best_ask)) / 2.0;
            
            // Order book imbalance
            if (bid_qty + ask_qty > 0) {
                features.imbalance = static_cast<double>(static_cast<int64_t>(bid_qty) - static_cast<int64_t>(ask_qty)) / 
                                     static_cast<double>(bid_qty + ask_qty);
            }
            
            // Calculate depth-weighted average prices
            calculateDepthFeatures(ticker_id, book);
        }
        
        features.last_update_ns = now_ns;
        
        // Update momentum
        updateMomentum(ticker_id, features.fair_price);
    }
    
    /// Update features on trade execution
    void onTradeUpdate(TickerId ticker_id, Side side, Price price, Qty quantity) noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return;
        
        auto& features = features_[ticker_id];
        auto& stats = trade_stats_[ticker_id];
        
        // Update VWAP
        stats.volume += quantity;
        stats.value += static_cast<uint64_t>(quantity) * static_cast<uint64_t>(price);
        
        if (stats.volume > 0) {
            features.vwap = static_cast<double>(stats.value) / static_cast<double>(stats.volume);
        }
        
        // Update aggressive trade ratio (trades hitting ask vs bid)
        if (side == 1) { // BUY
            stats.aggressive_buy_volume += quantity;
        } else {
            stats.aggressive_sell_volume += quantity;
        }
        
        const uint64_t total_agg_volume = stats.aggressive_buy_volume + stats.aggressive_sell_volume;
        if (total_agg_volume > 0) {
            features.agg_trade_ratio = static_cast<double>(stats.aggressive_buy_volume) / static_cast<double>(total_agg_volume);
        }
        
        // Update volatility
        updateVolatility(ticker_id, price);
    }
    
    /// Get features for a symbol
    const MarketFeatures* getFeatures(TickerId ticker_id) const noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return nullptr;
        return &features_[ticker_id];
    }
    
    /// Get fair market price
    double getFairPrice(TickerId ticker_id) const noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return std::numeric_limits<double>::quiet_NaN();
        return features_[ticker_id].fair_price;
    }
    
    /// Get spread in basis points
    double getSpreadBps(TickerId ticker_id) const noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return std::numeric_limits<double>::quiet_NaN();
        return features_[ticker_id].spread_bps;
    }
    
    /// Get order book imbalance
    double getImbalance(TickerId ticker_id) const noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return std::numeric_limits<double>::quiet_NaN();
        return features_[ticker_id].imbalance;
    }
    
    /// Check if we should place orders based on spread
    bool isSpreadWide(TickerId ticker_id, double min_spread_bps = 10.0) const noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return false;
        return features_[ticker_id].spread_bps > min_spread_bps;
    }
    
    /// Check if there's significant imbalance
    bool hasImbalance(TickerId ticker_id, double threshold = 0.2) const noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return false;
        return std::abs(features_[ticker_id].imbalance) > threshold;
    }
    
    /// Reset all features for new trading day
    void resetAll() noexcept {
        for (auto& f : features_) {
            f = MarketFeatures{};
        }
        for (auto& s : trade_stats_) {
            s = TradeStats{};
        }
        for (auto& m : momentum_) {
            m = MomentumData{};
        }
    }
    
    // toString() removed to avoid std::string in production code
    // Use generateReport() instead for debugging
    void generateReport(char* buffer, size_t buffer_size) const {
        if (!buffer || buffer_size == 0) return;
        
        size_t offset = 0;
        offset += static_cast<size_t>(snprintf(buffer + offset, buffer_size - offset,
                                               "FeatureEngine Report:\n"));
        
        for (size_t i = 0; i < ME_MAX_TICKERS && offset < buffer_size - 1; ++i) {
            const auto& f = features_[i];
            if (f.isValid()) {
                offset += static_cast<size_t>(snprintf(buffer + offset, buffer_size - offset,
                    "Ticker %zu: Fair=%.2f, Spread=%.2fbps, Imbalance=%.2f, Momentum=%.2f, VWAP=%.2f\n",
                    i, f.fair_price, f.spread_bps, f.imbalance, f.momentum, f.vwap));
            }
        }
    }
    
    // Delete copy/move constructors
    FeatureEngine(const FeatureEngine&) = delete;
    FeatureEngine& operator=(const FeatureEngine&) = delete;
    FeatureEngine(FeatureEngine&&) = delete;
    FeatureEngine& operator=(FeatureEngine&&) = delete;
    
private:
    // Feature storage for all symbols
    std::array<MarketFeatures, ME_MAX_TICKERS> features_;
    
    // Trade statistics for VWAP calculation
    struct TradeStats {
        uint64_t volume{0};
        uint64_t value{0};
        uint64_t aggressive_buy_volume{0};
        uint64_t aggressive_sell_volume{0};
    };
    std::array<TradeStats, ME_MAX_TICKERS> trade_stats_;
    
    // Momentum calculation
    struct MomentumData {
        static constexpr size_t WINDOW_SIZE = 20;
        std::array<double, WINDOW_SIZE> prices{};
        size_t index{0};
        size_t count{0};
    };
    std::array<MomentumData, ME_MAX_TICKERS> momentum_;
    
    /// Calculate depth-weighted features
    void calculateDepthFeatures(TickerId ticker_id, const MarketData::OrderBook<100>* book) noexcept {
        auto& features = features_[ticker_id];
        
        double weighted_bid_price = 0.0;
        double weighted_ask_price = 0.0;
        uint64_t total_bid_qty = 0;
        uint64_t total_ask_qty = 0;
        
        // Process top 5 levels for depth-weighted prices
        const size_t depth = 5;
        
        for (size_t i = 0; i < depth; ++i) {
            const auto [price, qty, orders] = book->getBidLevel(static_cast<uint8_t>(i));
            if (price == Price_INVALID || price == 0) break;
            weighted_bid_price += static_cast<double>(price) * static_cast<double>(qty);
            total_bid_qty += qty;
        }
        
        for (size_t i = 0; i < depth; ++i) {
            const auto [price, qty, orders] = book->getAskLevel(static_cast<uint8_t>(i));
            if (price == Price_INVALID || price == 0) break;
            weighted_ask_price += static_cast<double>(price) * static_cast<double>(qty);
            total_ask_qty += qty;
        }
        
        // Update micro price with depth weighting
        if (total_bid_qty > 0 && total_ask_qty > 0) {
            const double weighted_bid = weighted_bid_price / static_cast<double>(total_bid_qty);
            const double weighted_ask = weighted_ask_price / static_cast<double>(total_ask_qty);
            features.micro_price = (weighted_bid * static_cast<double>(total_ask_qty) + 
                                   weighted_ask * static_cast<double>(total_bid_qty)) / 
                                  static_cast<double>(total_bid_qty + total_ask_qty);
        }
    }
    
    /// Update momentum indicator
    void updateMomentum(TickerId ticker_id, double price) noexcept {
        if (std::isnan(price)) return;
        
        auto& mom = momentum_[ticker_id];
        mom.prices[mom.index] = price;
        mom.index = (mom.index + 1) % MomentumData::WINDOW_SIZE;
        if (mom.count < MomentumData::WINDOW_SIZE) {
            mom.count++;
        }
        
        // Calculate simple momentum (current vs average)
        if (mom.count >= 10) {
            double sum = 0.0;
            for (size_t i = 0; i < mom.count; ++i) {
                sum += mom.prices[i];
            }
            const double avg = sum / static_cast<double>(mom.count);
            features_[ticker_id].momentum = (price - avg) / avg * 10000.0; // In basis points
        }
    }
    
    /// Update volatility calculation
    void updateVolatility(TickerId ticker_id, Price /* price */) noexcept {
        // Simple rolling volatility (can be enhanced with EWMA)
        auto& features = features_[ticker_id];
        auto& mom = momentum_[ticker_id];
        
        if (mom.count >= 10) {
            double sum = 0.0;
            double sum_sq = 0.0;
            
            for (size_t i = 0; i < mom.count; ++i) {
                sum += mom.prices[i];
                sum_sq += mom.prices[i] * mom.prices[i];
            }
            
            const double mean = sum / static_cast<double>(mom.count);
            const double variance = (sum_sq / static_cast<double>(mom.count)) - (mean * mean);
            features.volatility = std::sqrt(variance) / mean * 10000.0; // In basis points
        }
    }
};

} // namespace Trading