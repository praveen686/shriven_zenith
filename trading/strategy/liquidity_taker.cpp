#include "liquidity_taker.h"
#include "common/time_utils.h"

namespace Trading {

using namespace Common;

LiquidityTaker::LiquidityTaker(OrderManager* order_manager,
                              FeatureEngine* feature_engine,
                              RiskManager* risk_manager,
                              PositionKeeper* position_keeper)
    : order_manager_(order_manager),
      feature_engine_(feature_engine),
      risk_manager_(risk_manager),
      position_keeper_(position_keeper) {
    
    // Initialize default configurations
    for (auto& config : ticker_configs_) {
        config = LiquidityTakerConfig{};
    }
    
    // Initialize last order times
    for (auto& time : last_order_time_) {
        time.store(0, std::memory_order_relaxed);
    }
    
    // Initialize momentum trackers
    for (auto& mom : momentum_) {
        mom.buy_volume.store(0, std::memory_order_relaxed);
        mom.sell_volume.store(0, std::memory_order_relaxed);
        mom.last_reset_ns.store(0, std::memory_order_relaxed);
    }
    
    LOG_INFO("LiquidityTaker strategy initialized");
}

} // namespace Trading