# Developer Guide

## Welcome to Shriven Zenith Development

This guide provides everything you need to develop, test, and optimize components for the Shriven Zenith ultra-low latency trading system.

## Quick Links

- [Coding Standards](coding_standards.md) - **MANDATORY** reading
- [Build System](build_system.md) - Compilation and linking
- [Testing Guide](testing.md) - Unit, integration, and benchmarks
- [Debugging Guide](debugging.md) - Tools and techniques
- [Performance Optimization](../performance/optimization_guide.md) - Latency reduction

## Development Environment Setup

### Prerequisites

```bash
# Required tools
gcc >= 11.0          # C++23 support
cmake >= 3.20        # Build system
ninja >= 1.10        # Build backend
curl >= 7.68         # HTTP client
openssl >= 1.1.1     # Cryptography

# Optional tools
perf                 # Performance profiling
valgrind            # Memory debugging
clang-format        # Code formatting
clang-tidy          # Static analysis
```

### Initial Setup

```bash
# 1. Clone the repository
git clone https://github.com/yourusername/shriven_zenith.git
cd shriven_zenith

# 2. Set up environment
cp .env.example .env
# Edit .env with your API credentials

# 3. Build the project
./scripts/build_strict.sh

# 4. Run tests
./scripts/run_tests.sh

# 5. Run auditor
./cmake/build-strict-debug/auditor/claude_audit
```

## Development Workflow

### 1. Feature Development

```bash
# Create feature branch
git checkout -b feature/your-feature

# Make changes following CLAUDE.md principles
vim src/your_file.cpp

# Build with strict flags
./scripts/build_strict.sh

# Run tests
./cmake/build-strict-debug/tests/test_your_component

# Run auditor
./cmake/build-strict-debug/auditor/claude_audit

# Commit with conventional format
git commit -m "feat(component): add new capability"
```

### 2. Code Style Requirements

**MANDATORY**: All code must follow [CLAUDE.md](../../CLAUDE.md) principles:

```cpp
// ✅ CORRECT - Explicit type conversion
size_t count = static_cast<size_t>(int_value);

// ❌ WRONG - Implicit conversion
size_t count = int_value;  // FORBIDDEN

// ✅ CORRECT - Cache-aligned structure
struct alignas(64) TradingData {
    std::atomic<uint64_t> sequence;
    char padding[56];
};

// ❌ WRONG - Not aligned
struct TradingData {
    std::atomic<uint64_t> sequence;  // FALSE SHARING
};
```

### 3. Performance Requirements

Every component must meet latency targets:

| Operation | Maximum Latency | How to Measure |
|-----------|----------------|----------------|
| Memory Allocation | 50ns | `benchmark_mem_pool` |
| Queue Operation | 100ns | `benchmark_lf_queue` |
| Logging | 100ns | `benchmark_logging` |
| Order Processing | 10μs | `benchmark_order_flow` |

## Project Structure

```
shriven_zenith/
├── common/           # Core infrastructure (lock-free, memory, etc.)
├── config/           # Configuration management
├── trading/          # Trading components
│   ├── auth/        # Exchange authentication
│   ├── market_data/ # Market data processing
│   ├── strategy/    # Trading strategies
│   └── risk/        # Risk management
├── tests/           # All tests
│   ├── unit/       # Unit tests
│   ├── benchmarks/ # Performance tests
│   └── integration/# Integration tests
├── scripts/         # Build and utility scripts
├── docs/           # Documentation
└── CLAUDE.md       # MANDATORY coding standards
```

## Component Development Guide

### Creating a New Component

1. **Design First**
   - Define interfaces in header
   - Document performance requirements
   - Plan memory layout

2. **Implement with Zero-Copy**
   ```cpp
   // Header-only for templates
   template<typename T>
   class Component {
       // Pre-allocated storage
       alignas(64) T data_[MAX_SIZE];
       size_t size_{0};
   public:
       // No dynamic allocation
       [[nodiscard]] auto process(const T& item) noexcept -> bool;
   };
   ```

3. **Write Tests First**
   ```cpp
   TEST(Component, MeetsLatencyTarget) {
       Component<Order> comp;
       auto start = rdtsc();
       comp.process(order);
       auto elapsed = rdtsc() - start;
       EXPECT_LT(elapsed, TARGET_CYCLES);
   }
   ```

4. **Benchmark Everything**
   ```cpp
   BENCHMARK(Component_Process) {
       Component<Order> comp;
       Order order{...};
       
       for (auto _ : state) {
           benchmark::DoNotOptimize(comp.process(order));
       }
   }
   ```

## Memory Management Rules

### Stack Allocation Preferred
```cpp
// ✅ GOOD - Stack allocation
char buffer[8192];
process(buffer, sizeof(buffer));

// ❌ BAD - Heap allocation
char* buffer = new char[8192];  // FORBIDDEN in hot path
```

### Use Memory Pools
```cpp
// ✅ GOOD - Pool allocation
auto* order = order_pool_.allocate();
if (order) {
    // Use order
    order_pool_.deallocate(order);
}

// ❌ BAD - Dynamic allocation
auto* order = new Order();  // FORBIDDEN
```

## Thread Management

### CPU Affinity
```cpp
// Pin thread to CPU core
ThreadUtils::setCurrentThreadAffinity(2);
ThreadUtils::setRealTimePriority(99);
```

### Thread Topology
```
CPU 0: OS/Interrupts (isolated)
CPU 1: Market Data
CPU 2: Strategy
CPU 3: Order Gateway
CPU 4: Risk Manager
```

## Testing Requirements

### Test Categories

1. **Unit Tests** - Every public function
2. **Integration Tests** - Component interactions
3. **Benchmark Tests** - Performance validation
4. **Stress Tests** - Load and concurrency
5. **Fuzz Tests** - Edge cases

### Running Tests

```bash
# All tests
./scripts/run_tests.sh

# Specific test
./cmake/build-strict-debug/tests/test_lf_queue

# Benchmarks only
./cmake/build-strict-release/tests/benchmark_all

# With sanitizers
./scripts/run_tests_asan.sh
```

## Debugging Techniques

### Performance Profiling
```bash
# CPU profiling
perf record -g ./trader_main
perf report

# Cache misses
perf stat -e cache-misses,cache-references ./trader_main

# Lock contention
perf record -e lock:* ./trader_main
```

### Memory Debugging
```bash
# AddressSanitizer (built-in)
./cmake/build-strict-debug/trader_main

# Valgrind
valgrind --leak-check=full ./trader_main

# Heap profiling
heaptrack ./trader_main
```

## Performance Optimization Checklist

- [ ] Profile first, optimize second
- [ ] Eliminate allocations in hot path
- [ ] Align data structures to cache lines
- [ ] Use lock-free algorithms
- [ ] Minimize branch mispredictions
- [ ] Enable compiler optimizations
- [ ] Use SIMD where applicable
- [ ] Prefetch data when predictable
- [ ] Batch operations when possible
- [ ] Profile again to verify

## Common Pitfalls to Avoid

### 1. False Sharing
```cpp
// ❌ BAD - Variables on same cache line
struct Stats {
    std::atomic<uint64_t> sent;     // Thread 1 writes
    std::atomic<uint64_t> received; // Thread 2 writes
};

// ✅ GOOD - Separate cache lines
struct Stats {
    alignas(64) std::atomic<uint64_t> sent;
    alignas(64) std::atomic<uint64_t> received;
};
```

### 2. Blocking Operations
```cpp
// ❌ BAD - Blocking I/O
read(fd, buffer, size);  // Can block

// ✅ GOOD - Non-blocking
fcntl(fd, F_SETFL, O_NONBLOCK);
read(fd, buffer, size);  // Returns immediately
```

### 3. Unnecessary Copies
```cpp
// ❌ BAD - Copy
void process(std::string data);

// ✅ GOOD - Move or reference
void process(std::string&& data);
void process(const std::string& data);
```

## Code Review Checklist

Before submitting PR, ensure:

- [ ] Builds with `./scripts/build_strict.sh`
- [ ] Zero compiler warnings
- [ ] Passes all tests
- [ ] Meets performance targets
- [ ] No dynamic allocation in hot path
- [ ] All type conversions explicit
- [ ] Cache-aligned shared data
- [ ] Documentation complete
- [ ] Audit passes with zero violations

## Getting Help

- **Documentation**: Start with [docs/README.md](../README.md)
- **API Reference**: See [docs/api/](../api/README.md)
- **Examples**: Check `tests/` directory
- **Issues**: GitHub Issues for bugs/features

## Contributing

1. Read [CLAUDE.md](../../CLAUDE.md) - **MANDATORY**
2. Follow coding standards exactly
3. Write tests for all code
4. Benchmark performance critical code
5. Document all public APIs
6. Submit PR with detailed description

---

*Remember: In trading, microseconds matter. In our code, nanoseconds matter.*