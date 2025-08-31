# Architecture Documentation

## Start Here → [01_system_overview.md](01_system_overview.md)

## Reading Order

This documentation is designed to be read sequentially. Each document builds on the previous one.

### Core Architecture (Read in Order)

1. **[01_system_overview.md](01_system_overview.md)** ← START HERE
   - What is Shriven Zenith?
   - System architecture layers
   - Key performance metrics
   - Critical design decisions

2. **[02_bldg_blocks_architecture.md](02_bldg_blocks_architecture.md)**
   - Foundation layer components
   - Memory pools, lock-free queues
   - Cache-aligned structures
   - Performance characteristics

3. **[03_memory_architecture.md](03_memory_architecture.md)** *(coming soon)*
   - Memory management strategy
   - NUMA optimization
   - Huge pages configuration
   - Cache hierarchy optimization

4. **[04_threading_model.md](04_threading_model.md)** *(coming soon)*
   - Thread architecture
   - CPU affinity and isolation
   - Real-time scheduling
   - Thread communication patterns

5. **[05_network_architecture.md](05_network_architecture.md)** *(coming soon)*
   - Kernel bypass networking
   - Zero-copy packet processing
   - Hardware timestamping
   - Multicast handling

6. **[06_trading_engine_design.md](06_trading_engine_design.md)** *(coming soon)*
   - Order management system
   - Risk checks implementation
   - Position management
   - Execution algorithms

7. **[07_latency_analysis.md](07_latency_analysis.md)** *(coming soon)*
   - Latency breakdown by component
   - Measurement methodology
   - Optimization techniques
   - Performance bottlenecks

### Implementation Details

- **[cache_aligned_implementation.md](cache_aligned_implementation.md)**
  - Technical details of CacheAligned template
  - Design decisions and trade-offs
  - Implementation challenges

## Quick Reference

### What Each Document Covers

| Document | Purpose | Read When |
|----------|---------|-----------|
| 01_system_overview | Understanding the platform | First time reading |
| 02_bldg_blocks | Foundation components | Understanding core building blocks |
| 03_memory_architecture | Memory optimization | Implementing memory management |
| 04_threading_model | Thread design | Setting up threading |
| 05_network_architecture | Network layer | Implementing network code |
| 06_trading_engine | Trading logic | Building trading features |
| 07_latency_analysis | Performance tuning | Optimizing performance |

## Key Concepts You'll Learn

1. **Why nanoseconds matter** in high-frequency trading
2. **How to eliminate** memory allocation from hot paths
3. **Lock-free programming** techniques that actually work
4. **Cache-line optimization** for multi-threaded performance
5. **Kernel bypass** for network operations
6. **CPU isolation** and affinity management
7. **Where every nanosecond goes** in the trading path

## Architecture Principles

### Non-Negotiable Requirements
- ✅ Zero allocations in hot path
- ✅ Lock-free data structures
- ✅ Cache-line alignment for shared data
- ✅ NUMA-aware memory allocation
- ✅ CPU isolation and affinity
- ✅ Kernel bypass networking

### Performance Targets
- Memory allocation: < 50ns
- Queue operations: < 100ns
- Logging: < 100ns
- Order validation: < 500ns
- Network send: < 10μs

## For Different Audiences

### If You're a Developer
Start with [01_system_overview.md](01_system_overview.md) and read sequentially through all documents.

### If You're a System Administrator
Focus on:
- [01_system_overview.md](01_system_overview.md) - Understanding the system
- [03_memory_architecture.md](03_memory_architecture.md) - Memory configuration
- [04_threading_model.md](04_threading_model.md) - CPU configuration

### If You're a Performance Engineer
Priority reading:
- [02_bldg_blocks_architecture.md](02_bldg_blocks_architecture.md) - Core components
- [07_latency_analysis.md](07_latency_analysis.md) - Performance analysis
- [cache_aligned_implementation.md](cache_aligned_implementation.md) - Optimization details

## Questions This Documentation Answers

- How can we achieve 26ns memory allocation?
- Why do we need cache-line alignment?
- How do lock-free queues really work?
- Where does kernel bypass fit in?
- What is NUMA and why does it matter?
- How do we measure nanosecond latencies?
- What are the trade-offs we're making?

---
*Last Updated: 2025-08-31*

**Next Step**: Open [01_system_overview.md](01_system_overview.md) to begin understanding the architecture.