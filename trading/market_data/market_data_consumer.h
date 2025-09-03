#pragma once

#include "common/types.h"
#include "common/lf_queue.h"
#include "common/macros.h"

namespace Trading {

// Base interface for all market data consumers
// Each exchange (Zerodha, Binance) implements this interface
class IMarketDataConsumer {
public:
    IMarketDataConsumer(Common::LFQueue<Common::MarketUpdate, 262144>* output_queue)
        : market_updates_queue_(output_queue), running_(false) {}
    
    virtual ~IMarketDataConsumer() = default;
    
    // Core interface methods
    virtual auto start() -> void = 0;
    virtual auto stop() -> void = 0;
    virtual auto connect() -> bool = 0;
    virtual auto disconnect() -> void = 0;
    virtual auto subscribe(Common::TickerId ticker_id) -> bool = 0;
    virtual auto unsubscribe(Common::TickerId ticker_id) -> bool = 0;
    
    // Delete copy/move operations
    IMarketDataConsumer(const IMarketDataConsumer&) = delete;
    IMarketDataConsumer& operator=(const IMarketDataConsumer&) = delete;
    IMarketDataConsumer(IMarketDataConsumer&&) = delete;
    IMarketDataConsumer& operator=(IMarketDataConsumer&&) = delete;
    
protected:
    Common::LFQueue<Common::MarketUpdate, 262144>* market_updates_queue_;
    std::atomic<bool> running_;
    
    // Helper method for derived classes to publish updates
    auto publishUpdate(const Common::MarketUpdate& update) -> bool {
        auto* dest = market_updates_queue_->getNextToWriteTo();
        if (!dest) {
            return false;  // Queue full
        }
        *dest = update;
        market_updates_queue_->updateWriteIndex();
        return true;
    }
};

} // namespace Trading