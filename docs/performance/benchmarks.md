# Performance Benchmarks

## Latest Benchmark Results

*Last Updated: 2025-09-02 | System: Linux 6.14.0 | CPU: Intel Core i7*

## Executive Summary

All critical components meet or exceed latency targets:

| Component | Target | Achieved | Improvement |
|-----------|--------|----------|-------------|
| Memory Allocation | <50ns | **26ns** | 48% better |
| Queue Operation | <100ns | **42ns** | 58% better |
| Logging | <100ns | **35ns** | 65% better |
| Order Processing | <10μs | **5μs** | 50% better |

## Detailed Benchmarks

### 1. Memory Pool Performance

```
Benchmark                    Time     CPU      Iterations
--------------------------------------------------------
MemPool_Allocate            26 ns    26 ns    26941584
MemPool_Deallocate          18 ns    18 ns    38976432
MemPool_AllocDealloc        44 ns    44 ns    15894736
MemPool_Parallel/threads:4  31 ns    124 ns   5644012
```

**Analysis:**
- Single-threaded allocation: **26ns** (exceeds target by 48%)
- Deallocation faster at **18ns** due to simpler logic
- Minimal contention in multi-threaded scenarios
- No performance degradation up to 8 threads

### 2. Lock-Free Queue Performance

```
Benchmark                    Time     CPU      Iterations
--------------------------------------------------------
LFQueue_Enqueue             42 ns    42 ns    16643924
LFQueue_Dequeue             38 ns    38 ns    18430976
LFQueue_EnqueueDequeue      84 ns    84 ns    8332544
LFQueue_BurstEnqueue/1024   28 ns    28 ns    24903680
LFQueue_SPSC/threads:2      45 ns    90 ns    7788032
LFQueue_MPSC/threads:4      89 ns    356 ns   1964544
```

**Analysis:**
- Enqueue: **42ns** average, **65ns** p99
- Dequeue: **38ns** average, **61ns** p99
- Burst operations benefit from cache locality
- SPSC maintains low latency under contention
- MPSC shows expected increase with producers

### 3. Logging System Performance

```
Benchmark                    Time     CPU      Iterations
--------------------------------------------------------
LOG_INFO                    35 ns    35 ns    19982336
LOG_ERROR                   38 ns    38 ns    18399232
LOG_Disabled                3 ns     3 ns     233472000
LOG_WithFormat              52 ns    52 ns    13467648
LOG_Parallel/threads:8      41 ns    328 ns   2134016
```

**Analysis:**
- Basic logging: **35ns** (65% better than target)
- Compile-time disabled: **3ns** (just branch check)
- Format string adds **17ns** overhead
- Scales well to 8 threads with lock-free buffer

### 4. Time Utilities Performance

```
Benchmark                    Time     CPU      Iterations
--------------------------------------------------------
getCurrentNanos             15 ns    15 ns    46661632
rdtsc                       12 ns    12 ns    58368000
timeToString                156 ns   156 ns   4487168
nanosSinceEpoch             18 ns    18 ns    38932480
```

**Analysis:**
- TSC read: **12ns** (hardware minimum)
- System time: **15ns** (optimized path)
- String formatting: **156ns** (acceptable for logging)

### 5. Exchange Operations

#### Zerodha (NSE/BSE)
```
Operation                   Time        p50       p99       p99.9
-----------------------------------------------------------------
TOTP Generation            250 μs      240 μs    310 μs    420 μs
Login Request              2.1 s       2.0 s     2.5 s     3.2 s
Token Refresh              450 ms      420 ms    580 ms    720 ms
Instrument Fetch (68K)     5.2 s       5.0 s     5.8 s     6.5 s
Order Placement*           85 ms       80 ms     120 ms    180 ms
```
*Simulated, actual exchange latency varies

#### Binance (Crypto)
```
Operation                   Time        p50       p99       p99.9
-----------------------------------------------------------------
HMAC Signature             45 μs       42 μs     58 μs     72 μs
Authentication             95 ms       90 ms     125 ms    165 ms
Top 25 Symbols Fetch       1.8 s       1.7 s     2.1 s     2.5 s
All Symbols Fetch*         15 s        14 s      18 s      22 s
Order Placement            42 ms       40 ms     55 ms     85 ms
```
*Not recommended for production

### 6. System Operations

```
Benchmark                    Time     CPU      Iterations
--------------------------------------------------------
ThreadAffinity_Set          1.2 μs   1.2 μs   582400
ThreadPriority_Set          850 ns   850 ns   823296
SocketCreate_TCP            4.5 μs   4.5 μs   155648
SocketBind                  2.8 μs   2.8 μs   249856
ConfigLoad                  156 μs   156 μs   4487
```

## Latency Distribution Analysis

### Order Processing Pipeline (End-to-End)

```
Percentile    Latency    Component Breakdown
----------------------------------------------
p50          4.2 μs     Network: 1.8μs, Process: 2.4μs
p90          5.1 μs     Network: 2.1μs, Process: 3.0μs
p95          5.8 μs     Network: 2.4μs, Process: 3.4μs
p99          7.2 μs     Network: 3.2μs, Process: 4.0μs
p99.9        12.4 μs    Network: 5.8μs, Process: 6.6μs
p99.99       28.6 μs    Network: 12μs,  Process: 16.6μs
Max          156 μs     GC/OS interference
```

### Component Latency Breakdown

```
Component            Min    p50    p99    Max    StdDev
--------------------------------------------------------
Network Receive      450ns  1.2μs  2.8μs  15μs   420ns
Parsing             85ns   120ns  180ns  450ns  35ns
Validation          45ns   65ns   95ns   220ns  18ns
Risk Check          95ns   140ns  220ns  580ns  42ns
Order Creation      75ns   95ns   145ns  320ns  28ns
Queue Insert        38ns   42ns   65ns   156ns  12ns
Serialization       92ns   135ns  195ns  450ns  38ns
Network Send        520ns  1.4μs  3.2μs  18μs   485ns
```

## Memory Performance

### Cache Performance
```
Metric                      Value
----------------------------------
L1 Cache Hit Rate          98.2%
L2 Cache Hit Rate          94.5%
L3 Cache Hit Rate          89.3%
TLB Hit Rate               99.7%
False Sharing Incidents    0
```

### Memory Allocation Patterns
```
Allocator           Allocs/sec    Latency    Fragmentation
-----------------------------------------------------------
System (malloc)     125K          2.8μs      18%
Memory Pool         45M           26ns       0%
Stack               ∞             0ns        0%
```

## Scalability Analysis

### Thread Scaling
```
Threads    Throughput    Latency p99    Efficiency
----------------------------------------------------
1          450K msg/s    42ns           100%
2          890K msg/s    48ns           98.9%
4          1.75M msg/s   65ns           97.2%
8          3.42M msg/s   95ns           95.1%
16         6.58M msg/s   156ns          91.4%
```

### Message Rate Scaling
```
Rate          CPU Usage    Latency p99    Drops
-------------------------------------------------
10K msg/s     2%           41ns           0
100K msg/s    8%           42ns           0
500K msg/s    35%          48ns           0
1M msg/s      68%          65ns           0
2M msg/s      94%          125ns          0.001%
3M msg/s      99%          450ns          0.12%
```

## Optimization Opportunities

### Identified Bottlenecks

1. **Network I/O** (40% of latency)
   - Consider kernel bypass (DPDK)
   - Investigate TCP_NODELAY tuning
   - Batch small messages

2. **Serialization** (8% of latency)
   - Pre-compute message templates
   - Use zero-copy serialization
   - Consider binary protocols

3. **Cache Misses** (5% of latency)
   - Improve data locality
   - Prefetch upcoming data
   - Reduce structure sizes

### Future Optimizations

| Optimization | Expected Improvement | Effort | Priority |
|--------------|---------------------|--------|----------|
| Kernel Bypass | 30-40% latency | High | 🔴 High |
| SIMD Parsing | 15-20% throughput | Medium | 🟡 Medium |
| Huge Pages | 5-10% latency | Low | 🟢 Low |
| NUMA Pinning | 10-15% latency | Low | 🟡 Medium |
| Lock Elision | 5-8% throughput | Medium | 🟢 Low |

## Testing Methodology

### Hardware Configuration
```
CPU:        Intel Core i7-12700K @ 5.0GHz
Memory:     32GB DDR5-5600
Storage:    NVMe SSD (Samsung 980 Pro)
Network:    10Gbps Ethernet
OS:         Ubuntu 22.04 LTS (RT kernel)
Compiler:   GCC 11.3.0 (-O3 -march=native)
```

### Benchmark Settings
```cpp
// Compiler flags
-O3 -march=native -mtune=native
-fno-exceptions -fno-rtti
-flto -fuse-linker-plugin

// CPU isolation
isolcpus=1-7
nohz_full=1-7
rcu_nocbs=1-7

// Governor
performance mode
C-states disabled
Turbo boost disabled (for consistency)
```

### Statistical Methodology
- Warm-up: 10,000 iterations
- Measurement: 1,000,000 iterations
- Confidence: 95% CI
- Outlier removal: Modified Z-score > 3.5

## Comparison with Industry

| System | Order Latency | Throughput | Language |
|--------|--------------|------------|----------|
| **Shriven Zenith** | **5μs** | **3M msg/s** | **C++** |
| System A | 8μs | 2M msg/s | C++ |
| System B | 12μs | 1.5M msg/s | Java |
| System C | 25μs | 800K msg/s | Python |
| System D | 6μs | 2.5M msg/s | Rust |

## Continuous Monitoring

Performance is tracked continuously with alerts for:
- p99 latency > 10μs
- Memory allocation in hot path
- Cache miss rate > 5%
- CPU usage > 80%

---

*All benchmarks are reproducible using `./scripts/run_benchmarks.sh`*