#pragma once

#include "common/types.h"
#include "common/lf_queue.h"
#include "common/macros.h"

namespace Trading {

// Base interface for all trading strategies
class IStrategy {
public:
    IStrategy() : running_(false) {}
    
    virtual ~IStrategy() = default;
    
    // Core strategy lifecycle
    virtual auto start() -> void = 0;
    virtual auto stop() -> void = 0;
    
    // Market data events
    virtual auto onMarketUpdate(const Common::MarketUpdate& update) -> void = 0;
    virtual auto onOrderResponse(const Common::OrderResponse& response) -> void = 0;
    
    // Strategy decisions
    virtual auto evaluate() -> void = 0;  // Called periodically to evaluate trading opportunities
    
    // Risk and position queries
    virtual auto getPosition(Common::TickerId ticker_id) const -> int32_t = 0;
    virtual auto getPnL() const -> int64_t = 0;
    
    // Delete copy/move operations
    IStrategy(const IStrategy&) = delete;
    IStrategy& operator=(const IStrategy&) = delete;
    IStrategy(IStrategy&&) = delete;
    IStrategy& operator=(IStrategy&&) = delete;
    
protected:
    std::atomic<bool> running_;
};

} // namespace Trading