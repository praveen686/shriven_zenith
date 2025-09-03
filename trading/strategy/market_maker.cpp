#include "market_maker.h"
#include "common/time_utils.h"

namespace Trading {

using namespace Common;

MarketMaker::MarketMaker(OrderManager* order_manager,
                        FeatureEngine* feature_engine,
                        RiskManager* risk_manager,
                        PositionKeeper* position_keeper)
    : order_manager_(order_manager),
      feature_engine_(feature_engine),
      risk_manager_(risk_manager),
      position_keeper_(position_keeper) {
    
    // Initialize default configurations
    for (auto& config : ticker_configs_) {
        config = MarketMakerConfig{};
    }
    
    LOG_INFO("MarketMaker strategy initialized");
}

} // namespace Trading