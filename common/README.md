# Common Library

Core components for low-latency trading systems.

## Components

### Memory Management
- `mem_pool.h` - Pre-allocated memory pools with O(1) allocation/deallocation
- `types.h` - Core type definitions and cache-aligned wrappers

### Data Structures  
- `lf_queue.h` - Lock-free SPSC and MPMC queues
- `macros.h` - Performance macros (branch prediction, cache alignment)

### Logging
- `logging.h` - Asynchronous logger with lock-free ring buffer
- `logging.cpp` - Logger implementation

### Threading
- `thread_utils.h` - CPU affinity, real-time scheduling, NUMA binding

### Networking
- `tcp_socket.h` - TCP socket wrapper
- `mcast_socket.h` - Multicast socket for market data
- `socket_utils.h` - Socket utilities
- `socket.h` - Base socket class
- `tcp_server.h` - TCP server implementation

### Utilities
- `time_utils.h` - High-resolution timing utilities
- `perf_utils.h` - Performance measurement tools

## Usage

Include headers directly:
```cpp
#include "common/mem_pool.h"
#include "common/lf_queue.h"
#include "common/logging.h"
```

Link against CommonImpl library:
```cmake
target_link_libraries(your_target CommonImpl)
```

## Performance Characteristics

| Component | Latency | Throughput |
|-----------|---------|------------|
| Memory Pool | 26ns allocation | 38M ops/sec |
| SPSC Queue | 45ns enqueue | 22M msgs/sec |
| Logger | 35-235ns | 4.2M logs/sec |

## Build Requirements

- C++23
- GCC 13+ or Clang 16+
- libnuma (optional, for NUMA support)
- pthreads

## Build Configuration Files

### CommonConfig.cmake.in
CMake package configuration template that enables the Common library to be found and used by other CMake projects via `find_package(Common)`. Processed during installation to create CommonConfig.cmake.

### config.h.in  
Build-time configuration template that CMake processes to generate config.h with:
- Version information (major, minor, patch)
- Feature detection (NUMA support)
- Compile-time flags based on system capabilities

## Namespace

All components are in the `Common` namespace:
```cpp
using namespace Common;
```