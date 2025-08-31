# Shriven Zenith Developer Guide

## Table of Contents
1. [Getting Started](#getting-started)
2. [Development Environment Setup](#development-environment-setup)
3. [Coding Standards](#coding-standards)
4. [Using BldgBlocks Components](#using-bldgblocks-components)
5. [Performance Guidelines](#performance-guidelines)
6. [Testing Requirements](#testing-requirements)
7. [Common Pitfalls](#common-pitfalls)
8. [Code Examples](#code-examples)

## Getting Started

### Prerequisites
- C++23 compatible compiler (GCC 13+ or Clang 16+)
- CMake 3.20+
- Ninja build system
- Linux kernel 5.15+ with RT patch (recommended)
- NUMA libraries (`libnuma-dev`)

### Initial Setup
```bash
# Clone repository
git clone <repository>
cd shriven_zenith

# Load configuration
cp config.toml.example config.toml
# Edit config.toml for your environment

# Build with strict checks
./scripts/build_strict.sh

# Verify build
./cmake/build-release/examples/examples all
```

## Development Environment Setup

### Required Compiler Flags

**MANDATORY**: All code MUST compile with these flags:
```bash
-Wall -Wextra -Werror -Wpedantic
-Wconversion -Wsign-conversion -Wold-style-cast
-Wformat-security -Weffc++ -Wno-unused
```

### IDE Configuration

#### VS Code (.vscode/c_cpp_properties.json)
```json
{
    "configurations": [{
        "name": "Linux",
        "compilerPath": "/usr/bin/g++",
        "cStandard": "c17",
        "cppStandard": "c++23",
        "compilerArgs": [
            "-Wall", "-Wextra", "-Werror", "-Wpedantic",
            "-Wconversion", "-Wsign-conversion"
        ]
    }]
}
```

### System Tuning for Development

```bash
# Disable CPU frequency scaling
sudo cpupower frequency-set -g performance

# Isolate CPUs for trading threads
# Add to /etc/default/grub:
GRUB_CMDLINE_LINUX="isolcpus=2,3,4,5 nohz_full=2,3,4,5"

# Huge pages
echo 1024 | sudo tee /proc/sys/vm/nr_hugepages

# Disable swap
sudo swapoff -a
```

## Coding Standards

### DO's - MANDATORY PRACTICES

#### 1. ALWAYS Use Explicit Type Conversions
```cpp
// ✅ CORRECT
size_t count = static_cast<size_t>(std::thread::hardware_concurrency());
uint64_t nanos = static_cast<uint64_t>(timespec.tv_sec) * 1'000'000'000ULL;

// ❌ WRONG - Implicit conversion
size_t count = std::thread::hardware_concurrency();  // WARNING!
```

#### 2. ALWAYS Initialize Members in Constructor Initialization List
```cpp
// ✅ CORRECT
class OrderBook {
    OrderBook() 
        : best_bid_(0),
          best_ask_(UINT64_MAX),
          last_update_time_(0),
          orders_() {
    }
};

// ❌ WRONG - Initialization in body
class OrderBook {
    OrderBook() {
        best_bid_ = 0;  // INEFFICIENT!
        best_ask_ = UINT64_MAX;
    }
};
```

#### 3. ALWAYS Follow Rule of Three/Five/Zero
```cpp
// ✅ CORRECT - Managing resources
class ResourceManager {
    void* resource_;
public:
    // Delete copy operations
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    
    // Define move operations
    ResourceManager(ResourceManager&& other) noexcept;
    ResourceManager& operator=(ResourceManager&& other) noexcept;
    
    ~ResourceManager();
};
```

#### 4. ALWAYS Use Static Cast for Pointer Arithmetic
```cpp
// ✅ CORRECT
char* ptr = static_cast<char*>(memory_region_);
size_t offset = static_cast<size_t>(
    reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(base)
);

// ❌ WRONG
size_t offset = ptr - base;  // Type mismatch!
```

#### 5. ALWAYS Use CacheAligned for Shared Data
```cpp
// ✅ CORRECT - Prevents false sharing
struct TradingStats {
    CacheAligned<std::atomic<uint64_t>> orders_sent;
    CacheAligned<std::atomic<uint64_t>> orders_filled;
    CacheAligned<std::atomic<uint64_t>> orders_rejected;
};

// ❌ WRONG - False sharing likely
struct TradingStats {
    std::atomic<uint64_t> orders_sent;      // Same cache line!
    std::atomic<uint64_t> orders_filled;    // Performance killer!
};
```

### DON'Ts - FORBIDDEN PRACTICES

#### 1. NEVER Use Dynamic Allocation in Hot Path
```cpp
// ❌ WRONG - Allocation in critical path
void process_order(const Order& order) {
    auto result = new ExecutionReport();  // FORBIDDEN!
    // ...
}

// ✅ CORRECT - Use pre-allocated pool
void process_order(const Order& order) {
    auto result = exec_report_pool_.allocate();
    if (!result) {
        LOG_ERROR("Pool exhausted");
        return;
    }
    // ...
    exec_report_pool_.deallocate(result);
}
```

#### 2. NEVER Ignore Compiler Warnings
```cpp
// ❌ WRONG - Suppressing warnings
#pragma GCC diagnostic ignored "-Wconversion"
int value = some_size_t_value;  // HIDING BUGS!

// ✅ CORRECT - Fix the warning
int value = static_cast<int>(some_size_t_value);
```

#### 3. NEVER Use Exceptions in Trading Code
```cpp
// ❌ WRONG - Exception throwing
Order* get_order() {
    if (!has_orders()) {
        throw std::runtime_error("No orders");  // FORBIDDEN!
    }
}

// ✅ CORRECT - Return error code
std::pair<Order*, ErrorCode> get_order() {
    if (!has_orders()) {
        return {nullptr, ErrorCode::NO_ORDERS};
    }
    return {order, ErrorCode::SUCCESS};
}
```

#### 4. NEVER Mix Signed and Unsigned Without Cast
```cpp
// ❌ WRONG - Mixed signedness
ssize_t bytes_read = read(fd, buffer, size);
total_bytes += bytes_read;  // WARNING: signed + unsigned

// ✅ CORRECT - Explicit handling
ssize_t bytes_read = read(fd, buffer, size);
if (bytes_read > 0) {
    total_bytes += static_cast<size_t>(bytes_read);
}
```

## Using BldgBlocks Components

### Logger Usage Pattern

#### Initialization (Once at Startup)
```cpp
int main() {
    // Setup configuration
    if (!BldgBlocks::initConfig("config.toml")) {
        std::cerr << "Failed to load config\n";
        return 1;
    }
    
    // Initialize logger with module-specific name
    std::string log_file = BldgBlocks::config().getLogsDir() + 
                           "/trading_" + get_timestamp() + ".log";
    BldgBlocks::initLogging(log_file);
    
    // Your application code
    run_trading_system();
    
    // Clean shutdown
    BldgBlocks::shutdownLogging();
    return 0;
}
```

#### Logging Throughout Application
```cpp
void process_market_data(const MarketData& md) {
    LOG_DEBUG("MD received: sym=%s, bid=%lu, ask=%lu",
              md.symbol, md.bid_price, md.ask_price);
    
    if (md.bid_price >= md.ask_price) {
        LOG_ERROR("Crossed market: bid=%lu >= ask=%lu",
                  md.bid_price, md.ask_price);
        return;
    }
    
    LOG_INFO("MD|%s|%lu|%lu|%u|%u|%ld",  // Structured for parsing
             md.symbol, md.bid_price, md.ask_price,
             md.bid_size, md.ask_size, md.timestamp);
}
```

### Memory Pool Usage Pattern

```cpp
class OrderManager {
    BldgBlocks::MemPool<Order> order_pool_;
    BldgBlocks::MemPool<Execution> exec_pool_;
    
public:
    OrderManager() 
        : order_pool_(1'000'000),  // Pre-allocate 1M orders
          exec_pool_(2'000'000) {   // Pre-allocate 2M executions
        LOG_INFO("OrderManager initialized: orders=%zu, execs=%zu",
                 order_pool_.capacity(), exec_pool_.capacity());
    }
    
    Order* create_order(OrderId id, Price px, Qty qty) {
        Order* order = order_pool_.allocate();
        if (UNLIKELY(!order)) {
            LOG_ERROR("Order pool exhausted at %zu orders",
                      order_pool_.capacity());
            return nullptr;
        }
        
        // Use placement new
        new (order) Order{id, px, qty};
        LOG_DEBUG("Order created: id=%lu, px=%lu, qty=%u",
                  id, px, qty);
        return order;
    }
    
    void destroy_order(Order* order) {
        if (!order) return;
        
        LOG_DEBUG("Destroying order: id=%lu", order->id);
        order->~Order();  // Explicit destructor
        order_pool_.deallocate(order);
    }
};
```

### Lock-Free Queue Usage Pattern

```cpp
class MarketDataHandler {
    BldgBlocks::LFQueue<MarketData> inbound_queue_;
    std::atomic<bool> running_{true};
    
public:
    MarketDataHandler() : inbound_queue_(65536) {
        LOG_INFO("MDHandler initialized with queue size 65536");
    }
    
    // Producer thread (network receiver)
    void on_market_data(MarketData&& data) {
        if (!inbound_queue_.enqueue(std::move(data))) {
            LOG_WARN("MD queue full, dropping: %s", data.symbol);
            stats_.dropped_msgs++;
        }
    }
    
    // Consumer thread (processor)
    void process_loop() {
        BldgBlocks::ThreadUtils::setThreadName("MD_Processor");
        BldgBlocks::ThreadUtils::setCurrentThreadAffinity(2);
        
        MarketData data;
        while (running_.load(std::memory_order_acquire)) {
            while (inbound_queue_.dequeue(data)) {
                process_market_data(data);
            }
            // Brief pause to prevent spinning
            __builtin_ia32_pause();
        }
    }
};
```

### Thread Utilities Usage Pattern

```cpp
class TradingEngine {
    void setup_threads() {
        // Main trading thread - highest priority
        std::thread trading_thread([this]() {
            BldgBlocks::ThreadUtils::setThreadName("Trading");
            BldgBlocks::ThreadUtils::setCurrentThreadAffinity(2);
            BldgBlocks::ThreadUtils::setRealTimePriority(99);
            
            LOG_INFO("Trading thread started on CPU 2");
            trading_loop();
        });
        
        // Market data thread
        std::thread md_thread([this]() {
            BldgBlocks::ThreadUtils::setThreadName("MarketData");
            BldgBlocks::ThreadUtils::setCurrentThreadAffinity(3);
            BldgBlocks::ThreadUtils::setRealTimePriority(95);
            
            LOG_INFO("MD thread started on CPU 3");
            market_data_loop();
        });
        
        // Risk management thread - lower priority
        std::thread risk_thread([this]() {
            BldgBlocks::ThreadUtils::setThreadName("Risk");
            BldgBlocks::ThreadUtils::setCurrentThreadAffinity(4);
            BldgBlocks::ThreadUtils::setNormalPriority();
            
            LOG_INFO("Risk thread started on CPU 4");
            risk_loop();
        });
    }
};
```

## Performance Guidelines

### 1. Measure Everything
```cpp
class LatencyTracker {
    struct Stats {
        uint64_t min = UINT64_MAX;
        uint64_t max = 0;
        uint64_t sum = 0;
        uint64_t count = 0;
    };
    
    CacheAligned<Stats> stats_;
    
public:
    void record(uint64_t latency_ns) {
        stats_.value.min = std::min(stats_.value.min, latency_ns);
        stats_.value.max = std::max(stats_.value.max, latency_ns);
        stats_.value.sum += latency_ns;
        stats_.value.count++;
    }
    
    void report() {
        if (stats_.value.count > 0) {
            LOG_INFO("Latency: min=%lu, max=%lu, avg=%lu ns",
                     stats_.value.min, stats_.value.max,
                     stats_.value.sum / stats_.value.count);
        }
    }
};
```

### 2. Inline Critical Functions
```cpp
// In header file
class OrderBook {
    [[gnu::always_inline]] inline Price get_best_bid() const noexcept {
        return best_bid_;
    }
    
    [[gnu::always_inline]] inline Price get_best_ask() const noexcept {
        return best_ask_;
    }
};
```

### 3. Use Branch Prediction Hints
```cpp
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

void process_order(Order* order) {
    if (LIKELY(order != nullptr)) {
        // Fast path - order exists
        execute_order(order);
    } else {
        // Slow path - null order
        LOG_ERROR("Null order received");
    }
}
```

### 4. Prefetch Data
```cpp
void process_orders(Order** orders, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        // Prefetch next order while processing current
        if (i + 1 < count) {
            __builtin_prefetch(orders[i + 1], 0, 3);
        }
        process_single_order(orders[i]);
    }
}
```

## Testing Requirements

### Unit Test Template
```cpp
// tests/test_order_manager.cpp
#include <gtest/gtest.h>
#include "order_manager.h"

class OrderManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize config and logging for tests
        BldgBlocks::initConfig("test_config.toml");
        BldgBlocks::initLogging("logs/test.log");
    }
    
    void TearDown() override {
        BldgBlocks::shutdownLogging();
    }
};

TEST_F(OrderManagerTest, CreatesAndDestroysOrders) {
    OrderManager mgr;
    
    auto* order = mgr.create_order(123, 10000, 100);
    ASSERT_NE(order, nullptr);
    EXPECT_EQ(order->id, 123);
    EXPECT_EQ(order->price, 10000);
    EXPECT_EQ(order->quantity, 100);
    
    mgr.destroy_order(order);
}

TEST_F(OrderManagerTest, HandlesPoolExhaustion) {
    OrderManager mgr(10);  // Small pool for testing
    
    std::vector<Order*> orders;
    for (int i = 0; i < 10; ++i) {
        orders.push_back(mgr.create_order(i, 100, 10));
    }
    
    // Pool should be exhausted
    auto* order = mgr.create_order(999, 100, 10);
    EXPECT_EQ(order, nullptr);
    
    // Free one and retry
    mgr.destroy_order(orders[0]);
    order = mgr.create_order(999, 100, 10);
    ASSERT_NE(order, nullptr);
}
```

### Benchmark Template
```cpp
// benchmarks/bench_memory_pool.cpp
#include <benchmark/benchmark.h>
#include "mem_pool.h"

static void BM_MemPoolAllocate(benchmark::State& state) {
    BldgBlocks::MemPool<Order> pool(1000000);
    
    for (auto _ : state) {
        auto* p = pool.allocate();
        benchmark::DoNotOptimize(p);
        pool.deallocate(p);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MemPoolAllocate);

static void BM_HeapAllocate(benchmark::State& state) {
    for (auto _ : state) {
        auto* p = new Order();
        benchmark::DoNotOptimize(p);
        delete p;
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HeapAllocate);
```

### Stress Test Template
```cpp
// tests/stress_test_queue.cpp
void stress_test_queue() {
    constexpr int NUM_PRODUCERS = 4;
    constexpr int NUM_CONSUMERS = 4;
    constexpr int ITEMS_PER_PRODUCER = 100000;
    
    BldgBlocks::LFQueue<int> queue(1000000);
    std::atomic<int> total_produced{0};
    std::atomic<int> total_consumed{0};
    
    // Launch producers
    std::vector<std::thread> producers;
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        producers.emplace_back([&, i]() {
            for (int j = 0; j < ITEMS_PER_PRODUCER; ++j) {
                int value = i * ITEMS_PER_PRODUCER + j;
                while (!queue.enqueue(value)) {
                    std::this_thread::yield();
                }
                total_produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    
    // Launch consumers
    std::vector<std::thread> consumers;
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        consumers.emplace_back([&]() {
            int value;
            while (total_consumed.load() < NUM_PRODUCERS * ITEMS_PER_PRODUCER) {
                if (queue.dequeue(value)) {
                    total_consumed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    
    // Wait for completion
    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();
    
    EXPECT_EQ(total_produced.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
    EXPECT_EQ(total_consumed.load(), NUM_PRODUCERS * ITEMS_PER_PRODUCER);
}
```

## Common Pitfalls

### 1. False Sharing
```cpp
// ❌ WRONG - Multiple atomics on same cache line
struct BadCounters {
    std::atomic<uint64_t> counter1;  // Bytes 0-7
    std::atomic<uint64_t> counter2;  // Bytes 8-15 - SAME CACHE LINE!
};

// ✅ CORRECT - Each counter on its own cache line
struct GoodCounters {
    CacheAligned<std::atomic<uint64_t>> counter1;  // Bytes 0-63
    CacheAligned<std::atomic<uint64_t>> counter2;  // Bytes 64-127
};
```

### 2. Memory Ordering Mistakes
```cpp
// ❌ WRONG - Too strong ordering
value.store(42, std::memory_order_seq_cst);  // Expensive!

// ✅ CORRECT - Weakest sufficient ordering
value.store(42, std::memory_order_release);  // Cheaper!
```

### 3. Allocation in Loops
```cpp
// ❌ WRONG - Allocation per iteration
for (const auto& order : orders) {
    auto* report = new ExecutionReport();  // ALLOCATION!
    process(order, report);
    delete report;
}

// ✅ CORRECT - Reuse single allocation
ExecutionReport report;  // Stack allocation
for (const auto& order : orders) {
    process(order, &report);
}
```

### 4. String Operations in Hot Path
```cpp
// ❌ WRONG - String formatting in critical path
void log_order(const Order& order) {
    std::string msg = "Order: " + std::to_string(order.id);  // SLOW!
    logger.log(msg);
}

// ✅ CORRECT - Use fixed-size buffers
void log_order(const Order& order) {
    LOG_INFO("Order: id=%lu, px=%lu, qty=%u",
             order.id, order.price, order.quantity);
}
```

## Code Examples

### Complete Trading Component Example

```cpp
// trading_engine.h
#pragma once

#include "bldg_blocks/mem_pool.h"
#include "bldg_blocks/lf_queue.h"
#include "bldg_blocks/logging.h"
#include "bldg_blocks/thread_utils.h"
#include "bldg_blocks/types.h"

class TradingEngine {
public:
    TradingEngine();
    ~TradingEngine();
    
    // Delete copy operations
    TradingEngine(const TradingEngine&) = delete;
    TradingEngine& operator=(const TradingEngine&) = delete;
    
    void start();
    void stop();
    void submit_order(OrderRequest&& request);
    
private:
    // Memory pools
    BldgBlocks::MemPool<Order> order_pool_;
    BldgBlocks::MemPool<Execution> exec_pool_;
    
    // Queues
    BldgBlocks::LFQueue<OrderRequest> order_queue_;
    BldgBlocks::LFQueue<MarketData> market_queue_;
    
    // State
    std::atomic<bool> running_{false};
    
    // Statistics (cache-aligned)
    struct Stats {
        BldgBlocks::CacheAligned<std::atomic<uint64_t>> orders_sent;
        BldgBlocks::CacheAligned<std::atomic<uint64_t>> orders_filled;
        BldgBlocks::CacheAligned<std::atomic<uint64_t>> orders_rejected;
        BldgBlocks::CacheAligned<std::atomic<uint64_t>> total_volume;
    } stats_;
    
    // Worker threads
    std::thread order_thread_;
    std::thread market_thread_;
    
    // Core methods
    void order_processing_loop();
    void market_data_loop();
    Order* create_order(const OrderRequest& req);
    void execute_order(Order* order, Price exec_price);
};

// trading_engine.cpp
#include "trading_engine.h"

TradingEngine::TradingEngine()
    : order_pool_(100'000),
      exec_pool_(200'000),
      order_queue_(10'000),
      market_queue_(50'000) {
    
    LOG_INFO("TradingEngine initialized: orders=%zu, execs=%zu",
             order_pool_.capacity(), exec_pool_.capacity());
}

TradingEngine::~TradingEngine() {
    stop();
}

void TradingEngine::start() {
    if (running_.exchange(true)) {
        LOG_WARN("TradingEngine already running");
        return;
    }
    
    LOG_INFO("Starting TradingEngine");
    
    // Start order processing thread
    order_thread_ = std::thread([this]() {
        BldgBlocks::ThreadUtils::setThreadName("OrderProc");
        BldgBlocks::ThreadUtils::setCurrentThreadAffinity(2);
        BldgBlocks::ThreadUtils::setRealTimePriority(99);
        
        LOG_INFO("Order processor started on CPU 2");
        order_processing_loop();
    });
    
    // Start market data thread
    market_thread_ = std::thread([this]() {
        BldgBlocks::ThreadUtils::setThreadName("MarketData");
        BldgBlocks::ThreadUtils::setCurrentThreadAffinity(3);
        BldgBlocks::ThreadUtils::setRealTimePriority(95);
        
        LOG_INFO("Market data processor started on CPU 3");
        market_data_loop();
    });
}

void TradingEngine::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    LOG_INFO("Stopping TradingEngine");
    
    if (order_thread_.joinable()) {
        order_thread_.join();
    }
    if (market_thread_.joinable()) {
        market_thread_.join();
    }
    
    // Log final statistics
    LOG_INFO("Final stats: sent=%lu, filled=%lu, rejected=%lu, volume=%lu",
             stats_.orders_sent.value.load(),
             stats_.orders_filled.value.load(),
             stats_.orders_rejected.value.load(),
             stats_.total_volume.value.load());
}

void TradingEngine::submit_order(OrderRequest&& request) {
    if (!order_queue_.enqueue(std::move(request))) {
        LOG_ERROR("Order queue full, rejecting order");
        stats_.orders_rejected.value.fetch_add(1, std::memory_order_relaxed);
    }
}

void TradingEngine::order_processing_loop() {
    OrderRequest request;
    
    while (running_.load(std::memory_order_acquire)) {
        while (order_queue_.dequeue(request)) {
            auto start = std::chrono::high_resolution_clock::now();
            
            Order* order = create_order(request);
            if (UNLIKELY(!order)) {
                stats_.orders_rejected.value.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            
            // Send order to exchange
            send_to_exchange(order);
            stats_.orders_sent.value.fetch_add(1, std::memory_order_relaxed);
            
            auto end = std::chrono::high_resolution_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            
            LOG_DEBUG("Order sent: id=%lu, latency=%ld ns", order->id, latency);
        }
        
        // Prevent CPU spinning
        __builtin_ia32_pause();
    }
}

Order* TradingEngine::create_order(const OrderRequest& req) {
    Order* order = order_pool_.allocate();
    if (UNLIKELY(!order)) {
        LOG_ERROR("Order pool exhausted");
        return nullptr;
    }
    
    // Placement new with order details
    new (order) Order{
        .id = generate_order_id(),
        .symbol = req.symbol,
        .price = req.price,
        .quantity = req.quantity,
        .side = req.side,
        .type = req.type,
        .timestamp = get_timestamp_ns()
    };
    
    return order;
}
```

## Build and Deploy

### Development Build
```bash
# Regular development build
./scripts/build.sh

# Run tests
./cmake/build-debug/tests/run_tests

# Run benchmarks
./cmake/build-release/tests/run_benchmarks
```

### Production Build
```bash
# Strict build with all warnings as errors
./scripts/build_strict.sh

# Verify no warnings
grep -c "warning:" logs/build_strict_*.log

# Run stress tests
./cmake/build-strict-release/tests/stress_tests
```

### Deployment Checklist
- [ ] All code compiles with strict flags
- [ ] Zero compiler warnings
- [ ] All unit tests pass
- [ ] Benchmarks meet latency requirements
- [ ] Stress tests pass without errors
- [ ] Memory leak check with valgrind
- [ ] CPU isolation configured
- [ ] Huge pages enabled
- [ ] Network tuning applied

## Support and Resources

- API Documentation: `bldg_blocks_api.md`
- Architecture Guide: `../architecture/index.md`
- Performance Reports: `../reports/index.md`
- Progress Tracking: `../trackers/index.md`

## Quick Reference Card

```cpp
// Memory Pool
auto* obj = pool.allocate();
pool.deallocate(obj);

// Lock-Free Queue
queue.enqueue(std::move(item));
queue.dequeue(item);

// Logging
LOG_INFO("Message: %s", data);
LOG_ERROR("Error: %d", code);

// Thread Setup
ThreadUtils::setCurrentThreadAffinity(cpu);
ThreadUtils::setRealTimePriority(99);

// Cache Alignment
CacheAligned<std::atomic<uint64_t>> counter{0};

// Type Conversions
size_t s = static_cast<size_t>(int_value);
uint64_t u = static_cast<uint64_t>(signed_value);
```

---

Remember: **Write it right the first time. Production code is not a prototype.**

---
*Last Updated: 2025-08-31*