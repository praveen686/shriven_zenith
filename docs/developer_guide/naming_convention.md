# Documentation Naming Convention Guide

## Folder Structure
```
docs/
├── architecture/       # System design and technical architecture
├── developer_guide/    # Developer documentation and API references
├── end_user_guide/     # User manuals and tutorials
├── reports/           # Analysis, lessons learned, and technical reports
└── trackers/          # Progress tracking and enhancement logs
```

## Naming Rules

### 1. General Rules
- **ALL LOWERCASE**: Use only lowercase letters in file and folder names
- **UNDERSCORES**: Use underscores (_) to separate words, not hyphens or spaces
- **NO CAPITALS**: Never use capital letters, even for acronyms
- **DESCRIPTIVE**: Names should clearly indicate content
- **CONCISE**: Keep names as short as possible while remaining clear

### 2. File Naming Patterns

#### Architecture Documents
```
architecture/
├── system_overview.md           # Not: System_Overview.md or SYSTEM-OVERVIEW.md
├── component_design.md          # Not: Component-Design.md
├── cache_aligned_design.md      # Not: CacheAligned_Design.md
└── trading_engine_arch.md       # Not: TradingEngine_Arch.md
```

#### Developer Guide Documents
```
developer_guide/
├── api_reference.md             # Not: API_Reference.md
├── bldg_blocks_api.md          # Not: BLDG_BLOCKS_API.md
├── coding_standards.md         # Not: Coding-Standards.md
└── getting_started.md          # Not: Getting_Started.md
```

#### End User Guide Documents
```
end_user_guide/
├── installation_guide.md       # Not: Installation-Guide.md
├── configuration_manual.md     # Not: Configuration_Manual.md
├── troubleshooting.md         # Not: TROUBLESHOOTING.md
└── quick_start.md             # Not: Quick-Start.md
```

#### Reports
```
reports/
├── performance_analysis.md          # Not: Performance_Analysis.md
├── compiler_warnings_lessons.md     # Not: Compiler-Warnings-Lessons.md
├── development_record_001.md        # Not: Development_Record_001.md
└── implementation_notes.md          # Not: Implementation-Notes.md
```

#### Trackers
```
trackers/
├── enhancement_tracker.md      # Not: ENHANCEMENT_TRACKER.md
├── bug_tracker.md              # Not: Bug-Tracker.md
├── progress_log.md             # Not: Progress_Log.md
└── release_notes.md           # Not: Release-Notes.md
```

### 3. Special Cases

#### Versioned Documents
```
# Use numbers with underscores
development_record_001.md       # Not: development-record-001.md
api_v2_reference.md            # Not: API_V2_Reference.md
release_notes_v1_0_0.md        # Not: release-notes-v1.0.0.md
```

#### Date-based Documents
```
# Use YYYY_MM_DD format with underscores
report_2025_08_31.md           # Not: report-2025-08-31.md
meeting_notes_2025_08_31.md    # Not: MeetingNotes_20250831.md
```

#### Acronyms
```
# Keep acronyms lowercase
api_reference.md                # Not: API_reference.md
cpu_optimization.md             # Not: CPU_Optimization.md
numa_configuration.md           # Not: NUMA_Configuration.md
hft_strategies.md               # Not: HFT_Strategies.md
```

### 4. Index Files
Each folder should have an index file:
```
architecture/index.md           # Table of contents for architecture docs
developer_guide/index.md        # Table of contents for developer docs
end_user_guide/index.md        # Table of contents for user docs
reports/index.md                # List of all reports
trackers/index.md               # List of all trackers
```

### 5. Asset Naming (if needed)
```
docs/assets/
├── images/
│   ├── architecture_diagram_01.png    # Not: Architecture-Diagram-01.PNG
│   └── flow_chart_trading.svg         # Not: FlowChart_Trading.SVG
└── examples/
    ├── config_example.toml             # Not: Config-Example.TOML
    └── sample_code_01.cpp              # Not: SampleCode01.CPP
```

## File Mapping for Current Documentation

### Current → New Location and Name

| Original File | Current Location | Current Name |
|-------------|--------------|----------|
| BLDG_BLOCKS_API.md | developer_guide/ | bldg_blocks_api.md |
| DEVELOPER_GUIDE.md | developer_guide/ | developer_guide.md |
| ENHANCEMENT_TRACKER.md | trackers/ | enhancement_tracker.md |
| development_record_001_cache_aligned.md | reports/ | development_record_001_cache_aligned.md |
| strict_compiler_warnings_lessons.md | reports/ | compiler_warnings_lessons.md |
| implementation_plan_cache_aligned.md | architecture/ | cache_aligned_implementation.md |

## Enforcement

1. **Pre-commit Hook**: Add a git pre-commit hook to check naming conventions
2. **CI/CD Check**: Automated pipeline to verify naming compliance
3. **Code Review**: Reviewers must verify naming conventions

## Examples of Common Mistakes to Avoid

❌ **WRONG**:
- API_Documentation.md
- BldgBlocks-API.md
- DEVELOPER_GUIDE.MD
- CacheAligned_Design.md
- TODO-List.md

✅ **CORRECT**:
- api_documentation.md
- bldg_blocks_api.md
- developer_guide.md
- cache_aligned_design.md
- todo_list.md

## Benefits of This Convention

1. **Consistency**: All files follow the same pattern
2. **Portability**: Works across all operating systems (case-sensitive and case-insensitive)
3. **Searchability**: Easy to grep/find files
4. **Simplicity**: No need to remember special cases
5. **URL-friendly**: Can be used directly in web URLs without encoding

---
*Last Updated: 2025-08-31*