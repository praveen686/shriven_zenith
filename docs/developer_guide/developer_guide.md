# Shriven Zenith Developer Guide

## ⚠️ DOCUMENTATION STATUS ⚠️

**This guide is being updated to match the actual codebase.**

Many sections contain **OUTDATED INFORMATION** that does not match the current implementation.

## Current Working Information

### Build System (VERIFIED)
```bash
# Build with strict compiler checks
./scripts/build_strict.sh

# Run examples to see actual API usage
./cmake/build-strict-release/examples/examples all
```

### Compiler Flags (VERIFIED)
**MANDATORY**: All code MUST compile with zero warnings using:
```bash
-Wall -Wextra -Werror -Wpedantic
-Wconversion -Wsign-conversion -Wold-style-cast
-Wformat-security -Weffc++ -Wno-unused
```

### Learning the Actual APIs

**CRITICAL**: Do NOT rely on documentation for API usage. Instead:

1. **Study working examples**:
   ```bash
   # See actual API usage patterns
   ls examples/
   cat examples/lf_queue_example.cpp
   cat examples/mem_pool_example.cpp
   ```

2. **Read actual header files**:
   ```bash
   # See real function signatures
   ls bldg_blocks/
   cat bldg_blocks/lf_queue.h
   cat bldg_blocks/logging.h
   ```

3. **Run working examples**:
   ```bash
   ./cmake/build-strict-release/examples/examples lf_queue
   ./cmake/build-strict-release/examples/examples mem_pool
   ```

## Key API Changes (VERIFIED)

### Logging System
```cpp
// CORRECT (current implementation)
#include "bldg_blocks/logging.h"
BldgBlocks::initLogging("logs/app.log");
LOG_INFO("Message: %s", data);
BldgBlocks::shutdownLogging();

// Global logger variable
extern Logger* g_logger;  // NOT g_opt_logger
```

### Lock-Free Queues
```cpp
// SPSC Queue - Zero-copy API
SPSCLFQueue<int> queue(1024);
auto* slot = queue.getNextToWriteTo();  // NOT enqueue()
if (slot) {
    *slot = value;
    queue.updateWriteIndex();
}

// MPMC Queue - Traditional API  
MPMCLFQueue<int> queue(1024);
queue.enqueue(value);  // This one has enqueue/dequeue
```

## CLAUDE.md Requirements (VERIFIED)

All code MUST follow strict requirements in `CLAUDE.md`:
- Zero warnings with strict compiler flags
- Explicit type conversions only
- No dynamic allocation in hot paths
- Cache-line aligned shared data structures
- Constructor initialization lists

## What's Being Updated

- [ ] API documentation (removed incorrect version)
- [ ] Code examples (verifying all work)
- [ ] Performance claims (need benchmarking)
- [ ] Function signatures (matching actual headers)
- [ ] Build instructions (keeping working parts)

## Temporary Development Approach

**Until documentation is rewritten:**

1. **Examples are truth**: Use `examples/` folder for API patterns
2. **Headers are truth**: Read `bldg_blocks/*.h` for function signatures  
3. **Build system works**: `./scripts/build_strict.sh` is reliable
4. **CLAUDE.md is enforced**: Follow all principles strictly

---

**Status**: Documentation being rewritten to match codebase  
**Date**: 2025-08-31  
**Accuracy**: Only sections marked "VERIFIED" are trustworthy