# Shriven Zenith

Low-latency trading platform components.

## Structure

```
shriven_zenith/
├── common/          # Core library components
├── examples/        # Usage examples
├── tests/          # Unit tests and benchmarks
├── docs/           # Documentation
├── scripts/        # Build scripts
└── cmake/          # Build files
```

## Building

### Requirements

- Linux with x86_64 architecture
- GCC 13+ or Clang 16+ 
- CMake 3.20+
- libnuma-dev
- googletest (for tests)

### Build Commands

```bash
# Full build with strict compiler flags
./scripts/build_strict.sh

# Run examples
./cmake/build-strict-release/examples/examples all

# Run specific example
./cmake/build-strict-release/examples/examples mem_pool
./cmake/build-strict-release/examples/examples lf_queue
./cmake/build-strict-release/examples/examples thread_utils
```

## Components

### Memory Pool
Pre-allocated memory pools with 26ns allocation latency.

### Lock-Free Queues
- SPSC (Single Producer Single Consumer) queue
- MPMC (Multiple Producer Multiple Consumer) queue
- 45ns enqueue/dequeue operations

### Logging
Asynchronous logger with 35-235ns overhead per log entry.

### Thread Utilities
CPU affinity, real-time scheduling, and NUMA support.

## Testing

```bash
# Run unit tests
./cmake/build-strict-release/tests/test_mem_pool
./cmake/build-strict-release/tests/test_lf_queue
./cmake/build-strict-release/tests/test_logger
./cmake/build-strict-release/tests/test_thread_utils
./cmake/build-strict-release/tests/test_zero_policy

# Run benchmarks
./cmake/build-strict-release/tests/benchmark_common
```

## Performance

Measured latencies on modern x86_64 hardware:

| Operation | Latency |
|-----------|---------|
| Memory allocation | 26ns |
| Queue operation | 45ns |
| Log entry | 35-235ns |

## Documentation

- [Technical Documentation](docs/technical_documentation.md) - Detailed architecture and design
- [Common Library](common/README.md) - Core components reference
- [CLAUDE.md](CLAUDE.md) - Coding standards and requirements

## Compiler Flags

The project enforces strict compilation with:
```
-Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion
-Wold-style-cast -Wformat-security -Weffc++ -O3 -march=native
```

## License

See license file for details.

## Author

Praveen Ayyasola (praveenkumar.avln@gmail.com)