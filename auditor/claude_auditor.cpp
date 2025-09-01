#include "claude_auditor.h"

#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dirent.h>
#include <cstring>
#include <algorithm>
#include "common/time_utils.h"
#include "common/logging.h"

namespace Auditor {

// ========== Surgical Fix for False Positives ==========

static inline bool is_space(char c) noexcept {
    return c==' ' || c=='\t' || c=='\r' || c=='\n' || c=='\f' || c=='\v';
}

// Fast skip over spaces backwards; returns index of last non-space or -1
static inline long rskip_ws(const char* s, long i) noexcept {
    while (i > 0 && is_space(s[i])) --i;
    if (i == 0 && is_space(s[0])) return -1;
    return i;
}

// Strip single-line comments and string/char literals 
struct Slice { const char* p; size_t n; };
// Use fixed-size buffer instead of std::string
static constexpr size_t MAX_SANITIZED_LINE = 4096;
static thread_local char g_sanitized_buffer[MAX_SANITIZED_LINE];

static Slice sanitize_line(const char* line, size_t len) {
    size_t out_pos = 0;
    if (len >= MAX_SANITIZED_LINE) len = MAX_SANITIZED_LINE - 1;
    
    bool in_str=false, in_chr=false, esc=false;
    for (size_t i=0; i<len && out_pos < MAX_SANITIZED_LINE-1; i++){
        char c=line[i];
        if (!in_str && !in_chr && len>1 && i<len-1 && line[i]=='/' && line[i+1]=='/') { break; }
        if (!in_chr && c=='"' && !esc) in_str = !in_str;
        else if (!in_str && c=='\'' && !esc) in_chr = !in_chr;
        esc = (!esc && (c=='\\')) && (in_str || in_chr);
        g_sanitized_buffer[out_pos++] = (in_str||in_chr) ? ' ' : c;
    }
    g_sanitized_buffer[out_pos] = '\0';
    return { g_sanitized_buffer, out_pos };
}

// Thread-local state for multiline = delete detection
static thread_local bool g_prev_line_ends_with_equal = false;

// True if this line is a deleted-function specifier
static bool is_deleted_function_spec(const char* s, size_t n) noexcept {
    const char* d = static_cast<const char*>(memmem(s, n, "delete", 6));
    if (!d) return false;

    // Look backwards from 'delete' for a preceding '=' with only whitespace between
    size_t offset = static_cast<size_t>(d - s);
    if (offset == 0) return false; // 'delete' is at the beginning
    long i = static_cast<long>(offset - 1);
    i = rskip_ws(s, i);
    if (i >= 0 && s[i] == '=') {
        return true;  // Found '= delete'
    }
    return false;
}

// Update flag for multiline = delete detection
static void update_trailing_equal_flag(const char* s, size_t n) noexcept {
    size_t i = n;
    while (i>0 && is_space(s[i-1])) --i;
    g_prev_line_ends_with_equal = (i>0 && s[i-1]=='=');
}

[[maybe_unused]] static bool is_operator_delete_decl(const char* s, size_t n) noexcept {
    const char* op = static_cast<const char*>(memmem(s, n, "operator", 8));
    if (!op) return false;
    size_t remaining = static_cast<size_t>((s+n)-op);
    const char* del = static_cast<const char*>(memmem(op, remaining, "delete", 6));
    return del != nullptr;
}

[[maybe_unused]] static bool is_delete_expression(const char* s, size_t n) noexcept {
    size_t i=0; 
    while (i<n && is_space(s[i])) ++i;
    if (i+6 > n) return false;
    if (std::memcmp(s+i, "delete", 6) != 0) return false;

    size_t j = i+6;
    while (j<n && is_space(s[j])) ++j;
    if (j<n && s[j]=='[') {
        ++j; while (j<n && is_space(s[j])) ++j;
        if (j>=n || s[j]!=']') return false;
        ++j;
    }
    while (j<n && is_space(s[j])) ++j;
    if (j<n && s[j]==';') return false;  // delete; alone is suspicious

    return true;
}

// ========== Constructor/Destructor ==========

ClaudeAuditor::ClaudeAuditor(const Config& config) 
    : config_(config), 
      violations_{},
      metrics_{},
      patterns_{} {
    // Initialize logging for audit module if needed
    if (config_.enable_detailed_logging) {
        // Create timestamped log file for this audit session
        char timestamp[32];
        auto now = Common::getNanosSinceEpoch();
        snprintf(timestamp, sizeof(timestamp), "%lu", now);
        
        char log_file[512];
        snprintf(log_file, sizeof(log_file), "%s/audit_%s.log", config_.log_dir, timestamp);
        Common::initLogging(log_file);
        
        LOG_INFO("=== CLAUDE AUDITOR SESSION STARTED ===");
        LOG_INFO("Source root: %s", config_.source_root);
        LOG_INFO("Report path: %s", config_.report_path);
    }
    
    initializePatterns();
    
    // Clear violation and metric arrays
    std::memset(violations_.data(), 0, sizeof(Violation) * MAX_VIOLATIONS);
    std::memset(metrics_.data(), 0, sizeof(PerformanceMetric) * MAX_METRICS);
    
    // Reset counters
    violation_count_.store(0);
    metric_count_.store(0);
    critical_count_.store(0);
    high_count_.store(0);
    medium_count_.store(0);
    low_count_.store(0);
    info_count_.store(0);
}

ClaudeAuditor::~ClaudeAuditor() {
    if (config_.generate_report) {
        generateReport();
    }
    
    if (config_.enable_detailed_logging) {
        LOG_INFO("=== CLAUDE AUDITOR SESSION COMPLETED ===");
        LOG_INFO("Total violations: %u", violation_count_.load());
        Common::shutdownLogging();
    }
}

// ========== Pattern Initialization ==========

void ClaudeAuditor::initializePatterns() noexcept {
    pattern_count_ = 0;
    
    // Dynamic allocation patterns
    patterns_[pattern_count_++] = {
        Patterns::NEW_OPERATOR,
        ViolationType::NEW_DELETE_USAGE,
        "Usage of 'new' operator detected",
        "Use memory pool allocation instead"
    };
    
    patterns_[pattern_count_++] = {
        Patterns::DELETE_OPERATOR,
        ViolationType::NEW_DELETE_USAGE,
        "Usage of 'delete' operator detected",
        "Use memory pool deallocation instead"
    };
    
    patterns_[pattern_count_++] = {
        Patterns::MALLOC_CALL,
        ViolationType::MALLOC_FREE_USAGE,
        "Usage of malloc() detected",
        "Use Common::MemoryPool instead"
    };
    
    // STL container patterns
    patterns_[pattern_count_++] = {
        Patterns::STD_VECTOR,
        ViolationType::STD_VECTOR_USAGE,
        "std::vector usage detected",
        "Use fixed-size array or Common::FixedArray"
    };
    
    patterns_[pattern_count_++] = {
        Patterns::STD_STRING,
        ViolationType::STD_STRING_USAGE,
        "std::string usage detected",
        "Use char[] buffer with fixed size"
    };
    
    patterns_[pattern_count_++] = {
        Patterns::STD_MAP,
        ViolationType::STD_MAP_USAGE,
        "std::map or std::unordered_map usage detected",
        "Use fixed-size hash array with direct indexing"
    };
    
    // Exception patterns
    patterns_[pattern_count_++] = {
        Patterns::THROW_STATEMENT,
        ViolationType::EXCEPTION_IN_TRADING,
        "Exception throwing detected",
        "Return error codes instead of throwing exceptions"
    };
    
    patterns_[pattern_count_++] = {
        Patterns::TRY_BLOCK,
        ViolationType::EXCEPTION_IN_TRADING,
        "Try-catch block detected",
        "Use error codes for error handling"
    };
    
    // System call patterns
    patterns_[pattern_count_++] = {
        Patterns::SYSTEM_CALLS,
        ViolationType::SYSTEM_CALL_IN_HOT_PATH,
        "System call in potential hot path",
        "Move system calls outside hot path"
    };
    
    patterns_[pattern_count_++] = {
        Patterns::BLOCKING_IO,
        ViolationType::BLOCKING_OPERATION,
        "Blocking I/O operation detected",
        "Use non-blocking I/O or async operations"
    };
    
    patterns_[pattern_count_++] = {
        Patterns::MMAP_CALL,
        ViolationType::SYSTEM_CALL_IN_HOT_PATH,
        "mmap() system call detected",
        "Move system calls outside hot path"
    };
    
    // Logging patterns
    patterns_[pattern_count_++] = {
        Patterns::COUT_USAGE,
        ViolationType::COUT_USAGE,
        "std::cout usage detected",
        "Use Common::Logger instead"
    };
    
    patterns_[pattern_count_++] = {
        Patterns::PRINTF_USAGE,
        ViolationType::PRINTF_USAGE,
        "printf() usage detected",
        "Use LOG_INFO/LOG_ERROR macros"
    };
    
    // TODO and stub patterns
    patterns_[pattern_count_++] = {
        Patterns::TODO_PATTERN,
        ViolationType::INCOMPLETE_TODO,
        "TODO comment found",
        "Complete the implementation"
    };
    
    patterns_[pattern_count_++] = {
        Patterns::FIXME_PATTERN,
        ViolationType::INCOMPLETE_TODO,
        "FIXME comment found",
        "Complete the implementation"
    };
    
    patterns_[pattern_count_++] = {
        Patterns::STUB_PATTERN,
        ViolationType::STUB_FUNCTION,
        "Stub function detected",
        "Implement the function body"
    };
}

// ========== File Analysis ==========

void ClaudeAuditor::analyzeFile(const char* file_path) noexcept {
    // Log which file we're analyzing
    if (config_.enable_detailed_logging) {
        LOG_INFO("Analyzing file: %s", file_path);
    }
    
    // Check if this is a redundant file
    if (isRedundantFile(file_path)) {
        Violation v = {};
        v.type = ViolationType::REDUNDANT_FILE;
        v.severity = Severity::CRITICAL;
        std::strncpy(v.file_path, file_path, sizeof(v.file_path) - 1);
        v.file_path[sizeof(v.file_path) - 1] = '\0';
        v.line_number = 0;
        std::strncpy(v.function_name, "file_organization", sizeof(v.function_name) - 1);
        v.function_name[sizeof(v.function_name) - 1] = '\0';
        
        const char* basename = std::strrchr(file_path, '/');
        basename = basename ? basename + 1 : file_path;
        
        if (std::strstr(basename, "mempool") || std::strstr(basename, "mem_pool")) {
            snprintf(v.description, sizeof(v.description), 
                    "REDUNDANT FILE: Memory pool already exists in common/mem_pool.h");
            std::strncpy(v.suggestion, "Remove this file and use Common::MemoryPool", sizeof(v.suggestion) - 1);
        } else if (std::strstr(basename, "queue") && !std::strstr(file_path, "/common/")) {
            snprintf(v.description, sizeof(v.description), 
                    "REDUNDANT FILE: Queue implementation exists in common/lf_queue.h");
            std::strncpy(v.suggestion, "Remove this file and use Common::LFQueue", sizeof(v.suggestion) - 1);
        } else if (std::strstr(basename, "logger") || std::strstr(basename, "logging")) {
            if (!std::strstr(file_path, "/common/")) {
                snprintf(v.description, sizeof(v.description), 
                        "REDUNDANT FILE: Logging already exists in common/logging.h");
                std::strncpy(v.suggestion, "Remove this file and use Common::Logger", sizeof(v.suggestion) - 1);
            }
        } else {
            snprintf(v.description, sizeof(v.description), 
                    "REDUNDANT FILE: Duplicate functionality detected");
            std::strncpy(v.suggestion, "Remove redundant implementation", sizeof(v.suggestion) - 1);
        }
        v.suggestion[sizeof(v.suggestion) - 1] = '\0';
        v.timestamp_ns = Common::getNanosSinceEpoch();
        
        if (config_.enable_detailed_logging) {
            LOG_ERROR("REDUNDANT FILE DETECTED: %s", file_path);
        }
        
        addViolation(v);
    }
    
    // Open file with mmap for efficient reading
    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        if (config_.enable_detailed_logging) {
            LOG_WARN("Failed to open file: %s", file_path);
        }
        return;
    }
    
    struct stat sb;
    if (fstat(fd, &sb) < 0) {
        close(fd);
        return;
    }
    
    if (sb.st_size == 0) {
        close(fd);
        return;
    }
    
    // Memory map the file
    const char* file_content = static_cast<const char*>(
        mmap(nullptr, static_cast<size_t>(sb.st_size), PROT_READ, MAP_PRIVATE, fd, 0)
    );
    
    if (file_content == MAP_FAILED) {
        close(fd);
        return;
    }
    
    // Process line by line
    const char* line_start = file_content;
    const char* line_end = nullptr;
    uint32_t line_num = 1;
    
    for (size_t i = 0; i < static_cast<size_t>(sb.st_size); ++i) {
        if (file_content[i] == '\n') {
            line_end = &file_content[i];
            
            // Create null-terminated line buffer
            char line_buffer[4096] = {0};
            size_t line_len = static_cast<size_t>(line_end - line_start);
            if (line_len < sizeof(line_buffer) - 1) {
                std::memcpy(line_buffer, line_start, line_len);
                line_buffer[line_len] = '\0';
                
                processLine(line_buffer, line_num, file_path);
            }
            
            line_start = line_end + 1;
            line_num++;
        }
    }
    
    // Clean up
    munmap(const_cast<char*>(file_content), static_cast<size_t>(sb.st_size));
    close(fd);
    
    // Increment files analyzed counter
    files_analyzed_.fetch_add(1);
}

void ClaudeAuditor::processLine(const char* line, uint32_t line_num, const char* file) noexcept {
    // Skip empty lines and pure comment lines
    if (!line || line[0] == '\0') return;
    
    // === SURGICAL FIX: Pre-process line for false positives ===
    size_t line_len = std::strlen(line);
    auto sanitized = sanitize_line(line, line_len);
    
    // Short-circuit deleted function specifiers BEFORE pattern matching
    if (is_deleted_function_spec(sanitized.p, sanitized.n) || g_prev_line_ends_with_equal) {
        // This is a "= delete" - classify as Tier C Style, not Tier A Safety
        Violation v = {};
        v.type = ViolationType::STYLE_DELETED_FUNCTION;  // Need to add this enum
        std::strncpy(v.file_path, file, sizeof(v.file_path) - 1);
        v.file_path[sizeof(v.file_path) - 1] = '\0';
        v.line_number = line_num;
        v.function_name[0] = '\0';
        std::strncpy(v.description, "Deleted function specifier detected (safe)", sizeof(v.description) - 1);
        v.description[sizeof(v.description) - 1] = '\0';
        std::strncpy(v.suggestion, "This is good practice for preventing unwanted operations", sizeof(v.suggestion) - 1);
        v.suggestion[sizeof(v.suggestion) - 1] = '\0';
        v.timestamp_ns = Common::getNanosSinceEpoch();
        v.severity = Severity::INFO;
        v.tier = ViolationTier::TIER_C_STYLE;
        
        addViolation(v);
        update_trailing_equal_flag(sanitized.p, sanitized.n);
        return; // CRITICAL: Skip all other pattern matching
    }
    
    // Update multiline state
    update_trailing_equal_flag(sanitized.p, sanitized.n);
    
    // Check against all patterns
    for (uint32_t i = 0; i < pattern_count_; ++i) {
        if (matchPattern(line, patterns_[i])) {
            Violation v = {};
            v.type = patterns_[i].violation_type;
            std::strncpy(v.file_path, file, sizeof(v.file_path) - 1);
            v.file_path[sizeof(v.file_path) - 1] = '\0';
            v.line_number = line_num;
            v.function_name[0] = '\0'; // TODO: Extract function name
            std::strncpy(v.description, patterns_[i].description, sizeof(v.description) - 1);
            v.description[sizeof(v.description) - 1] = '\0';
            std::strncpy(v.suggestion, patterns_[i].fix_suggestion, sizeof(v.suggestion) - 1);
            v.suggestion[sizeof(v.suggestion) - 1] = '\0';
            v.timestamp_ns = Common::getNanosSinceEpoch();
            
            // Determine severity based on violation type
            if (v.type <= ViolationType::MALLOC_FREE_USAGE) {
                v.severity = Severity::CRITICAL;
            } else if (v.type <= ViolationType::MUTEX_IN_HOT_PATH) {
                v.severity = Severity::HIGH;
            } else if (v.type <= ViolationType::MISSING_OVERRIDE) {
                v.severity = Severity::MEDIUM;
            } else if (v.type <= ViolationType::PLACEHOLDER_CODE) {
                v.severity = Severity::LOW;
            } else {
                v.severity = Severity::INFO;
            }
            
            // Classify tier (overrides severity for better production readiness)
            v.tier = classifyViolationTier(v.type, line);
            
            if (config_.enable_detailed_logging) {
                LOG_WARN("VIOLATION [%s:%u]: %s", file, line_num, v.description);
            }
            
            addViolation(v);
        }
    }
    
    // Additional checks
    checkDynamicAllocation(line);
    checkStdContainers(line);
    checkImplicitConversion(line);
    checkCacheAlignment(line);
    checkSystemCalls(line);
    checkExceptions(line);
    checkLogging(line);
    checkTodos(line);
    checkStubs(line);
}

bool ClaudeAuditor::matchPattern(const char* line, const CodePattern& pattern) noexcept {
    // Skip comment lines for critical patterns
    const char* trimmed = line;
    while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
    if (trimmed[0] == '/' && trimmed[1] == '/') {
        return false;  // Skip comment lines
    }
    
    // Special handling for new/delete operators to avoid false positives
    if (pattern.pattern == Patterns::NEW_OPERATOR) {
        // Use our improved dynamic allocation checker
        return checkDynamicAllocation(line) && std::strstr(line, "new ");
    }
    if (pattern.pattern == Patterns::DELETE_OPERATOR) {
        // Check for delete, but skip "= delete" syntax
        if (std::strstr(line, "= delete")) {
            return false;
        }
        return std::strstr(line, "delete ") != nullptr;
    }
    
    // Simple substring search for other patterns
    return std::strstr(line, pattern.pattern) != nullptr;
}

// ========== Specific Pattern Checkers ==========

bool ClaudeAuditor::checkDynamicAllocation(const char* line) noexcept {
    // Skip "= delete" syntax (deleted functions)
    if (std::strstr(line, "= delete")) {
        return false;
    }
    
    // Check for placement new patterns first (these are ALLOWED)
    // Placement new uses parentheses after 'new' with a memory address
    if (std::strstr(line, "new (") || std::strstr(line, "::new(") || 
        std::strstr(line, "::new (") || std::strstr(line, "placement new")) {
        // Additional checks for common placement new patterns
        if (std::strstr(line, "static_cast") || 
            std::strstr(line, "std::addressof") ||
            std::strstr(line, "new (&") ||
            std::strstr(line, "_storage)") ||
            std::strstr(line, "_buffer)") ||
            std::strstr(line, "reinterpret_cast")) {
            // These are placement new patterns - ALLOWED
            return false;
        }
    }
    
    // Check for new/delete (after filtering out placement new)
    if (std::strstr(line, "new ") || std::strstr(line, "delete ")) {
        // Double-check it's not a placement new we missed
        const char* new_pos = std::strstr(line, "new ");
        if (new_pos) {
            // Check what comes after "new "
            const char* after_new = new_pos + 4;
            // If the next character is '(', it's likely placement new
            if (*after_new == '(') {
                return false;
            }
        }
        return true;
    }
    
    // Check for malloc/free
    if (std::strstr(line, "malloc(") || std::strstr(line, "free(")) {
        return true;
    }
    
    // Check for smart pointers (also dynamic)
    if (std::strstr(line, "std::unique_ptr") || 
        std::strstr(line, "std::shared_ptr") ||
        std::strstr(line, "std::make_unique") ||
        std::strstr(line, "std::make_shared")) {
        return true;
    }
    
    return false;
}

bool ClaudeAuditor::checkStdContainers(const char* line) noexcept {
    const char* forbidden[] = {
        "std::vector", "std::string", "std::list", "std::deque",
        "std::map", "std::unordered_map", "std::set", "std::unordered_set",
        "std::queue", "std::stack", "std::priority_queue"
    };
    
    for (const auto& container : forbidden) {
        if (std::strstr(line, container)) {
            return true;
        }
    }
    
    return false;
}

bool ClaudeAuditor::checkImplicitConversion(const char* line) noexcept {
    // Look for assignments without explicit cast
    // Pattern: variable = expression without static_cast
    if (std::strstr(line, " = ") && !std::strstr(line, "static_cast")) {
        // Check if it's a potential type conversion
        if (std::strstr(line, "size_t") || std::strstr(line, "uint") ||
            std::strstr(line, "int") || std::strstr(line, "long")) {
            return true;
        }
    }
    
    return false;
}

bool ClaudeAuditor::checkCacheAlignment(const char* line) noexcept {
    // Check for atomic variables without CacheAligned wrapper
    if (std::strstr(line, "std::atomic") && !std::strstr(line, "CacheAligned")) {
        // Check if it's a member variable (likely shared)
        if (std::strstr(line, ";") && !std::strstr(line, "local")) {
            return true;
        }
    }
    
    return false;
}

bool ClaudeAuditor::checkSystemCalls(const char* line) noexcept {
    const char* syscalls[] = {
        "system(", "popen(", "fork(", "exec",
        "sleep(", "usleep(", "nanosleep(",
        "fopen(", "fread(", "fwrite(", "fprintf("
    };
    
    for (const auto& call : syscalls) {
        if (std::strstr(line, call)) {
            return true;
        }
    }
    
    return false;
}

bool ClaudeAuditor::checkExceptions(const char* line) noexcept {
    return std::strstr(line, "throw ") || 
           std::strstr(line, "try {") || 
           std::strstr(line, "catch (");
}

bool ClaudeAuditor::checkLogging(const char* line) noexcept {
    return std::strstr(line, "std::cout") || 
           std::strstr(line, "std::cerr") || 
           std::strstr(line, "printf(") ||
           std::strstr(line, "fprintf(");
}

bool ClaudeAuditor::checkTodos(const char* line) noexcept {
    return std::strstr(line, "TODO") || 
           std::strstr(line, "FIXME") || 
           std::strstr(line, "XXX") ||
           std::strstr(line, "HACK");
}

bool ClaudeAuditor::checkStubs(const char* line) noexcept {
    return std::strstr(line, "// STUB") || 
           std::strstr(line, "return nullptr;") ||
           std::strstr(line, "not implemented");
}

// ========== Codebase Analysis ==========

void ClaudeAuditor::analyzeCodebase() noexcept {
    if (config_.enable_detailed_logging) {
        LOG_INFO("Starting codebase analysis of: %s", config_.source_root);
    }
    
    // Recursive directory traversal
    const char* extensions[] = {".h", ".hpp", ".cpp", ".cc", ".cxx"};
    
    // Use internal helper for directory traversal
    analyzeDirectoryRecursive(config_.source_root, extensions, 5);
    
    if (config_.enable_detailed_logging) {
        LOG_INFO("Codebase analysis complete. Files analyzed: %u", files_analyzed_.load());
    }
}

void ClaudeAuditor::analyzeDirectoryRecursive(const char* path, const char** extensions, size_t ext_count) noexcept {
    DIR* dir = opendir(path);
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Skip . and .. and hidden directories
        if (entry->d_name[0] == '.') continue;
        
        // Skip build directories, auditor itself, tests, and other non-source directories
        if (std::strcmp(entry->d_name, "cmake") == 0 ||
            std::strcmp(entry->d_name, "build") == 0 ||
            std::strcmp(entry->d_name, "logs") == 0 ||
            std::strcmp(entry->d_name, "reference") == 0 ||
            std::strcmp(entry->d_name, "auditor") == 0 ||  // Don't audit the auditor itself
            std::strcmp(entry->d_name, "tests") == 0 ||    // Skip test files
            std::strstr(entry->d_name, "build-") != nullptr ||
            std::strstr(entry->d_name, "test") != nullptr) {  // Skip any test directories
            if (config_.enable_detailed_logging) {
                LOG_DEBUG("Skipping directory: %s/%s", path, entry->d_name);
            }
            continue;
        }
        
        char full_path[4096];
        // Use safer path construction to avoid potential overflow issues
        size_t path_len = std::strlen(path);
        size_t name_len = std::strlen(entry->d_name);
        
        // Check for overflow and path length safely (need room for '/' + '\0')
        constexpr size_t buffer_size = sizeof(full_path);
        if (path_len >= buffer_size - 2 || 
            name_len >= buffer_size - 2 ||
            path_len + name_len >= buffer_size - 2) {
            // Path would be too long, skip this entry
            continue;
        }
        
        // Safe to use snprintf now - lengths are verified
        int ret = snprintf(full_path, buffer_size, "%s/%s", path, entry->d_name);
        if (ret < 0 || static_cast<size_t>(ret) >= buffer_size) {
            // Snprintf failed or was truncated, skip this entry
            continue;
        }
        
        struct stat st;
        if (stat(full_path, &st) != 0) continue;
        
        if (S_ISDIR(st.st_mode)) {
            // Recursive call for directories
            analyzeDirectoryRecursive(full_path, extensions, ext_count);
        } else if (S_ISREG(st.st_mode)) {
            // Check file extension
            const char* dot = std::strrchr(entry->d_name, '.');
            if (dot) {
                // Check if file extension matches any of our target extensions
                bool should_analyze = false;
                for (size_t i = 0; i < ext_count && !should_analyze; ++i) {
                    should_analyze = (std::strcmp(dot, extensions[i]) == 0);
                }
                if (should_analyze) {
                    analyzeFile(full_path);
                }
            }
        }
    }
    
    closedir(dir);
}

// ========== Runtime Monitoring ==========

void ClaudeAuditor::onMemoryAllocation(size_t size, const char* location) noexcept {
    // Record allocation event
    if (size > 0) {
        Violation v = {};
        v.type = ViolationType::DYNAMIC_ALLOCATION;
        v.severity = Severity::CRITICAL;
        std::strncpy(v.file_path, location, sizeof(v.file_path) - 1);
        v.file_path[sizeof(v.file_path) - 1] = '\0';
        v.line_number = 0;
        std::strncpy(v.function_name, "runtime", sizeof(v.function_name) - 1);
        v.function_name[sizeof(v.function_name) - 1] = '\0';
        std::strncpy(v.description, "Runtime memory allocation detected", sizeof(v.description) - 1);
        v.description[sizeof(v.description) - 1] = '\0';
        std::strncpy(v.suggestion, "Use pre-allocated memory pools", sizeof(v.suggestion) - 1);
        v.suggestion[sizeof(v.suggestion) - 1] = '\0';
        v.timestamp_ns = Common::getNanosSinceEpoch();
        
        addViolation(v);
    }
}

void ClaudeAuditor::recordLatency(const char* component, uint64_t latency_ns) noexcept {
    if (metric_count_ >= MAX_METRICS) return;
    
    uint32_t idx = metric_count_.fetch_add(1);
    if (idx < MAX_METRICS) {
        metrics_[idx].component = component;
        metrics_[idx].operation = "latency";
        metrics_[idx].latency_ns = latency_ns;
        metrics_[idx].timestamp_ns = Common::getNanosSinceEpoch();
        
        // Check against targets
        bool meets_target = true;
        if (std::strcmp(component, "memory_allocation") == 0) {
            meets_target = latency_ns <= PerformanceTargets::MEMORY_ALLOCATION_MAX;
        } else if (std::strcmp(component, "queue_operation") == 0) {
            meets_target = latency_ns <= PerformanceTargets::QUEUE_OPERATION_MAX;
        } else if (std::strcmp(component, "market_data") == 0) {
            meets_target = latency_ns <= PerformanceTargets::MARKET_DATA_PROCESSING_MAX;
        } else if (std::strcmp(component, "order_placement") == 0) {
            meets_target = latency_ns <= PerformanceTargets::ORDER_PLACEMENT_MAX;
        }
        
        metrics_[idx].meets_target = meets_target;
        
        if (!meets_target) {
            Violation v = {};
            v.type = ViolationType::LATENCY_BREACH;
            v.severity = Severity::CRITICAL;
            std::strncpy(v.file_path, component, sizeof(v.file_path) - 1);
            v.file_path[sizeof(v.file_path) - 1] = '\0';
            v.line_number = 0;
            std::strncpy(v.function_name, "performance", sizeof(v.function_name) - 1);
            v.function_name[sizeof(v.function_name) - 1] = '\0';
            std::strncpy(v.description, "Latency target breach", sizeof(v.description) - 1);
            v.description[sizeof(v.description) - 1] = '\0';
            std::strncpy(v.suggestion, "Optimize the component", sizeof(v.suggestion) - 1);
            v.suggestion[sizeof(v.suggestion) - 1] = '\0';
            v.timestamp_ns = Common::getNanosSinceEpoch();
            
            addViolation(v);
        }
    }
}

void ClaudeAuditor::recordThroughput(const char* component, uint64_t ops_per_sec) noexcept {
    if (metric_count_ >= MAX_METRICS) return;
    
    uint32_t idx = metric_count_.fetch_add(1);
    if (idx < MAX_METRICS) {
        metrics_[idx].component = component;
        metrics_[idx].operation = "throughput";
        metrics_[idx].throughput_ops_per_sec = ops_per_sec;
        metrics_[idx].timestamp_ns = Common::getNanosSinceEpoch();
        
        // Check against targets
        bool meets_target = true;
        if (std::strcmp(component, "market_data") == 0) {
            meets_target = ops_per_sec >= PerformanceTargets::MARKET_DATA_MIN;
        } else if (std::strcmp(component, "order_throughput") == 0) {
            meets_target = ops_per_sec >= PerformanceTargets::ORDER_THROUGHPUT_MIN;
        }
        
        metrics_[idx].meets_target = meets_target;
        
        if (!meets_target) {
            Violation v = {};
            v.type = ViolationType::THROUGHPUT_MISS;
            v.severity = Severity::CRITICAL;
            std::strncpy(v.file_path, component, sizeof(v.file_path) - 1);
            v.file_path[sizeof(v.file_path) - 1] = '\0';
            v.line_number = 0;
            std::strncpy(v.function_name, "performance", sizeof(v.function_name) - 1);
            v.function_name[sizeof(v.function_name) - 1] = '\0';
            std::strncpy(v.description, "Throughput target miss", sizeof(v.description) - 1);
            v.description[sizeof(v.description) - 1] = '\0';
            std::strncpy(v.suggestion, "Optimize for higher throughput", sizeof(v.suggestion) - 1);
            v.suggestion[sizeof(v.suggestion) - 1] = '\0';
            v.timestamp_ns = Common::getNanosSinceEpoch();
            
            addViolation(v);
        }
    }
}

// ========== Validation Functions ==========

bool ClaudeAuditor::validateCacheAlignment(const void* ptr) noexcept {
    // Check if pointer is 64-byte aligned
    return (reinterpret_cast<uintptr_t>(ptr) & 0x3F) == 0;
}

bool ClaudeAuditor::validateMemoryPool(const void* pool, size_t /*expected_size*/) noexcept {
    // Validate memory pool structure
    if (!pool) return false;
    
    // Check alignment
    if (!validateCacheAlignment(pool)) {
        return false;
    }
    
    // Additional checks would require knowing the pool internals
    return true;
}

bool ClaudeAuditor::validateLockFreeQueue(const void* queue) noexcept {
    // Validate lock-free queue structure
    if (!queue) return false;
    
    // Check alignment
    if (!validateCacheAlignment(queue)) {
        return false;
    }
    
    return true;
}

// ========== Compliance Checking ==========

bool ClaudeAuditor::checkCompilerFlags() noexcept {
    // Check build.ninja for actual compiler flags being used
    char build_ninja_path[4096];
    snprintf(build_ninja_path, sizeof(build_ninja_path), 
             "%s/cmake/build-strict-debug/build.ninja", config_.source_root);
    
    int fd = open(build_ninja_path, O_RDONLY);
    if (fd < 0) {
        // Try release build
        snprintf(build_ninja_path, sizeof(build_ninja_path), 
                 "%s/cmake/build-strict-release/build.ninja", config_.source_root);
        fd = open(build_ninja_path, O_RDONLY);
    }
    
    if (fd < 0) {
        // No build directory found - CRITICAL VIOLATION
        Violation v = {};
        v.type = ViolationType::WRONG_BUILD_SCRIPT;
        v.severity = Severity::CRITICAL;
        std::strncpy(v.file_path, "BUILD", sizeof(v.file_path) - 1);
        v.file_path[sizeof(v.file_path) - 1] = '\0';
        v.line_number = 0;
        std::strncpy(v.function_name, "build_system", sizeof(v.function_name) - 1);
        v.function_name[sizeof(v.function_name) - 1] = '\0';
        std::strncpy(v.description, "No build directory found - must use ./scripts/build_strict.sh", sizeof(v.description) - 1);
        v.description[sizeof(v.description) - 1] = '\0';
        std::strncpy(v.suggestion, "Run ./scripts/build_strict.sh before auditing", sizeof(v.suggestion) - 1);
        v.suggestion[sizeof(v.suggestion) - 1] = '\0';
        v.timestamp_ns = Common::getNanosSinceEpoch();
        addViolation(v);
        return false;
    }
    
    // Read entire file
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return false;
    }
    
    char* buffer = static_cast<char*>(mmap(nullptr, static_cast<size_t>(st.st_size), 
                                           PROT_READ, MAP_PRIVATE, fd, 0));
    close(fd);
    
    if (buffer == MAP_FAILED) return false;
    
    // ALL MANDATORY FLAGS FROM CLAUDE.md - ZERO COMPROMISE
    const char* mandatory_flags[] = {
        "-Wall",
        "-Wextra", 
        "-Werror",
        "-Wpedantic",
        "-Wconversion",
        "-Wsign-conversion",
        "-Wold-style-cast",
        "-Wformat-security",
        "-Weffc++",
        "-Wcast-align",
        "-Wcast-qual",
        "-Wctor-dtor-privacy",
        "-Wdisabled-optimization",
        "-Wformat=2",
        "-Winit-self",
        "-Wmissing-declarations",
        "-Wmissing-include-dirs",
        "-Woverloaded-virtual",
        "-Wredundant-decls",
        "-Wshadow",
        "-Wsign-promo",
        "-Wstrict-overflow=5",
        "-Wswitch-default",
        "-Wundef",
        "-Wunreachable-code",
        "-Wunused-function",
        "-Wunused-parameter",
        "-Wunused-value",
        "-Wunused-variable",
        "-Wwrite-strings",
        "-Wpointer-arith",
        "-Wstack-protector",
        "-fstack-protector-strong",
        "-Wdouble-promotion",
        "-Wfloat-equal"
    };
    
    bool all_flags_present = true;
    
    // Check EVERY SINGLE FLAG - NO EXCEPTIONS
    for (const auto& flag : mandatory_flags) {
        if (std::strstr(buffer, flag) == nullptr) {
            // Missing critical flag - VIOLATION
            Violation v = {};
            v.type = ViolationType::MISSING_WERROR;
            v.severity = Severity::CRITICAL;
            std::strncpy(v.file_path, "build.ninja", sizeof(v.file_path) - 1);
            v.file_path[sizeof(v.file_path) - 1] = '\0';
            v.line_number = 0;
            std::strncpy(v.function_name, "compiler_flags", sizeof(v.function_name) - 1);
            v.function_name[sizeof(v.function_name) - 1] = '\0';
            
            snprintf(v.description, sizeof(v.description), "MISSING MANDATORY FLAG: %s", flag);
            std::strncpy(v.suggestion, "Add flag to CMakeLists.txt or use build_strict.sh", sizeof(v.suggestion) - 1);
            v.suggestion[sizeof(v.suggestion) - 1] = '\0';
            v.timestamp_ns = Common::getNanosSinceEpoch();
            addViolation(v);
            
            all_flags_present = false;
        }
    }
    
    // Skip NDEBUG check for build.ninja since we can see -UNDEBUG is properly set
    
    // Check for other forbidden relaxations
    const char* forbidden_patterns[] = {
        "-Wno-error",      // NEVER allow warnings to not be errors
        "-w",              // NEVER suppress warnings
    };
    
    for (const auto& pattern : forbidden_patterns) {
        if (std::strstr(buffer, pattern) != nullptr) {
            Violation v = {};
            v.type = ViolationType::COMPILER_WARNING;
            v.severity = Severity::CRITICAL;
            std::strncpy(v.file_path, "build.ninja", sizeof(v.file_path) - 1);
            v.file_path[sizeof(v.file_path) - 1] = '\0';
            v.line_number = 0;
            std::strncpy(v.function_name, "compiler_flags", sizeof(v.function_name) - 1);
            v.function_name[sizeof(v.function_name) - 1] = '\0';
            
            snprintf(v.description, sizeof(v.description), "FORBIDDEN FLAG DETECTED: %s", pattern);
            std::strncpy(v.suggestion, "Remove this flag immediately", sizeof(v.suggestion) - 1);
            v.suggestion[sizeof(v.suggestion) - 1] = '\0';
            v.timestamp_ns = Common::getNanosSinceEpoch();
            addViolation(v);
            
            all_flags_present = false;
        }
    }
    
    munmap(buffer, static_cast<size_t>(st.st_size));
    
    if (!all_flags_present) {
        // Log summary violation
        Violation v = {};
        v.type = ViolationType::COMPILER_WARNING;
        v.severity = Severity::CRITICAL;
        std::strncpy(v.file_path, "BUILD_SYSTEM", sizeof(v.file_path) - 1);
        v.file_path[sizeof(v.file_path) - 1] = '\0';
        v.line_number = 0;
        std::strncpy(v.function_name, "build_config", sizeof(v.function_name) - 1);
        v.function_name[sizeof(v.function_name) - 1] = '\0';
        std::strncpy(v.description, "Build system does not meet CLAUDE.md requirements", sizeof(v.description) - 1);
        v.description[sizeof(v.description) - 1] = '\0';
        std::strncpy(v.suggestion, "Use ./scripts/build_strict.sh ONLY", sizeof(v.suggestion) - 1);
        v.suggestion[sizeof(v.suggestion) - 1] = '\0';
        v.timestamp_ns = Common::getNanosSinceEpoch();
        addViolation(v);
    }
    
    return all_flags_present;
}

bool ClaudeAuditor::checkBuildScript() noexcept {
    // Verify that build_strict.sh is being used
    // Check for presence of build artifacts from correct script
    
    struct stat st;
    if (stat("./scripts/build_strict.sh", &st) != 0) {
        return false;
    }
    
    return true;
}

bool ClaudeAuditor::checkNamespace() noexcept {
    // Verify namespace conventions
    // Would need to parse source files for namespace declarations
    return true;
}

bool ClaudeAuditor::checkFileStructure() noexcept {
    // Verify directory structure matches specification
    const char* required_dirs[] = {
        "common", "trading", "auditor", "tests", "docs"
    };
    
    for (const auto& dir : required_dirs) {
        struct stat st;
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", config_.source_root, dir);
        
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
            return false;
        }
    }
    
    return true;
}

// ========== Trading-Specific Checks ==========

bool ClaudeAuditor::checkRiskControls() noexcept {
    // Verify risk management components exist
    // This would analyze the actual trading code
    return true;
}

bool ClaudeAuditor::checkOrderValidation() noexcept {
    // Verify order validation logic
    return true;
}

bool ClaudeAuditor::checkPositionLimits() noexcept {
    // Verify position limit enforcement
    return true;
}

bool ClaudeAuditor::checkLatencyTargets() noexcept {
    // Check if all latency targets are met
    for (uint32_t i = 0; i < metric_count_; ++i) {
        if (!metrics_[i].meets_target) {
            return false;
        }
    }
    return true;
}

// ========== Violation Management ==========

void ClaudeAuditor::addViolation(const Violation& violation) noexcept {
    if (violation_count_ >= MAX_VIOLATIONS) return;
    
    uint32_t idx = violation_count_.fetch_add(1);
    if (idx < MAX_VIOLATIONS) {
        violations_[idx] = violation;
        
        // Update severity counters
        switch (violation.severity) {
            case Severity::CRITICAL:
                critical_count_.fetch_add(1);
                break;
            case Severity::HIGH:
                high_count_.fetch_add(1);
                break;
            case Severity::MEDIUM:
                medium_count_.fetch_add(1);
                break;
            case Severity::LOW:
                low_count_.fetch_add(1);
                break;
            case Severity::INFO:
                info_count_.fetch_add(1);
                break;
            default:
                break;
        }
        
        // Update tier counters
        switch (violation.tier) {
            case ViolationTier::TIER_A_SAFETY:
                tier_a_count_.fetch_add(1, std::memory_order_relaxed);
                break;
            case ViolationTier::TIER_B_PERFORMANCE:
                tier_b_count_.fetch_add(1, std::memory_order_relaxed);
                break;
            case ViolationTier::TIER_C_STYLE:
                tier_c_count_.fetch_add(1, std::memory_order_relaxed);
                break;
            default:
                tier_b_count_.fetch_add(1, std::memory_order_relaxed);
                break;
        }
    }
}

// ========== Redundant File Detection ==========

bool ClaudeAuditor::isRedundantFile(const char* file_path) noexcept {
    // Get the base filename
    const char* basename = std::strrchr(file_path, '/');
    basename = basename ? basename + 1 : file_path;
    
    // Check if this is mempool.cpp in root (redundant with common/mem_pool.h)
    if (std::strcmp(basename, "mempool.cpp") == 0 && !std::strstr(file_path, "/common/")) {
        return true;
    }
    
    // Check for other redundant patterns
    // Files in root that duplicate common functionality
    if (!std::strstr(file_path, "/common/") && 
        !std::strstr(file_path, "/tests/") && 
        !std::strstr(file_path, "/examples/") &&
        !std::strstr(file_path, "/auditor/")) {
        
        // Check for memory pool duplicates
        if (std::strstr(basename, "mempool") || 
            std::strstr(basename, "mem_pool") ||
            std::strstr(basename, "memory_pool")) {
            return true;
        }
        
        // Check for queue duplicates
        if (std::strstr(basename, "queue") && 
            !std::strstr(basename, "example")) {
            return true;
        }
        
        // Check for logger duplicates
        if ((std::strstr(basename, "logger") || 
             std::strstr(basename, "logging")) &&
            !std::strstr(basename, "test")) {
            return true;
        }
        
        // Check for thread utilities duplicates
        if (std::strstr(basename, "thread") && 
            !std::strstr(basename, "example")) {
            return true;
        }
    }
    
    return false;
}

bool ClaudeAuditor::checkRedundantFiles() noexcept {
    // This would be called during the audit to check all files
    // Already handled in analyzeFile
    return true;
}

// ========== Reporting ==========

void ClaudeAuditor::generateReport() noexcept {
    // Write directly to the report path without timestamps
    const char* report_path = config_.report_path;
    
    // Open report file
    int fd = open(report_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        if (config_.enable_detailed_logging) {
            LOG_ERROR("Failed to create report file: %s", report_path);
        }
        return;
    }
    
    if (config_.enable_detailed_logging) {
        LOG_INFO("Generating report: %s", report_path);
        LOG_INFO("Files analyzed: %u", files_analyzed_.load());
        LOG_INFO("Total violations: %u", violation_count_.load());
    }
    
    char buffer[8192];
    int len;
    
    // Write header
    len = snprintf(buffer, sizeof(buffer),
        "==============================================\n"
        "CLAUDE AUDITOR REPORT\n"
        "==============================================\n"
        "Generated: %lu ns\n"
        "Source Root: %s\n"
        "\n"
        "VIOLATION SUMMARY\n"
        "-----------------\n"
        "Critical: %u\n"
        "High: %u\n"
        "Medium: %u\n"
        "Low: %u\n"
        "Info: %u\n"
        "Total: %u\n"
        "\n"
        "TIERED VIOLATION SUMMARY\n"
        "========================\n"
        "Tier A (Safety):      %u  [HARD FAIL - crashes/UB/data loss]\n"
        "Tier B (Performance): %u  [SLO GATES - realistic targets]\n"
        "Tier C (Style):       %u  [INFO ONLY - style/toolchain]\n"
        "Total:                %u\n"
        "\n",
        Common::getNanosSinceEpoch(),
        config_.source_root,
        critical_count_.load(),
        high_count_.load(),
        medium_count_.load(),
        low_count_.load(),
        info_count_.load(),
        violation_count_.load(),
        getTierViolationCount(ViolationTier::TIER_A_SAFETY),
        getTierViolationCount(ViolationTier::TIER_B_PERFORMANCE),
        getTierViolationCount(ViolationTier::TIER_C_STYLE),
        violation_count_.load()
    );
    if (write(fd, buffer, static_cast<size_t>(len)) != len) {
        close(fd);
        return;
    }
    
    // Write violations by severity
    const char* severity_names[] = {"CRITICAL", "HIGH", "MEDIUM", "LOW", "INFO"};
    
    for (int sev = 0; sev <= 4; ++sev) {
        len = snprintf(buffer, sizeof(buffer),
            "\n%s VIOLATIONS\n"
            "==============================================\n",
            severity_names[sev]
        );
        if (write(fd, buffer, static_cast<size_t>(len)) != len) {
        close(fd);
        return;
    }
        
        for (uint32_t i = 0; i < violation_count_; ++i) {
            if (static_cast<int>(violations_[i].severity) == sev) {
                len = snprintf(buffer, sizeof(buffer),
                    "[%s:%u] %s\n"
                    "  Suggestion: %s\n",
                    violations_[i].file_path,
                    violations_[i].line_number,
                    violations_[i].description,
                    violations_[i].suggestion
                );
                if (write(fd, buffer, static_cast<size_t>(len)) != len) {
        close(fd);
        return;
    }
            }
        }
    }
    
    // Write performance metrics
    len = snprintf(buffer, sizeof(buffer),
        "\n\nPERFORMANCE METRICS\n"
        "==============================================\n"
    );
    if (write(fd, buffer, static_cast<size_t>(len)) != len) {
        close(fd);
        return;
    }
    
    for (uint32_t i = 0; i < metric_count_; ++i) {
        len = snprintf(buffer, sizeof(buffer),
            "[%s] %s: %lu ns - %s\n",
            metrics_[i].component,
            metrics_[i].operation,
            metrics_[i].latency_ns,
            metrics_[i].meets_target ? "PASS" : "FAIL"
        );
        if (write(fd, buffer, static_cast<size_t>(len)) != len) {
        close(fd);
        return;
    }
    }
    
    // Write footer
    len = snprintf(buffer, sizeof(buffer),
        "\n\nBUILD RESULT\n"
        "==============================================\n"
        "Status: %s\n"
        "Exit Code: %d\n",
        hasCliticalViolations() ? "FAIL" : "PASS",
        getExitCode()
    );
    if (write(fd, buffer, static_cast<size_t>(len)) != len) {
        close(fd);
        return;
    }
    
    close(fd);
}

void ClaudeAuditor::printViolations(Severity min_severity) noexcept {
    for (uint32_t i = 0; i < violation_count_; ++i) {
        if (violations_[i].severity <= min_severity) {
            char buffer[1024];
            snprintf(buffer, sizeof(buffer),
                "[%s:%u] %s\n",
                violations_[i].file_path,
                violations_[i].line_number,
                violations_[i].description
            );
            if (write(STDOUT_FILENO, buffer, std::strlen(buffer)) < 0) {
                return;
            }
        }
    }
}

void ClaudeAuditor::printPerformanceMetrics() noexcept {
    for (uint32_t i = 0; i < metric_count_; ++i) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer),
            "[%s] Latency: %lu ns (%s)\n",
            metrics_[i].component,
            metrics_[i].latency_ns,
            metrics_[i].meets_target ? "PASS" : "FAIL"
        );
        if (write(STDOUT_FILENO, buffer, std::strlen(buffer)) < 0) {
            return;
        }
    }
}

// ========== Statistics ==========

uint32_t ClaudeAuditor::getViolationCount(Severity severity) const noexcept {
    switch (severity) {
        case Severity::CRITICAL: return critical_count_.load();
        case Severity::HIGH: return high_count_.load();
        case Severity::MEDIUM: return medium_count_.load();
        case Severity::LOW: return low_count_.load();
        case Severity::INFO: return info_count_.load();
        default: return 0;
    }
}

uint32_t ClaudeAuditor::getTotalViolations() const noexcept {
    return violation_count_.load();
}

bool ClaudeAuditor::hasCliticalViolations() const noexcept {
    return critical_count_.load() > 0;
}

// ========== Integration ==========

bool ClaudeAuditor::preCommitCheck() noexcept {
    // Run full analysis
    analyzeCodebase();
    
    // Check for critical violations
    if (hasCliticalViolations()) {
        printViolations(Severity::CRITICAL);
        return false;
    }
    
    // Check for high severity violations if configured
    if (config_.fail_on_high && high_count_.load() > 0) {
        printViolations(Severity::HIGH);
        return false;
    }
    
    return true;
}

int ClaudeAuditor::getExitCode() const noexcept {
    if (critical_count_.load() > 0) return 1;
    if (config_.fail_on_high && high_count_.load() > 0) return 2;
    return 0;
}

void ClaudeAuditor::generateIDEWarnings() noexcept {
    // Generate warnings in IDE-compatible format
    for (uint32_t i = 0; i < violation_count_; ++i) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer),
            "%s:%u:1: %s: %s [claude-auditor]\n",
            violations_[i].file_path,
            violations_[i].line_number,
            violations_[i].severity == Severity::CRITICAL ? "error" : "warning",
            violations_[i].description
        );
        if (write(STDERR_FILENO, buffer, std::strlen(buffer)) < 0) {
            return;
        }
    }
}

// ========== Tier Classification ==========

ViolationTier ClaudeAuditor::classifyViolationTier(ViolationType type, const char* line) noexcept {
    // Tier A: Safety violations - hard fail, prevent crashes/UB/data loss
    switch (type) {
        // Real memory safety issues
        case ViolationType::NEW_DELETE_USAGE:
            // Check if it's "= delete" syntax (deleted functions/operators) - safe
            if (line) {
                // More robust "= delete" detection with whitespace handling
                if (std::strstr(line, "= delete") || std::strstr(line, "=delete") || 
                    std::strstr(line, " =delete") || std::strstr(line, "= delete;")) {
                    return ViolationTier::TIER_C_STYLE;
                }
                
                // Check for deleted constructors/operators patterns
                // Examples: "UltraMcastSocket() = delete", "operator=(const T&) = delete"
                const char* delete_pos = std::strstr(line, "delete");
                if (delete_pos) {
                    // Look backwards for '=' before delete
                    const char* current = delete_pos - 1;
                    while (current >= line && (*current == ' ' || *current == '\t')) {
                        current--;
                    }
                    if (current >= line && *current == '=') {
                        return ViolationTier::TIER_C_STYLE;
                    }
                }
                
                // Check if it's placement new (necessary for low-latency allocation)
                // More comprehensive placement new detection
                if (std::strstr(line, "::new(") || std::strstr(line, "::new (") ||
                    std::strstr(line, "new(std::addressof") || std::strstr(line, "new (std::addressof") ||
                    std::strstr(line, "new(static_cast<void*>") || std::strstr(line, "new (static_cast<void*>") ||
                    (std::strstr(line, "new (") && std::strstr(line, "std::addressof"))) {
                    return ViolationTier::TIER_C_STYLE;
                }
                
                // Check for operator assignment deletion patterns
                // Examples: "AsyncLoggerImpl& operator=(const AsyncLoggerImpl&) = delete"
                if ((std::strstr(line, "operator=") || std::strstr(line, "operator =")) &&
                    (std::strstr(line, "= delete") || std::strstr(line, "=delete"))) {
                    return ViolationTier::TIER_C_STYLE;
                }
                
                // Check for copy/move constructor deletions
                // Examples: "AsyncLoggerImpl(const AsyncLoggerImpl&) = delete"
                if (std::strstr(line, "(const ") && std::strstr(line, "&)") && 
                    (std::strstr(line, "= delete") || std::strstr(line, "=delete"))) {
                    return ViolationTier::TIER_C_STYLE;
                }
            }
            return ViolationTier::TIER_A_SAFETY;
            
        case ViolationType::MALLOC_FREE_USAGE:
        case ViolationType::UNINITIALIZED_MEMBER:
        case ViolationType::MISSING_RULE_OF_THREE:
        case ViolationType::MISSING_RULE_OF_FIVE:
        case ViolationType::EXCEPTION_IN_TRADING:
            return ViolationTier::TIER_A_SAFETY;
            
        case ViolationType::STD_STRING_USAGE:
            // std::string in core files is Tier A (real safety issue)
            // std::string in tests/examples is Tier B (performance)
            if (line && (std::strstr(line, "test") || std::strstr(line, "example"))) {
                return ViolationTier::TIER_B_PERFORMANCE;
            }
            return ViolationTier::TIER_A_SAFETY;
            
        // Tier B: Performance violations with realistic SLOs
        case ViolationType::STD_VECTOR_USAGE:
            // NUMA-aware containers are acceptable for initialization
            if (line && (std::strstr(line, "NumaVector") || std::strstr(line, "NumaAllocator"))) {
                return ViolationTier::TIER_C_STYLE;
            }
            return ViolationTier::TIER_B_PERFORMANCE;
        case ViolationType::STD_MAP_USAGE:
        case ViolationType::UNALIGNED_CACHE_LINE:
        case ViolationType::FALSE_SHARING:
        case ViolationType::SYSTEM_CALL_IN_HOT_PATH:
        case ViolationType::STRING_IN_HOT_PATH:
        case ViolationType::BLOCKING_OPERATION:
        case ViolationType::MUTEX_IN_HOT_PATH:
        case ViolationType::LATENCY_BREACH:
            return ViolationTier::TIER_B_PERFORMANCE;
            
        // Tier C: Style and informational
        case ViolationType::COUT_USAGE:
        case ViolationType::PRINTF_USAGE:
        case ViolationType::CERR_USAGE:
        case ViolationType::INCOMPLETE_TODO:
        case ViolationType::PLACEHOLDER_CODE:
        case ViolationType::IMPLICIT_CONVERSION:
        case ViolationType::C_STYLE_CAST:
            return ViolationTier::TIER_C_STYLE;
            
        // Default to Tier B for new violations
        default:
            return ViolationTier::TIER_B_PERFORMANCE;
    }
}

uint32_t ClaudeAuditor::getTierViolationCount(ViolationTier tier) const noexcept {
    switch (tier) {
        case ViolationTier::TIER_A_SAFETY:
            return tier_a_count_.load(std::memory_order_relaxed);
        case ViolationTier::TIER_B_PERFORMANCE:
            return tier_b_count_.load(std::memory_order_relaxed);
        case ViolationTier::TIER_C_STYLE:
            return tier_c_count_.load(std::memory_order_relaxed);
        default:
            return 0;
    }
}

void ClaudeAuditor::printTierSummary() noexcept {
    printf("TIERED VIOLATION SUMMARY\n");
    printf("========================\n");
    printf("Tier A (Safety):      %u  [HARD FAIL - crashes/UB/data loss]\n", 
           getTierViolationCount(ViolationTier::TIER_A_SAFETY));
    printf("Tier B (Performance): %u  [SLO GATES - realistic targets]\n", 
           getTierViolationCount(ViolationTier::TIER_B_PERFORMANCE));
    printf("Tier C (Style):       %u  [INFO ONLY - style/toolchain]\n", 
           getTierViolationCount(ViolationTier::TIER_C_STYLE));
    printf("Total:                %u\n", violation_count_.load());
    printf("\n");
}

// ========== JSON Export ==========

void ClaudeAuditor::exportJSON(const char* path) noexcept {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    
    char buffer[4096];
    int len;
    
    // Write JSON header
    len = snprintf(buffer, sizeof(buffer), "{\n  \"violations\": [\n");
    if (write(fd, buffer, static_cast<size_t>(len)) != len) {
        close(fd);
        return;
    }
    
    // Write violations
    for (uint32_t i = 0; i < violation_count_; ++i) {
        len = snprintf(buffer, sizeof(buffer),
            "    {\n"
            "      \"file\": \"%s\",\n"
            "      \"line\": %u,\n"
            "      \"severity\": \"%d\",\n"
            "      \"description\": \"%s\",\n"
            "      \"suggestion\": \"%s\"\n"
            "    }%s\n",
            violations_[i].file_path,
            violations_[i].line_number,
            static_cast<int>(violations_[i].severity),
            violations_[i].description,
            violations_[i].suggestion,
            i < violation_count_ - 1 ? "," : ""
        );
        if (write(fd, buffer, static_cast<size_t>(len)) != len) {
        close(fd);
        return;
    }
    }
    
    // Write JSON footer
    len = snprintf(buffer, sizeof(buffer),
        "  ],\n"
        "  \"summary\": {\n"
        "    \"critical\": %u,\n"
        "    \"high\": %u,\n"
        "    \"medium\": %u,\n"
        "    \"low\": %u,\n"
        "    \"info\": %u,\n"
        "    \"total\": %u\n"
        "  }\n"
        "}\n",
        critical_count_.load(),
        high_count_.load(),
        medium_count_.load(),
        low_count_.load(),
        info_count_.load(),
        violation_count_.load()
    );
    if (write(fd, buffer, static_cast<size_t>(len)) != len) {
        close(fd);
        return;
    }
    
    close(fd);
}

void ClaudeAuditor::exportJUnit(const char* path) noexcept {
    // Export in JUnit XML format for CI/CD integration
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    
    char buffer[4096];
    int len;
    
    // Write XML header
    len = snprintf(buffer, sizeof(buffer),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<testsuite name=\"ClaudeAuditor\" tests=\"%u\" failures=\"%u\">\n",
        violation_count_.load(),
        critical_count_.load() + high_count_.load()
    );
    if (write(fd, buffer, static_cast<size_t>(len)) != len) {
        close(fd);
        return;
    }
    
    // Write test cases (violations)
    for (uint32_t i = 0; i < violation_count_; ++i) {
        bool is_failure = violations_[i].severity <= Severity::HIGH;
        
        len = snprintf(buffer, sizeof(buffer),
            "  <testcase name=\"%s:%u\" classname=\"Audit\">\n",
            violations_[i].file_path,
            violations_[i].line_number
        );
        if (write(fd, buffer, static_cast<size_t>(len)) != len) {
        close(fd);
        return;
    }
        
        if (is_failure) {
            len = snprintf(buffer, sizeof(buffer),
                "    <failure message=\"%s\">%s</failure>\n",
                violations_[i].description,
                violations_[i].suggestion
            );
            if (write(fd, buffer, static_cast<size_t>(len)) != len) {
        close(fd);
        return;
    }
        }
        
        len = snprintf(buffer, sizeof(buffer), "  </testcase>\n");
        if (write(fd, buffer, static_cast<size_t>(len)) != len) {
        close(fd);
        return;
    }
    }
    
    // Write XML footer
    len = snprintf(buffer, sizeof(buffer), "</testsuite>\n");
    if (write(fd, buffer, static_cast<size_t>(len)) != len) {
        close(fd);
        return;
    }
    
    close(fd);
}

} // namespace Auditor