#pragma once

#include "common/types.h"
#include "common/lf_queue.h"
#include "common/macros.h"

namespace Trading {

// Base interface for all order gateways
// Each exchange (Zerodha, Binance) implements this interface
class IOrderGateway {
public:
    IOrderGateway(Common::LFQueue<Common::OrderRequest, 65536>* request_queue,
                  Common::LFQueue<Common::OrderResponse, 65536>* response_queue)
        : order_requests_queue_(request_queue),
          order_responses_queue_(response_queue),
          running_(false) {}
    
    virtual ~IOrderGateway() = default;
    
    // Core interface methods
    virtual auto start() -> void = 0;
    virtual auto stop() -> void = 0;
    virtual auto connect() -> bool = 0;
    virtual auto disconnect() -> void = 0;
    
    // Order operations
    virtual auto sendOrder(const Common::OrderRequest& request) -> bool = 0;
    virtual auto cancelOrder(Common::OrderId order_id) -> bool = 0;
    virtual auto modifyOrder(Common::OrderId order_id, Common::Price new_price, Common::Qty new_qty) -> bool = 0;
    
    // Delete copy/move operations
    IOrderGateway(const IOrderGateway&) = delete;
    IOrderGateway& operator=(const IOrderGateway&) = delete;
    IOrderGateway(IOrderGateway&&) = delete;
    IOrderGateway& operator=(IOrderGateway&&) = delete;
    
protected:
    Common::LFQueue<Common::OrderRequest, 65536>* order_requests_queue_;
    Common::LFQueue<Common::OrderResponse, 65536>* order_responses_queue_;
    std::atomic<bool> running_;
    
    // Helper method for derived classes to publish responses
    auto publishResponse(const Common::OrderResponse& response) -> bool {
        return order_responses_queue_->enqueue(response);
    }
};

} // namespace Trading