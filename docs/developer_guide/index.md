# Developer Guide Documentation

## Overview
This directory contains all developer documentation including API references, coding standards, and development guides for the Shriven Zenith platform.

## Core Documentation

### Getting Started
- [Developer Guide](developer_guide.md) - Complete guide for developers working on the platform
- [Naming Convention](naming_convention.md) - Documentation naming standards and conventions

### API References
- [BldgBlocks API](bldg_blocks_api.md) - Complete API documentation for the BldgBlocks library

### Coming Soon
- `getting_started.md` - Quick start guide for new developers
- `coding_standards.md` - Detailed coding standards and practices
- `build_system.md` - Build system documentation
- `testing_guide.md` - Testing requirements and patterns
- `performance_guide.md` - Performance optimization guide
- `debugging_guide.md` - Debugging techniques for low-latency systems

## Development Standards

### Mandatory Requirements
1. **Compiler Flags**: All code must compile with strict flags
2. **Zero Warnings**: No compiler warnings allowed
3. **Explicit Conversions**: All type conversions must be explicit
4. **Memory Management**: No dynamic allocation in hot path
5. **Documentation**: All public APIs must be documented

### Quick Reference
```bash
# Build commands
./scripts/build_strict.sh  # Always use for development
./scripts/build.sh         # Quick builds only

# Test commands
./cmake/build-release/examples/examples all
```

## Key Files to Study
1. Start with [developer_guide.md](developer_guide.md)
2. Review [bldg_blocks_api.md](bldg_blocks_api.md)
3. Understand [naming_convention.md](naming_convention.md)
4. Study the examples in `/examples/`

---
*Last Updated: 2025-08-31*