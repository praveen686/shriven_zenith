#include "position_keeper.h"
#include "common/time_utils.h"

namespace Trading {

using namespace Common;

PositionKeeper::PositionKeeper() {
    // Initialize all positions
    for (auto& pos : positions_) {
        pos.reset();
    }
    
    total_realized_pnl_.store(0, std::memory_order_relaxed);
    total_unrealized_pnl_.store(0, std::memory_order_relaxed);
    
    LOG_INFO("PositionKeeper initialized");
}

} // namespace Trading