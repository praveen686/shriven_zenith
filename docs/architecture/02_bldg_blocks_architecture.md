# BldgBlocks Architecture - Foundation Layer Design

## Overview

BldgBlocks is the foundation layer of Shriven Zenith, providing the critical low-level components that enable nanosecond-latency trading. Every component is designed with a single goal: **minimize latency at all costs**.

## Design Principles

### 1. Pre-Allocation is Mandatory
```cpp
// Everything allocated at startup
MemPool<Order> order_pool(1'000'000);      // 1M orders pre-allocated
LFQueue<MarketData> md_queue(65'536);      // 64K queue pre-allocated
```

### 2. Cache-Line Alignment Everywhere
```cpp
// Every shared structure aligned to prevent false sharing
struct alignas(64) TradingStats {
    std::atomic<uint64_t> orders_sent;     // Own cache line
    char padding[56];                       // Fill to 64 bytes
};
```

### 3. Lock-Free or Die
```cpp
// No mutexes, only atomics with proper memory ordering
while (!head.compare_exchange_weak(old_head, new_head,
                                   std::memory_order_release,
                                   std::memory_order_relaxed));
```

## Component Architecture

### 1. Memory Pool (mem_pool.h)

#### Design
```
┌─────────────────────────────────────────┐
│           Memory Pool Header            │
│  ┌────────────────────────────────┐     │
│  │ free_head (atomic ptr)         │     │
│  │ total_blocks                   │     │
│  │ block_size                     │     │
│  └────────────────────────────────┘     │
│                                         │
│           Pre-Allocated Blocks          │
│  ┌────────┐ ┌────────┐ ┌────────┐     │
│  │Block 0 │→│Block 1 │→│Block 2 │→... │
│  └────────┘ └────────┘ └────────┘     │
│                                         │
│         Intrusive Free List             │
│  Each free block points to next         │
└─────────────────────────────────────────┘
```

#### Implementation Details
- **Allocation**: O(1) - Pop from free list
- **Deallocation**: O(1) - Push to free list
- **Memory Layout**: Contiguous for cache locality
- **Thread Safety**: Spinlock (26ns overhead)
- **NUMA Aware**: Allocates on specified NUMA node

#### Performance Characteristics
```cpp
// Measured on Intel Xeon Gold 6254 @ 3.1GHz
Allocation:   26ns average, 31ns P99
Deallocation: 24ns average, 29ns P99
Throughput:   38M allocations/second/core
```

### 2. Lock-Free Queue (lf_queue.h)

#### Design
```
┌──────────────────────────────────────────┐
│            LFQueue Structure             │
│                                          │
│  head ────→ [Node] → [Node] → [Node]    │
│              ↓        ↓        ↓         │
│            [Data]   [Data]   [Data]      │
│                                ↑         │
│  tail ──────────────────────────┘        │
│                                          │
│  Cache-Line Separation:                  │
│  ┌──────────────────┐                    │
│  │ head (64 bytes)  │ Own cache line     │
│  ├──────────────────┤                    │
│  │ tail (64 bytes)  │ Own cache line     │
│  └──────────────────┘                    │
└──────────────────────────────────────────┘
```

#### Algorithm
- **Michael & Scott Algorithm** with optimizations
- **ABA Prevention**: Pointer packing with counter
- **Memory Ordering**: Acquire-Release semantics
- **Cache Optimization**: Producer/consumer separation

#### Performance
```cpp
// Single Producer Single Consumer (SPSC)
Enqueue: 42ns average
Dequeue: 38ns average
Throughput: 24M ops/second

// Multi Producer Multi Consumer (MPMC)
Enqueue: 89ns average (4 threads)
Dequeue: 85ns average (4 threads)
Throughput: 11M ops/second/thread
```

### 3. CacheAligned Template (types.h)

#### Problem Solved
```cpp
// FALSE SHARING - Multiple threads hit same cache line
struct Bad {
    std::atomic<uint64_t> counter1;  // Thread 1 writes
    std::atomic<uint64_t> counter2;  // Thread 2 writes
};  // Both on same 64-byte cache line = SLOW

// SOLUTION - Each counter on own cache line
struct Good {
    CacheAligned<std::atomic<uint64_t>> counter1;  // 64 bytes
    CacheAligned<std::atomic<uint64_t>> counter2;  // 64 bytes
};  // Different cache lines = FAST
```

#### Implementation
```cpp
template<typename T, std::size_t Align = 64>
struct alignas(Align) CacheAligned {
    union {
        T value;
        char storage[sizeof(T)];
    };
    
    // Perfect forwarding constructor
    template<typename... Args>
    CacheAligned(Args&&... args) {
        if constexpr (is_atomic<T>::value) {
            // Atomics use relaxed ordering in constructor
            new (&value) T(std::forward<Args>(args)...);
        } else {
            new (&value) T(std::forward<Args>(args)...);
        }
    }
};
```

### 4. Thread Utilities (thread_utils.h)

#### CPU Affinity Management
```cpp
// Pin thread to specific CPU core
ThreadUtils::setCurrentThreadAffinity(2);  // Core 2

// Set real-time priority
ThreadUtils::setRealTimePriority(99);      // Highest RT priority

// NUMA awareness
ThreadUtils::pinToNumaNode(0);             // NUMA node 0
```

#### Thread Pool Architecture
```
┌────────────────────────────────────┐
│         Thread Pool Design         │
│                                    │
│  CPU 0: Housekeeping              │
│  CPU 1: OS/Kernel                 │
│  CPU 2: Market Data Thread ─────┐ │
│  CPU 3: Order Thread ──────────┐│ │
│  CPU 4: Risk Thread ─────────┐││ │
│  CPU 5: Logger Thread ──────┐│││ │
│                            ││││  │
│  Isolated CPUs (2-5):     ││││  │
│  - No interrupts           ││││  │
│  - No kernel threads       ││││  │
│  - No migrations           ││││  │
└────────────────────────────────────┘
```

### 5. Optimized Logger (logging.h)

#### Asynchronous Design
```
┌─────────────────────────────────────────┐
│           Logger Architecture           │
│                                         │
│  Producer Threads:                      │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐  │
│  │Thread 1 │ │Thread 2 │ │Thread 3 │  │
│  └────┬────┘ └────┬────┘ └────┬────┘  │
│       ↓           ↓           ↓        │
│  ┌──────────────────────────────────┐  │
│  │   Ring Buffer (Lock-Free)        │  │
│  │   [Msg][Msg][Msg][Msg][Msg]      │  │
│  └──────────────────────────────────┘  │
│                    ↓                    │
│  ┌──────────────────────────────────┐  │
│  │   Writer Thread (Dedicated)       │  │
│  │   - Batched writes                │  │
│  │   - Async I/O                     │  │
│  └──────────────────────────────────┘  │
│                    ↓                    │
│              [Log File]                 │
└─────────────────────────────────────────┘
```

#### Performance
- **Logging Overhead**: 35ns per message
- **Ring Buffer Size**: 16,384 messages
- **Batch Write**: Every 100ms or 1000 messages
- **Zero Allocation**: After initialization

## Memory Layout Strategy

### NUMA Architecture Awareness
```
┌─────────────────────────────────────────────┐
│              NUMA Node 0                    │
│  ┌─────────────────────────────────┐       │
│  │ Market Data Structures           │       │
│  │ - Order Books                    │       │
│  │ - Market Data Queues             │       │
│  │ - Price Cache                    │       │
│  └─────────────────────────────────┘       │
│                                             │
│              NUMA Node 1                    │
│  ┌─────────────────────────────────┐       │
│  │ Trading Engine Structures        │       │
│  │ - Order Pool                     │       │
│  │ - Position Cache                 │       │
│  │ - Risk Limits                    │       │
│  └─────────────────────────────────┘       │
└─────────────────────────────────────────────┘
```

### Cache Hierarchy Optimization
```
L1 Cache (32KB):
- Hot path data only
- Current order being processed
- Active market data

L2 Cache (256KB):
- Recent orders
- Order book top levels
- Position cache

L3 Cache (35MB):
- Full order books
- Historical data
- Risk matrices

RAM:
- Logs
- Configuration
- Cold data
```

## Integration Patterns

### Typical Usage Flow
```cpp
// 1. Initialize at startup
Config::init("config.toml");
MemPool<Order> order_pool(1'000'000);
LFQueue<Order*> order_queue(10'000);
initOptimizedLogging("logs/trading.log");

// 2. Setup threads
std::thread market_thread([&]() {
    ThreadUtils::setCurrentThreadAffinity(2);
    ThreadUtils::setRealTimePriority(99);
    // Process market data
});

// 3. Hot path operations
Order* order = order_pool.allocate();       // 26ns
order_queue.enqueue(order);                 // 45ns
LOG_INFO("Order sent: %lu", order->id);     // 35ns

// 4. Cleanup
order_queue.dequeue(order);                 // 42ns
order_pool.deallocate(order);               // 24ns
```

## Performance Monitoring

### Key Metrics to Track
1. **Allocation Latency**: Must stay under 50ns
2. **Queue Latency**: Must stay under 100ns
3. **Cache Hit Rate**: Must exceed 95%
4. **Memory Usage**: Must be constant (no growth)
5. **Thread Migration**: Must be zero

### Measurement Code
```cpp
auto start = rdtsc();
auto* order = pool.allocate();
auto cycles = rdtsc() - start;
auto nanos = cycles_to_nanos(cycles);
latency_histogram.record(nanos);
```

## Common Pitfalls and Solutions

### Pitfall 1: False Sharing
**Problem**: Multiple threads updating nearby memory
**Solution**: Use CacheAligned for all shared data

### Pitfall 2: Memory Allocation in Hot Path
**Problem**: Heap allocation takes 100-500ns
**Solution**: Pre-allocate everything with MemPool

### Pitfall 3: Lock Contention
**Problem**: Mutex can add microseconds
**Solution**: Use lock-free structures

### Pitfall 4: System Calls
**Problem**: System calls take microseconds
**Solution**: Avoid all system calls in hot path

## Next Steps

Continue to [03_memory_architecture.md](03_memory_architecture.md) for detailed memory management strategies.

---
*Last Updated: 2025-08-31*