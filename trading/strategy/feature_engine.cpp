#include "feature_engine.h"
#include "common/logging.h"

namespace Trading {

using namespace Common;

FeatureEngine::FeatureEngine() {
    // Initialize all features and statistics
    resetAll();
    
    LOG_INFO("FeatureEngine initialized");
}

} // namespace Trading