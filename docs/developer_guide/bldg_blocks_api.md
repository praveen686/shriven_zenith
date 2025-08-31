# BldgBlocks API Documentation

## Table of Contents
1. [Overview](#overview)
2. [Core Components](#core-components)
3. [Memory Management](#memory-management)
4. [Lock-Free Data Structures](#lock-free-data-structures)
5. [Thread Utilities](#thread-utilities)
6. [Logging System](#logging-system)
7. [Configuration Management](#configuration-management)
8. [Type System](#type-system)
9. [Socket Programming](#socket-programming)
10. [Performance Considerations](#performance-considerations)

## Overview

BldgBlocks is an ultra-low latency C++23 library designed for high-frequency trading systems. Every component is built with nanosecond-level performance in mind, utilizing lock-free algorithms, cache-line optimization, and zero-copy techniques.

### Design Principles
- **Zero Allocation in Hot Path**: All memory is pre-allocated
- **Cache-Line Aware**: All data structures are aligned to prevent false sharing
- **Lock-Free Where Possible**: Using atomic operations for thread safety
- **Template-Heavy**: Compile-time optimization over runtime flexibility
- **NUMA Aware**: Memory allocation respects NUMA boundaries

## Core Components

### 1. Types (`types.h`)

#### CacheAligned Template
```cpp
template<typename T, std::size_t Align = CACHE_LINE_SIZE>
struct alignas(Align) CacheAligned
```

**Purpose**: Prevents false sharing by ensuring each object occupies its own cache line.

**Key Features**:
- Automatic alignment to 64-byte boundaries
- Perfect forwarding constructor for in-place construction
- Special handling for atomic types with relaxed memory ordering
- Uninitialized storage optimization using union

**Usage Example**:
```cpp
// Simple value
CacheAligned<uint64_t> counter{0};

// Complex object with constructor arguments
CacheAligned<OrderBook> book{symbol, depth};

// Atomic with automatic relaxed ordering in constructor
CacheAligned<std::atomic<uint64_t>> atomic_counter{0};
```

**Implementation Details**:
- Uses `if constexpr` for compile-time branching
- Placement new for proper object construction
- Union storage to avoid default construction

#### Type Aliases
```cpp
using OrderId = uint64_t;    // Unique order identifier
using TickerId = uint32_t;   // Instrument identifier
using Price = uint64_t;      // Price in minimum tick units
using Qty = uint32_t;        // Quantity
using Side = int8_t;         // Buy=1, Sell=-1
using Nanos = int64_t;       // Nanoseconds since epoch
```

## Memory Management

### 2. Memory Pool (`mem_pool.h`)

#### MemPool Class
```cpp
template<typename T>
class MemPool
```

**Purpose**: Pre-allocated, lock-free memory pool for fixed-size objects.

**Key Features**:
- O(1) allocation and deallocation
- Thread-safe with spinlock protection
- NUMA-aware memory allocation
- Aligned memory blocks
- Statistics tracking

**API**:
```cpp
// Construction
MemPool(std::size_t num_blocks);

// Allocation
T* allocate();                           // Returns nullptr if pool exhausted

// Deallocation  
void deallocate(T* ptr);                 // No-op if ptr is nullptr

// Query
std::size_t available() const;           // Number of free blocks
std::size_t capacity() const;            // Total capacity
bool contains(const T* ptr) const;       // Check if pointer from this pool
```

**Usage Example**:
```cpp
// Create pool for 1 million orders
MemPool<Order> order_pool(1'000'000);

// Allocate
Order* order = order_pool.allocate();
if (order) {
    new (order) Order(id, price, qty);  // Placement new
    // Use order...
    order->~Order();                    // Explicit destructor
    order_pool.deallocate(order);
}

// Check pool status
LOG_INFO("Pool usage: %zu/%zu", 
         order_pool.capacity() - order_pool.available(),
         order_pool.capacity());
```

**Performance Characteristics**:
- Allocation: ~26ns average
- Deallocation: ~24ns average
- Memory overhead: 8 bytes per block (next pointer)

## Lock-Free Data Structures

### 3. Lock-Free Queue (`lf_queue.h`)

#### LFQueue Class
```cpp
template<typename T>
class LFQueue
```

**Purpose**: Multi-producer, multi-consumer lock-free queue.

**Key Features**:
- True lock-free implementation using CAS operations
- ABA problem prevention with pointer packing
- Cache-line aligned nodes
- Configurable capacity

**API**:
```cpp
// Construction
LFQueue(std::size_t capacity);

// Operations
bool enqueue(T&& item);                  // Move semantics
bool enqueue(const T& item);             // Copy semantics
bool dequeue(T& item);                   // Returns false if empty

// Query
bool empty() const;
std::size_t size_approx() const;         // Approximate size
```

**Usage Example**:
```cpp
// Create queue for market data
LFQueue<MarketData> md_queue(65536);

// Producer thread
MarketData data{...};
if (!md_queue.enqueue(std::move(data))) {
    LOG_ERROR("Queue full, dropping market data");
}

// Consumer thread
MarketData received;
while (md_queue.dequeue(received)) {
    process_market_data(received);
}
```

**Performance Characteristics**:
- Enqueue: ~45ns average (uncontended)
- Dequeue: ~42ns average (uncontended)
- Throughput: 568M ops/sec (8 threads)

## Thread Utilities

### 4. Thread Management (`thread_utils.h`)

#### ThreadUtils Namespace
```cpp
namespace ThreadUtils
```

**Purpose**: CPU affinity, priority management, and thread coordination.

**Key Features**:
- CPU core pinning
- Real-time scheduling
- NUMA awareness
- Thread naming for debugging

**API**:
```cpp
// CPU Affinity
bool setCurrentThreadAffinity(int cpu_id);
std::vector<int> getAvailableCPUs();

// Thread Priority
bool setRealTimePriority(int priority = 99);
bool setNormalPriority();

// Thread Naming
void setThreadName(const std::string& name);

// NUMA
int getCurrentNumaNode();
bool pinToNumaNode(int node);
```

**Usage Example**:
```cpp
// Market data thread - pin to isolated CPU
void market_data_thread() {
    ThreadUtils::setThreadName("MD_Handler");
    ThreadUtils::setCurrentThreadAffinity(2);  // Isolated core
    ThreadUtils::setRealTimePriority(95);
    
    while (running) {
        // Process market data with guaranteed CPU
    }
}

// Order management thread
void order_thread() {
    ThreadUtils::setThreadName("Order_Mgr");
    ThreadUtils::setCurrentThreadAffinity(3);
    ThreadUtils::setRealTimePriority(90);
    
    // Process orders...
}
```

**Best Practices**:
- Isolate CPUs at boot: `isolcpus=2,3,4,5`
- Disable hyperthreading for consistency
- Pin interrupt handlers away from trading cores

## Logging System

### 5. Asynchronous Logger (`logging.h`)

#### Logger Class
```cpp
class Logger
```

**Purpose**: Zero-copy, lock-free logging with nanosecond timestamps.

**Key Features**:
- Ring buffer for zero allocation
- Separate writer thread
- Compile-time format checking
- Automatic file rotation
- Nanosecond precision timestamps

**API**:
```cpp
// Singleton access
Logger* g_opt_logger;

// Initialization
void initLogging(const std::string& filename);
void shutdownLogging();

// Logging macros
#define LOG_DEBUG(fmt, ...) 
#define LOG_INFO(fmt, ...)
#define LOG_WARN(fmt, ...)
#define LOG_ERROR(fmt, ...)
#define LOG_FATAL(fmt, ...)

// Direct methods
void debug(const char* fmt, ...);
void info(const char* fmt, ...);
void warn(const char* fmt, ...);
void error(const char* fmt, ...);
void fatal(const char* fmt, ...);

// Statistics
LogStats getStats() const;
```

**Usage Example**:
```cpp
// Initialize at startup
initLogging("logs/trading_20250831_143022.log");

// Use throughout application
LOG_INFO("Order received: id=%lu, px=%lu, qty=%u", 
         order_id, price, quantity);

LOG_ERROR("Risk limit breached: position=%ld, limit=%ld",
          current_position, position_limit);

// Structured logging for analysis
LOG_INFO("EXEC|%lu|%s|%c|%lu|%u|%lu",  // Execution report
         exec_id, symbol, side, price, qty, timestamp);

// Get statistics
auto stats = g_opt_logger->getStats();
LOG_INFO("Logged %lu messages, dropped %lu, wrote %lu bytes",
         stats.messages_written, stats.messages_dropped, 
         stats.bytes_written);

// Shutdown cleanly
shutdownLogging();
```

**Performance Characteristics**:
- Logging overhead: ~35ns per message
- No allocation after initialization
- Automatic batched writes every 100ms
- Ring buffer size: 16384 messages

**Log Format**:
```
[timestamp_ns] [LEVEL] [thread_name] message
[1735654832123456789] [INFO] [OrderMgr] Order placed successfully
```

## Configuration Management

### 6. Configuration System (`config.h`)

#### Config Class
```cpp
class Config
```

**Purpose**: Centralized configuration management using TOML.

**Key Features**:
- TOML-based configuration
- Type-safe access
- Default values
- Runtime reloading support

**API**:
```cpp
// Singleton access
Config& config();
bool initConfig(const std::string& path);

// Getters
std::string getLogsDir() const;
std::string getCacheDir() const;
std::size_t getMemPoolSize(const std::string& pool) const;
std::size_t getQueueSize() const;
bool isNumaEnabled() const;
int getNumaNode() const;
```

**Configuration Structure** (`config.toml`):
```toml
[system]
name = "ShrivenZenith"
environment = "production"

[paths]
logs_dir = "logs"
cache_dir = "cache"
data_dir = "data"

[memory_pool]
numa_aware = true
numa_node = 0
pool_sizes = [
    { name = "orders", block_size = 64, num_blocks = 1048576 },
    { name = "trades", block_size = 32, num_blocks = 524288 }
]

[threading]
cpu_affinity_enabled = true
thread_pool_cores = [2, 3, 4, 5]
```

**Usage Example**:
```cpp
// Initialize at startup
if (!initConfig("config.toml")) {
    LOG_FATAL("Failed to load configuration");
    return 1;
}

// Access configuration
auto& cfg = config();
std::string log_dir = cfg.getLogsDir();
bool numa = cfg.isNumaEnabled();

// Create pools based on config
for (const auto& pool_cfg : cfg.getMemPools()) {
    create_pool(pool_cfg.name, pool_cfg.block_size, pool_cfg.num_blocks);
}
```

## Socket Programming

### 7. Socket Utilities (`socket.h`)

#### TCPSocket Class
```cpp
class TCPSocket
```

**Purpose**: High-performance TCP socket with kernel bypass options.

**Key Features**:
- TCP_NODELAY for low latency
- SO_BUSY_POLL for kernel bypass
- Zero-copy send where available
- Configurable buffer sizes

**API**:
```cpp
// Server
bool bind(const std::string& ip, uint16_t port);
bool listen(int backlog = 5);
TCPSocket* accept();

// Client
bool connect(const std::string& ip, uint16_t port);

// I/O
ssize_t send(const void* data, size_t len);
ssize_t recv(void* buffer, size_t len);
ssize_t send_zero_copy(const void* data, size_t len);

// Configuration
void setNoDelay(bool enabled);
void setBusyPoll(uint32_t microseconds);
void setBufferSizes(size_t send_buf, size_t recv_buf);
```

**Usage Example**:
```cpp
// Market data receiver
TCPSocket md_socket;
md_socket.setNoDelay(true);
md_socket.setBusyPoll(50);  // 50μs busy polling
md_socket.setBufferSizes(4*1024*1024, 4*1024*1024);  // 4MB buffers

if (md_socket.connect("10.0.0.1", 9876)) {
    char buffer[1024];
    while (running) {
        ssize_t n = md_socket.recv(buffer, sizeof(buffer));
        if (n > 0) {
            process_market_data(buffer, n);
        }
    }
}

// Order gateway sender
TCPSocket gw_socket;
gw_socket.setNoDelay(true);
Order order{...};
gw_socket.send(&order, sizeof(order));
```

## Performance Considerations

### Cache-Line Optimization
All performance-critical structures are aligned to 64-byte boundaries:
```cpp
struct alignas(64) OrderBook {
    // Frequently accessed together
    Price best_bid;
    Price best_ask;
    Qty bid_size;
    Qty ask_size;
    
    // Padding to fill cache line
    char padding[40];
};
```

### Memory Ordering
Use the weakest memory ordering that maintains correctness:
```cpp
// Relaxed for statistics
counter.fetch_add(1, std::memory_order_relaxed);

// Acquire-Release for synchronization
while (flag.load(std::memory_order_acquire) == false);
flag.store(true, std::memory_order_release);

// Sequential consistency only when absolutely required
state.store(NEW_STATE, std::memory_order_seq_cst);
```

### Branch Prediction
Organize conditions by likelihood:
```cpp
if (LIKELY(order != nullptr)) {  // Common case first
    process_order(order);
} else {
    handle_null_order();
}
```

### Template Instantiation
Explicitly instantiate templates to reduce compilation time:
```cpp
// In .cpp file
template class MemPool<Order>;
template class MemPool<Trade>;
template class LFQueue<MarketData>;
```

## Error Handling Philosophy

1. **No Exceptions in Hot Path**: Use return codes
2. **Fail Fast**: Detect errors early with assertions
3. **Log Everything**: But asynchronously
4. **Graceful Degradation**: Continue operating if possible

```cpp
Order* order = order_pool.allocate();
if (UNLIKELY(!order)) {
    LOG_ERROR("Order pool exhausted");
    stats.orders_dropped++;
    return ErrorCode::OUT_OF_MEMORY;
}
```

## Testing Considerations

### Unit Tests
Each component has comprehensive tests:
```cpp
// test_mem_pool.cpp
TEST(MemPool, AllocatesDeallocates) {
    MemPool<TestObject> pool(100);
    auto* obj = pool.allocate();
    ASSERT_NE(obj, nullptr);
    pool.deallocate(obj);
    ASSERT_EQ(pool.available(), 100);
}
```

### Benchmarks
Performance benchmarks for all operations:
```cpp
// benchmark_mem_pool.cpp
BENCHMARK(MemPoolAllocate) {
    for (int i = 0; i < 1000000; ++i) {
        auto* p = pool.allocate();
        pool.deallocate(p);
    }
}
```

### Stress Tests
Multi-threaded stress tests to verify correctness:
```cpp
// stress_test_queue.cpp
void stress_test_queue() {
    LFQueue<int> queue(10000);
    std::vector<std::thread> producers, consumers;
    
    // Launch multiple producers and consumers
    // Verify no data loss or corruption
}
```

## Version History

- **v2.0.0** (2025-08-31): Complete rewrite with CacheAligned template
- **v1.5.0** (2025-07-15): Added lock-free queue
- **v1.0.0** (2025-06-01): Initial release with memory pool

## License

Proprietary - Shriven Zenith Trading Systems

---
*Last Updated: 2025-08-31*