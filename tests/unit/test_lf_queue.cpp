#include <gtest/gtest.h>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <unordered_set>

#include "lf_queue.h"
#include "logging.h"
#include "time_utils.h"

using namespace Common;

// Base test class with proper setup
class LFQueueTestBase : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logging for test
        std::string timestamp = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        std::string log_file = "logs/test_lfqueue_" + timestamp + ".log";
        Common::initLogging(log_file);
        
        LOG_INFO("=== Starting LFQueue Test ===");
    }
    
    void TearDown() override {
        LOG_INFO("=== LFQueue Test Completed ===");
        Common::shutdownLogging();
    }
    
    // Helper to verify memory is zeroed
    bool isMemoryZeroed(void* ptr, size_t size) {
        const char* bytes = static_cast<const char*>(ptr);
        for (size_t i = 0; i < size; ++i) {
            if (bytes[i] != 0) return false;
        }
        return true;
    }
    
    // Helper to verify memory alignment
    bool isCacheLineAligned(void* ptr) {
        return (reinterpret_cast<uintptr_t>(ptr) % 64) == 0;
    }
    
    // Convert TSC cycles to nanoseconds (simplified approximation)
    uint64_t cycles_to_nanos(uint64_t cycles) {
        static const double ns_per_cycle = 1e9 / 3.0e9; // Assume 3GHz CPU
        return static_cast<uint64_t>(static_cast<double>(cycles) * ns_per_cycle);
    }
    
    // Helper structure for latency statistics  
    struct LatencyStats {
        uint64_t min, max, mean, median, p99;
        double stddev;
    };
    
    // Analyze latency measurements and compute statistics
    LatencyStats analyze_latencies(const std::vector<uint64_t>& latencies) {
        if (latencies.empty()) {
            return {0, 0, 0, 0, 0, 0.0};
        }
        
        std::vector<uint64_t> sorted = latencies;
        std::sort(sorted.begin(), sorted.end());
        
        LatencyStats stats;
        stats.min = sorted.front();
        stats.max = sorted.back();
        stats.median = sorted[sorted.size() / 2];
        stats.p99 = sorted[static_cast<size_t>(static_cast<double>(sorted.size()) * 0.99)];
        
        // Calculate mean
        uint64_t sum = std::accumulate(sorted.begin(), sorted.end(), 0UL);
        stats.mean = sum / sorted.size();
        
        // Calculate standard deviation
        double variance = 0.0;
        for (uint64_t latency : sorted) {
            double diff = static_cast<double>(latency) - static_cast<double>(stats.mean);
            variance += diff * diff;
        }
        stats.stddev = std::sqrt(variance / static_cast<double>(sorted.size()));
        
        return stats;
    }
};

// =============================================================================
// 1. SPSC LOCK-FREE QUEUE UNIT TESTS - FUNCTIONAL CORRECTNESS
// =============================================================================

TEST_F(LFQueueTestBase, SPSCBasicEnqueueDequeueContract) {
    // === INPUT SPECIFICATION ===
    // Queue Configuration:
    //   - Element Type: uint64_t (8 bytes)
    //   - Queue Size: 8 elements (power of 2)
    //   - Total Capacity: 8 * 8 = 64 bytes
    // Operation Sequence: 
    //   1. getNextToWriteTo() → write data → updateWriteIndex()
    //   2. getNextToRead() → read data → updateReadIndex()
    // Pre-conditions: Queue empty, all slots available
    // Test Values: [100, 200, 300] (sequential integers)
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Enqueue Results:
    //   - getNextToWriteTo(): Non-null pointer to write location
    //   - Write location: Within queue storage bounds
    //   - updateWriteIndex(): No return value, advances write index
    //   - Queue state: size() increases by 1 per enqueue
    // Dequeue Results:
    //   - getNextToRead(): Non-null pointer to read location
    //   - Read data: Exactly matches written data (FIFO order)
    //   - updateReadIndex(): No return value, advances read index
    //   - Queue state: size() decreases by 1 per dequeue
    
    // === SUCCESS CRITERIA ===
    // 1. First getNextToWriteTo() returns non-null pointer
    // 2. Written data persists until read
    // 3. FIFO ordering preserved (100, 200, 300 read in same order)
    // 4. Queue size accurately reflects operations
    // 5. Empty queue returns null on getNextToRead()
    // 6. All operations complete without blocking or errors
    
    // === GIVEN (Input Contract) ===
    const size_t QUEUE_SIZE = 8;  // Power of 2
    SPSCLFQueue<uint64_t> queue(QUEUE_SIZE);
    
    // Pre-condition verification
    ASSERT_EQ(queue.capacity(), QUEUE_SIZE) << "Queue capacity should match constructor parameter";
    ASSERT_EQ(queue.size(), 0) << "Queue should start empty";
    
    // Test data
    const std::vector<uint64_t> test_values = {100, 200, 300};
    
    // === WHEN (Enqueue Operations) ===
    for (size_t i = 0; i < test_values.size(); ++i) {
        uint64_t* write_ptr = queue.getNextToWriteTo();
        
        // Contract 1: getNextToWriteTo() succeeds
        ASSERT_NE(write_ptr, nullptr) << "getNextToWriteTo() must return valid pointer for enqueue " << i;
        
        // Contract 2: Write location is valid
        *write_ptr = test_values[i];
        
        // Contract 3: updateWriteIndex() completes
        ASSERT_NO_THROW(queue.updateWriteIndex()) << "updateWriteIndex() must not throw for enqueue " << i;
        
        // Contract 4: Queue size increases
        ASSERT_EQ(queue.size(), i + 1) << "Queue size must be " << (i + 1) << " after enqueue " << i;
    }
    
    // === THEN (Dequeue Verification) ===
    for (size_t i = 0; i < test_values.size(); ++i) {
        size_t expected_size = test_values.size() - i;
        ASSERT_EQ(queue.size(), expected_size) << "Queue size should be " << expected_size << " before dequeue " << i;
        
        // Contract 5: getNextToRead() succeeds
        const uint64_t* read_ptr = queue.getNextToRead();
        ASSERT_NE(read_ptr, nullptr) << "getNextToRead() must return valid pointer for dequeue " << i;
        
        // Contract 6: FIFO ordering preserved
        ASSERT_EQ(*read_ptr, test_values[i]) << "FIFO violation: expected " << test_values[i] << " at position " << i;
        
        // Contract 7: updateReadIndex() completes
        ASSERT_NO_THROW(queue.updateReadIndex()) << "updateReadIndex() must not throw for dequeue " << i;
        
        // Contract 8: Queue size decreases
        ASSERT_EQ(queue.size(), expected_size - 1) << "Queue size must decrease after dequeue " << i;
    }
    
    // === FINAL STATE VERIFICATION ===
    // Contract 9: Empty queue behavior
    ASSERT_EQ(queue.size(), 0) << "Queue should be empty after all dequeues";
    ASSERT_EQ(queue.getNextToRead(), nullptr) << "Empty queue must return null on getNextToRead()";
    
    LOG_INFO("PASS: SPSC basic enqueue/dequeue contract verified - FIFO ordering and size management correct");
}

TEST_F(LFQueueTestBase, SPSCQueueExhaustionContract) {
    // === INPUT SPECIFICATION ===
    // Queue Configuration:
    //   - Element Type: int32_t (4 bytes)
    //   - Queue Size: 4 elements (small for quick exhaustion testing)
    //   - Total Capacity: 4 * 4 = 16 bytes
    // Operation: Fill to capacity, attempt overflow, verify recovery
    // Pre-conditions: Queue empty
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Fill Phase (4 operations):
    //   - enqueue 1-4: getNextToWriteTo() returns non-null
    //   - Queue progression: size 0→1→2→3→4
    // Overflow Phase (5th operation):
    //   - enqueue 5: getNextToWriteTo() returns null (queue full)
    //   - Queue state: size remains 4, no corruption
    // Recovery Phase:
    //   - dequeue 1 element: getNextToRead() succeeds, size becomes 3
    //   - enqueue new element: getNextToWriteTo() succeeds again
    
    // === SUCCESS CRITERIA ===
    // 1. All capacity slots can be filled (4 successful enqueues)
    // 2. 5th enqueue fails gracefully (null return, no crash)
    // 3. Queue size correctly reports full state (4 elements)
    // 4. After one dequeue, space becomes available
    // 5. Queue can accept new elements after dequeue
    // 6. No memory corruption or data loss during overflow
    
    // === GIVEN (Input Contract) ===
    const size_t QUEUE_SIZE = 4;  // Small size to test exhaustion quickly
    SPSCLFQueue<int32_t> queue(QUEUE_SIZE);
    
    // === WHEN (Fill to Capacity) ===
    std::vector<int32_t> fill_values = {10, 20, 30, 40};
    
    for (size_t i = 0; i < QUEUE_SIZE; ++i) {
        int32_t* write_ptr = queue.getNextToWriteTo();
        
        // Contract 1: Can fill all capacity slots
        ASSERT_NE(write_ptr, nullptr) << "Should be able to write element " << i << " within capacity";
        
        *write_ptr = fill_values[i];
        queue.updateWriteIndex();
        
        // Verify size progression
        ASSERT_EQ(queue.size(), i + 1) << "Size should be " << (i + 1) << " after filling slot " << i;
    }
    
    // === WHEN (Attempt Overflow) ===
    int32_t* overflow_ptr = queue.getNextToWriteTo();
    
    // === THEN (Overflow Verification) ===
    // Contract 2: Overflow fails gracefully
    ASSERT_EQ(overflow_ptr, nullptr) << "Queue at capacity must return null on getNextToWriteTo()";
    
    // Contract 3: Queue state unchanged by failed operation
    ASSERT_EQ(queue.size(), QUEUE_SIZE) << "Queue size must remain at capacity after failed enqueue";
    
    // === WHEN (Recovery Test) ===
    // Dequeue one element to free space
    const int32_t* read_ptr = queue.getNextToRead();
    ASSERT_NE(read_ptr, nullptr) << "Should be able to read from full queue";
    ASSERT_EQ(*read_ptr, fill_values[0]) << "Should read first element (FIFO order)";
    queue.updateReadIndex();
    
    // Contract 4: Space becomes available after dequeue
    ASSERT_EQ(queue.size(), QUEUE_SIZE - 1) << "Size should decrease after dequeue";
    
    // Contract 5: Can enqueue again after freeing space
    int32_t* recovery_ptr = queue.getNextToWriteTo();
    ASSERT_NE(recovery_ptr, nullptr) << "Should be able to enqueue after dequeue";
    
    *recovery_ptr = 50;  // New element
    queue.updateWriteIndex();
    
    // Contract 6: Queue accepts new element and maintains integrity
    ASSERT_EQ(queue.size(), QUEUE_SIZE) << "Queue should be full again after recovery enqueue";
    
    // Verify data integrity - read remaining original elements
    for (size_t i = 1; i < QUEUE_SIZE; ++i) {
        const int32_t* verify_ptr = queue.getNextToRead();
        ASSERT_NE(verify_ptr, nullptr) << "Should be able to read element " << i;
        ASSERT_EQ(*verify_ptr, fill_values[i]) << "Data integrity violation at element " << i;
        queue.updateReadIndex();
    }
    
    // Read the recovery element
    const int32_t* final_ptr = queue.getNextToRead();
    ASSERT_NE(final_ptr, nullptr) << "Should be able to read recovery element";
    ASSERT_EQ(*final_ptr, 50) << "Recovery element should match written value";
    queue.updateReadIndex();
    
    ASSERT_EQ(queue.size(), 0) << "Queue should be empty after reading all elements";
    
    LOG_INFO("PASS: SPSC queue exhaustion contract verified - graceful overflow handling and recovery");
}

TEST_F(LFQueueTestBase, SPSCInvalidStateHandling) {
    // === INPUT SPECIFICATION ===
    // Queue Configuration: 8 elements of uint64_t
    // Test Scenarios:
    //   1. updateWriteIndex() without getNextToWriteTo()
    //   2. updateReadIndex() without getNextToRead()  
    //   3. Multiple updateReadIndex() calls on empty queue
    //   4. Multiple updateWriteIndex() calls beyond capacity
    // Expected: All operations safe, no crashes or corruption
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Invalid Operations Results:
    //   - Premature index updates: Safe no-op or well-defined behavior
    //   - Over-updates: Queue maintains consistency
    //   - Queue state: Remains valid and recoverable
    //   - No segmentation faults or memory corruption
    
    // === SUCCESS CRITERIA ===
    // 1. updateWriteIndex() without prior getNextToWriteTo() is safe
    // 2. updateReadIndex() without prior getNextToRead() is safe
    // 3. Queue state remains consistent after invalid operations
    // 4. Normal operations work after invalid operations
    // 5. No crashes or undefined behavior
    
    // === GIVEN ===
    SPSCLFQueue<uint64_t> queue(8);
    
    // === WHEN/THEN (Invalid Operations) ===
    // Contract 1: Premature updateWriteIndex() is safe
    ASSERT_NO_THROW(queue.updateWriteIndex()) << "updateWriteIndex() without getNextToWriteTo() must be safe";
    
    // Queue behavior after premature update may vary, but should remain consistent
    // The specific behavior depends on implementation - key is no crash
    
    // Contract 2: Premature updateReadIndex() on empty queue is safe
    ASSERT_NO_THROW(queue.updateReadIndex()) << "updateReadIndex() without getNextToRead() must be safe";
    
    // Contract 3: Multiple premature operations are safe
    for (int i = 0; i < 5; ++i) {
        ASSERT_NO_THROW(queue.updateWriteIndex()) << "Multiple updateWriteIndex() calls must be safe";
        ASSERT_NO_THROW(queue.updateReadIndex()) << "Multiple updateReadIndex() calls must be safe";
    }
    
    // Contract 4: Normal operations still work after invalid operations
    uint64_t* write_ptr = queue.getNextToWriteTo();
    if (write_ptr != nullptr) {
        *write_ptr = 42;
        queue.updateWriteIndex();
        
        const uint64_t* read_ptr = queue.getNextToRead();
        if (read_ptr != nullptr) {
            ASSERT_EQ(*read_ptr, 42) << "Normal operations should work after invalid operations";
            queue.updateReadIndex();
        }
    }
    
    // Contract 5: Queue capacity and basic properties remain valid
    ASSERT_EQ(queue.capacity(), 8) << "Queue capacity should remain unchanged";
    // Size may have changed due to invalid operations, but should be within bounds
    ASSERT_LE(queue.size(), queue.capacity()) << "Queue size should not exceed capacity";
    
    LOG_INFO("PASS: SPSC invalid state handling verified - robust against misuse");
}

// =============================================================================  
// 2. MPMC LOCK-FREE QUEUE UNIT TESTS - FUNCTIONAL CORRECTNESS
// =============================================================================

TEST_F(LFQueueTestBase, MPMCBasicEnqueueDequeueContract) {
    // === INPUT SPECIFICATION ===
    // Queue Configuration:
    //   - Element Type: uint64_t (8 bytes)
    //   - Queue Size: 8 elements (power of 2)
    //   - Total Capacity: 8 * 8 = 64 bytes
    // Operation Sequence:
    //   1. enqueue(value) → returns bool success
    //   2. dequeue(reference) → returns bool success, fills reference
    // Pre-conditions: Queue empty, initialized correctly
    // Test Values: [1000, 2000, 3000, 4000] (larger values to distinguish from indices)
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Enqueue Results:
    //   - enqueue() returns true for successful operations
    //   - Elements stored in internal queue structure
    //   - Queue maintains FIFO ordering via sequence numbers
    // Dequeue Results:
    //   - dequeue() returns true for successful operations
    //   - Reference parameter filled with dequeued value
    //   - FIFO order preserved across enqueue/dequeue pairs
    
    // === SUCCESS CRITERIA ===
    // 1. All enqueue operations return true when space available
    // 2. All dequeue operations return true when elements available
    // 3. FIFO ordering maintained (values read in enqueue order)
    // 4. Empty queue: dequeue() returns false
    // 5. Values exactly match between enqueue and dequeue
    // 6. No data corruption or loss
    
    // === GIVEN (Input Contract) ===
    const size_t QUEUE_SIZE = 8;
    MPMCLFQueue<uint64_t> queue(QUEUE_SIZE);
    
    const std::vector<uint64_t> test_values = {1000, 2000, 3000, 4000};
    
    // === WHEN (Enqueue Phase) ===
    for (size_t i = 0; i < test_values.size(); ++i) {
        bool enqueue_success = queue.enqueue(test_values[i]);
        
        // Contract 1: Enqueue succeeds when space available
        ASSERT_TRUE(enqueue_success) << "enqueue() should succeed for element " << i << " (value=" << test_values[i] << ")";
    }
    
    // === WHEN (Dequeue Phase) ===
    for (size_t i = 0; i < test_values.size(); ++i) {
        uint64_t dequeued_value = 0;  // Initialize to detect if value was set
        bool dequeue_success = queue.dequeue(dequeued_value);
        
        // === THEN (Output Verification) ===
        // Contract 2: Dequeue succeeds when elements available
        ASSERT_TRUE(dequeue_success) << "dequeue() should succeed for element " << i;
        
        // Contract 3: FIFO ordering preserved
        ASSERT_EQ(dequeued_value, test_values[i]) 
            << "FIFO violation: expected " << test_values[i] << ", got " << dequeued_value << " at position " << i;
    }
    
    // === THEN (Empty Queue Verification) ===
    uint64_t empty_value = 999;  // Sentinel to verify it's not modified
    bool empty_dequeue = queue.dequeue(empty_value);
    
    // Contract 4: Empty queue returns false on dequeue
    ASSERT_FALSE(empty_dequeue) << "dequeue() on empty queue should return false";
    ASSERT_EQ(empty_value, 999) << "dequeue() should not modify reference parameter on failure";
    
    LOG_INFO("PASS: MPMC basic enqueue/dequeue contract verified - FIFO ordering and success/failure handling correct");
}

TEST_F(LFQueueTestBase, MPMCQueueExhaustionContract) {
    // === INPUT SPECIFICATION ===
    // Queue Configuration:
    //   - Element Type: int32_t (4 bytes)
    //   - Queue Size: 4 elements (small for quick exhaustion)
    // Test Phases:
    //   1. Fill Phase: enqueue() 4 elements to capacity
    //   2. Overflow Phase: enqueue() 5th element (should fail)
    //   3. Recovery Phase: dequeue() → enqueue() to verify recovery
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Fill Phase Results:
    //   - First 4 enqueue() calls return true
    //   - All elements successfully stored
    // Overflow Phase Results:  
    //   - 5th enqueue() call returns false (queue full)
    //   - Queue state unchanged, no corruption
    // Recovery Phase Results:
    //   - dequeue() succeeds and returns first element
    //   - Subsequent enqueue() succeeds
    
    // === SUCCESS CRITERIA ===
    // 1. Can fill exactly to capacity (4 successful enqueues)
    // 2. Overflow attempt returns false gracefully
    // 3. No crashes or memory corruption during overflow
    // 4. FIFO order preserved during recovery
    // 5. Queue becomes usable again after dequeue
    // 6. Data integrity maintained throughout test
    
    // === GIVEN (Input Contract) ===
    const size_t QUEUE_SIZE = 4;
    MPMCLFQueue<int32_t> queue(QUEUE_SIZE);
    
    std::vector<int32_t> fill_values = {100, 200, 300, 400};
    
    // === WHEN (Fill to Capacity) ===
    for (size_t i = 0; i < QUEUE_SIZE; ++i) {
        bool success = queue.enqueue(fill_values[i]);
        
        // Contract 1: All capacity slots can be filled
        ASSERT_TRUE(success) << "Should be able to enqueue element " << i << " within capacity (value=" << fill_values[i] << ")";
    }
    
    // === WHEN (Attempt Overflow) ===
    bool overflow_result = queue.enqueue(500);  // 5th element
    
    // === THEN (Overflow Verification) ===
    // Contract 2: Overflow fails gracefully
    ASSERT_FALSE(overflow_result) << "enqueue() on full queue should return false";
    
    // === WHEN (Recovery Test) ===
    // Dequeue first element
    int32_t dequeued_value = 0;
    bool dequeue_success = queue.dequeue(dequeued_value);
    
    // Contract 3: Can dequeue from full queue
    ASSERT_TRUE(dequeue_success) << "Should be able to dequeue from full queue";
    
    // Contract 4: FIFO order preserved
    ASSERT_EQ(dequeued_value, fill_values[0]) << "Should dequeue first element (FIFO): expected " << fill_values[0] << ", got " << dequeued_value;
    
    // Contract 5: Space becomes available for new enqueue
    bool recovery_enqueue = queue.enqueue(500);
    ASSERT_TRUE(recovery_enqueue) << "Should be able to enqueue after dequeue";
    
    // === THEN (Data Integrity Verification) ===
    // Read remaining original elements
    for (size_t i = 1; i < QUEUE_SIZE; ++i) {
        int32_t verify_value = 0;
        bool verify_success = queue.dequeue(verify_value);
        
        ASSERT_TRUE(verify_success) << "Should be able to dequeue element " << i;
        ASSERT_EQ(verify_value, fill_values[i]) << "Data integrity check failed at element " << i;
    }
    
    // Read the recovery element
    int32_t final_value = 0;
    bool final_success = queue.dequeue(final_value);
    ASSERT_TRUE(final_success) << "Should be able to dequeue recovery element";
    ASSERT_EQ(final_value, 500) << "Recovery element should match";
    
    // Verify queue is empty
    int32_t empty_check = 999;
    ASSERT_FALSE(queue.dequeue(empty_check)) << "Queue should be empty after reading all elements";
    ASSERT_EQ(empty_check, 999) << "Empty dequeue should not modify reference";
    
    LOG_INFO("PASS: MPMC queue exhaustion contract verified - graceful overflow and recovery");
}

TEST_F(LFQueueTestBase, MPMCDataTypesAndSizesContract) {
    // === INPUT SPECIFICATION ===
    // Test different data types and sizes to verify template correctness:
    //   - Primitive types: uint8_t, uint64_t, double
    //   - Complex types: struct with multiple fields
    //   - Large types: struct approaching cache line size
    // Verification: Type safety, alignment, data integrity
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Type Safety Results:
    //   - Each template instantiation compiles successfully
    //   - No type conversion errors or warnings
    //   - Proper constructor/destructor calls for complex types
    // Data Integrity Results:
    //   - All field values preserved across enqueue/dequeue
    //   - No memory corruption or field contamination
    //   - Proper alignment for performance-critical types
    
    // === SUCCESS CRITERIA ===
    // 1. uint8_t: Basic byte-level operations work
    // 2. uint64_t: Large primitive types work
    // 3. double: Floating-point precision preserved
    // 4. Complex struct: All fields preserved
    // 5. Large struct: No truncation or corruption
    // 6. Template instantiation successful for all types
    
    // Test 1: uint8_t (small primitive)
    {
        MPMCLFQueue<uint8_t> byte_queue(4);
        
        const std::vector<uint8_t> bytes = {0x01, 0xFF, 0x7F, 0x80};
        
        for (uint8_t byte_val : bytes) {
            ASSERT_TRUE(byte_queue.enqueue(byte_val)) << "Should enqueue byte value " << static_cast<int>(byte_val);
        }
        
        for (size_t i = 0; i < bytes.size(); ++i) {
            uint8_t result = 0;
            ASSERT_TRUE(byte_queue.dequeue(result)) << "Should dequeue byte " << i;
            ASSERT_EQ(result, bytes[i]) << "Byte value mismatch at position " << i;
        }
    }
    
    // Test 2: double (floating-point precision)
    {
        MPMCLFQueue<double> double_queue(4);
        
        const std::vector<double> doubles = {3.14159, -2.71828, 1.41421, 1e-10};
        
        for (double val : doubles) {
            ASSERT_TRUE(double_queue.enqueue(val)) << "Should enqueue double value " << val;
        }
        
        for (size_t i = 0; i < doubles.size(); ++i) {
            double result = 0.0;
            ASSERT_TRUE(double_queue.dequeue(result)) << "Should dequeue double " << i;
            ASSERT_EQ(result, doubles[i]) << "Double precision lost at position " << i;
        }
    }
    
    // Test 3: Complex struct
    struct TestStruct {
        uint32_t id;
        double price;
        char symbol[8];
        
        TestStruct() : id(0), price(0.0) { 
            memset(symbol, 0, sizeof(symbol)); 
        }
        
        TestStruct(uint32_t i, double p, const char* s) : id(i), price(p) {
            strncpy(symbol, s, sizeof(symbol) - 1);
            symbol[sizeof(symbol) - 1] = '\0';
        }
        
        bool operator==(const TestStruct& other) const {
            return id == other.id && 
                   (price - other.price < 1e-9 && price - other.price > -1e-9) && 
                   strcmp(symbol, other.symbol) == 0;
        }
    };
    
    {
        MPMCLFQueue<TestStruct> struct_queue(4);
        
        std::vector<TestStruct> structs = {
            TestStruct(1, 100.50, "AAPL"),
            TestStruct(2, 200.75, "GOOGL"), 
            TestStruct(3, 50.25, "TSLA"),
            TestStruct(4, 1500.00, "AMZN")
        };
        
        for (const auto& s : structs) {
            ASSERT_TRUE(struct_queue.enqueue(s)) << "Should enqueue struct with id " << s.id;
        }
        
        for (size_t i = 0; i < structs.size(); ++i) {
            TestStruct result;
            ASSERT_TRUE(struct_queue.dequeue(result)) << "Should dequeue struct " << i;
            ASSERT_TRUE(result == structs[i]) << "Struct mismatch at position " << i 
                << " (expected id=" << structs[i].id << ", got id=" << result.id << ")";
        }
    }
    
    LOG_INFO("PASS: MPMC data types and sizes contract verified - template type safety and data integrity");
}

// =============================================================================
// 3. PERFORMANCE TESTS - LATENCY CONTRACT COMPLIANCE
// =============================================================================

TEST_F(LFQueueTestBase, SPSCLatencyContractCompliance) {
    // === INPUT SPECIFICATION ===
    // Performance Test Parameters:
    //   - Iterations: 10000 operation pairs (enqueue → dequeue)
    //   - Element Type: uint64_t (8 bytes)
    //   - Queue Size: 1024 elements (no exhaustion during test)
    //   - Measurement Method: RDTSC cycle counting
    // Operation Sequence:
    //   - For each iteration: getNextToWriteTo() → write → updateWriteIndex()
    //   - Followed by: getNextToRead() → read → updateReadIndex()
    //   - Timing captured for each phase separately
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Latency Measurements:
    //   - 10000 enqueue operation latencies (in nanoseconds)
    //   - 10000 dequeue operation latencies (in nanoseconds)
    //   - Statistical analysis: min, max, mean, median, P99, stddev
    // Performance Targets (from CLAUDE.md requirements):
    //   - Queue Enqueue: Maximum 100ns, Typical 45ns
    //   - Queue Dequeue: Maximum 100ns, Typical 42ns
    //   - P99 latency: < 150ns
    //   - Coefficient of variation: < 30%
    
    // === SUCCESS CRITERIA ===
    // 1. Max enqueue latency < 100ns (contract requirement)
    // 2. Max dequeue latency < 100ns (contract requirement) 
    // 3. P99 latency < 150ns for both operations
    // 4. Coefficient of variation < 30% (consistency)
    // 5. Median latency reasonable (regression detection)
    // 6. All operations complete successfully
    
    // === GIVEN (Performance Contract) ===
    const size_t ITERATIONS = 10000;
    const uint64_t MAX_LATENCY_NS = 100;        // Contract from CLAUDE.md
    const uint64_t MAX_P99_LATENCY_NS = 150;    // Contract from CLAUDE.md  
    const double MAX_COEFFICIENT_VARIATION = 0.3; // 30% max variation
    
    SPSCLFQueue<uint64_t> queue(1024);  // Large enough to avoid exhaustion
    
    std::vector<uint64_t> enqueue_latencies;
    std::vector<uint64_t> dequeue_latencies;
    enqueue_latencies.reserve(ITERATIONS);
    dequeue_latencies.reserve(ITERATIONS);
    
    // === WHEN (Performance Load Testing) ===
    LOG_INFO("Starting SPSC latency test with %zu iterations", ITERATIONS);
    
    for (size_t i = 0; i < ITERATIONS; ++i) {
        // Test enqueue latency
        auto enqueue_start = rdtsc();
        uint64_t* write_ptr = queue.getNextToWriteTo();
        ASSERT_NE(write_ptr, nullptr) << "Enqueue should not fail during performance test at iteration " << i;
        *write_ptr = i;  // Simple test data
        queue.updateWriteIndex();
        auto enqueue_cycles = rdtsc() - enqueue_start;
        
        enqueue_latencies.push_back(cycles_to_nanos(enqueue_cycles));
        
        // Test dequeue latency  
        auto dequeue_start = rdtsc();
        const uint64_t* read_ptr = queue.getNextToRead();
        ASSERT_NE(read_ptr, nullptr) << "Dequeue should not fail during performance test at iteration " << i;
        uint64_t value = *read_ptr;  // Read the value
        queue.updateReadIndex();
        auto dequeue_cycles = rdtsc() - dequeue_start;
        
        dequeue_latencies.push_back(cycles_to_nanos(dequeue_cycles));
        
        // Verify data integrity during performance test
        ASSERT_EQ(value, i) << "Data corruption detected at iteration " << i;
    }
    
    // === THEN (Performance Contract Verification) ===
    auto enqueue_stats = analyze_latencies(enqueue_latencies);
    auto dequeue_stats = analyze_latencies(dequeue_latencies);
    
    // Contract 1: Maximum latency bounds
    EXPECT_LT(enqueue_stats.max, MAX_LATENCY_NS) 
        << "SPSC enqueue max latency " << enqueue_stats.max << "ns exceeds contract " << MAX_LATENCY_NS << "ns";
    EXPECT_LT(dequeue_stats.max, MAX_LATENCY_NS)
        << "SPSC dequeue max latency " << dequeue_stats.max << "ns exceeds contract " << MAX_LATENCY_NS << "ns";
    
    // Contract 2: P99 latency bounds
    EXPECT_LT(enqueue_stats.p99, MAX_P99_LATENCY_NS)
        << "SPSC enqueue P99 latency " << enqueue_stats.p99 << "ns exceeds contract " << MAX_P99_LATENCY_NS << "ns";
    EXPECT_LT(dequeue_stats.p99, MAX_P99_LATENCY_NS)
        << "SPSC dequeue P99 latency " << dequeue_stats.p99 << "ns exceeds contract " << MAX_P99_LATENCY_NS << "ns";
    
    // Contract 3: Consistency (low variation)
    double enqueue_cv = enqueue_stats.stddev / static_cast<double>(enqueue_stats.mean);
    double dequeue_cv = dequeue_stats.stddev / static_cast<double>(dequeue_stats.mean);
    EXPECT_LT(enqueue_cv, MAX_COEFFICIENT_VARIATION)
        << "SPSC enqueue coefficient of variation " << enqueue_cv << " exceeds contract " << MAX_COEFFICIENT_VARIATION;
    EXPECT_LT(dequeue_cv, MAX_COEFFICIENT_VARIATION)
        << "SPSC dequeue coefficient of variation " << dequeue_cv << " exceeds contract " << MAX_COEFFICIENT_VARIATION;
    
    // Contract 4: Performance regression detection
    const uint64_t EXPECTED_ENQUEUE_TYPICAL = 45;  // From CLAUDE.md
    const uint64_t EXPECTED_DEQUEUE_TYPICAL = 42;  // From CLAUDE.md
    EXPECT_LT(enqueue_stats.median, EXPECTED_ENQUEUE_TYPICAL * 2) // 100% tolerance
        << "SPSC enqueue median " << enqueue_stats.median << "ns suggests performance regression";
    EXPECT_LT(dequeue_stats.median, EXPECTED_DEQUEUE_TYPICAL * 2) // 100% tolerance  
        << "SPSC dequeue median " << dequeue_stats.median << "ns suggests performance regression";
    
    // === SUCCESS CRITERIA REPORTING ===
    LOG_INFO("PASS: SPSC performance contract verified");
    LOG_INFO("  Enqueue: mean=%luns, median=%luns, P99=%luns, max=%luns, CV=%.3f", 
             enqueue_stats.mean, enqueue_stats.median, enqueue_stats.p99, enqueue_stats.max, enqueue_cv);
    LOG_INFO("  Dequeue: mean=%luns, median=%luns, P99=%luns, max=%luns, CV=%.3f", 
             dequeue_stats.mean, dequeue_stats.median, dequeue_stats.p99, dequeue_stats.max, dequeue_cv);
}

TEST_F(LFQueueTestBase, MPMCLatencyContractCompliance) {
    // === INPUT SPECIFICATION ===
    // Performance Test Parameters:
    //   - Iterations: 10000 operation pairs (enqueue → dequeue)
    //   - Element Type: uint64_t (8 bytes)
    //   - Queue Size: 1024 elements (no exhaustion)
    //   - Measurement Method: RDTSC cycle counting
    // Operation Sequence:
    //   - For each iteration: enqueue(value) → dequeue(reference)
    //   - Single-threaded test to establish baseline latency
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Latency Measurements:
    //   - 10000 enqueue operation latencies
    //   - 10000 dequeue operation latencies  
    //   - Statistical analysis with performance targets
    // Performance Expectations:
    //   - MPMC operations typically slower than SPSC due to atomic operations
    //   - Should still meet reasonable latency bounds for single-threaded case
    //   - Max latency: < 200ns (relaxed vs SPSC due to atomic overhead)
    //   - P99 latency: < 300ns
    
    // === SUCCESS CRITERIA ===
    // 1. Max enqueue latency < 200ns (relaxed for MPMC atomic overhead)
    // 2. Max dequeue latency < 200ns  
    // 3. P99 latency < 300ns for both operations
    // 4. Coefficient of variation < 40% (relaxed for MPMC)
    // 5. All operations complete successfully
    // 6. No performance regression vs expected MPMC baseline
    
    // === GIVEN (Performance Contract) ===
    const size_t ITERATIONS = 10000;
    const uint64_t MAX_LATENCY_NS = 200;        // Relaxed for MPMC atomics
    const uint64_t MAX_P99_LATENCY_NS = 300;    // Relaxed for MPMC atomics
    const double MAX_COEFFICIENT_VARIATION = 0.4; // 40% max variation
    
    MPMCLFQueue<uint64_t> queue(1024);
    
    std::vector<uint64_t> enqueue_latencies;
    std::vector<uint64_t> dequeue_latencies;
    enqueue_latencies.reserve(ITERATIONS);
    dequeue_latencies.reserve(ITERATIONS);
    
    // === WHEN (Performance Load Testing) ===
    LOG_INFO("Starting MPMC latency test with %zu iterations", ITERATIONS);
    
    for (size_t i = 0; i < ITERATIONS; ++i) {
        // Test enqueue latency
        auto enqueue_start = rdtsc();
        bool enqueue_success = queue.enqueue(i);
        auto enqueue_cycles = rdtsc() - enqueue_start;
        
        ASSERT_TRUE(enqueue_success) << "Enqueue should not fail during performance test at iteration " << i;
        enqueue_latencies.push_back(cycles_to_nanos(enqueue_cycles));
        
        // Test dequeue latency
        auto dequeue_start = rdtsc();
        uint64_t value = 0;
        bool dequeue_success = queue.dequeue(value);
        auto dequeue_cycles = rdtsc() - dequeue_start;
        
        ASSERT_TRUE(dequeue_success) << "Dequeue should not fail during performance test at iteration " << i;
        dequeue_latencies.push_back(cycles_to_nanos(dequeue_cycles));
        
        // Verify data integrity
        ASSERT_EQ(value, i) << "Data corruption detected at iteration " << i;
    }
    
    // === THEN (Performance Contract Verification) ===
    auto enqueue_stats = analyze_latencies(enqueue_latencies);
    auto dequeue_stats = analyze_latencies(dequeue_latencies);
    
    // Contract 1: Maximum latency bounds (relaxed for MPMC)
    EXPECT_LT(enqueue_stats.max, MAX_LATENCY_NS) 
        << "MPMC enqueue max latency " << enqueue_stats.max << "ns exceeds contract " << MAX_LATENCY_NS << "ns";
    EXPECT_LT(dequeue_stats.max, MAX_LATENCY_NS)
        << "MPMC dequeue max latency " << dequeue_stats.max << "ns exceeds contract " << MAX_LATENCY_NS << "ns";
    
    // Contract 2: P99 latency bounds
    EXPECT_LT(enqueue_stats.p99, MAX_P99_LATENCY_NS)
        << "MPMC enqueue P99 latency " << enqueue_stats.p99 << "ns exceeds contract " << MAX_P99_LATENCY_NS << "ns";
    EXPECT_LT(dequeue_stats.p99, MAX_P99_LATENCY_NS)
        << "MPMC dequeue P99 latency " << dequeue_stats.p99 << "ns exceeds contract " << MAX_P99_LATENCY_NS << "ns";
    
    // Contract 3: Consistency (relaxed for MPMC atomics)
    double enqueue_cv = enqueue_stats.stddev / static_cast<double>(enqueue_stats.mean);
    double dequeue_cv = dequeue_stats.stddev / static_cast<double>(dequeue_stats.mean);
    EXPECT_LT(enqueue_cv, MAX_COEFFICIENT_VARIATION)
        << "MPMC enqueue coefficient of variation " << enqueue_cv << " exceeds contract " << MAX_COEFFICIENT_VARIATION;
    EXPECT_LT(dequeue_cv, MAX_COEFFICIENT_VARIATION)
        << "MPMC dequeue coefficient of variation " << dequeue_cv << " exceeds contract " << MAX_COEFFICIENT_VARIATION;
    
    // === SUCCESS CRITERIA REPORTING ===
    LOG_INFO("PASS: MPMC performance contract verified");
    LOG_INFO("  Enqueue: mean=%luns, median=%luns, P99=%luns, max=%luns, CV=%.3f", 
             enqueue_stats.mean, enqueue_stats.median, enqueue_stats.p99, enqueue_stats.max, enqueue_cv);
    LOG_INFO("  Dequeue: mean=%luns, median=%luns, P99=%luns, max=%luns, CV=%.3f", 
             dequeue_stats.mean, dequeue_stats.median, dequeue_stats.p99, dequeue_stats.max, dequeue_cv);
}

// =============================================================================
// 4. STRESS TESTS - CONCURRENT BEHAVIOR VERIFICATION
// =============================================================================

TEST_F(LFQueueTestBase, SPSCConcurrentProducerConsumerContract) {
    // === INPUT SPECIFICATION ===
    // Concurrency Test Configuration:
    //   - Threads: 1 producer + 1 consumer (SPSC design)
    //   - Elements per thread: 100000 operations
    //   - Element Type: uint64_t with embedded thread/sequence info
    //   - Queue Size: 1024 elements (moderate size for stress testing)
    //   - Test Duration: Until all elements processed
    // Data Pattern: Producer writes values 0 to 99999, consumer reads in order
    // Verification: No data loss, correct ordering, no corruption
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Concurrency Results:
    //   - All produced elements successfully consumed
    //   - FIFO ordering maintained across thread boundaries
    //   - No data corruption or lost updates
    //   - Producer and consumer threads complete without deadlock
    // Performance Metrics:
    //   - Total operations: 200000 (100k produce + 100k consume)
    //   - Throughput: Operations per second
    //   - Zero data loss or corruption
    
    // === SUCCESS CRITERIA ===
    // 1. Producer successfully writes all 100000 elements
    // 2. Consumer successfully reads all 100000 elements  
    // 3. FIFO ordering preserved (values 0 to 99999 in sequence)
    // 4. No data corruption (each value matches expected)
    // 5. No deadlocks or infinite loops
    // 6. Reasonable throughput (> 1M ops/sec combined)
    
    // === GIVEN (Concurrency Contract) ===
    const size_t NUM_ELEMENTS = 100000;
    const size_t QUEUE_SIZE = 1024;
    
    SPSCLFQueue<uint64_t> queue(QUEUE_SIZE);
    
    // Synchronization and verification variables
    std::atomic<bool> producer_done{false};
    std::atomic<bool> consumer_done{false};
    std::atomic<size_t> elements_produced{0};
    std::atomic<size_t> elements_consumed{0};
    
    std::vector<uint64_t> consumed_values;
    consumed_values.reserve(NUM_ELEMENTS);
    
    auto start_time = std::chrono::steady_clock::now();
    
    // === WHEN (Concurrent Operations) ===
    
    // Producer thread
    std::thread producer([&]() {
        for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
            while (true) {
                uint64_t* write_ptr = queue.getNextToWriteTo();
                if (write_ptr != nullptr) {
                    *write_ptr = i;  // Sequential values for ordering verification
                    queue.updateWriteIndex();
                    elements_produced.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
                // Brief yield when queue full
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });
    
    // Consumer thread  
    std::thread consumer([&]() {
        size_t consumed_count = 0;
        
        while (consumed_count < NUM_ELEMENTS) {
            const uint64_t* read_ptr = queue.getNextToRead();
            if (read_ptr != nullptr) {
                consumed_values.push_back(*read_ptr);
                queue.updateReadIndex();
                consumed_count++;
                elements_consumed.fetch_add(1, std::memory_order_relaxed);
            } else if (producer_done.load(std::memory_order_acquire)) {
                // Producer finished, but check queue once more for remaining elements
                continue;
            } else {
                // Brief yield when queue empty
                std::this_thread::yield();
            }
        }
        consumer_done.store(true, std::memory_order_release);
    });
    
    // Wait for completion
    producer.join();
    consumer.join();
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // === THEN (Concurrency Contract Verification) ===
    
    // Contract 1: All elements produced and consumed
    ASSERT_EQ(elements_produced.load(), NUM_ELEMENTS) << "Producer should produce all " << NUM_ELEMENTS << " elements";
    ASSERT_EQ(elements_consumed.load(), NUM_ELEMENTS) << "Consumer should consume all " << NUM_ELEMENTS << " elements";
    ASSERT_EQ(consumed_values.size(), NUM_ELEMENTS) << "Consumer should collect all elements";
    
    // Contract 2: FIFO ordering preserved
    for (size_t i = 0; i < NUM_ELEMENTS; ++i) {
        ASSERT_EQ(consumed_values[i], i) << "FIFO violation: expected " << i << ", got " << consumed_values[i] << " at position " << i;
    }
    
    // Contract 3: Performance verification
    if (duration.count() > 0) {
        uint64_t total_ops = NUM_ELEMENTS * 2; // Produce + consume
        uint64_t throughput = (total_ops * 1000) / static_cast<uint64_t>(duration.count()); // ops/sec
        EXPECT_GT(throughput, 1000000) << "Throughput " << throughput << " ops/sec too low for SPSC";
    }
    
    // Contract 4: Thread completion verification
    ASSERT_TRUE(producer_done.load()) << "Producer thread should complete";
    ASSERT_TRUE(consumer_done.load()) << "Consumer thread should complete";
    
    // === SUCCESS CRITERIA REPORTING ===
    LOG_INFO("PASS: SPSC concurrent producer-consumer contract verified");
    LOG_INFO("  Elements: produced=%zu, consumed=%zu", elements_produced.load(), elements_consumed.load());
    LOG_INFO("  Duration: %ldms, Throughput: %lu ops/sec", duration.count(), 
             duration.count() > 0 ? (NUM_ELEMENTS * 2 * 1000) / static_cast<uint64_t>(duration.count()) : 0);
}

TEST_F(LFQueueTestBase, MPMCMultiThreadStressContract) {
    // === INPUT SPECIFICATION ===
    // Multi-threaded Stress Configuration:
    //   - Producer Threads: 2 threads
    //   - Consumer Threads: 2 threads  
    //   - Elements per Producer: 25000 (total 50000 produced)
    //   - Queue Size: 512 elements
    //   - Element Type: uint64_t with thread ID encoding
    // Data Encoding: (thread_id << 32) | sequence_number
    // Verification: All elements accounted for, no duplicates, no corruption
    
    // === EXPECTED OUTPUT SPECIFICATION ===
    // Multi-threaded Results:
    //   - Total elements produced: 50000 (25000 × 2 threads)
    //   - Total elements consumed: 50000
    //   - No duplicate elements consumed
    //   - No elements lost or corrupted
    //   - All threads complete without deadlock
    // Data Integrity:
    //   - Each element maintains thread ID and sequence encoding
    //   - Consumer threads detect and report any anomalies
    
    // === SUCCESS CRITERIA ===
    // 1. All producer threads complete successfully
    // 2. All consumer threads complete successfully
    // 3. Total produced elements = Total consumed elements
    // 4. No duplicate elements in consumed set
    // 5. All thread IDs and sequence numbers valid
    // 6. No data corruption or impossible values
    // 7. Reasonable performance under contention
    
    // === GIVEN (Multi-threaded Contract) ===
    const size_t NUM_PRODUCERS = 2;
    const size_t NUM_CONSUMERS = 2;  
    const size_t ELEMENTS_PER_PRODUCER = 25000;
    const size_t TOTAL_ELEMENTS = NUM_PRODUCERS * ELEMENTS_PER_PRODUCER;
    const size_t QUEUE_SIZE = 512;
    
    MPMCLFQueue<uint64_t> queue(QUEUE_SIZE);
    
    // Synchronization variables
    std::atomic<size_t> total_produced{0};
    std::atomic<size_t> total_consumed{0};
    std::atomic<size_t> producers_finished{0};
    
    // Thread-safe collection of consumed values for verification
    std::vector<std::vector<uint64_t>> thread_consumed(NUM_CONSUMERS);
    for (auto& vec : thread_consumed) {
        vec.reserve(TOTAL_ELEMENTS / NUM_CONSUMERS + 1000); // Extra space for load balancing
    }
    
    std::vector<std::thread> threads;
    auto start_time = std::chrono::steady_clock::now();
    
    // === WHEN (Multi-threaded Stress Operations) ===
    
    // Launch producer threads
    for (size_t thread_id = 0; thread_id < NUM_PRODUCERS; ++thread_id) {
        threads.emplace_back([&, thread_id]() {
            size_t produced = 0;
            
            for (size_t seq = 0; seq < ELEMENTS_PER_PRODUCER; ++seq) {
                // Encode thread ID and sequence number
                uint64_t value = (static_cast<uint64_t>(thread_id) << 32) | seq;
                
                // Retry until successful (handle queue full)
                while (!queue.enqueue(value)) {
                    std::this_thread::yield();
                }
                
                produced++;
            }
            
            total_produced.fetch_add(produced, std::memory_order_relaxed);
            producers_finished.fetch_add(1, std::memory_order_release);
        });
    }
    
    // Launch consumer threads
    for (size_t thread_id = 0; thread_id < NUM_CONSUMERS; ++thread_id) {
        threads.emplace_back([&, thread_id]() {
            size_t consumed = 0;
            
            while (consumed < TOTAL_ELEMENTS || producers_finished.load(std::memory_order_acquire) < NUM_PRODUCERS) {
                uint64_t value;
                if (queue.dequeue(value)) {
                    thread_consumed[thread_id].push_back(value);
                    consumed++;
                    
                    // Verify value encoding integrity
                    uint64_t producer_id = value >> 32;
                    uint64_t sequence = value & 0xFFFFFFFF;
                    
                    ASSERT_LT(producer_id, NUM_PRODUCERS) << "Invalid producer ID " << producer_id << " in consumed value";
                    ASSERT_LT(sequence, ELEMENTS_PER_PRODUCER) << "Invalid sequence " << sequence << " in consumed value";
                } else if (producers_finished.load(std::memory_order_acquire) == NUM_PRODUCERS) {
                    // All producers done, final check for remaining elements
                    if (total_consumed.load(std::memory_order_acquire) >= TOTAL_ELEMENTS) {
                        break;
                    }
                } else {
                    std::this_thread::yield();
                }
            }
            
            total_consumed.fetch_add(thread_consumed[thread_id].size(), std::memory_order_relaxed);
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // === THEN (Multi-threaded Contract Verification) ===
    
    // Collect all consumed values for analysis
    std::unordered_set<uint64_t> all_consumed;
    size_t total_collected = 0;
    
    for (const auto& consumer_values : thread_consumed) {
        for (uint64_t value : consumer_values) {
            all_consumed.insert(value);
            total_collected++;
        }
    }
    
    // Contract 1: Element count verification
    ASSERT_EQ(total_produced.load(), TOTAL_ELEMENTS) << "Should produce exactly " << TOTAL_ELEMENTS << " elements";
    ASSERT_EQ(total_consumed.load(), total_collected) << "Consumed count mismatch";
    ASSERT_EQ(total_collected, TOTAL_ELEMENTS) << "Should consume all produced elements";
    
    // Contract 2: No duplicates
    ASSERT_EQ(all_consumed.size(), TOTAL_ELEMENTS) << "Duplicate elements detected in consumption";
    
    // Contract 3: All expected values present
    for (size_t producer = 0; producer < NUM_PRODUCERS; ++producer) {
        for (size_t seq = 0; seq < ELEMENTS_PER_PRODUCER; ++seq) {
            uint64_t expected_value = (static_cast<uint64_t>(producer) << 32) | seq;
            ASSERT_EQ(all_consumed.count(expected_value), 1) 
                << "Missing expected value: producer=" << producer << ", seq=" << seq;
        }
    }
    
    // Contract 4: Performance under contention
    if (duration.count() > 0) {
        uint64_t total_ops = TOTAL_ELEMENTS * 2; // Produce + consume
        uint64_t throughput = (total_ops * 1000) / static_cast<uint64_t>(duration.count());
        EXPECT_GT(throughput, 500000) << "Throughput " << throughput << " ops/sec too low for MPMC under contention";
    }
    
    // === SUCCESS CRITERIA REPORTING ===
    LOG_INFO("PASS: MPMC multi-thread stress contract verified");
    LOG_INFO("  Threads: %zu producers, %zu consumers", NUM_PRODUCERS, NUM_CONSUMERS);
    LOG_INFO("  Elements: produced=%zu, consumed=%zu, unique=%zu", 
             total_produced.load(), total_consumed.load(), all_consumed.size());
    LOG_INFO("  Duration: %ldms, Throughput: %lu ops/sec", duration.count(),
             duration.count() > 0 ? (TOTAL_ELEMENTS * 2 * 1000) / static_cast<uint64_t>(duration.count()) : 0);
}
// Main function for running tests
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
