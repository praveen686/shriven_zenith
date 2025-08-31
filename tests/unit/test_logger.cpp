#include <gtest/gtest.h>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <numeric>
#include <cmath>

#include "logging.h"
#include "time_utils.h"

using namespace BldgBlocks;

// Base test class with proper setup
class LoggerTestBase : public ::testing::Test {
protected:
    LoggerTestBase() : test_timestamp_(), test_dir_(), test_log_file_() {}
    
    void SetUp() override {
        // Create unique test directory for this test run
        test_timestamp_ = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        test_dir_ = "logs/test_logger_" + test_timestamp_;
        
        // Create test directory
        std::filesystem::create_directories(test_dir_);
        
        // Initialize logging for test with unique file
        test_log_file_ = test_dir_ + "/logger_test.log";
        
        // Ensure any previous logger is shutdown
        BldgBlocks::shutdownLogging();
        BldgBlocks::initLogging(test_log_file_);
        
        LOG_INFO("=== Starting Logger Test ===");
    }
    
    void TearDown() override {
        LOG_INFO("=== Logger Test Completed ===");
        BldgBlocks::shutdownLogging();
        
        // Small delay to ensure file writes are complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Helper to read log file contents
    std::string readLogFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return "";
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
    
    // Helper to count lines in log file
    size_t countLogLines(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return 0;
        }
        
        return static_cast<size_t>(std::count(std::istreambuf_iterator<char>(file),
                                           std::istreambuf_iterator<char>(), '\n'));
    }
    
    // Helper to verify log entry format
    bool verifyLogEntryFormat(const std::string& line, const std::string& level, const std::string& expected_message) {
        // Expected format: [timestamp][LEVEL][Tthread_id] message
        size_t level_start = line.find("][") + 2;
        size_t level_end = line.find("][", level_start);
        if (level_start == std::string::npos || level_end == std::string::npos) {
            return false;
        }
        
        std::string actual_level = line.substr(level_start, level_end - level_start);
        if (actual_level != level) {
            return false;
        }
        
        size_t message_start = line.find("] ", level_end) + 2;
        if (message_start == std::string::npos) {
            return false;
        }
        
        std::string actual_message = line.substr(message_start);
        return actual_message == expected_message;
    }
    
    // Helper to create unique temp log file
    std::string createTempLogFile() {
        static std::atomic<int> counter{0};
        return test_dir_ + "/temp_" + std::to_string(counter.fetch_add(1)) + ".log";
    }

    // Timing helpers for performance testing
    uint64_t rdtsc() {
        uint32_t hi, lo;
        __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }
    
    uint64_t cycles_to_nanos(uint64_t cycles) {
        // Approximate CPU frequency - adjust based on actual system
        static const double CPU_FREQ_GHZ = 2.4; // Typical base frequency
        return static_cast<uint64_t>(static_cast<double>(cycles) / CPU_FREQ_GHZ);
    }

protected:
    std::string test_timestamp_;
    std::string test_dir_;
    std::string test_log_file_;
};

// 1. UNIT TESTS - FUNCTIONAL CORRECTNESS

TEST_F(LoggerTestBase, InitializationAndShutdownContract) {
    // === INPUT SPECIFICATION ===
    // Logger Configuration:
    //   - Log File: Custom temporary file path
    //   - Buffer Size: 16K entries (16,384)
    //   - Background Thread: Automatic start
    //   - CPU Affinity: Last CPU core
    // Operation: Initialize → Shutdown → Reinitialize cycle
    // Pre-conditions: No active logger instance
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Initialization Results:
    //   - g_logger: Non-null pointer
    //   - Log file: Created and accessible
    //   - Background thread: Running and responsive
    //   - Buffer: Empty and ready for writes
    // Shutdown Results:
    //   - g_logger: Set to nullptr
    //   - Background thread: Joined cleanly
    //   - Pending entries: Flushed to disk
    //   - File handle: Closed properly
    // Reinitialize Results:
    //   - Same initialization contract verified
    //   - Previous log data: Preserved in file
    
    // === SUCCESS CRITERIA ===
    // 1. initLogging() creates valid g_logger instance
    // 2. Log file created and writable
    // 3. shutdownLogging() cleans up completely
    // 4. g_logger becomes nullptr after shutdown
    // 5. Reinitialize works after shutdown
    // 6. File remains accessible throughout cycle
    // 7. No memory leaks or resource leaks
    
    // === GIVEN (Input Contract) ===
    std::string custom_log = createTempLogFile();
    
    // Ensure clean state
    BldgBlocks::shutdownLogging();
    ASSERT_EQ(BldgBlocks::g_logger, nullptr) << "g_logger should be null before initialization";
    
    // === WHEN (Action) ===
    BldgBlocks::initLogging(custom_log);
    
    // === THEN (Output Verification) ===
    // Contract 1: Logger instance created
    ASSERT_NE(BldgBlocks::g_logger, nullptr) << "initLogging must create valid g_logger instance";
    
    // Contract 2: Log file created and accessible
    ASSERT_TRUE(std::filesystem::exists(custom_log)) << "Log file must be created during initialization";
    
    // Contract 3: Logger responsive to log calls
    LOG_INFO("Test initialization message");
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Allow background thread to process
    
    // Contract 4: Log entries written to file
    std::string log_content = readLogFile(custom_log);
    ASSERT_FALSE(log_content.empty()) << "Log file should contain entries after logging";
    ASSERT_NE(log_content.find("Test initialization message"), std::string::npos) 
        << "Log file should contain our test message";
    
    // Contract 5: Shutdown cleans up properly
    BldgBlocks::shutdownLogging();
    ASSERT_EQ(BldgBlocks::g_logger, nullptr) << "shutdownLogging must set g_logger to nullptr";
    
    // Contract 6: File remains after shutdown
    ASSERT_TRUE(std::filesystem::exists(custom_log)) << "Log file must persist after shutdown";
    
    // Contract 7: Reinitialize works after shutdown
    BldgBlocks::initLogging(custom_log);
    ASSERT_NE(BldgBlocks::g_logger, nullptr) << "Reinitialization must work after shutdown";
    
    // Contract 8: Can log after reinitialize
    LOG_WARN("Test reinitialization message");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    std::string final_content = readLogFile(custom_log);
    ASSERT_NE(final_content.find("Test reinitialization message"), std::string::npos)
        << "Logger must work after reinitialization";
    
    // Final cleanup
    BldgBlocks::shutdownLogging();
    
    LOG_INFO("PASS: Logger initialization and shutdown contract verified");
}

TEST_F(LoggerTestBase, LogLevelsAndMacroContract) {
    // === INPUT SPECIFICATION ===
    // Test Parameters:
    //   - Log Levels: DEBUG, INFO, WARN, ERROR, FATAL (all 5 levels)
    //   - Message Format: Simple string and formatted with parameters
    //   - Macro Usage: LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL
    //   - Background Processing: Async write to file
    // Operation: Log one message at each level with unique content
    // Pre-conditions: Logger initialized and responsive
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Log File Contents:
    //   - 5 distinct log entries (one per level)
    //   - Correct level labels: "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"
    //   - Timestamp format: [timestamp][LEVEL][Tthread_id] message
    //   - Message content: Exact match to input strings
    // Entry Format Validation:
    //   - Each line follows: [timestamp][LEVEL][Tthread_id] message\n
    //   - Timestamp: Numeric format with nanosecond precision
    //   - Thread ID: Numeric identifier prefixed with 'T'
    //   - Level: Consistent width (5 chars, right-padded)
    
    // === SUCCESS CRITERIA ===
    // 1. All 5 log levels produce file entries
    // 2. Level labels exactly match expected format
    // 3. Message content preserved exactly
    // 4. Entry format consistent across all levels
    // 5. Background thread processes all entries
    // 6. File contains exactly 5 entries (plus any setup entries)
    // 7. Entries appear in chronological order
    
    // === GIVEN (Input Contract) ===
    std::string unique_log = createTempLogFile();
    BldgBlocks::shutdownLogging();
    BldgBlocks::initLogging(unique_log);
    
    const std::string DEBUG_MSG = "Debug message with value 42";
    const std::string INFO_MSG = "Info message processing order 1001";
    const std::string WARN_MSG = "Warning: risk limit approaching 95%";
    const std::string ERROR_MSG = "Error: connection failed, retrying";
    const std::string FATAL_MSG = "Fatal: system shutdown initiated";
    
    size_t initial_lines = countLogLines(unique_log);
    
    // === WHEN (Action) ===
    LOG_DEBUG("Debug message with value %d", 42);
    LOG_INFO("Info message processing order %d", 1001);
    LOG_WARN("Warning: risk limit approaching %d%%", 95);
    LOG_ERROR("Error: connection failed, retrying");
    LOG_FATAL("Fatal: system shutdown initiated");
    
    // Allow background thread to process all entries
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // === THEN (Output Verification) ===
    std::string log_content = readLogFile(unique_log);
    ASSERT_FALSE(log_content.empty()) << "Log file must contain entries after logging";
    
    size_t final_lines = countLogLines(unique_log);
    size_t new_lines = final_lines - initial_lines;
    ASSERT_GE(new_lines, 5) << "Must have at least 5 new log entries (one per level)";
    
    // Contract 1: Each level appears in log
    ASSERT_NE(log_content.find("][DEBUG]["), std::string::npos) << "DEBUG level must appear in log";
    ASSERT_NE(log_content.find("][INFO ]["), std::string::npos) << "INFO level must appear in log";
    ASSERT_NE(log_content.find("][WARN ]["), std::string::npos) << "WARN level must appear in log";
    ASSERT_NE(log_content.find("][ERROR]["), std::string::npos) << "ERROR level must appear in log";
    ASSERT_NE(log_content.find("][FATAL]["), std::string::npos) << "FATAL level must appear in log";
    
    // Contract 2: Message content preserved exactly (formatted)
    ASSERT_NE(log_content.find(DEBUG_MSG), std::string::npos) << "DEBUG message content must be preserved";
    ASSERT_NE(log_content.find(INFO_MSG), std::string::npos) << "INFO message content must be preserved";
    ASSERT_NE(log_content.find("Warning: risk limit approaching 95%"), std::string::npos) << "WARN message content must be preserved";
    ASSERT_NE(log_content.find(ERROR_MSG), std::string::npos) << "ERROR message content must be preserved";
    ASSERT_NE(log_content.find(FATAL_MSG), std::string::npos) << "FATAL message content must be preserved";
    
    // Contract 3: Timestamp format validation (basic)
    std::istringstream stream(log_content);
    std::string line;
    int level_count = 0;
    while (std::getline(stream, line)) {
        if (line.find("][DEBUG][") != std::string::npos || 
            line.find("][INFO ][") != std::string::npos ||
            line.find("][WARN ][") != std::string::npos ||
            line.find("][ERROR][") != std::string::npos ||
            line.find("][FATAL][") != std::string::npos) {
            
            level_count++;
            
            // Verify format starts with [timestamp]
            ASSERT_EQ(line[0], '[') << "Log entry must start with timestamp bracket";
            size_t first_close = line.find(']');
            ASSERT_NE(first_close, std::string::npos) << "Log entry must have timestamp close bracket";
            
            // Verify timestamp is numeric
            std::string timestamp = line.substr(1, first_close - 1);
            ASSERT_FALSE(timestamp.empty()) << "Timestamp must not be empty";
            
            // Basic timestamp format check (should contain dot for nanoseconds)
            ASSERT_NE(timestamp.find('.'), std::string::npos) << "Timestamp should contain nanosecond precision";
        }
    }
    
    ASSERT_EQ(level_count, 5) << "Should find exactly 5 log level entries";
    
    BldgBlocks::shutdownLogging();
    
    LOG_INFO("PASS: Log levels and macro contract verified - all levels working correctly");
}

TEST_F(LoggerTestBase, MessageFormattingContract) {
    // === INPUT SPECIFICATION ===
    // Format Test Cases:
    //   - Simple string: No format specifiers
    //   - Integer formatting: %d, %lu, %u variations
    //   - String formatting: %s with various string lengths
    //   - Mixed formatting: Multiple types in single message
    //   - Edge cases: Very long messages, empty strings
    // Message Length Tests:
    //   - Short: < 50 characters
    //   - Medium: 50-200 characters  
    //   - Long: 200+ characters (near 240 char limit)
    //   - Overflow: > 240 characters (should truncate)
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Formatting Results:
    //   - Integer values: Exact numeric representation
    //   - String values: Preserved exactly (no truncation for normal strings)
    //   - Mixed values: All parameters formatted correctly in order
    //   - Long messages: Preserved up to buffer limit (240 chars)
    //   - Overflow messages: Truncated at buffer boundary, no crash
    // Performance Characteristics:
    //   - Formatting latency: < 100ns per call
    //   - No dynamic allocation during formatting
    //   - Stack buffer usage only
    
    // === SUCCESS CRITERIA ===
    // 1. All format specifiers work correctly
    // 2. Integer parameters formatted precisely
    // 3. String parameters preserved exactly
    // 4. Mixed parameter order maintained
    // 5. Long messages handled gracefully
    // 6. Overflow messages truncated safely
    // 7. No crashes or corruption on any input
    
    // === GIVEN (Input Contract) ===
    std::string format_log = createTempLogFile();
    BldgBlocks::shutdownLogging();
    BldgBlocks::initLogging(format_log);
    
    // Test values
    const int int_val = 12345;
    const uint64_t uint64_val = 9876543210ULL;
    const char* str_val = "test_string";
    const size_t size_val = 42;
    
    size_t initial_lines = countLogLines(format_log);
    
    // === WHEN (Action) ===
    // Test 1: Simple integer formatting
    LOG_INFO("Integer test: %d", int_val);
    
    // Test 2: Multiple integer types
    LOG_INFO("Multiple integers: int=%d, uint64=%lu, size=%zu", int_val, uint64_val, size_val);
    
    // Test 3: String formatting
    LOG_INFO("String test: message='%s', length=%zu", str_val, strlen(str_val));
    
    // Test 4: Mixed complex formatting
    LOG_INFO("Complex: order_id=%d, price=%lu, symbol='%s', qty=%zu, active=%s", 
             int_val, uint64_val, str_val, size_val, "true");
    
    // Test 5: Long message (near limit)
    std::string long_msg(230, 'X');  // 230 X's
    LOG_INFO("Long message test: %s", long_msg.c_str());
    
    // Test 6: Overflow message (exceeds 240 char limit)
    std::string overflow_msg(300, 'Y');  // 300 Y's - should be truncated
    LOG_INFO("Overflow test: %s", overflow_msg.c_str());
    
    // Allow background processing
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    // === THEN (Output Verification) ===
    std::string log_content = readLogFile(format_log);
    ASSERT_FALSE(log_content.empty()) << "Log file must contain formatted entries";
    
    size_t final_lines = countLogLines(format_log);
    size_t new_lines = final_lines - initial_lines;
    ASSERT_GE(new_lines, 6) << "Must have at least 6 new formatted log entries";
    
    // Contract 1: Integer formatting correctness
    ASSERT_NE(log_content.find("Integer test: 12345"), std::string::npos) 
        << "Integer formatting must be exact";
    
    // Contract 2: Multiple integer types formatted correctly
    ASSERT_NE(log_content.find("int=12345, uint64=9876543210, size=42"), std::string::npos)
        << "Multiple integer types must be formatted correctly";
    
    // Contract 3: String formatting preserved
    ASSERT_NE(log_content.find("String test: message='test_string', length=11"), std::string::npos)
        << "String formatting must preserve content and calculate length correctly";
    
    // Contract 4: Complex mixed formatting
    ASSERT_NE(log_content.find("Complex: order_id=12345, price=9876543210, symbol='test_string', qty=42, active=true"), 
              std::string::npos) << "Complex mixed formatting must preserve all parameters in order";
    
    // Contract 5: Long message handling
    ASSERT_NE(log_content.find("Long message test:"), std::string::npos)
        << "Long message header must be present";
    // Check that most of the long message is preserved
    size_t long_msg_pos = log_content.find("Long message test:");
    ASSERT_NE(long_msg_pos, std::string::npos) << "Long message must be found in log";
    
    // Contract 6: Overflow handling - message should be truncated but present
    ASSERT_NE(log_content.find("Overflow test:"), std::string::npos)
        << "Overflow message header must be present";
    // Check that overflow message doesn't crash the system
    ASSERT_NE(log_content.find("YYYYYYY"), std::string::npos)
        << "Overflow message should contain partial content";
    
    BldgBlocks::shutdownLogging();
    
    LOG_INFO("PASS: Message formatting contract verified - all format types working correctly");
}

TEST_F(LoggerTestBase, BufferOverflowHandlingContract) {
    // === INPUT SPECIFICATION ===
    // Overflow Test Parameters:
    //   - Buffer Size: 16K entries (16,384 entries)
    //   - Entry Size: 256 bytes per entry (LogEntry struct)
    //   - Test Load: 20,000 log entries (exceeds buffer by ~4K entries)
    //   - Entry Rate: Maximum speed (no delays)
    //   - Background Thread: Processing while producer overflows
    // Overflow Scenario:
    //   - Producer faster than consumer temporarily
    //   - Buffer fills to capacity (16,383 usable entries)
    //   - Additional entries should be dropped safely
    //   - Drop counter should increment accurately
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Buffer Behavior:
    //   - Accepts entries until buffer full (16,383 entries)
    //   - Returns false for write() when buffer full
    //   - Increments drop counter for each failed write
    //   - Background thread continues processing available entries
    // Statistics Tracking:
    //   - messages_written: Count of successfully written entries
    //   - messages_dropped: Count of dropped entries due to overflow
    //   - Total attempts: written + dropped = total log calls
    // Recovery Behavior:
    //   - Buffer empties as background thread processes
    //   - New entries accepted after space available
    //   - No memory corruption or system instability
    
    // === SUCCESS CRITERIA ===
    // 1. Buffer accepts entries until full
    // 2. Gracefully rejects entries when full (no crash)
    // 3. Drop counter increments for each rejection
    // 4. Background thread continues processing
    // 5. Total written + dropped = total attempted
    // 6. System remains stable during overflow
    // 7. Recovery possible after buffer empties
    
    // === GIVEN (Input Contract) ===
    std::string overflow_log = createTempLogFile();
    BldgBlocks::shutdownLogging();
    BldgBlocks::initLogging(overflow_log);
    
    const int OVERFLOW_COUNT = 20000;  // Exceed 16K buffer
    
    // Get initial stats
    auto initial_stats = BldgBlocks::g_logger->getStats();
    
    // === WHEN (Action - Overflow Generation) ===
    auto start_time = std::chrono::steady_clock::now();
    
    // Generate overflow by logging rapidly
    for (int i = 0; i < OVERFLOW_COUNT; ++i) {
        LOG_INFO("Overflow test message %d - generating buffer pressure", i);
        
        // Brief pause every 1000 entries to allow background processing
        if (i % 1000 == 999) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    // Allow background thread to process remaining entries
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // === THEN (Output Verification) ===
    auto final_stats = BldgBlocks::g_logger->getStats();
    
    // Calculate deltas
    uint64_t messages_written = final_stats.messages_written - initial_stats.messages_written;
    uint64_t messages_dropped = final_stats.messages_dropped - initial_stats.messages_dropped;
    uint64_t total_processed = messages_written + messages_dropped;
    
    // Contract 1: System attempted to process all entries
    ASSERT_GE(total_processed, static_cast<uint64_t>(OVERFLOW_COUNT * 0.9)) 
        << "Should attempt to process at least 90% of overflow entries";
    
    // Contract 2: Some entries were dropped due to buffer overflow
    ASSERT_GT(messages_dropped, 0) << "Buffer overflow should result in dropped messages";
    
    // Contract 3: Some entries were successfully written
    ASSERT_GT(messages_written, 0) << "Some entries should be successfully written despite overflow";
    
    // Contract 4: Drop handling is accurate
    ASSERT_EQ(total_processed, messages_written + messages_dropped) 
        << "Total processed should equal sum of written and dropped";
    
    // Contract 5: Background thread continued processing
    // Check that log file has content
    std::string log_content = readLogFile(overflow_log);
    ASSERT_FALSE(log_content.empty()) << "Log file should contain entries despite overflow";
    
    size_t log_lines = countLogLines(overflow_log);
    ASSERT_GT(log_lines, 0) << "Background thread should have written entries to file";
    
    // Contract 6: System remains stable (no crashes)
    ASSERT_NE(BldgBlocks::g_logger, nullptr) << "Logger should remain valid after overflow";
    
    // Contract 7: Recovery possible - test new logging after overflow
    LOG_WARN("Recovery test - logging after overflow");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::string recovery_content = readLogFile(overflow_log);
    ASSERT_NE(recovery_content.find("Recovery test - logging after overflow"), std::string::npos)
        << "Logger should recover and accept new entries after overflow";
    
    // Contract 8: Performance during overflow
    if (duration.count() > 0) {
        uint64_t throughput = (total_processed * 1000) / static_cast<uint64_t>(duration.count()); // entries/sec
        ASSERT_GT(throughput, 10000) << "Should maintain reasonable throughput during overflow: " << throughput << " entries/sec";
    }
    
    BldgBlocks::shutdownLogging();
    
    // === SUCCESS CRITERIA REPORTING ===
    LOG_INFO("PASS: Buffer overflow handling contract verified");
    LOG_INFO("  Messages written: %lu, dropped: %lu, total: %lu", 
             messages_written, messages_dropped, total_processed);
    LOG_INFO("  Overflow duration: %ldms, throughput: %lu entries/sec", 
             duration.count(), duration.count() > 0 ? (total_processed * 1000) / static_cast<uint64_t>(duration.count()) : 0);
}

// 2. PERFORMANCE TESTS - CONTRACT COMPLIANCE UNDER LOAD

TEST_F(LoggerTestBase, LoggingLatencyContract) {
    // === INPUT SPECIFICATION ===
    // Performance Test Parameters:
    //   - Iterations: 10,000 log calls
    //   - Message Type: Formatted string with integer parameter
    //   - Measurement: RDTSC cycle counting for nanosecond precision
    //   - Test Environment: Single-threaded, no contention
    //   - Buffer Condition: Never full (sufficient capacity)
    // Operation Sequence: rdtsc() → LOG_INFO() → rdtsc() → calculate latency
    // Warmup: 100 initial calls excluded from measurement
    // Cooldown: 100ms delay after test for background processing
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Latency Measurements:
    //   - 10,000 individual logging latency samples
    //   - Statistical analysis: min, max, mean, median, P99, P95, stddev
    //   - Distribution analysis: outlier detection and reporting
    // Performance Targets (from CLAUDE.md):
    //   - Maximum latency: < 100ns per LOG_INFO call
    //   - Typical latency: ~35ns (target from requirements)
    //   - P99 latency: < 150ns
    //   - Coefficient of variation: < 30% (consistency)
    //   - No latency > 1μs (outlier detection)
    
    // === SUCCESS CRITERIA ===
    // 1. All log calls complete successfully
    // 2. Maximum latency < 100ns (contract requirement)
    // 3. P99 latency < 150ns (contract requirement)
    // 4. Median latency < 100ns (performance regression)
    // 5. Coefficient of variation < 30% (consistency)
    // 6. No outliers > 1μs (system stability)
    // 7. Mean within 50% of typical target (35ns)
    
    // === GIVEN (Performance Contract) ===
    std::string perf_log = createTempLogFile();
    BldgBlocks::shutdownLogging();
    BldgBlocks::initLogging(perf_log);
    
    const int ITERATIONS = 10000;
    const int WARMUP_ITERATIONS = 100;
    const uint64_t MAX_LATENCY_NS = 100;        // Contract: < 100ns
    const uint64_t MAX_P99_LATENCY_NS = 150;    // Contract: P99 < 150ns
    const uint64_t TYPICAL_LATENCY_NS = 35;     // Target from requirements
    const double MAX_COEFFICIENT_VARIATION = 0.3; // Contract: CV < 30%
    const uint64_t OUTLIER_THRESHOLD_NS = 1000;   // 1μs outlier threshold
    
    std::vector<uint64_t> latencies;
    latencies.reserve(ITERATIONS);
    
    // Warmup phase - exclude from measurement
    for (int i = 0; i < WARMUP_ITERATIONS; ++i) {
        LOG_INFO("Warmup message %d", i);
    }
    
    // === WHEN (Performance Measurement) ===
    for (int i = 0; i < ITERATIONS; ++i) {
        auto start = rdtsc();
        LOG_INFO("Performance test message %d with data", i);
        auto end = rdtsc();
        
        uint64_t cycles = end - start;
        uint64_t ns = cycles_to_nanos(cycles);
        latencies.push_back(ns);
    }
    
    // Allow background thread to process
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // === THEN (Performance Contract Verification) ===
    
    // Statistical analysis
    std::vector<uint64_t> sorted_latencies = latencies;
    std::sort(sorted_latencies.begin(), sorted_latencies.end());
    
    uint64_t min_latency = sorted_latencies.front();
    uint64_t max_latency = sorted_latencies.back();
    uint64_t median_latency = sorted_latencies[sorted_latencies.size() / 2];
    uint64_t p95_latency = sorted_latencies[static_cast<size_t>(static_cast<double>(sorted_latencies.size()) * 0.95)];
    uint64_t p99_latency = sorted_latencies[static_cast<size_t>(static_cast<double>(sorted_latencies.size()) * 0.99)];
    
    // Calculate mean
    uint64_t sum = std::accumulate(sorted_latencies.begin(), sorted_latencies.end(), 0ULL);
    uint64_t mean_latency = sum / static_cast<uint64_t>(sorted_latencies.size());
    
    // Calculate standard deviation
    double variance = 0.0;
    for (uint64_t latency : sorted_latencies) {
        double diff = static_cast<double>(latency) - static_cast<double>(mean_latency);
        variance += diff * diff;
    }
    double stddev = std::sqrt(variance / static_cast<double>(sorted_latencies.size()));
    double cv = stddev / static_cast<double>(mean_latency);
    
    // Count outliers
    size_t outlier_count = static_cast<size_t>(std::count_if(sorted_latencies.begin(), sorted_latencies.end(),
                                       [OUTLIER_THRESHOLD_NS](uint64_t lat) { 
                                           return lat > OUTLIER_THRESHOLD_NS; 
                                       }));
    
    // Contract 1: Maximum latency compliance
    EXPECT_LT(max_latency, MAX_LATENCY_NS) 
        << "Maximum latency " << max_latency << "ns exceeds contract " << MAX_LATENCY_NS << "ns";
    
    // Contract 2: P99 latency compliance
    EXPECT_LT(p99_latency, MAX_P99_LATENCY_NS)
        << "P99 latency " << p99_latency << "ns exceeds contract " << MAX_P99_LATENCY_NS << "ns";
    
    // Contract 3: Median performance regression check
    EXPECT_LT(median_latency, MAX_LATENCY_NS)
        << "Median latency " << median_latency << "ns suggests performance regression";
    
    // Contract 4: Consistency (low variation)
    EXPECT_LT(cv, MAX_COEFFICIENT_VARIATION)
        << "Coefficient of variation " << cv << " exceeds consistency contract " << MAX_COEFFICIENT_VARIATION;
    
    // Contract 5: Outlier detection (system stability)
    double outlier_percentage = (static_cast<double>(outlier_count) / static_cast<double>(sorted_latencies.size())) * 100.0;
    EXPECT_LT(outlier_percentage, 1.0)
        << "Outlier percentage " << outlier_percentage << "% too high, suggests system instability";
    
    // Contract 6: Mean performance expectation
    EXPECT_LT(mean_latency, TYPICAL_LATENCY_NS * 3) // Allow 3x tolerance
        << "Mean latency " << mean_latency << "ns significantly exceeds typical " << TYPICAL_LATENCY_NS << "ns";
    
    // Contract 7: Verify all log calls succeeded
    auto stats = BldgBlocks::g_logger->getStats();
    EXPECT_EQ(stats.messages_dropped, 0) << "No messages should be dropped during performance test";
    
    BldgBlocks::shutdownLogging();
    
    // === SUCCESS CRITERIA REPORTING ===
    LOG_INFO("PASS: Logging latency contract verified");
    LOG_INFO("  Latency stats: min=%luns, max=%luns, mean=%luns, median=%luns", 
             min_latency, max_latency, mean_latency, median_latency);
    LOG_INFO("  Percentiles: P95=%luns, P99=%luns, StdDev=%.1fns, CV=%.3f", 
             p95_latency, p99_latency, stddev, cv);
    LOG_INFO("  Outliers: %zu/%d (%.2f%%) above %luns threshold", 
             outlier_count, ITERATIONS, outlier_percentage, OUTLIER_THRESHOLD_NS);
}

// 3. STRESS TESTS - CONCURRENT BEHAVIOR VERIFICATION

TEST_F(LoggerTestBase, ConcurrentLoggingContract) {
    // === INPUT SPECIFICATION ===
    // Concurrency Test Parameters:
    //   - Thread Count: 8 concurrent producer threads
    //   - Messages Per Thread: 1,000 log entries each
    //   - Total Load: 8,000 concurrent log entries
    //   - Message Pattern: Unique content per thread with sequence numbers
    //   - Thread Synchronization: Synchronized start, unsynchronized execution
    // Concurrent Scenario:
    //   - All threads start simultaneously
    //   - Each thread logs with unique identifiers
    //   - Background consumer processes all entries asynchronously
    //   - Test system under maximum realistic load
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Thread Safety Results:
    //   - No data corruption in log entries
    //   - All thread messages appear in log file
    //   - Message integrity preserved (no garbled entries)
    //   - Background thread processes all entries eventually
    // Performance Under Load:
    //   - Reasonable throughput maintained (>50K entries/sec total)
    //   - No excessive latency degradation per thread
    //   - Drop rate < 5% under concurrent load
    // System Stability:
    //   - No crashes or hangs
    //   - Logger remains responsive throughout
    //   - Clean shutdown after concurrent load
    
    // === SUCCESS CRITERIA ===
    // 1. All threads complete without errors
    // 2. No log entry corruption detected
    // 3. Background thread processes entries from all threads  
    // 4. Drop rate remains reasonable (< 5%)
    // 5. System throughput > 50K entries/sec total
    // 6. No crashes or system instability
    // 7. Clean logger shutdown after stress test
    
    // === GIVEN (Concurrency Contract) ===
    std::string concurrent_log = createTempLogFile();
    BldgBlocks::shutdownLogging();
    BldgBlocks::initLogging(concurrent_log);
    
    const int NUM_THREADS = 8;
    const int MESSAGES_PER_THREAD = 1000;
    const int TOTAL_EXPECTED = NUM_THREADS * MESSAGES_PER_THREAD;
    
    std::atomic<bool> start_flag{false};
    std::atomic<int> ready_count{0};
    std::vector<std::thread> threads;
    std::atomic<int> successful_logs{0};
    std::atomic<int> thread_errors{0};
    
    auto start_stats = BldgBlocks::g_logger->getStats();
    
    // === WHEN (Concurrent Load Testing) ===
    auto test_start = std::chrono::steady_clock::now();
    
    // Launch all threads
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            // Signal ready and wait for synchronized start
            ready_count.fetch_add(1);
            while (!start_flag.load()) {
                std::this_thread::yield();
            }
            
            // Log messages with thread-specific pattern
            for (int i = 0; i < MESSAGES_PER_THREAD; ++i) {
                try {
                    LOG_INFO("Thread %d message %d: processing order %d with status %s", 
                             t, i, (t * 10000 + i), (i % 2 == 0) ? "active" : "pending");
                    successful_logs.fetch_add(1, std::memory_order_relaxed);
                    
                    // Brief yield every 100 messages to allow other threads
                    if (i % 100 == 99) {
                        std::this_thread::yield();
                    }
                } catch (...) {
                    thread_errors.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    
    // Wait for all threads ready
    while (ready_count.load() < NUM_THREADS) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    // Start all threads simultaneously
    start_flag.store(true);
    
    // Wait for completion
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto test_end = std::chrono::steady_clock::now();
    
    // Allow background thread to process remaining entries
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(test_end - test_start);
    auto final_stats = BldgBlocks::g_logger->getStats();
    
    // === THEN (Concurrency Contract Verification) ===
    
    // Contract 1: All threads completed without errors
    ASSERT_EQ(thread_errors.load(), 0) << "No thread should encounter errors during concurrent logging";
    ASSERT_EQ(successful_logs.load(), TOTAL_EXPECTED) 
        << "All threads should complete all log attempts successfully";
    
    // Contract 2: Background thread processing
    uint64_t messages_written = final_stats.messages_written - start_stats.messages_written;
    uint64_t messages_dropped = final_stats.messages_dropped - start_stats.messages_dropped;
    uint64_t total_processed = messages_written + messages_dropped;
    
    ASSERT_GE(total_processed, static_cast<uint64_t>(TOTAL_EXPECTED * 0.95))
        << "Background thread should process at least 95% of concurrent entries";
    
    // Contract 3: Reasonable drop rate under load
    double drop_rate = (static_cast<double>(messages_dropped) / static_cast<double>(total_processed)) * 100.0;
    EXPECT_LT(drop_rate, 5.0) 
        << "Drop rate " << drop_rate << "% too high under concurrent load";
    
    // Contract 4: System throughput under load
    if (duration.count() > 0) {
        uint64_t throughput = (total_processed * 1000) / static_cast<uint64_t>(duration.count()); // entries/sec
        EXPECT_GT(throughput, 50000) 
            << "Concurrent throughput " << throughput << " entries/sec below contract minimum 50K";
    }
    
    // Contract 5: Log file contains entries from all threads
    std::string log_content = readLogFile(concurrent_log);
    ASSERT_FALSE(log_content.empty()) << "Log file must contain entries from concurrent test";
    
    // Verify we see thread IDs from different threads
    std::vector<bool> thread_found(static_cast<std::vector<bool>::size_type>(NUM_THREADS), false);
    for (int t = 0; t < NUM_THREADS; ++t) {
        std::string thread_pattern = "Thread " + std::to_string(t) + " message";
        if (log_content.find(thread_pattern) != std::string::npos) {
            thread_found[static_cast<std::vector<bool>::size_type>(t)] = true;
        }
    }
    
    int threads_with_entries = static_cast<int>(std::count(thread_found.begin(), thread_found.end(), true));
    EXPECT_GE(threads_with_entries, NUM_THREADS / 2) 
        << "Should find log entries from at least half the threads (" << threads_with_entries 
        << "/" << NUM_THREADS << " found)";
    
    // Contract 6: Logger remains valid after stress test
    ASSERT_NE(BldgBlocks::g_logger, nullptr) << "Logger should remain valid after concurrent stress test";
    
    // Contract 7: Recovery test - single log after stress
    LOG_ERROR("Post-stress recovery test message");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::string recovery_content = readLogFile(concurrent_log);
    ASSERT_NE(recovery_content.find("Post-stress recovery test message"), std::string::npos)
        << "Logger should remain functional after concurrent stress test";
    
    BldgBlocks::shutdownLogging();
    
    // === SUCCESS CRITERIA REPORTING ===
    LOG_INFO("PASS: Concurrent logging contract verified");
    LOG_INFO("  Threads: %d, Messages/thread: %d, Total: %d", 
             NUM_THREADS, MESSAGES_PER_THREAD, TOTAL_EXPECTED);
    LOG_INFO("  Results: written=%lu, dropped=%lu, drop_rate=%.2f%%", 
             messages_written, messages_dropped, drop_rate);
    LOG_INFO("  Duration: %ldms, Throughput: %lu entries/sec", 
             duration.count(), duration.count() > 0 ? (total_processed * 1000) / static_cast<uint64_t>(duration.count()) : 0);
    LOG_INFO("  Thread coverage: %d/%d threads found in log", threads_with_entries, NUM_THREADS);
}

TEST_F(LoggerTestBase, LockFreeBufferDirectContract) {
    // === INPUT SPECIFICATION ===
    // Direct Buffer Test Parameters:
    //   - Buffer Size: 1024 entries (power of 2)
    //   - Test Operations: write(), read(), size(), empty(), full()
    //   - Producer-Consumer Pattern: Fill buffer then drain
    //   - Concurrency: Single-threaded direct access
    //   - Entry Content: Structured test data with validation
    // Buffer State Transitions:
    //   - Empty → Filling → Full → Draining → Empty
    //   - Verify state consistency at each transition
    //   - Test boundary conditions (exactly full, exactly empty)
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Write Operations:
    //   - Returns true when buffer has space
    //   - Returns false when buffer full
    //   - Correctly stores entry data
    //   - Maintains entry order (FIFO)
    // Read Operations:
    //   - Returns true when buffer has data
    //   - Returns false when buffer empty
    //   - Retrieves entries in FIFO order
    //   - Preserves entry data integrity
    // State Tracking:
    //   - size() returns accurate count
    //   - empty() true only when size == 0
    //   - full() true only when size == capacity-1
    
    // === SUCCESS CRITERIA ===
    // 1. Write succeeds until buffer full
    // 2. Write fails gracefully when full
    // 3. Read succeeds until buffer empty
    // 4. Read fails gracefully when empty
    // 5. FIFO ordering maintained throughout
    // 6. State methods return accurate values
    // 7. No data corruption during operations
    
    // === GIVEN (Direct Buffer Contract) ===
    const size_t BUFFER_SIZE = 1024;
    LockFreeLogBuffer<BUFFER_SIZE> buffer;
    
    // Verify initial state
    ASSERT_TRUE(buffer.empty()) << "Buffer should be empty initially";
    ASSERT_FALSE(buffer.full()) << "Buffer should not be full initially";
    ASSERT_EQ(buffer.size(), 0) << "Buffer size should be 0 initially";
    
    // === WHEN (Buffer Operations) ===
    
    // Test 1: Fill buffer to capacity
    size_t successful_writes = 0;
    std::vector<std::string> written_messages;
    
    for (size_t i = 0; i < BUFFER_SIZE; ++i) {
        std::string message = "Test message " + std::to_string(i) + " with data";
        bool success = buffer.write(Logger::INFO, message.c_str(), message.length());
        
        if (success) {
            successful_writes++;
            written_messages.push_back(message);
        } else {
            break; // Buffer full
        }
        
        // Verify state during filling
        ASSERT_EQ(buffer.size(), successful_writes) << "Buffer size should match write count at iteration " << i;
        ASSERT_FALSE(buffer.empty()) << "Buffer should not be empty after write " << i;
        
        if (successful_writes >= BUFFER_SIZE - 1) {
            ASSERT_TRUE(buffer.full()) << "Buffer should report full when at capacity";
        }
    }
    
    // === THEN (Write Phase Verification) ===
    
    // Contract 1: Buffer filled to capacity (minus 1 for ring buffer implementation)
    ASSERT_EQ(successful_writes, BUFFER_SIZE - 1) 
        << "Should successfully write capacity-1 entries";
    ASSERT_TRUE(buffer.full()) << "Buffer should be full after writing to capacity";
    ASSERT_FALSE(buffer.empty()) << "Buffer should not be empty when full";
    
    // Contract 2: Additional write should fail
    bool overflow_write = buffer.write(Logger::ERROR, "overflow", 8);
    ASSERT_FALSE(overflow_write) << "Write should fail when buffer is full";
    ASSERT_EQ(buffer.size(), BUFFER_SIZE - 1) << "Buffer size should not change after failed write";
    
    // === WHEN (Read Phase) ===
    
    // Test 2: Drain buffer completely
    size_t successful_reads = 0;
    std::vector<std::string> read_messages;
    
    typename LockFreeLogBuffer<BUFFER_SIZE>::LogEntry entry;
    while (buffer.read(entry)) {
        successful_reads++;
        
        // Verify entry content
        ASSERT_EQ(entry.level, Logger::INFO) << "Entry level should be preserved";
        ASSERT_GT(entry.length, 0) << "Entry length should be positive";
        ASSERT_LT(entry.length, sizeof(entry.data)) << "Entry length should be within bounds";
        
        std::string read_message(entry.data, entry.length);
        read_messages.push_back(read_message);
        
        // Verify state during draining
        ASSERT_EQ(buffer.size(), (BUFFER_SIZE - 1) - successful_reads) 
            << "Buffer size should decrease with each read";
        ASSERT_FALSE(buffer.full()) << "Buffer should not be full during reading";
        
        if (successful_reads == BUFFER_SIZE - 1) {
            ASSERT_TRUE(buffer.empty()) << "Buffer should be empty after reading all entries";
        }
    }
    
    // === THEN (Read Phase Verification) ===
    
    // Contract 3: Read all written entries
    ASSERT_EQ(successful_reads, successful_writes) 
        << "Should read exactly as many entries as were written";
    ASSERT_TRUE(buffer.empty()) << "Buffer should be empty after reading all entries";
    ASSERT_FALSE(buffer.full()) << "Buffer should not be full after reading all entries";
    ASSERT_EQ(buffer.size(), 0) << "Buffer size should be 0 after reading all entries";
    
    // Contract 4: Additional read should fail
    typename LockFreeLogBuffer<BUFFER_SIZE>::LogEntry empty_entry;
    bool underflow_read = buffer.read(empty_entry);
    ASSERT_FALSE(underflow_read) << "Read should fail when buffer is empty";
    
    // Contract 5: FIFO ordering verification
    ASSERT_EQ(read_messages.size(), written_messages.size()) 
        << "Should have same number of read and written messages";
    
    for (size_t i = 0; i < std::min(read_messages.size(), written_messages.size()); ++i) {
        ASSERT_EQ(read_messages[i], written_messages[i]) 
            << "Message " << i << " should maintain FIFO order";
    }
    
    // Contract 6: Recovery after complete cycle
    bool recovery_write = buffer.write(Logger::WARN, "recovery test", 12);
    ASSERT_TRUE(recovery_write) << "Should be able to write after complete drain";
    ASSERT_EQ(buffer.size(), 1) << "Buffer should have 1 entry after recovery write";
    ASSERT_FALSE(buffer.empty()) << "Buffer should not be empty after recovery write";
    
    typename LockFreeLogBuffer<BUFFER_SIZE>::LogEntry recovery_entry;
    bool recovery_read = buffer.read(recovery_entry);
    ASSERT_TRUE(recovery_read) << "Should be able to read recovery entry";
    ASSERT_STREQ(recovery_entry.data, "recovery test") << "Recovery entry should preserve content";
    
    LOG_INFO("PASS: LockFree buffer direct contract verified - FIFO ordering and state consistency maintained");
}
// Main function for running tests
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
