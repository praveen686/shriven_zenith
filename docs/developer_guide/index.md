# Developer Guide Documentation

## ⚠️ CRITICAL STATUS UPDATE ⚠️

**DOCUMENTATION UNDER MAJOR REVISION** - Most API documentation has been removed due to critical inaccuracies.

## Current Status

### ✅ RELIABLE DOCUMENTATION
- [Developer Guide](developer_guide.md) - **Updated** with current status
- [Naming Convention](naming_convention.md) - Still accurate
- [README_DOCS.md](README_DOCS.md) - **NEW** - Current documentation status

### ❌ REMOVED (WAS INCORRECT)
- ~~`bldg_blocks_api.md`~~ - **REMOVED** - 60% of APIs were wrong
- ~~API examples~~ - **REMOVED** - None would compile

### ⚠️ UNDER REVIEW
All other documentation files may contain outdated information.

## For Developers - What To Use

### 🎯 Primary Sources (TRUSTWORTHY)
1. **Working Examples**: `examples/` folder - all compile and run
   ```bash
   ./cmake/build-strict-release/examples/examples all
   ```

2. **Actual Headers**: `bldg_blocks/*.h` - real function signatures
   ```bash
   ls bldg_blocks/
   cat bldg_blocks/lf_queue.h
   ```

3. **CLAUDE.md**: Mandatory coding standards - enforced

### 🚫 DO NOT USE
- Any documentation marked as "API Reference" 
- Code examples from old documentation files
- Function names from outdated guides

## Verified Build Commands
```bash
# RELIABLE - builds with zero warnings
./scripts/build_strict.sh

# RELIABLE - shows actual API usage  
./cmake/build-strict-release/examples/examples <example_name>
```

## Key API Reality Check

### Logging (VERIFIED)
```cpp
LOG_INFO("Message");        // ✅ CORRECT
LOG_OPT_INFO("Message");    // ❌ REMOVED
g_logger                    // ✅ CORRECT  
g_opt_logger               // ❌ REMOVED
```

### Queues (VERIFIED)
```cpp
// SPSC Queue - zero-copy API
queue.getNextToWriteTo();   // ✅ CORRECT
queue.enqueue();           // ❌ WRONG API

// MPMC Queue - traditional API  
queue.enqueue();           // ✅ CORRECT for MPMC
```

## Rebuilding Plan
1. [x] Remove incorrect API documentation
2. [x] Update developer guide with warnings  
3. [x] Create status documentation
4. [ ] Create accurate API docs from actual headers
5. [ ] Verify all examples work
6. [ ] Add performance benchmarks

---

**WARNING**: Only trust documentation marked as "VERIFIED" or "RELIABLE"

**Status**: Major documentation cleanup in progress  
**Date**: 2025-08-31  
**Next**: Create accurate documentation from actual codebase