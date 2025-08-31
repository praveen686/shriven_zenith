# CLAUDE.md - MANDATORY DESIGN PRINCIPLES

**CRITICAL**: This document contains MANDATORY design principles for the Shriven Zenith codebase. 
**VIOLATION OF THESE PRINCIPLES WILL RESULT IN CODE REJECTION.**

## THE PRIME DIRECTIVE

> **"Production code is NOT a prototype. Every line of code must be written as if it will handle millions of dollars in trades. Because it will."**

## ABSOLUTE REQUIREMENTS - NON-NEGOTIABLE

### 1. COMPILER FLAGS - ALWAYS ENFORCED

**EVERY FILE MUST COMPILE WITH ZERO WARNINGS USING:**
```bash
-Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion
-Wold-style-cast -Wformat-security -Weffc++ -Wno-unused
```

**NO EXCEPTIONS. NO EXCUSES. NO PRAGMA SUPPRESSIONS.**

### 2. BUILD DISCIPLINE

```bash
# ONLY USE THESE COMMANDS:
./scripts/build_strict.sh  # For ALL development
./scripts/build.sh         # Only for quick tests

# FORBIDDEN:
cmake .                    # NEVER run cmake directly
make                       # NEVER use make
g++ *.cpp                  # NEVER compile manually
```

### 3. CODE REVIEW CRITERIA - AUTOMATIC REJECTION

Your code will be REJECTED if it contains:
- [ ] ANY compiler warnings
- [ ] Implicit type conversions
- [ ] Dynamic allocation (new/delete/malloc/free)
- [ ] Exceptions in trading code
- [ ] Uninitialized member variables
- [ ] Missing copy control (Rule of 3/5/0)
- [ ] Non-aligned shared data structures
- [ ] String operations in hot path
- [ ] System calls in hot path

## MANDATORY CODING PATTERNS

### Type Conversions - ALWAYS EXPLICIT

```cpp
// ✅ MANDATORY PATTERN
size_t count = static_cast<size_t>(int_value);
uint64_t ns = static_cast<uint64_t>(timespec.tv_sec) * 1000000000ULL;
int written = write(fd, buf, len);
if (written > 0) {
    bytes_written += static_cast<size_t>(written);
}

// ❌ INSTANT REJECTION
size_t count = int_value;              // FORBIDDEN
uint64_t ns = timespec.tv_sec * 1e9;   // FORBIDDEN
bytes_written += written;              // FORBIDDEN
```

### Constructor Initialization - ALWAYS USE LISTS

```cpp
// ✅ MANDATORY PATTERN
class TradingSystem {
    TradingSystem() 
        : orders_sent_(0),
          orders_filled_(0),
          running_(false),
          thread_pool_(),
          order_pool_(1000000) {
    }
};

// ❌ INSTANT REJECTION
class TradingSystem {
    TradingSystem() {
        orders_sent_ = 0;      // FORBIDDEN
        orders_filled_ = 0;    // FORBIDDEN
    }
};
```

### Resource Management - ALWAYS FOLLOW RULE OF 3/5/0

```cpp
// ✅ MANDATORY PATTERN
class ResourceOwner {
    void* resource_;
public:
    // Delete copy
    ResourceOwner(const ResourceOwner&) = delete;
    ResourceOwner& operator=(const ResourceOwner&) = delete;
    
    // Define move
    ResourceOwner(ResourceOwner&&) noexcept = default;
    ResourceOwner& operator=(ResourceOwner&&) noexcept = default;
    
    ~ResourceOwner();
};

// ❌ INSTANT REJECTION
class ResourceOwner {
    void* resource_;  // Has pointer but no copy control - FORBIDDEN
};
```

### Cache Alignment - ALWAYS FOR SHARED DATA

```cpp
// ✅ MANDATORY PATTERN
struct TradingStats {
    BldgBlocks::CacheAligned<std::atomic<uint64_t>> orders_sent;
    BldgBlocks::CacheAligned<std::atomic<uint64_t>> orders_filled;
};

// ❌ INSTANT REJECTION
struct TradingStats {
    std::atomic<uint64_t> orders_sent;    // FALSE SHARING - FORBIDDEN
    std::atomic<uint64_t> orders_filled;  // FALSE SHARING - FORBIDDEN
};
```

### Memory Allocation - NEVER IN HOT PATH

```cpp
// ✅ MANDATORY PATTERN
void process_order(const Order& order) {
    ExecutionReport* report = exec_pool_.allocate();  // Pool allocation
    if (!report) {
        LOG_ERROR("Pool exhausted");
        return;
    }
    // Use report...
    exec_pool_.deallocate(report);
}

// ❌ INSTANT REJECTION
void process_order(const Order& order) {
    auto* report = new ExecutionReport();  // FORBIDDEN - heap allocation
    // ...
    delete report;                         // FORBIDDEN
}
```

## LOGGING DISCIPLINE

### MANDATORY Logging Pattern

```cpp
// AT STARTUP - ONCE
int main() {
    // 1. Initialize config
    if (!BldgBlocks::initConfig("config.toml")) {
        std::cerr << "Config failed\n";  // Console only before logger
        return 1;
    }
    
    // 2. Initialize logger with module name
    std::string module = "order_gateway";  // Your module name
    std::string timestamp = get_timestamp();
    std::string log_file = config().getLogsDir() + "/" + 
                          module + "_" + timestamp + ".log";
    BldgBlocks::initLogging(log_file);
    
    // 3. Run application
    run();
    
    // 4. Shutdown logger
    BldgBlocks::shutdownLogging();
}

// THROUGHOUT APPLICATION
LOG_INFO("Order sent: id=%lu, px=%lu, qty=%u", id, price, qty);
LOG_ERROR("Risk breach: pos=%ld, limit=%ld", position, limit);

// NEVER USE:
std::cout << "Debug message\n";        // FORBIDDEN
printf("Order: %d\n", id);            // FORBIDDEN
std::cerr << "Error: " << msg << "\n"; // FORBIDDEN
```

## PERFORMANCE REQUIREMENTS

### Latency Targets (MANDATORY)

| Operation | Maximum Latency | Typical Latency |
|-----------|----------------|-----------------|
| Memory Allocation | 50ns | 26ns |
| Queue Enqueue | 100ns | 45ns |
| Queue Dequeue | 100ns | 42ns |
| Logging | 100ns | 35ns |
| Order Send | 10μs | 5μs |

**Code failing these targets will be REJECTED.**

### MANDATORY Performance Patterns

```cpp
// Branch Prediction
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

if (LIKELY(order != nullptr)) {
    process_order(order);  // Fast path
}

// Memory Ordering
counter.fetch_add(1, std::memory_order_relaxed);     // Statistics
flag.store(true, std::memory_order_release);         // Synchronization
while (flag.load(std::memory_order_acquire) == false);

// Prefetching
__builtin_prefetch(next_order, 0, 3);

// Inline Critical Functions
[[gnu::always_inline]] inline Price get_price() const noexcept {
    return price_;
}
```

## TESTING REQUIREMENTS

### MANDATORY Test Coverage

Every component MUST have:
1. **Unit Tests** - 100% coverage of public API
2. **Benchmark Tests** - Prove latency requirements
3. **Stress Tests** - Multi-threaded correctness
4. **Fuzz Tests** - Edge cases and error handling

```cpp
// MANDATORY test structure
TEST(Component, BasicFunctionality) { /* ... */ }
TEST(Component, ErrorHandling) { /* ... */ }
TEST(Component, EdgeCases) { /* ... */ }
TEST(Component, ThreadSafety) { /* ... */ }

BENCHMARK(Component_Operation);
STRESS_TEST(Component_Concurrent);
```

## DOCUMENTATION REQUIREMENTS

### MANDATORY Documentation

Every public API MUST have:
```cpp
/**
 * @brief One-line description
 * @param param Description with units/range
 * @return Description with possible values
 * @throws Never (no exceptions allowed)
 * @complexity O(1) or specific complexity
 * @thread_safety Thread-safe/Not thread-safe
 * @performance Typical latency in nanoseconds
 */
```

## GIT COMMIT STANDARDS

### MANDATORY Commit Format

```
type(scope): description (max 50 chars)

Detailed explanation of what and why (not how).

Performance impact: [None/Positive/Negative]
Breaking changes: [Yes/No]
```

Types: `feat`, `fix`, `perf`, `refactor`, `test`, `docs`

## CODE REVIEW CHECKLIST

**EVERY PR MUST PASS:**

- [ ] Builds with `./scripts/build_strict.sh`
- [ ] ZERO compiler warnings
- [ ] All tests pass
- [ ] Benchmarks meet targets
- [ ] No dynamic allocation
- [ ] All type conversions explicit
- [ ] All shared data cache-aligned
- [ ] Proper memory ordering on atomics
- [ ] Documentation complete
- [ ] Code follows ALL patterns in this document

## CONSEQUENCES OF VIOLATIONS

1. **First Violation**: Code rejected, must rewrite
2. **Second Violation**: Mandatory review of this document
3. **Third Violation**: Removal from project

## THE TEN COMMANDMENTS OF LOW LATENCY

1. **Thou shalt not allocate memory in the hot path**
2. **Thou shalt align thy shared data to cache lines**
3. **Thou shalt make all type conversions explicit**
4. **Thou shalt initialize all members in constructor lists**
5. **Thou shalt follow the Rule of Three/Five/Zero**
6. **Thou shalt not use exceptions in trading code**
7. **Thou shalt measure everything with nanosecond precision**
8. **Thou shalt compile with all warnings as errors**
9. **Thou shalt not make system calls in the hot path**
10. **Thou shalt write tests before code**

## CRITICAL FILES TO STUDY

**MANDATORY READING BEFORE CODING:**

1. `docs/architecture/01_system_overview.md` - System architecture overview
2. `docs/developer_guide/developer_guide.md` - Complete development guide
3. `docs/developer_guide/bldg_blocks_api.md` - API documentation
4. `docs/reports/compiler_warnings_lessons.md` - Lessons learned
5. `bldg_blocks/types.h` - Core type system
6. `examples/` - Reference implementations

**Documentation Hub**: `docs/index.md` - Complete documentation structure

## QUICK REFERENCE - COPY & PASTE PATTERNS

```cpp
// Static cast for size_t
size_t s = static_cast<size_t>(int_val);

// Static cast for uint64_t
uint64_t u = static_cast<uint64_t>(signed_val);

// Safe signed to unsigned
if (signed_val > 0) {
    unsigned_val = static_cast<size_t>(signed_val);
}

// Memory pool allocation
auto* obj = pool.allocate();
if (!obj) {
    LOG_ERROR("Pool exhausted");
    return;
}
// Use obj...
pool.deallocate(obj);

// Cache-aligned atomic
BldgBlocks::CacheAligned<std::atomic<uint64_t>> counter{0};

// Lock-free queue
if (!queue.enqueue(std::move(item))) {
    LOG_WARN("Queue full");
}

// Logging
LOG_INFO("Event: val1=%lu, val2=%u", val1, val2);

// Thread setup
ThreadUtils::setCurrentThreadAffinity(cpu_id);
ThreadUtils::setRealTimePriority(99);
```

## FINAL WARNING

**This codebase represents hundreds of hours of optimization work. One careless developer can destroy it all.**

**Follow these principles EXACTLY or find another project.**

**There are no exceptions. There are no excuses. There is only excellence.**

---

*"In trading, microseconds matter. In our code, nanoseconds matter."*

**Build Command**: `./scripts/build_strict.sh`
**Zero Warnings Allowed**
**Zero Tolerance for Violations**

---

Last Updated: 2025-08-31
Version: 2.0.0
Status: **ENFORCED**
- My name is Praveen Ayyasola and email id praveenkumar.avln@gmail.com