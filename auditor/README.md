# Claude Auditor

## Ultra-Low Latency C++ Code Compliance Checker

Claude Auditor is a comprehensive static and runtime analysis tool designed to enforce strict coding standards for ultra-low latency trading systems. It ensures compliance with CLAUDE.md principles and validates performance targets for Zerodha and Binance trading platforms.

## Features

### 🔍 Static Code Analysis
- **Dynamic Allocation Detection**: Identifies `new`, `delete`, `malloc`, `free`
- **STL Container Detection**: Flags usage of `std::vector`, `std::string`, `std::map`
- **Type Safety Validation**: Detects implicit conversions, C-style casts
- **Cache Alignment Checks**: Identifies potential false sharing issues
- **Exception Detection**: Flags exception usage in trading code
- **System Call Detection**: Identifies blocking operations in hot paths
- **TODO/Stub Detection**: Tracks incomplete implementations

### ⚡ Performance Monitoring
- **Latency Tracking**: Validates operations against nanosecond targets
- **Throughput Monitoring**: Ensures minimum operations per second
- **Memory Allocation Timing**: Tracks allocation performance
- **Cache Miss Detection**: Identifies cache-unfriendly patterns

### 📊 Compliance Validation
- **Compiler Flag Verification**: Ensures `-Wall -Wextra -Werror` usage
- **Build Script Validation**: Verifies `build_strict.sh` usage
- **Namespace Convention Checks**: Validates Trading:: namespace structure
- **File Structure Validation**: Ensures proper directory organization

### 🎯 Trading-Specific Checks
- **Risk Control Validation**: Ensures pre-trade checks exist
- **Order Validation**: Verifies order sanity checks
- **Position Limit Checks**: Validates position management
- **Float Price Detection**: Ensures integer price representation

## Installation

### Build from Source
```bash
cd /home/isoula/om/shriven_zenith
mkdir -p cmake/build-strict-debug
cd cmake/build-strict-debug
cmake ../.. -DCMAKE_BUILD_TYPE=Debug
ninja claude_audit
```

### Install Pre-commit Hook
```bash
chmod +x .githooks/pre-commit
git config core.hooksPath .githooks
```

## Usage

### Basic Audit
```bash
./claude_audit --source /path/to/code
```

### Pre-commit Mode
```bash
./claude_audit --pre-commit --fail-on-high
```

### CI/CD Integration
```bash
./claude_audit --source . --junit results.xml --json violations.json
```

### IDE Integration
```bash
./claude_audit --ide --source .
```

## Violation Severities

| Severity | Description | Examples | Action |
|----------|-------------|----------|--------|
| **CRITICAL** | Build-breaking violations | Dynamic allocation, exceptions | Must fix immediately |
| **HIGH** | Performance-impacting | String usage, unaligned data | Fix before commit |
| **MEDIUM** | Best practices | Missing const, implicit conversions | Fix soon |
| **LOW** | Style violations | Naming conventions | Fix when possible |
| **INFO** | Informational | TODOs, stubs | Track for completion |

## Performance Targets

| Component | Target Latency | Measurement |
|-----------|---------------|-------------|
| Memory Allocation | < 50ns | Per allocation |
| Queue Operations | < 100ns | Enqueue/Dequeue |
| Logging | < 100ns | Per log entry |
| Market Data Processing | < 1μs | Tick to strategy |
| Order Placement | < 10μs | Signal to gateway |
| Risk Checks | < 100ns | Per validation |

## Violation Types

### Memory Violations (CRITICAL)
```cpp
// ❌ FORBIDDEN
std::vector<int> data;
std::string name = "test";
Order* order = new Order();
void* buffer = malloc(1024);

// ✅ REQUIRED
std::array<int, 100> data;
char name[32] = "test";
Order* order = order_pool.allocate();
char buffer[1024];
```

### Type Safety Violations (HIGH)
```cpp
// ❌ FORBIDDEN
size_t count = int_value;
int result = (int)double_value;

// ✅ REQUIRED
size_t count = static_cast<size_t>(int_value);
int result = static_cast<int>(double_value);
```

### Cache Alignment Violations (HIGH)
```cpp
// ❌ FORBIDDEN
std::atomic<uint64_t> counter;

// ✅ REQUIRED
Common::CacheAligned<std::atomic<uint64_t>> counter;
```

### Exception Violations (CRITICAL)
```cpp
// ❌ FORBIDDEN
throw std::runtime_error("Error");
try { ... } catch(...) { ... }

// ✅ REQUIRED
return ErrorCode::FAILURE;
if (result != ErrorCode::SUCCESS) { ... }
```

## Integration Examples

### CMake Integration
```cmake
# Add to your CMakeLists.txt
add_subdirectory(auditor)

# Add audit target
add_custom_target(audit
    COMMAND claude_audit --source ${CMAKE_SOURCE_DIR}
    DEPENDS claude_audit
)

# Add as test
add_test(NAME CodeAudit
    COMMAND claude_audit --source ${CMAKE_SOURCE_DIR} --junit results.xml
)
```

### GitHub Actions
```yaml
- name: Run Claude Auditor
  run: |
    ./build/auditor/claude_audit \
      --source . \
      --junit audit-results.xml \
      --fail-on-high
```

### Pre-commit Hook
```bash
#!/bin/bash
./claude_audit --pre-commit --fail-on-high
if [ $? -ne 0 ]; then
    echo "Commit blocked due to violations"
    exit 1
fi
```

## Runtime Integration

### Performance Tracking
```cpp
// In your code
#ifdef ENABLE_AUDIT
    uint64_t start = Common::getCurrentNanos();
    processMarketData(tick);
    AUDIT_LATENCY("market_data", start);
#endif
```

### Throughput Monitoring
```cpp
// Track operations per second
AUDIT_THROUGHPUT("order_gateway", orders_processed);
```

### Alignment Validation
```cpp
// Validate cache alignment
AUDIT_ALIGNMENT(&shared_data);
```

## Report Formats

### Text Report
```
CLAUDE AUDITOR REPORT
=====================
Critical: 2
High: 5
Medium: 12
Low: 8
Info: 15

CRITICAL VIOLATIONS
[file.cpp:123] Dynamic allocation detected
  Suggestion: Use memory pool allocation
```

### JSON Export
```json
{
  "violations": [
    {
      "file": "order.cpp",
      "line": 45,
      "severity": "CRITICAL",
      "description": "std::string usage detected",
      "suggestion": "Use char[] buffer"
    }
  ],
  "summary": {
    "critical": 2,
    "high": 5,
    "total": 42
  }
}
```

### JUnit XML
```xml
<testsuite name="ClaudeAuditor" tests="42" failures="7">
  <testcase name="order.cpp:45" classname="Audit">
    <failure message="std::string usage detected">
      Use char[] buffer with fixed size
    </failure>
  </testcase>
</testsuite>
```

## Best Practices

### 1. Run Before Every Commit
```bash
git config core.hooksPath .githooks
```

### 2. Integrate with CI/CD
- Add to GitHub Actions
- Include in Jenkins pipeline
- Configure GitLab CI

### 3. Monitor Performance Continuously
```cpp
#define ENABLE_AUDIT 1  // Enable in development
```

### 4. Fix Violations Immediately
- CRITICAL: Block build
- HIGH: Block commit
- MEDIUM: Fix within sprint
- LOW: Track in backlog

## Troubleshooting

### Common Issues

#### "Too many violations"
- Focus on CRITICAL first
- Use `--fail-on-critical` initially
- Gradually increase strictness

#### "False positives"
- Check pattern definitions
- Submit issue with example
- Use inline suppression sparingly

#### "Performance overhead"
- Disable runtime checks in production
- Use compile-time flags
- Profile the auditor itself

## Contributing

### Adding New Checks
1. Add pattern to `patterns_` array
2. Create violation type enum
3. Implement checker function
4. Add test cases

### Improving Performance
- Use mmap for file reading
- Compile regex patterns once
- Batch violation reporting

## License

Proprietary - Shriven Zenith Trading Systems

## Contact

**Tech Lead**: Praveen Ayyasola  
**Email**: praveenkumar.avln@gmail.com

---

**Remember**: *In trading, microseconds matter. In our code, nanoseconds matter.*