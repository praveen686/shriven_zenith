#include "risk_manager.h"
#include "common/time_utils.h"

namespace Trading {

using namespace Common;

RiskManager::RiskManager() {
    // Initialize all risk tracking
    for (auto& risk : symbol_risk_) {
        risk.position.store(0, std::memory_order_relaxed);
        risk.position_value.store(0, std::memory_order_relaxed);
        risk.realized_pnl.store(0, std::memory_order_relaxed);
        risk.unrealized_pnl.store(0, std::memory_order_relaxed);
        risk.order_count.store(0, std::memory_order_relaxed);
        risk.last_order_time_ns.store(0, std::memory_order_relaxed);
        
        // Set default risk limits
        risk.config.max_position = 1000000;      // Rs 10 lakh
        risk.config.max_loss = 50000;           // Rs 50k
        risk.config.max_order_size = 10000;     // 10k shares
        risk.config.max_order_rate = 100;       // 100 orders/sec
        risk.config.min_price = 100;            // Rs 1
        risk.config.max_price = 1000000000;     // Rs 10k
    }
    
    LOG_INFO("RiskManager initialized with default limits");
}

} // namespace Trading