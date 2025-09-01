#include "claude_auditor.h"
#include <cstring>
#include <cstdio>
#include <unistd.h>

static void printUsage(const char* program) {
    const char* usage = R"(
USAGE: %s [OPTIONS]

Claude Auditor - Ultra-Low Latency C++ Code Compliance Checker

OPTIONS:
    --source <path>         Source directory to audit (default: current dir)
    --report <path>         Output report path (default: /home/isoula/om/shriven_zenith/logs/audit/audit_report.txt)
    --json <path>           Export violations as JSON
    --junit <path>          Export as JUnit XML for CI/CD
    --fail-on-critical      Exit with error on critical violations (default)
    --fail-on-high          Exit with error on high severity violations
    --no-static             Skip static code analysis
    --no-performance        Skip performance tracking
    --verbose               Print all violations to stdout
    --ide                   Output IDE-compatible warnings
    --pre-commit            Run as pre-commit hook
    --help                  Show this help message

EXAMPLES:
    # Basic audit of current directory
    %s

    # Audit specific directory with JSON output
    %s --source /path/to/code --json violations.json

    # Pre-commit hook mode
    %s --pre-commit --fail-on-high

    # CI/CD integration with JUnit output
    %s --source . --junit test-results.xml

EXIT CODES:
    0 - No critical violations
    1 - Critical violations found
    2 - High severity violations found (with --fail-on-high)
    3 - Configuration error

VIOLATION SEVERITIES:
    CRITICAL - Build-breaking (dynamic allocation, exceptions)
    HIGH     - Performance-impacting (strings, unaligned data)
    MEDIUM   - Best practices (missing const, implicit conversions)
    LOW      - Style violations (naming conventions)
    INFO     - Informational (TODOs, stubs)

PERFORMANCE TARGETS:
    Memory Allocation: < 50ns
    Queue Operations:  < 100ns
    Market Data:       < 1μs
    Order Placement:   < 10μs
    Risk Checks:       < 100ns

)";
    
    fprintf(stdout, "%s", usage);
    fprintf(stdout, "\nProgram: %s\n", program);
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    Auditor::ClaudeAuditor::Config config;
    
    bool verbose = false;
    bool ide_mode = false;
    bool pre_commit = false;
    const char* json_path = nullptr;
    const char* junit_path = nullptr;
    
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
        else if (std::strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
            config.source_root = argv[++i];
        }
        else if (std::strcmp(argv[i], "--report") == 0 && i + 1 < argc) {
            config.report_path = argv[++i];
        }
        else if (std::strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            json_path = argv[++i];
        }
        else if (std::strcmp(argv[i], "--junit") == 0 && i + 1 < argc) {
            junit_path = argv[++i];
        }
        else if (std::strcmp(argv[i], "--fail-on-critical") == 0) {
            config.fail_on_critical = true;
        }
        else if (std::strcmp(argv[i], "--fail-on-high") == 0) {
            config.fail_on_high = true;
        }
        else if (std::strcmp(argv[i], "--no-static") == 0) {
            config.enable_static_analysis = false;
        }
        else if (std::strcmp(argv[i], "--no-performance") == 0) {
            config.enable_performance_tracking = false;
        }
        else if (std::strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        }
        else if (std::strcmp(argv[i], "--ide") == 0) {
            ide_mode = true;
        }
        else if (std::strcmp(argv[i], "--pre-commit") == 0) {
            pre_commit = true;
        }
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage(argv[0]);
            return 3;
        }
    }
    
    // Print header
    if (!ide_mode && !pre_commit) {
        printf("==============================================\n");
        printf("CLAUDE AUDITOR - Ultra-Low Latency Compliance\n");
        printf("==============================================\n");
        printf("Source: %s\n", config.source_root);
        printf("Report: %s\n", config.report_path);
        printf("\n");
    }
    
    // Create auditor instance (static to avoid stack overflow)
    static Auditor::ClaudeAuditor auditor(config);
    
    // Run static analysis
    if (config.enable_static_analysis) {
        if (!pre_commit && !ide_mode) {
            printf("Running static analysis...\n");
        }
        auditor.analyzeCodebase();
    }
    
    // Check compliance
    if (!pre_commit && !ide_mode) {
        printf("Checking compliance...\n");
    }
    
    bool compiler_ok = auditor.checkCompilerFlags();
    bool build_ok = auditor.checkBuildScript();
    bool namespace_ok = auditor.checkNamespace();
    bool structure_ok = auditor.checkFileStructure();
    
    if (!pre_commit && !ide_mode) {
        printf("  Compiler flags: %s\n", compiler_ok ? "PASS" : "FAIL");
        printf("  Build script:   %s\n", build_ok ? "PASS" : "FAIL");
        printf("  Namespaces:     %s\n", namespace_ok ? "PASS" : "FAIL");
        printf("  File structure: %s\n", structure_ok ? "PASS" : "FAIL");
        printf("\n");
    }
    
    // Print summary
    if (!pre_commit && !ide_mode) {
        printf("VIOLATION SUMMARY\n");
        printf("-----------------\n");
        printf("Critical: %u\n", auditor.getViolationCount(Auditor::Severity::CRITICAL));
        printf("High:     %u\n", auditor.getViolationCount(Auditor::Severity::HIGH));
        printf("Medium:   %u\n", auditor.getViolationCount(Auditor::Severity::MEDIUM));
        printf("Low:      %u\n", auditor.getViolationCount(Auditor::Severity::LOW));
        printf("Info:     %u\n", auditor.getViolationCount(Auditor::Severity::INFO));
        printf("Total:    %u\n", auditor.getTotalViolations());
        printf("\n");
        
        // Show tiered summary
        auditor.printTierSummary();
    }
    
    // Handle different output modes
    if (pre_commit) {
        // Pre-commit mode - only show critical/high violations
        if (!auditor.preCommitCheck()) {
            printf("\n❌ Pre-commit check FAILED\n");
            printf("Fix the violations above before committing.\n");
            return auditor.getExitCode();
        }
        printf("✅ Pre-commit check PASSED\n");
    }
    else if (ide_mode) {
        // IDE mode - generate IDE-compatible warnings
        auditor.generateIDEWarnings();
    }
    else if (verbose) {
        // Verbose mode - print all violations
        printf("VIOLATIONS\n");
        printf("----------\n");
        auditor.printViolations(Auditor::Severity::INFO);
        printf("\n");
        
        printf("PERFORMANCE METRICS\n");
        printf("-------------------\n");
        auditor.printPerformanceMetrics();
    }
    
    // Export reports
    if (json_path) {
        auditor.exportJSON(json_path);
        if (!pre_commit && !ide_mode) {
            printf("JSON report exported to: %s\n", json_path);
        }
    }
    
    if (junit_path) {
        auditor.exportJUnit(junit_path);
        if (!pre_commit && !ide_mode) {
            printf("JUnit report exported to: %s\n", junit_path);
        }
    }
    
    // Generate main report
    auditor.generateReport();
    
    // Final result
    int exit_code = auditor.getExitCode();
    
    if (!pre_commit && !ide_mode) {
        printf("\n");
        printf("==============================================\n");
        if (exit_code == 0) {
            printf("✅ AUDIT PASSED - No critical violations\n");
        } else {
            printf("❌ AUDIT FAILED - Exit code: %d\n", exit_code);
            printf("See %s for details\n", config.report_path);
        }
        printf("==============================================\n");
    }
    
    return exit_code;
}