# Shriven Zenith - Ultra Low-Latency Trading Platform

## 📊 Project Status: State 0 - Foundation Phase

This repository contains the foundational building blocks for an ultra low-latency trading platform. The current implementation provides core components that need significant hardening before production use in high-frequency trading.

## 🏗️ Current State (State 0)

### Components Implemented

#### 1. **Lock-Free Queue** (`common/lf_queue.h`)
- **Status**: ⚠️ Partially Functional
- **Current Implementation**:
  - Uses `std::atomic<size_t>` for indices
  - Pre-allocated vector storage
  - Basic FIFO operations
- **Critical Issues**:
  - NOT truly lock-free (missing memory barriers)
  - Not SPSC/MPMC safe
  - False sharing due to lack of cache-line alignment
  - No producer/consumer separation

#### 2. **Memory Pool** (`common/mem_pool.h`)
- **Status**: ⚠️ Needs Redesign
- **Current Implementation**:
  - Pre-allocated object storage
  - Placement new for object construction
  - Simple allocation/deallocation interface
- **Critical Issues**:
  - O(n) linear search in `updateNextFreeIndex()`
  - Not thread-safe
  - No cache-line alignment
  - Uses `std::vector` (potential reallocation)

#### 3. **Async Logger** (`common/logging.h`)
- **Status**: ✅ Good Pattern, Minor Issues
- **Current Implementation**:
  - Lock-free queue for log entries
  - Background thread for disk I/O
  - Type-safe logging with unions
- **Issues**:
  - 10ms sleep in flush loop adds latency
  - Union type switching overhead
  - Timestamping in hot path

#### 4. **Thread Utilities** (`common/thread_utils.h`)
- **Status**: ⚠️ Functional but Inefficient
- **Current Implementation**:
  - CPU core affinity pinning
  - Thread creation helper
- **Issues**:
  - 1 second sleep after thread creation
  - Heap allocation for threads
  - No NUMA awareness

#### 5. **Socket Utilities** (`common/socket_utils.h`)
- **Status**: ✅ Good Foundation
- **Current Implementation**:
  - Non-blocking sockets
  - TCP_NODELAY (Nagle disabled)
  - SO_TIMESTAMP support
  - Multicast support

### Performance Metrics (Current)

| Component | Current Latency | Target Latency | Status |
|-----------|----------------|----------------|---------|
| Lock-free Queue | ~100-500ns | < 20ns | ❌ |
| Memory Pool | ~1-10μs | < 50ns | ❌ |
| Logging | ~50ns | < 10ns | ⚠️ |
| Thread Creation | 1 second | Pre-created | ❌ |

## 🚀 Future State (Target Architecture)

### Phase 1: Core Infrastructure Hardening (Weeks 1-2)
- [ ] True lock-free SPSC/MPMC queue with memory barriers
- [ ] O(1) memory pool with free-list
- [ ] Cache-line aligned data structures
- [ ] Thread pool with NUMA awareness

### Phase 2: Trading Components (Weeks 3-4)
- [ ] Order book with intrusive RB-tree
- [ ] SIMD-accelerated price level aggregation
- [ ] Market data feed handlers
- [ ] Order management system

### Phase 3: Performance Optimization (Weeks 5-6)
- [ ] Kernel bypass networking (DPDK/ef_vi)
- [ ] Huge pages support
- [ ] RDTSC-based timestamps
- [ ] PTP clock synchronization

### Phase 4: Strategy Framework (Weeks 7-8)
- [ ] Strategy base classes
- [ ] Risk management framework
- [ ] Position tracking
- [ ] P&L calculation

## 📈 Enhancement Tracker

### Critical Priority (P0) - Must Fix

| ID | Component | Enhancement | Impact | Status | Assigned | ETA |
|----|-----------|------------|--------|---------|----------|-----|
| P0-001 | LFQueue | Implement proper memory barriers | Prevent race conditions | 🔴 Not Started | - | Week 1 |
| P0-002 | LFQueue | Add cache-line alignment | Eliminate false sharing | 🔴 Not Started | - | Week 1 |
| P0-003 | MemPool | Replace linear search with free-list | O(1) allocation | 🔴 Not Started | - | Week 1 |
| P0-004 | MemPool | Add thread-safety with CAS | Concurrent access | 🔴 Not Started | - | Week 1 |
| P0-005 | Thread | Remove 1-second sleep | Reduce startup time | 🔴 Not Started | - | Week 2 |

### High Priority (P1) - Performance

| ID | Component | Enhancement | Impact | Status | Assigned | ETA |
|----|-----------|------------|--------|---------|----------|-----|
| P1-001 | Logger | Remove sleep, use condition variable | Reduce latency | 🔴 Not Started | - | Week 2 |
| P1-002 | Logger | Batch processing optimization | Throughput | 🔴 Not Started | - | Week 2 |
| P1-003 | Memory | Huge pages support | TLB efficiency | 🔴 Not Started | - | Week 3 |
| P1-004 | Time | RDTSC timestamps | Nanosecond precision | 🔴 Not Started | - | Week 3 |

### Medium Priority (P2) - Features

| ID | Component | Enhancement | Impact | Status | Assigned | ETA |
|----|-----------|------------|--------|---------|----------|-----|
| P2-001 | Network | Kernel bypass (DPDK) | Ultra-low latency | 🔴 Not Started | - | Week 4 |
| P2-002 | OrderBook | Intrusive RB-tree | O(log n) operations | 🔴 Not Started | - | Week 4 |
| P2-003 | OrderBook | SIMD price aggregation | Parallel processing | 🔴 Not Started | - | Week 5 |

## 🏗️ Build Instructions

### Prerequisites
- CMake 3.18+
- GCC 11+ with C++20 support
- Linux kernel 5.4+
- Optional: DPDK 21.11 for kernel bypass

### Building
```bash
# Clone the repository
git clone https://github.com/praveen686/shriven_zenith.git
cd shriven_zenith

# Build the project
./scripts/build.sh

# Run tests
./cmake-build-release/common/lf_queue_example
./cmake-build-release/common/logging_example
./cmake-build-release/common/mem_pool_example
```

## 📊 Progress Monitoring

### Week-by-Week Milestones

#### Week 1 (Current)
- [x] Initial codebase analysis
- [x] Document current state
- [ ] Fix lock-free queue
- [ ] Fix memory pool

#### Week 2
- [ ] Thread pool implementation
- [ ] Logger optimization
- [ ] Initial benchmarks

#### Week 3
- [ ] Time management system
- [ ] Memory optimizations
- [ ] Performance testing

#### Week 4
- [ ] Network stack enhancement
- [ ] Order book structure
- [ ] Integration testing

## 🔍 Testing Strategy

### Unit Tests
- Lock-free queue concurrent access
- Memory pool stress testing
- Logger throughput benchmarks

### Performance Tests
- Latency measurements (99th percentile)
- Throughput under load
- Memory usage patterns

### Integration Tests
- End-to-end message flow
- Multi-threaded scenarios
- Failure recovery

## 📝 Development Guidelines

### Code Style
- C++20 standard
- `constexpr` for compile-time constants
- `noexcept` for non-throwing functions
- RAII for resource management
- No raw `new`/`delete` except placement new

### Performance Rules
1. **NO heap allocations in hot path**
2. **Cache-line align all shared data**
3. **Use LIKELY/UNLIKELY for branch hints**
4. **Pre-allocate all memory at startup**
5. **Pin threads to CPU cores**

## 📚 Documentation

### Architecture & Design
- [System Architecture Overview](docs/architecture/01_system_overview.md) - Start here to understand the platform
- [BldgBlocks Foundation](docs/architecture/02_bldg_blocks_architecture.md) - Core components design
- [Architecture Index](docs/architecture/index.md) - Complete architecture documentation

### Developer Resources  
- [Developer Guide](docs/developer_guide/developer_guide.md) - Comprehensive development guide
- [BldgBlocks API Reference](docs/developer_guide/bldg_blocks_api.md) - Complete API documentation
- [Naming Conventions](docs/developer_guide/naming_convention.md) - Documentation standards

### Reports & Analysis
- [Compiler Warnings Lessons](docs/reports/compiler_warnings_lessons.md) - Lessons from strict compilation
- [Development Records](docs/reports/development_record_001_cache_aligned.md) - Technical decision records

### Project Tracking
- [Enhancement Tracker](docs/trackers/enhancement_tracker.md) - Progress and enhancements tracking

### Critical Reading
- [CLAUDE.md](CLAUDE.md) - **MANDATORY** design principles and coding standards

## 🤝 Contributing

This is a work-in-progress foundation for an ultra low-latency trading platform. Contributions focusing on performance improvements and latency reduction are welcome.

## 📜 License

Proprietary - All rights reserved

---

## State History

### State 0 - Foundation (Current)
- Date: August 31, 2025
- Commit: Initial foundation with basic components
- Status: Needs hardening for production use
- Next: State 1 - Core Infrastructure Hardening

---

*This document tracks the evolution of Shriven Zenith from foundation to production-ready ultra low-latency trading platform.*