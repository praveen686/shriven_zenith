# Documentation Status

## ⚠️ CRITICAL WARNING ⚠️

**ALL API DOCUMENTATION HAS BEEN REMOVED** due to critical inconsistencies with the actual codebase.

## What Was Wrong

- ~60% of documented APIs had incorrect function names
- 100% of code examples would fail to compile  
- Major classes documented didn't match implementation
- Performance claims were unverified

## Current Status

- **bldg_blocks_api.md**: REMOVED - was completely incorrect
- **Code examples**: All removed - none would compile
- **API references**: All removed - contained wrong function signatures

## For Developers

**DO NOT** rely on any existing documentation for API usage. Instead:

1. **Read the actual header files** in `bldg_blocks/` folder
2. **Study the working examples** in `examples/` folder  
3. **Run the examples** to see actual API usage:
   ```bash
   ./cmake/build-strict-release/examples/examples all
   ```

## What to Use Instead

### Lock-Free Queues
- **SPSC Queue**: `SPSCLFQueue` with zero-copy API (`getNextToWriteTo()`, `updateWriteIndex()`)
- **MPMC Queue**: `MPMCLFQueue` with traditional API (`enqueue()`, `dequeue()`)
- **See**: `examples/lf_queue_example.cpp` for working examples

### Logging
- **Macros**: `LOG_INFO()`, `LOG_ERROR()`, etc. (NOT `LOG_OPT_*`)
- **Global logger**: `g_logger` (NOT `g_opt_logger`)
- **See**: `examples/example_main.cpp` for initialization

### Memory Pools
- **See**: `bldg_blocks/mem_pool.h` for actual API
- **Example**: `examples/mem_pool_example.cpp`

### Thread Utils
- **See**: `bldg_blocks/thread_utils.h` for actual functions
- **Example**: `examples/thread_example.cpp`

## Rebuilding Documentation

Documentation will be rewritten from scratch to match the actual implementation.

**Priority**: Accurate documentation > No documentation > Wrong documentation

---

**Date**: 2025-08-31  
**Status**: Documentation cleanup in progress  
**Next**: Create accurate API docs from actual code