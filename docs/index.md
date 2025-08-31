# Shriven Zenith Documentation

## Overview
Complete documentation for the Shriven Zenith ultra-low latency trading platform.

## Documentation Structure

### 📐 [Architecture](architecture/index.md)
System design and technical architecture documentation
- Cache-aligned implementation
- System architecture (coming soon)
- Component designs (coming soon)

### 👨‍💻 [Developer Guide](developer_guide/index.md)
Developer documentation and API references
- Complete developer guide
- BldgBlocks API documentation
- Naming conventions
- Coding standards (coming soon)

### 📖 [End User Guide](end_user_guide/index.md)
User manuals and operational documentation
- Installation guide (coming soon)
- Configuration manual (coming soon)
- Troubleshooting (coming soon)

### 📊 [Reports](reports/index.md)
Technical reports and analysis
- Development records
- Compiler warnings lessons learned
- Performance analysis (coming soon)

### 📋 [Trackers](trackers/index.md)
Progress tracking and project management
- Enhancement tracker
- Bug tracker (coming soon)
- Release notes (coming soon)

## Quick Links

### For Developers
- [Developer Guide](developer_guide/developer_guide.md) - Start here
- [BldgBlocks API](developer_guide/bldg_blocks_api.md) - API reference
- [Naming Convention](developer_guide/naming_convention.md) - Documentation standards

### For Users
- Configuration: `config.toml` in project root
- Examples: `./cmake/build-release/examples/examples all`
- Logs: Check `logs/` directory

### Key Reports
- [Compiler Warnings Lessons](reports/compiler_warnings_lessons.md)
- [Cache-Aligned Development](reports/development_record_001_cache_aligned.md)

## Documentation Standards

All documentation follows these principles:
1. **Lowercase naming**: All files use lowercase with underscores
2. **Clear structure**: Organized into 5 main categories
3. **Comprehensive**: Every component is documented
4. **Up-to-date**: Regular updates with version tracking

## Navigation

```
docs/
├── index.md                    # This file
├── architecture/               # System design
│   ├── index.md
│   └── cache_aligned_implementation.md
├── developer_guide/            # Developer docs
│   ├── index.md
│   ├── bldg_blocks_api.md
│   ├── developer_guide.md
│   └── naming_convention.md
├── end_user_guide/            # User docs
│   └── index.md
├── reports/                   # Technical reports
│   ├── index.md
│   ├── compiler_warnings_lessons.md
│   └── development_record_001_cache_aligned.md
└── trackers/                  # Project tracking
    ├── index.md
    └── enhancement_tracker.md
```

## Contributing to Documentation

When adding new documentation:
1. Follow the [naming convention](developer_guide/naming_convention.md)
2. Place in appropriate category folder
3. Update the relevant index.md
4. Use lowercase with underscores
5. Include "Last Updated" timestamp

## Version
- **Platform Version**: 2.0.0
- **Documentation Version**: 2.0.0
- **Last Updated**: 2025-08-31

---

*For the main project README, see [../README.md](../README.md)*
*For critical design principles, see [../CLAUDE.md](../CLAUDE.md)*