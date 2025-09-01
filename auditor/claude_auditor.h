#pragma once

#include <cstdint>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <regex>

#include "common/macros.h"
#include "common/logging.h"
#include "common/types.h"

namespace Auditor {

/// Violation severity levels
enum class Severity : uint8_t {
    CRITICAL = 0,  // Build-breaking violations (dynamic allocation, exceptions)
    HIGH = 1,      // Performance-impacting violations (string usage, unaligned data)
    MEDIUM = 2,    // Best practice violations (missing const, implicit conversions)
    LOW = 3,       // Style violations (naming conventions)
    INFO = 4       // Informational (TODOs, stubs)
};

/// Violation categories based on CLAUDE.md
enum class ViolationType : uint16_t {
    // Memory violations (CRITICAL)
    DYNAMIC_ALLOCATION = 0,
    NEW_DELETE_USAGE = 1,
    STD_VECTOR_USAGE = 2,
    STD_STRING_USAGE = 3,
    STD_MAP_USAGE = 4,
    MALLOC_FREE_USAGE = 5,
    
    // Type safety violations (HIGH)
    IMPLICIT_CONVERSION = 10,
    C_STYLE_CAST = 11,
    MISSING_EXPLICIT_CAST = 12,
    SIGNED_UNSIGNED_MIX = 13,
    
    // Performance violations (HIGH)
    UNALIGNED_CACHE_LINE = 20,
    FALSE_SHARING = 21,
    SYSTEM_CALL_IN_HOT_PATH = 22,
    STRING_IN_HOT_PATH = 23,
    EXCEPTION_IN_TRADING = 24,
    BLOCKING_OPERATION = 25,
    MUTEX_IN_HOT_PATH = 26,
    
    // Constructor violations (MEDIUM)
    UNINITIALIZED_MEMBER = 30,
    ASSIGNMENT_IN_CONSTRUCTOR = 31,
    MISSING_INIT_LIST = 32,
    
    // Resource management violations (HIGH)
    MISSING_RULE_OF_THREE = 40,
    MISSING_RULE_OF_FIVE = 41,
    MISSING_DELETE_COPY = 42,
    MISSING_NOEXCEPT_MOVE = 43,
    
    // Code quality violations (MEDIUM)
    UNUSED_VARIABLE = 50,
    UNUSED_PARAMETER = 51,
    DEAD_CODE = 52,
    MAGIC_NUMBER = 53,
    MISSING_CONST = 54,
    MISSING_OVERRIDE = 55,
    
    // Documentation violations (LOW)
    MISSING_DOCUMENTATION = 60,
    INCOMPLETE_TODO = 61,
    STUB_FUNCTION = 62,
    PLACEHOLDER_CODE = 63,
    
    // Logging violations (MEDIUM)
    COUT_USAGE = 70,
    PRINTF_USAGE = 71,
    CERR_USAGE = 72,
    NON_ASYNC_LOGGING = 73,
    
    // Testing violations (MEDIUM)
    MISSING_TEST = 80,
    LOW_COVERAGE = 81,
    MISSING_BENCHMARK = 82,
    
    // Latency violations (CRITICAL)
    LATENCY_BREACH = 90,
    THROUGHPUT_MISS = 91,
    MEMORY_ALLOCATION_TIME = 92,
    
    // Trading specific violations (HIGH)
    MISSING_RISK_CHECK = 100,
    UNVALIDATED_ORDER = 101,
    MISSING_POSITION_CHECK = 102,
    FLOAT_PRICE_USAGE = 103,  // Prices should be integers
    
    // Build violations (CRITICAL)
    COMPILER_WARNING = 110,
    MISSING_WERROR = 111,
    WRONG_BUILD_SCRIPT = 112,
    
    // File organization violations (CRITICAL)
    REDUNDANT_FILE = 113,
    DUPLICATE_IMPLEMENTATION = 114,
    MISPLACED_FILE = 115
};

/// Represents a single code violation
struct Violation {
    ViolationType type;
    Severity severity;
    char file_path[256];  // Fixed buffer for file path
    uint32_t line_number;
    char function_name[128];  // Fixed buffer for function name
    char description[256];  // Fixed buffer for description
    char suggestion[256];  // Fixed buffer for suggestion
    uint64_t timestamp_ns;
};

/// Performance metric for tracking
struct PerformanceMetric {
    const char* component;
    const char* operation;
    uint64_t latency_ns;
    uint64_t throughput_ops_per_sec;
    uint64_t timestamp_ns;
    bool meets_target;
};

/// Source code pattern for detection
struct CodePattern {
    const char* pattern;
    ViolationType violation_type;
    const char* description;
    const char* fix_suggestion;
};

/// ClaudeAuditor - Main auditing component
class ClaudeAuditor {
public:
    /// Configuration for auditor
    struct Config {
        bool enable_runtime_checks = true;
        bool enable_static_analysis = true;
        bool enable_performance_tracking = true;
        bool fail_on_critical = true;
        bool fail_on_high = true;
        bool generate_report = true;
        bool enable_detailed_logging = true;
        uint32_t max_violations = 1000;
        const char* report_path = "/home/isoula/om/shriven_zenith/logs/audit/audit_report.txt";
        const char* source_root = "/home/isoula/om/shriven_zenith";
        const char* log_dir = "/home/isoula/om/shriven_zenith/logs/audit";
    };
    
    explicit ClaudeAuditor(const Config& config);
    ~ClaudeAuditor();
    
    // ========== Static Analysis Functions ==========
    
    /// Analyze a single source file for violations
    void analyzeFile(const char* file_path) noexcept;
    
    /// Analyze entire codebase
    void analyzeCodebase() noexcept;
    
    /// Check for specific patterns
    bool checkDynamicAllocation(const char* line) noexcept;
    bool checkStdContainers(const char* line) noexcept;
    bool checkImplicitConversion(const char* line) noexcept;
    bool checkCacheAlignment(const char* line) noexcept;
    bool checkSystemCalls(const char* line) noexcept;
    bool checkExceptions(const char* line) noexcept;
    bool checkLogging(const char* line) noexcept;
    bool checkTodos(const char* line) noexcept;
    bool checkStubs(const char* line) noexcept;
    
    // ========== Runtime Audit Functions ==========
    
    /// Track memory allocation (hook into allocator)
    void onMemoryAllocation(size_t size, const char* location) noexcept;
    
    /// Track performance metrics
    void recordLatency(const char* component, uint64_t latency_ns) noexcept;
    void recordThroughput(const char* component, uint64_t ops_per_sec) noexcept;
    
    /// Validate data structures at runtime
    bool validateCacheAlignment(const void* ptr) noexcept;
    bool validateMemoryPool(const void* pool, size_t expected_size) noexcept;
    bool validateLockFreeQueue(const void* queue) noexcept;
    
    // ========== Compliance Checking ==========
    
    /// Check CLAUDE.md compliance
    bool checkCompilerFlags() noexcept;
    bool checkBuildScript() noexcept;
    bool checkNamespace() noexcept;
    bool checkFileStructure() noexcept;
    
    /// Check for redundant files
    bool checkRedundantFiles() noexcept;
    bool isRedundantFile(const char* file_path) noexcept;
    
    /// Check trading-specific requirements
    bool checkRiskControls() noexcept;
    bool checkOrderValidation() noexcept;
    bool checkPositionLimits() noexcept;
    bool checkLatencyTargets() noexcept;
    
    // ========== Reporting Functions ==========
    
    /// Generate audit report
    void generateReport() noexcept;
    void printViolations(Severity min_severity = Severity::LOW) noexcept;
    void printPerformanceMetrics() noexcept;
    
    /// Get violation statistics
    uint32_t getViolationCount(Severity severity) const noexcept;
    uint32_t getTotalViolations() const noexcept;
    bool hasCliticalViolations() const noexcept;
    
    /// Export violations for CI/CD
    void exportJSON(const char* path) noexcept;
    void exportJUnit(const char* path) noexcept;
    
    // ========== Integration Hooks ==========
    
    /// Pre-commit hook
    bool preCommitCheck() noexcept;
    
    /// CI/CD integration
    int getExitCode() const noexcept;
    
    /// IDE integration
    void generateIDEWarnings() noexcept;
    
    // Deleted constructors
    ClaudeAuditor() = delete;
    ClaudeAuditor(const ClaudeAuditor&) = delete;
    ClaudeAuditor& operator=(const ClaudeAuditor&) = delete;
    
private:
    Config config_;
    
    // Internal helper for recursive directory traversal
    void analyzeDirectoryRecursive(const char* path, const char** extensions, size_t ext_count) noexcept;
    
    // Violation storage (fixed-size, no dynamic allocation)
    static constexpr size_t MAX_VIOLATIONS = 10000;
    std::array<Violation, MAX_VIOLATIONS> violations_;
    std::atomic<uint32_t> violation_count_{0};
    
    // Performance metrics storage
    static constexpr size_t MAX_METRICS = 100000;
    std::array<PerformanceMetric, MAX_METRICS> metrics_;
    std::atomic<uint32_t> metric_count_{0};
    
    // Violation counts by severity
    std::atomic<uint32_t> critical_count_{0};
    std::atomic<uint32_t> high_count_{0};
    std::atomic<uint32_t> medium_count_{0};
    std::atomic<uint32_t> low_count_{0};
    std::atomic<uint32_t> info_count_{0};
    
    // Pattern matchers (pre-compiled regex)
    static constexpr size_t MAX_PATTERNS = 200;
    std::array<CodePattern, MAX_PATTERNS> patterns_;
    uint32_t pattern_count_{0};
    
    // Statistics
    std::atomic<uint32_t> files_analyzed_{0};
    
    // Internal methods
    void initializePatterns() noexcept;
    void addViolation(const Violation& violation) noexcept;
    void processLine(const char* line, uint32_t line_num, const char* file) noexcept;
    bool matchPattern(const char* line, const CodePattern& pattern) noexcept;
    void analyzeASTNode(const char* node_type, const char* content) noexcept;
};

// ========== Inline Audit Macros ==========

#ifdef ENABLE_AUDIT
    #define AUDIT_LATENCY(component, start_ns) \
        ClaudeAuditor::instance()->recordLatency(component, Common::getCurrentNanos() - start_ns)
    
    #define AUDIT_THROUGHPUT(component, ops) \
        ClaudeAuditor::instance()->recordThroughput(component, ops)
    
    #define AUDIT_ALIGNMENT(ptr) \
        ASSERT(ClaudeAuditor::instance()->validateCacheAlignment(ptr), "Cache alignment violation")
    
    #define AUDIT_CHECK() \
        ASSERT(!ClaudeAuditor::instance()->hasCliticalViolations(), "Critical violations detected")
#else
    #define AUDIT_LATENCY(component, start_ns) ((void)0)
    #define AUDIT_THROUGHPUT(component, ops) ((void)0)
    #define AUDIT_ALIGNMENT(ptr) ((void)0)
    #define AUDIT_CHECK() ((void)0)
#endif

// ========== Performance Target Constants ==========

namespace PerformanceTargets {
    // Latency targets in nanoseconds
    constexpr uint64_t MEMORY_ALLOCATION_MAX = 50;
    constexpr uint64_t QUEUE_OPERATION_MAX = 100;
    constexpr uint64_t LOGGING_MAX = 100;
    constexpr uint64_t MARKET_DATA_PROCESSING_MAX = 1000;
    constexpr uint64_t ORDER_PLACEMENT_MAX = 10000;
    constexpr uint64_t RISK_CHECK_MAX = 100;
    constexpr uint64_t STRATEGY_DECISION_MAX = 2000;
    
    // Throughput targets (operations per second)
    constexpr uint64_t MARKET_DATA_MIN = 1000000;
    constexpr uint64_t ORDER_THROUGHPUT_MIN = 100000;
    constexpr uint64_t RISK_CHECK_MIN = 500000;
}

// ========== Pattern Definitions ==========

namespace Patterns {
    // Dynamic allocation patterns - simplified for substring matching
    constexpr const char* NEW_OPERATOR = "new ";
    constexpr const char* DELETE_OPERATOR = "delete";
    constexpr const char* MALLOC_CALL = "malloc(";
    constexpr const char* FREE_CALL = "free(";
    
    // STL container patterns
    constexpr const char* STD_VECTOR = "std::vector";
    constexpr const char* STD_STRING = "std::string";
    constexpr const char* STD_MAP = "std::map";
    constexpr const char* STD_LIST = "std::list";
    
    // Type conversion patterns - simplified for substring matching
    constexpr const char* C_STYLE_CAST = "(int*)";
    constexpr const char* IMPLICIT_CONVERSION = "implicit";
    
    // Exception patterns
    constexpr const char* THROW_STATEMENT = "throw ";
    constexpr const char* TRY_BLOCK = "try {";
    constexpr const char* CATCH_BLOCK = "catch (";
    
    // System call patterns
    constexpr const char* SYSTEM_CALLS = "system(";
    constexpr const char* BLOCKING_IO = "fopen(";
    constexpr const char* MMAP_CALL = "mmap(";
    
    // Logging patterns
    constexpr const char* COUT_USAGE = "std::cout";
    constexpr const char* PRINTF_USAGE = "printf(";
    constexpr const char* CERR_USAGE = "std::cerr";
    
    // TODO and stub patterns
    constexpr const char* TODO_PATTERN = "TODO:";
    constexpr const char* FIXME_PATTERN = "FIXME:";
    constexpr const char* STUB_PATTERN = "STUB";
    constexpr const char* NOT_IMPLEMENTED = "not implemented";
}

} // namespace Auditor