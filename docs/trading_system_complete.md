# Shriven Zenith Ultra-Low Latency Trading System
## Complete Implementation Document

**Project Status**: 🟡 In Progress  
**Start Date**: 2025-01-09  
**Target Completion**: 2025-02-06  
**Version**: 2.0.0  
**Owner**: Praveen Ayyasola (praveenkumar.avln@gmail.com)

---

## Executive Summary

Shriven Zenith is an ultra-low latency trading system designed for sub-microsecond market data processing and sub-10 microsecond order execution. The system integrates with Zerodha (Indian markets) and Binance (crypto markets) while maintaining strict performance requirements through zero-allocation architecture and lock-free data structures.

### Key Performance Achievements
- **Memory Pool Allocation**: 26ns (verified)
- **Lock-Free Queue Operations**: 42-45ns (verified)
- **Async Logging**: 35ns (verified)
- **Target Market Data Latency**: < 1μs
- **Target Order Placement**: < 10μs

---

## Table of Contents
1. [System Architecture](#system-architecture)
2. [Core Design Principles](#core-design-principles)
3. [Implementation Status](#implementation-status)
4. [Component Details](#component-details)
5. [API Integration](#api-integration)
6. [Performance Metrics](#performance-metrics)
7. [Development Standards](#development-standards)
8. [Testing Framework](#testing-framework)
9. [Deployment Strategy](#deployment-strategy)
10. [Risk Management](#risk-management)

---

## System Architecture

### High-Level Data Flow

```
[MARKET DATA SOURCES]
    ├── Zerodha WebSocket → Binary Protocol
    └── Binance WebSocket → JSON Protocol
                ↓
        [Market Data Consumer]
                ↓
        Lock-Free Queue (262,144 slots)
                ↓
        [TRADE ENGINE]
        ├── FeatureEngine (Signal Calculation)
        ├── OrderManager (Order Lifecycle)
        ├── RiskManager (Pre-trade Checks)
        ├── Strategy (MM/Taker/Arbitrage)
        └── PositionKeeper (P&L Tracking)
                ↓
        Lock-Free Queue (65,536 slots)
                ↓
        [Order Gateway]
    ├── Zerodha REST API → TOTP/OAuth2
    └── Binance REST API → HMAC-SHA256
                ↓
        [EXCHANGE EXECUTION]
```

### Thread Architecture

| Thread ID | CPU Core | Component | Priority | Purpose |
|-----------|----------|-----------|----------|---------|
| 0 | Core 0 | Main Controller | Normal | Coordination |
| 1 | Core 1 | Zerodha Market Data | Real-time | WebSocket processing |
| 2 | Core 2 | Binance Market Data | Real-time | WebSocket processing |
| 3 | Core 3 | Trade Engine | Real-time 99 | Core trading logic |
| 4 | Core 4 | Zerodha Orders | Real-time | Order management |
| 5 | Core 5 | Binance Orders | Real-time | Order management |
| 6 | Core 6 | Risk Manager | Real-time | Risk monitoring |
| 7 | Core 7 | Logger | Normal | Async logging |

---

## Core Design Principles

### 1. Single Source of Truth Philosophy
**CRITICAL**: ALL types and infrastructure exist in Common library ONLY
- No duplicate type definitions anywhere
- No Trading::Types namespace
- Business logic uses Common types directly
- Common library is the ONLY place for type definitions

### 2. Zero Dynamic Allocation
- All memory pre-allocated at startup (~30MB fixed)
- Memory pools for all objects
- No heap allocation in trading path
- Fixed-size containers only

### 3. Lock-Free Communication
- SPSC queues between components
- Atomic operations for shared state
- No mutexes in hot path
- Cache-line aligned data structures

### 4. Explicit Type Safety
```cpp
// MANDATORY: All type conversions must be explicit
size_t count = static_cast<size_t>(int_value);
uint64_t ns = static_cast<uint64_t>(timespec.tv_sec) * 1000000000ULL;

// FORBIDDEN: Implicit conversions
size_t count = int_value;  // COMPILE ERROR
```

---

## Implementation Status

### Phase Completion Summary

| Phase | Component | Status | Completion | Notes |
|-------|-----------|--------|------------|-------|
| **Phase 1** | Core Infrastructure | ✅ Complete | 100% | Common library, memory pools, queues |
| **Phase 2** | API Connectors | 🟡 In Progress | 15% | Authentication modules started |
| **Phase 3** | Trade Engine | ⏸️ Pending | 0% | Waiting on connectors |
| **Phase 4** | Strategy Framework | ⏸️ Pending | 0% | Requires engine completion |
| **Phase 5** | Integration & Testing | ⏸️ Pending | 0% | Final phase |

### Detailed Component Status

#### ✅ Completed Components (Phase 1)
- `Common::MemoryPool<T, SIZE>` - Lock-free memory allocation
- `Common::LFQueue<T, SIZE>` - SPSC lock-free queues
- `Common::Logger` - Async logging system
- `Common::ThreadUtils` - CPU affinity and RT priority
- `Common::CacheAligned<T>` - False sharing prevention
- Extended Common types for trading (Order, Position, MarketUpdate)

#### 🟡 In Progress Components (Phase 2)
- Zerodha TOTP authentication (RFC 6238)
- Zerodha OAuth2 flow
- Binance HMAC-SHA256 signing
- WebSocket connection managers

#### ⏸️ Pending Components
- Trade Engine core loop
- Order Manager (without std::map)
- Risk Manager (atomic limits)
- Position Keeper (fixed arrays)
- Market Maker strategy
- Liquidity Taker strategy
- Cross-exchange arbitrage

---

## Component Details

### Trade Engine Core

```cpp
namespace Trading {
    class TradeEngine {
    private:
        // Memory pools - using Common types directly
        Common::MemoryPool<Common::Order, 10000> order_pool_;
        Common::MemoryPool<Common::Position, 1000> position_pool_;
        
        // Lock-free queues for communication
        Common::LFQueue<Common::OrderRequest, 65536> order_request_queue_;
        Common::LFQueue<Common::OrderResponse, 65536> order_response_queue_;
        Common::LFQueue<Common::MarketUpdate, 262144> market_data_queue_;
        
        // Fixed-size order book storage
        struct OrderBook {
            Common::CacheAligned<Price> bid_prices[MAX_DEPTH];
            Common::CacheAligned<Qty> bid_qtys[MAX_DEPTH];
            Common::CacheAligned<Price> ask_prices[MAX_DEPTH];
            Common::CacheAligned<Qty> ask_qtys[MAX_DEPTH];
            std::atomic<uint64_t> last_update_ns{0};
        };
        
        std::array<OrderBook, MAX_SYMBOLS> order_books_;
        
        // Main event loop - zero allocation
        void run() {
            while (running_) {
                // Process market data
                if (auto* update = market_queue_->dequeue()) {
                    updateOrderBook(update);
                    strategy_->onMarketData(update);
                    market_pool_.deallocate(update);
                }
                
                // Process order responses
                if (auto* response = order_response_queue_->dequeue()) {
                    updatePosition(response);
                    strategy_->onOrderUpdate(response);
                    response_pool_.deallocate(response);
                }
            }
        }
    };
}
```

### Order Manager Design

```cpp
class OrderManager {
private:
    // Fixed-size order storage - NO std::map
    struct OrderEntry {
        Common::Order order;
        std::atomic<bool> active{false};
        std::atomic<uint64_t> last_update_ns{0};
    };
    
    // Direct indexed access: O(1) lookup
    static constexpr size_t MAX_ORDERS = 10000;
    Common::CacheAligned<OrderEntry> orders_[MAX_ORDERS];
    
public:
    // All operations are O(1) with no allocation
    Order* createOrder(const OrderRequest& req) noexcept;
    bool cancelOrder(OrderId id) noexcept;
    const Order* getOrder(OrderId id) const noexcept {
        const size_t idx = id % MAX_ORDERS;
        return orders_[idx].active ? &orders_[idx].order : nullptr;
    }
};
```

### Risk Manager Implementation

```cpp
class RiskManager {
private:
    struct RiskLimits {
        std::atomic<int64_t> max_position{1000000};    // Rs 10 lakh
        std::atomic<int64_t> max_loss{50000};          // Rs 50k
        std::atomic<uint32_t> max_order_rate{100};     // per second
        std::atomic<uint32_t> max_order_size{10000};   // shares
    };
    
    struct SymbolRisk {
        std::atomic<int64_t> position{0};
        std::atomic<int64_t> realized_pnl{0};
        std::atomic<int64_t> unrealized_pnl{0};
        std::atomic<uint32_t> order_count{0};
    };
    
    Common::CacheAligned<SymbolRisk> symbol_risk_[MAX_SYMBOLS];
    
public:
    enum class RiskCheckResult {
        PASS,
        POSITION_LIMIT_BREACH,
        LOSS_LIMIT_BREACH,
        ORDER_RATE_BREACH
    };
    
    // Sub-100ns risk check
    [[gnu::always_inline]] 
    inline RiskCheckResult checkOrder(const Order& order) noexcept {
        const auto& risk = symbol_risk_[order.symbol_id];
        
        if (UNLIKELY(risk.position + order.quantity > max_position))
            return RiskCheckResult::POSITION_LIMIT_BREACH;
            
        if (UNLIKELY(risk.realized_pnl < -max_loss))
            return RiskCheckResult::LOSS_LIMIT_BREACH;
            
        return RiskCheckResult::PASS;
    }
};
```

---

## API Integration

### Zerodha KiteConnect v3

#### Authentication Flow
1. Login with API credentials
2. Generate TOTP code (RFC 6238)
3. Complete OAuth2 flow
4. Obtain access token (24-hour validity)
5. Subscribe to WebSocket with token

#### Implementation
```cpp
namespace Trading::Connectors::Zerodha {
    class KiteConnector {
    private:
        struct Credentials {
            char api_key[64];
            char api_secret[128];
            char user_id[32];
            char totp_secret[64];
            char access_token[128];
            uint64_t token_expiry_ns;
        };
        
        class MarketDataHandler {
            Common::TCPSocket websocket_;
            MarketDataQueue* output_queue_;
            
            void onBinaryTick(const uint8_t* data, size_t len) {
                // Parse Kite binary protocol
                auto* update = update_pool_.allocate();
                
                // Extract fields (big-endian)
                update->symbol_id = ntohl(*reinterpret_cast<const uint32_t*>(data));
                update->last_price = ntohl(*reinterpret_cast<const uint32_t*>(data + 4));
                update->timestamp_ns = Common::getCurrentNanos();
                
                output_queue_->enqueue(update);
            }
        };
        
        class OrderHandler {
            void placeOrder(const Common::OrderRequest* req) {
                char url[256];
                snprintf(url, sizeof(url), 
                    "/orders/regular?"
                    "exchange=%s&"
                    "tradingsymbol=%s&"
                    "transaction_type=%s&"
                    "quantity=%u&"
                    "order_type=LIMIT&"
                    "price=%.2f",
                    req->exchange,
                    req->symbol,
                    req->side == Side::BUY ? "BUY" : "SELL",
                    req->quantity,
                    req->price / 100.0);  // Convert paise to rupees
                
                sendHttpRequest("POST", url);
            }
        };
    };
}
```

### Binance API v3

#### Authentication
- HMAC-SHA256 signature for all requests
- API key in header
- Timestamp and signature in query parameters

#### Implementation
```cpp
namespace Trading::Connectors::Binance {
    class BinanceConnector {
    private:
        struct HmacSigner {
            uint8_t secret_key_[64];
            
            void sign(const char* payload, char* signature) {
                // HMAC-SHA256 without OpenSSL
                uint8_t hash[32];
                hmac_sha256(secret_key_, payload, strlen(payload), hash);
                
                // Convert to hex string
                for (int i = 0; i < 32; ++i) {
                    sprintf(signature + (i * 2), "%02x", hash[i]);
                }
            }
        };
        
        class WebSocketClient {
            void subscribeDepth(const char* symbol) {
                char stream[128];
                snprintf(stream, sizeof(stream), 
                    "%s@depth20@100ms", symbol);
                
                subscribe(stream);
            }
            
            void onDepthUpdate(const char* json) {
                // RapidJSON parsing (stack allocated)
                rapidjson::Document doc;
                doc.Parse(json);
                
                auto* update = update_pool_.allocate();
                update->symbol_id = getSymbolId(doc["s"].GetString());
                
                // Parse bids
                const auto& bids = doc["b"].GetArray();
                for (size_t i = 0; i < std::min(5UL, bids.Size()); ++i) {
                    update->bid_prices[i] = 
                        static_cast<int64_t>(bids[i][0].GetDouble() * 100000000);
                    update->bid_qtys[i] = 
                        static_cast<uint32_t>(bids[i][1].GetDouble() * 100000000);
                }
                
                output_queue_->enqueue(update);
            }
        };
    };
}
```

---

## Performance Metrics

### Latency Measurements (Actual)

| Component | Target | Achieved | Method | Status |
|-----------|--------|----------|--------|--------|
| Memory Pool Alloc | 50ns | 26ns | rdtsc benchmark | ✅ Verified |
| Queue Enqueue | 100ns | 45ns | rdtsc benchmark | ✅ Verified |
| Queue Dequeue | 100ns | 42ns | rdtsc benchmark | ✅ Verified |
| Async Logging | 100ns | 35ns | rdtsc benchmark | ✅ Verified |
| Risk Check | 100ns | - | Pending impl | ⏸️ TBD |
| Order Placement | 10μs | - | Pending impl | ⏸️ TBD |

### Throughput Targets

| Operation | Target | Notes |
|-----------|--------|-------|
| Market Data Processing | 1M messages/sec | Per symbol |
| Order Placement | 100K orders/sec | Aggregate |
| Risk Checks | 500K checks/sec | Pre-trade |
| Position Updates | 200K updates/sec | Post-trade |

### Memory Footprint

| Component | Size | Count | Total | Purpose |
|-----------|------|-------|-------|---------|
| Order Pool | 160B | 10,000 | 1.6MB | Active orders |
| Position Pool | 200B | 1,000 | 200KB | Position tracking |
| Market Update Pool | 80B | 100,000 | 8MB | Market data |
| Order Book | 4KB | 100 | 400KB | Local book per symbol |
| LF Queues | - | - | 20MB | All queues combined |
| **Total Fixed** | - | - | **~30MB** | No dynamic allocation |

---

## Development Standards

### Mandatory Compiler Flags
```bash
-Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion
-Wold-style-cast -Wformat-security -Weffc++ -Wno-unused
-O3 -march=native -mtune=native -flto
```

### Build Process
```bash
# ONLY approved build commands:
./scripts/build_strict.sh      # Development (zero warnings)
./scripts/build_coverage_gcc.sh # Coverage analysis
./scripts/build_benchmark.sh    # Performance testing

# FORBIDDEN:
cmake .                         # Never run cmake directly
make                           # Never use make
g++ *.cpp                      # Never compile manually
```

### Code Review Checklist
- [ ] Builds with zero warnings
- [ ] No dynamic allocation
- [ ] All type conversions explicit
- [ ] Cache-aligned shared data
- [ ] Memory ordering specified for atomics
- [ ] Constructors use initialization lists
- [ ] Rule of 3/5/0 followed
- [ ] Performance benchmarks pass

### Git Commit Format
```
type(scope): description (max 50 chars)

Detailed explanation of what and why.

Performance impact: [None/Positive/Negative]
Breaking changes: [Yes/No]
```

---

## Testing Framework

### Test Categories

#### Unit Tests (100% coverage required)
```cpp
TEST(OrderManager, CreateOrder) {
    OrderManager mgr;
    OrderRequest req{.symbol_id = 1, .price = 100, .quantity = 10};
    
    auto* order = mgr.createOrder(req);
    ASSERT_NE(order, nullptr);
    EXPECT_EQ(order->symbol_id, 1);
    EXPECT_EQ(order->price, 100);
}
```

#### Benchmark Tests
```cpp
BENCHMARK(OrderManager_CreateOrder) {
    OrderManager mgr;
    OrderRequest req{.symbol_id = 1, .price = 100, .quantity = 10};
    
    auto start = rdtsc();
    auto* order = mgr.createOrder(req);
    auto end = rdtsc();
    
    ASSERT_LT(end - start, 1000);  // < 1000 cycles
}
```

#### Stress Tests
```cpp
STRESS_TEST(OrderManager_Concurrent) {
    OrderManager mgr;
    std::atomic<uint64_t> orders_created{0};
    
    // Launch 8 threads
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&mgr, &orders_created] {
            for (int j = 0; j < 100000; ++j) {
                OrderRequest req{...};
                if (mgr.createOrder(req)) {
                    orders_created.fetch_add(1);
                }
            }
        });
    }
    
    // Verify no crashes, correct count
    EXPECT_GT(orders_created, 700000);  // Some may fail due to pool exhaustion
}
```

### Test Execution Matrix

| Test Type | Frequency | Duration | Pass Criteria |
|-----------|-----------|----------|---------------|
| Unit Tests | Every commit | < 1 sec | 100% pass |
| Integration | Every PR | < 10 sec | 100% pass |
| Benchmarks | Every PR | < 30 sec | Meet targets |
| Stress Tests | Daily | < 5 min | No crashes |
| Load Tests | Weekly | 1 hour | Sustained perf |

---

## Deployment Strategy

### Environment Setup

#### Hardware Requirements
- CPU: Intel Xeon Gold 6248R or better
- Cores: Minimum 8 physical cores
- RAM: 32GB DDR4-2933 ECC
- Network: 10Gbps NIC (Intel X710)
- Storage: NVMe SSD (Samsung PM983)

#### OS Configuration
```bash
# Disable CPU frequency scaling
echo performance > /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Disable hyperthreading
echo off > /sys/devices/system/cpu/smt/control

# Set IRQ affinity
echo 2 > /proc/irq/24/smp_affinity  # Network IRQ to CPU 2

# Increase network buffers
sysctl -w net.core.rmem_max=134217728
sysctl -w net.core.wmem_max=134217728
```

### Deployment Phases

1. **Development Environment**
   - Local testing with mock exchange
   - Unit and integration tests
   - Performance profiling

2. **Paper Trading**
   - Real market data
   - Simulated order execution
   - 2-week validation period

3. **Production Pilot**
   - Small position limits (Rs 10,000)
   - Single symbol initially
   - Gradual scale-up

4. **Full Production**
   - All configured symbols
   - Full position limits
   - 24/7 monitoring

### Monitoring & Alerting

```cpp
namespace Trading::Monitoring {
    class MetricsCollector {
        // Prometheus metrics
        prometheus::Counter orders_sent{"orders_sent", "Total orders sent"};
        prometheus::Counter orders_filled{"orders_filled", "Total orders filled"};
        prometheus::Histogram order_latency{"order_latency_us", "Order latency"};
        prometheus::Gauge active_positions{"active_positions", "Active positions"};
        prometheus::Gauge unrealized_pnl{"unrealized_pnl", "Unrealized P&L"};
        
        void recordOrder(uint64_t latency_ns) {
            orders_sent.Increment();
            order_latency.Observe(latency_ns / 1000.0);  // Convert to microseconds
        }
    };
}
```

---

## Risk Management

### System Risks

| Risk | Impact | Probability | Mitigation | Status |
|------|--------|-------------|------------|--------|
| Exchange API failure | Critical | Medium | Redundant connections, circuit breaker | 🟡 Implementing |
| Network latency spike | High | Medium | Colocate servers, monitor RTT | 🔴 Planning |
| Memory corruption | Critical | Low | AddressSanitizer in dev, bounds checking | 🟢 Mitigated |
| False sharing | High | Medium | Cache-line alignment verified | 🟢 Mitigated |
| Order duplication | Critical | Low | Idempotency keys, sequence numbers | 🟡 Implementing |

### Trading Risks

| Control | Limit | Action | Implementation |
|---------|-------|--------|----------------|
| Position Limit | Rs 10 lakh per symbol | Reject new orders | RiskManager::checkOrder() |
| Loss Limit | Rs 50k daily | Stop all trading | RiskManager::checkDailyLoss() |
| Order Rate | 100 orders/sec | Throttle submissions | OrderGateway::rateLimit() |
| Fat Finger | 10x avg order size | Require confirmation | OrderValidator::checkSize() |

### Operational Procedures

#### Daily Startup
1. Verify network connectivity
2. Authenticate with exchanges
3. Load configuration
4. Initialize memory pools
5. Start market data feeds
6. Enable trading after warm-up

#### Emergency Shutdown
1. Cancel all open orders
2. Flatten all positions
3. Disconnect from exchanges
4. Dump state to disk
5. Send alert notifications

---

## Critical Files Reference

### Must Read Documentation
1. `/CLAUDE.md` - Mandatory coding standards
2. `/docs/architecture/01_system_overview.md` - System design
3. `/docs/developer_guide/developer_guide.md` - Development guide
4. `/docs/developer_guide/bldg_blocks_api.md` - Common library API
5. `/docs/reports/compiler_warnings_lessons.md` - Lessons learned

### Core Implementation Files
1. `/common/types.h` - All type definitions
2. `/common/mem_pool.h` - Memory pool implementation
3. `/common/lf_queue.h` - Lock-free queue
4. `/trading/trade_engine.h` - Main engine
5. `/examples/` - Reference implementations

---

## Open Issues & TODOs

### Critical Path Items
| ID | Issue | Priority | Assigned | Target Date |
|----|-------|----------|----------|-------------|
| #001 | Complete TOTP implementation | P0 | - | Jan 15 |
| #002 | HMAC-SHA256 without OpenSSL | P0 | - | Jan 16 |
| #003 | WebSocket reconnection logic | P0 | - | Jan 17 |
| #004 | Order book builder | P1 | - | Jan 20 |
| #005 | Position reconciliation | P1 | - | Jan 22 |

### Performance Optimizations
| Optimization | Expected Gain | Status |
|--------------|---------------|--------|
| SIMD for feature calculation | 30% faster | 🔴 Planned |
| Huge pages for memory pools | 10% less TLB misses | 🔴 Planned |
| Kernel bypass networking | 50% latency reduction | 🔴 Research |

---

## Contact & Support

**Project Owner**: Praveen Ayyasola  
**Email**: praveenkumar.avln@gmail.com  
**Approval Required**: Any deviation from documented standards

**Development Team**:
- All code must pass review
- Zero tolerance for warnings
- Performance regression = automatic rejection

---

## Appendix: Quick Reference

### Type Conversion Patterns
```cpp
// Always explicit
size_t s = static_cast<size_t>(int_val);
uint64_t u = static_cast<uint64_t>(signed_val);

// Safe signed to unsigned
if (signed_val > 0) {
    unsigned_val = static_cast<size_t>(signed_val);
}
```

### Memory Pool Usage
```cpp
auto* obj = pool.allocate();
if (!obj) {
    LOG_ERROR("Pool exhausted");
    return;
}
// Use obj...
pool.deallocate(obj);
```

### Lock-Free Queue
```cpp
if (!queue.enqueue(std::move(item))) {
    LOG_WARN("Queue full");
}

if (auto* item = queue.dequeue()) {
    process(item);
}
```

### Cache-Aligned Atomic
```cpp
Common::CacheAligned<std::atomic<uint64_t>> counter{0};
counter->fetch_add(1, std::memory_order_relaxed);
```

### Thread Setup
```cpp
ThreadUtils::setCurrentThreadAffinity(cpu_id);
ThreadUtils::setRealTimePriority(99);
```

---

**Document Version**: 2.0.0  
**Last Updated**: 2025-01-09  
**Status**: ACTIVE  
**Build Command**: `./scripts/build_strict.sh`

*"In trading, microseconds matter. In our code, nanoseconds matter."*