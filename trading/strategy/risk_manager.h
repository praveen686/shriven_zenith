#pragma once

#include "common/types.h"
#include "common/logging.h"
#include "common/macros.h"
#include "common/time_utils.h"
#include <atomic>

namespace Trading {

using namespace Common;

/// Risk check results
enum class RiskCheckResult : uint8_t {
    PASS = 0,
    POSITION_LIMIT_BREACH = 1,
    LOSS_LIMIT_BREACH = 2,
    ORDER_SIZE_BREACH = 3,
    ORDER_RATE_BREACH = 4,
    INVALID_PRICE = 5
};

/// Risk configuration per symbol
struct RiskConfig {
    int64_t max_position{1000000};      // Maximum position value
    int64_t max_loss{50000};           // Maximum loss allowed
    uint32_t max_order_size{10000};    // Maximum single order size
    uint32_t max_order_rate{100};      // Max orders per second
    Price min_price{100};               // Minimum allowed price
    Price max_price{1000000000};        // Maximum allowed price
};

/// Risk tracking per symbol
struct SymbolRisk {
    std::atomic<int64_t> position{0};           // Current position
    std::atomic<int64_t> position_value{0};     // Position value
    std::atomic<int64_t> realized_pnl{0};       // Realized P&L
    std::atomic<int64_t> unrealized_pnl{0};     // Unrealized P&L
    std::atomic<uint32_t> order_count{0};       // Orders this second
    std::atomic<uint64_t> last_order_time_ns{0}; // Last order timestamp
    RiskConfig config;                          // Risk limits for this symbol
};

/// Risk Manager - pre-trade and post-trade risk checks
class RiskManager {
public:
    RiskManager();
    ~RiskManager() = default;
    
    /// Configure risk limits for a symbol
    void configureSymbol(TickerId ticker_id, const RiskConfig& config) noexcept {
        if (ticker_id < ME_MAX_TICKERS) {
            symbol_risk_[ticker_id].config = config;
        }
    }
    
    /// Pre-trade risk check (sub-100ns target)
    [[gnu::always_inline]]
    inline RiskCheckResult checkOrder(TickerId ticker_id, Side side, 
                                     Price price, Qty quantity) noexcept {
        if (UNLIKELY(ticker_id >= ME_MAX_TICKERS)) {
            return RiskCheckResult::INVALID_PRICE;
        }
        
        auto& risk = symbol_risk_[ticker_id];
        const auto& config = risk.config;
        
        // Check order size
        if (UNLIKELY(quantity > config.max_order_size)) {
            return RiskCheckResult::ORDER_SIZE_BREACH;
        }
        
        // Check price bounds
        if (UNLIKELY(price < config.min_price || price > config.max_price)) {
            return RiskCheckResult::INVALID_PRICE;
        }
        
        // Calculate position after order
        const int64_t position_delta = (side == 1) ? 
            static_cast<int64_t>(quantity) : -static_cast<int64_t>(quantity);
        const int64_t new_position = risk.position.load(std::memory_order_relaxed) + position_delta;
        const int64_t new_position_value = new_position * price;
        
        // Check position limits
        if (UNLIKELY(std::abs(new_position_value) > config.max_position)) {
            return RiskCheckResult::POSITION_LIMIT_BREACH;
        }
        
        // Check loss limits
        const int64_t total_pnl = risk.realized_pnl.load(std::memory_order_relaxed) + 
                                 risk.unrealized_pnl.load(std::memory_order_relaxed);
        if (UNLIKELY(total_pnl < -config.max_loss)) {
            return RiskCheckResult::LOSS_LIMIT_BREACH;
        }
        
        // Check order rate
        const uint64_t now_ns = Common::getNanosSinceEpoch();
        const uint64_t last_order_ns = risk.last_order_time_ns.load(std::memory_order_relaxed);
        if ((now_ns - last_order_ns) >= 1000000000) { // New second
            risk.order_count.store(0, std::memory_order_relaxed);
        }
        
        const uint32_t current_rate = risk.order_count.load(std::memory_order_relaxed);
        if (UNLIKELY(current_rate >= config.max_order_rate)) {
            return RiskCheckResult::ORDER_RATE_BREACH;
        }
        
        // Update counters for successful check
        risk.order_count.fetch_add(1, std::memory_order_relaxed);
        risk.last_order_time_ns.store(now_ns, std::memory_order_relaxed);
        
        return RiskCheckResult::PASS;
    }
    
    /// Update position after fill
    void updatePosition(TickerId ticker_id, Side side, Qty filled_qty, Price fill_price) noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return;
        
        auto& risk = symbol_risk_[ticker_id];
        const int64_t position_delta = (side == 1) ? 
            static_cast<int64_t>(filled_qty) : -static_cast<int64_t>(filled_qty);
        
        risk.position.fetch_add(position_delta, std::memory_order_relaxed);
        risk.position_value.store(
            risk.position.load(std::memory_order_relaxed) * fill_price,
            std::memory_order_relaxed
        );
    }
    
    /// Update P&L
    void updatePnL(TickerId ticker_id, int64_t realized, int64_t unrealized) noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return;
        
        auto& risk = symbol_risk_[ticker_id];
        risk.realized_pnl.store(realized, std::memory_order_relaxed);
        risk.unrealized_pnl.store(unrealized, std::memory_order_relaxed);
    }
    
    /// Get current position
    int64_t getPosition(TickerId ticker_id) const noexcept {
        if (ticker_id >= ME_MAX_TICKERS) return 0;
        return symbol_risk_[ticker_id].position.load(std::memory_order_relaxed);
    }
    
    /// Get total P&L
    int64_t getTotalPnL() const noexcept {
        int64_t total = 0;
        for (size_t i = 0; i < ME_MAX_TICKERS; ++i) {
            total += symbol_risk_[i].realized_pnl.load(std::memory_order_relaxed);
            total += symbol_risk_[i].unrealized_pnl.load(std::memory_order_relaxed);
        }
        return total;
    }
    
    /// Emergency: flatten all positions
    void flattenAll() noexcept {
        for (size_t i = 0; i < ME_MAX_TICKERS; ++i) {
            symbol_risk_[i].position.store(0, std::memory_order_relaxed);
            symbol_risk_[i].position_value.store(0, std::memory_order_relaxed);
        }
    }
    
    // Delete copy/move constructors
    RiskManager(const RiskManager&) = delete;
    RiskManager& operator=(const RiskManager&) = delete;
    RiskManager(RiskManager&&) = delete;
    RiskManager& operator=(RiskManager&&) = delete;
    
private:
    // Risk tracking for all symbols
    std::array<SymbolRisk, ME_MAX_TICKERS> symbol_risk_;
    
    // Global risk metrics
    std::atomic<int64_t> total_exposure_{0};
    std::atomic<int64_t> total_realized_pnl_{0};
    std::atomic<int64_t> total_unrealized_pnl_{0};
    std::atomic<uint64_t> total_orders_{0};
    std::atomic<uint64_t> total_rejects_{0};
};

} // namespace Trading