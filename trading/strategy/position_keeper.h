#pragma once

#include "common/types.h"
#include "common/logging.h"
#include "common/time_utils.h"
#include <atomic>
#include <array>
// #include <sstream>  // Commented out to avoid std::string usage

namespace Trading {

using namespace Common;

/// Position information for a single symbol
struct PositionInfo {
    std::atomic<int64_t> position{0};           // Net position (positive=long, negative=short)
    std::atomic<int64_t> buy_volume{0};         // Total buy volume
    std::atomic<int64_t> sell_volume{0};        // Total sell volume
    std::atomic<int64_t> buy_value{0};          // Total buy value (for VWAP)
    std::atomic<int64_t> sell_value{0};         // Total sell value (for VWAP)
    std::atomic<int64_t> realized_pnl{0};       // Realized P&L
    std::atomic<int64_t> unrealized_pnl{0};     // Unrealized P&L
    std::atomic<Price> last_price{Price_INVALID}; // Last traded price
    std::atomic<Price> avg_buy_price{0};        // Average buy price
    std::atomic<Price> avg_sell_price{0};       // Average sell price
    std::atomic<uint64_t> last_update_ns{0};    // Last update timestamp
    
    void reset() noexcept {
        position.store(0, std::memory_order_relaxed);
        buy_volume.store(0, std::memory_order_relaxed);
        sell_volume.store(0, std::memory_order_relaxed);
        buy_value.store(0, std::memory_order_relaxed);
        sell_value.store(0, std::memory_order_relaxed);
        realized_pnl.store(0, std::memory_order_relaxed);
        unrealized_pnl.store(0, std::memory_order_relaxed);
        last_price.store(Price_INVALID, std::memory_order_relaxed);
        avg_buy_price.store(0, std::memory_order_relaxed);
        avg_sell_price.store(0, std::memory_order_relaxed);
        last_update_ns.store(0, std::memory_order_relaxed);
    }
    
    /// Calculate VWAP
    Price getVWAP() const noexcept {
        const int64_t total_volume = buy_volume.load(std::memory_order_relaxed) + 
                                    sell_volume.load(std::memory_order_relaxed);
        const int64_t total_value = buy_value.load(std::memory_order_relaxed) + 
                                   sell_value.load(std::memory_order_relaxed);
        
        if (total_volume == 0) return 0;
        return static_cast<Price>(total_value / total_volume);
    }
};

/// Position Keeper - tracks positions and P&L across all symbols
class PositionKeeper {
public:
    PositionKeeper();
    ~PositionKeeper() = default;
    
    /// Update position on fill
    void onFill(TickerId ticker_id, Side side, Qty filled_qty, Price fill_price) noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return;
        
        auto& pos = positions_[ticker_id];
        const uint64_t now_ns = Common::getNanosSinceEpoch();
        
        // Update volumes and values
        if (side == 1) { // Buy
            pos.buy_volume.fetch_add(static_cast<int64_t>(filled_qty), std::memory_order_relaxed);
            pos.buy_value.fetch_add(static_cast<int64_t>(filled_qty) * fill_price, std::memory_order_relaxed);
            pos.position.fetch_add(static_cast<int64_t>(filled_qty), std::memory_order_relaxed);
            
            // Update average buy price
            const int64_t total_buy_vol = pos.buy_volume.load(std::memory_order_relaxed);
            const int64_t total_buy_val = pos.buy_value.load(std::memory_order_relaxed);
            if (total_buy_vol > 0) {
                pos.avg_buy_price.store(total_buy_val / total_buy_vol, std::memory_order_relaxed);
            }
        } else { // Sell
            pos.sell_volume.fetch_add(static_cast<int64_t>(filled_qty), std::memory_order_relaxed);
            pos.sell_value.fetch_add(static_cast<int64_t>(filled_qty) * fill_price, std::memory_order_relaxed);
            pos.position.fetch_sub(static_cast<int64_t>(filled_qty), std::memory_order_relaxed);
            
            // Update average sell price
            const int64_t total_sell_vol = pos.sell_volume.load(std::memory_order_relaxed);
            const int64_t total_sell_val = pos.sell_value.load(std::memory_order_relaxed);
            if (total_sell_vol > 0) {
                pos.avg_sell_price.store(total_sell_val / total_sell_vol, std::memory_order_relaxed);
            }
            
            // Calculate realized P&L on sells
            const Price avg_buy = pos.avg_buy_price.load(std::memory_order_relaxed);
            if (avg_buy > 0) {
                const int64_t pnl = static_cast<int64_t>(filled_qty) * (fill_price - avg_buy);
                pos.realized_pnl.fetch_add(pnl, std::memory_order_relaxed);
                total_realized_pnl_.fetch_add(pnl, std::memory_order_relaxed);
            }
        }
        
        pos.last_price.store(fill_price, std::memory_order_relaxed);
        pos.last_update_ns.store(now_ns, std::memory_order_relaxed);
        
        // Update unrealized P&L
        updateUnrealizedPnL(ticker_id, fill_price);
    }
    
    /// Update market price (for unrealized P&L calculation)
    void updateMarketPrice(TickerId ticker_id, Price market_price) noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return;
        
        auto& pos = positions_[ticker_id];
        pos.last_price.store(market_price, std::memory_order_relaxed);
        updateUnrealizedPnL(ticker_id, market_price);
    }
    
    /// Get position for a symbol
    int64_t getPosition(TickerId ticker_id) const noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return 0;
        return positions_[ticker_id].position.load(std::memory_order_relaxed);
    }
    
    /// Get position info for a symbol
    const PositionInfo* getPositionInfo(TickerId ticker_id) const noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return nullptr;
        return &positions_[ticker_id];
    }
    
    /// Get total realized P&L
    int64_t getTotalRealizedPnL() const noexcept {
        return total_realized_pnl_.load(std::memory_order_relaxed);
    }
    
    /// Get total unrealized P&L
    int64_t getTotalUnrealizedPnL() const noexcept {
        return total_unrealized_pnl_.load(std::memory_order_relaxed);
    }
    
    /// Get total P&L (realized + unrealized)
    int64_t getTotalPnL() const noexcept {
        return getTotalRealizedPnL() + getTotalUnrealizedPnL();
    }
    
    /// Get total exposure (sum of absolute positions)
    int64_t getTotalExposure() const noexcept {
        int64_t exposure = 0;
        for (size_t i = 0; i < ME_MAX_TICKERS; ++i) {
            const int64_t pos = positions_[i].position.load(std::memory_order_relaxed);
            const Price price = positions_[i].last_price.load(std::memory_order_relaxed);
            if (price != Price_INVALID) {
                exposure += std::abs(pos * price);
            }
        }
        return exposure;
    }
    
    /// Reset all positions (for new trading day)
    void resetAll() noexcept {
        for (auto& pos : positions_) {
            pos.reset();
        }
        total_realized_pnl_.store(0, std::memory_order_relaxed);
        total_unrealized_pnl_.store(0, std::memory_order_relaxed);
    }
    
    // toString() removed to avoid std::string in production code
    // Use generateReport() instead for debugging
    void generateReport(char* buffer, size_t buffer_size) const {
        if (!buffer || buffer_size == 0) return;
        
        size_t offset = 0;
        offset += static_cast<size_t>(snprintf(buffer + offset, buffer_size - offset,
            "PositionKeeper Report:\n"
            "Total Realized P&L: %ld\n"
            "Total Unrealized P&L: %ld\n"
            "Total P&L: %ld\n"
            "Total Exposure: %ld\n\n"
            "Active Positions:\n",
            getTotalRealizedPnL(),
            getTotalUnrealizedPnL(),
            getTotalPnL(),
            getTotalExposure()));
        
        for (size_t i = 0; i < ME_MAX_TICKERS && offset < buffer_size - 1; ++i) {
            const auto& pos = positions_[i];
            const int64_t position = pos.position.load(std::memory_order_relaxed);
            if (position != 0) {
                offset += static_cast<size_t>(snprintf(buffer + offset, buffer_size - offset,
                    "Ticker %zu: Pos=%ld, AvgBuy=%ld, AvgSell=%ld, Realized=%ld, Unrealized=%ld\n",
                    i, position,
                    pos.avg_buy_price.load(std::memory_order_relaxed),
                    pos.avg_sell_price.load(std::memory_order_relaxed),
                    pos.realized_pnl.load(std::memory_order_relaxed),
                    pos.unrealized_pnl.load(std::memory_order_relaxed)));
            }
        }
    }
    
    // Delete copy/move constructors
    PositionKeeper(const PositionKeeper&) = delete;
    PositionKeeper& operator=(const PositionKeeper&) = delete;
    PositionKeeper(PositionKeeper&&) = delete;
    PositionKeeper& operator=(PositionKeeper&&) = delete;
    
private:
    // Position tracking for all symbols
    std::array<PositionInfo, ME_MAX_TICKERS> positions_;
    
    // Global P&L tracking
    std::atomic<int64_t> total_realized_pnl_{0};
    std::atomic<int64_t> total_unrealized_pnl_{0};
    
    /// Update unrealized P&L for a position
    void updateUnrealizedPnL(TickerId ticker_id, Price market_price) noexcept {
        auto& pos = positions_[ticker_id];
        const int64_t position = pos.position.load(std::memory_order_relaxed);
        
        if (position != 0 && market_price != Price_INVALID) {
            const Price avg_price = (position > 0) ? 
                pos.avg_buy_price.load(std::memory_order_relaxed) :
                pos.avg_sell_price.load(std::memory_order_relaxed);
            
            if (avg_price > 0) {
                const int64_t unrealized = position * (market_price - avg_price);
                const int64_t old_unrealized = pos.unrealized_pnl.exchange(unrealized, std::memory_order_relaxed);
                total_unrealized_pnl_.fetch_add(unrealized - old_unrealized, std::memory_order_relaxed);
            }
        }
    }
};

} // namespace Trading