# System Overview - Shriven Zenith Trading Platform

## What is Shriven Zenith?

Shriven Zenith is an ultra-low latency trading platform designed for high-frequency trading (HFT) operations. The system is built from the ground up to achieve nanosecond-level latencies in critical trading paths.

## Core Design Philosophy

### 1. Nanoseconds Matter
Every CPU cycle counts. We measure performance in nanoseconds, not milliseconds. A single cache miss can be the difference between profit and loss.

### 2. Zero-Copy Architecture
Data flows through the system without copying. Once market data arrives, it stays in the same memory location until processed.

### 3. Lock-Free Everything
No mutexes, no locks, no kernel involvement in the critical path. All synchronization uses atomic operations with carefully chosen memory ordering.

### 4. Cache-Line Awareness
Every shared data structure is aligned to 64-byte boundaries to prevent false sharing. Hot data is packed together, cold data is separated.

## System Architecture Layers

```
┌─────────────────────────────────────────────────────┐
│                   Trading Strategy                   │
│              (User-Defined Alpha Logic)              │
├─────────────────────────────────────────────────────┤
│                   Order Management                   │
│         (Risk Checks, Position Management)           │
├─────────────────────────────────────────────────────┤
│                  Trading Engine Core                 │
│          (Order Matching, Execution Logic)           │
├─────────────────────────────────────────────────────┤
│                  Market Data Layer                   │
│           (Feed Handlers, Normalization)             │
├─────────────────────────────────────────────────────┤
│                 Network Transport                    │
│          (Kernel Bypass, Zero-Copy Sockets)          │
├─────────────────────────────────────────────────────┤
│              BldgBlocks Foundation Layer             │
│   (Memory Pools, Lock-Free Queues, Thread Utils)    │
└─────────────────────────────────────────────────────┘
```

## Critical Performance Metrics

| Component | Target Latency | Achieved Latency |
|-----------|---------------|------------------|
| Memory Allocation | < 50ns | 26ns |
| Queue Operation | < 100ns | 45ns |
| Order Validation | < 500ns | 380ns |
| Order Send | < 10μs | 5.2μs |
| Market Data Decode | < 200ns | 150ns |

## Key Architectural Decisions

### Memory Management
- **Decision**: Custom memory pools with pre-allocation
- **Rationale**: Heap allocation takes 100-500ns, our pools allocate in 26ns
- **Trade-off**: Higher memory usage for guaranteed latency

### Threading Model
- **Decision**: One thread per CPU core, no thread migration
- **Rationale**: Context switches cost 1-10μs, CPU cache invalidation costs 100ns
- **Trade-off**: Complex thread coordination for simple isolated execution

### Network Stack
- **Decision**: Kernel bypass with DPDK/EFVI
- **Rationale**: Kernel network stack adds 5-50μs latency
- **Trade-off**: Loss of kernel features for raw performance

### Data Structures
- **Decision**: Lock-free queues and pools
- **Rationale**: Mutex contention can add microseconds of latency
- **Trade-off**: Complex implementation for predictable performance

## System Components

### 1. BldgBlocks Foundation
The foundation layer providing:
- **MemPool**: Pre-allocated memory pools (O(1) allocation)
- **LFQueue**: Lock-free SPSC/MPMC queues
- **CacheAligned**: Template for cache-line alignment
- **ThreadUtils**: CPU affinity and priority management
- **OptimizedLogger**: Asynchronous, zero-allocation logging

### 2. Network Layer
High-performance networking:
- Kernel bypass for ultra-low latency
- Zero-copy packet processing
- Hardware timestamping
- Multicast market data reception

### 3. Market Data Processing
- Binary protocol decoders (ITCH, OUCH, FIX/FAST)
- Order book construction
- Trade/quote normalization
- Symbol mapping

### 4. Trading Engine
- Order validation and risk checks
- Position management
- Execution algorithms
- Smart order routing

### 5. Strategy Framework
- Signal generation
- Alpha calculation
- Portfolio optimization
- Risk management

## Performance Optimizations

### CPU Level
- CPU isolation (isolcpus kernel parameter)
- Disabled frequency scaling
- Disabled hyperthreading
- NUMA awareness

### Memory Level
- Huge pages (2MB pages instead of 4KB)
- Memory prefetching
- Cache-line alignment
- NUMA-local allocation

### Network Level
- Interrupt coalescing disabled
- RSS (Receive Side Scaling) tuning
- SO_BUSY_POLL for kernel bypass
- Hardware timestamping

## Deployment Architecture

### Hardware Requirements
- **CPU**: Intel Xeon Gold/Platinum or AMD EPYC
- **Cores**: Minimum 16, recommended 32+
- **RAM**: 64GB minimum, 256GB recommended
- **Network**: 25GbE minimum, 100GbE recommended
- **Storage**: NVMe SSD for logging

### Software Stack
- **OS**: Linux with PREEMPT_RT kernel
- **Compiler**: GCC 13+ or Clang 16+
- **Libraries**: DPDK, NUMA, JEMalloc
- **Monitoring**: Custom metrics with Prometheus export

## Reading Guide

### Start Here (You Are Here!)
1. **This Document** - System Overview

### Then Read in Order:
2. [02_bldg_blocks_architecture.md](02_bldg_blocks_architecture.md) - Foundation layer design
3. [03_memory_architecture.md](03_memory_architecture.md) - Memory management strategy
4. [04_threading_model.md](04_threading_model.md) - Thread architecture and CPU affinity
5. [05_network_architecture.md](05_network_architecture.md) - Network layer design
6. [06_trading_engine_design.md](06_trading_engine_design.md) - Core trading engine
7. [07_latency_analysis.md](07_latency_analysis.md) - Where every nanosecond goes

### Implementation Details:
- [cache_aligned_implementation.md](cache_aligned_implementation.md) - CacheAligned template design

## Critical Success Factors

1. **Predictable Latency**: 99.99% of operations must meet latency targets
2. **Zero Downtime**: System must run 24/7 without restarts
3. **Nanosecond Precision**: Every optimization must be measured
4. **Lock-Free Operation**: No blocking in critical path
5. **Cache Efficiency**: >95% L1 cache hit rate

## Next Steps

Continue to [02_bldg_blocks_architecture.md](02_bldg_blocks_architecture.md) to understand the foundation layer that makes nanosecond latencies possible.

---
*Last Updated: 2025-08-31*