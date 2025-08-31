/**
 * Test ZeroPolicy functionality
 */

#include <gtest/gtest.h>
#include <vector>
#include <cstring>
#include "bldg_blocks/mem_pool.h"

using namespace BldgBlocks;

// Helper to check if memory is zeroed
static bool isZeroed(void* ptr, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(ptr);
    for (size_t i = 0; i < size; ++i) {
        if (bytes[i] != 0) return false;
    }
    return true;
}

// Helper to write pattern to memory
static void writePattern(void* ptr, size_t size, uint8_t pattern) {
    memset(ptr, pattern, size);
}

TEST(ZeroPolicy, NonePolicy_NoZeroing) {
    // Pool with no zeroing policy
    MemoryPool<128, 10, ZeroPolicy::None> pool;
    
    // Allocate, write pattern, deallocate
    void* ptr1 = pool.allocate();
    ASSERT_NE(ptr1, nullptr);
    writePattern(ptr1, 128, 0xAB);
    pool.deallocate(ptr1);
    
    // Allocate again - should get same block with old data
    void* ptr2 = pool.allocate();
    ASSERT_EQ(ptr1, ptr2) << "Should reuse same block";
    
    // Memory should NOT be zeroed (contains old pattern)
    EXPECT_FALSE(isZeroed(ptr2, 128)) << "ZeroPolicy::None should not zero memory";
    
    pool.deallocate(ptr2);
}

TEST(ZeroPolicy, OnAcquirePolicy_ZerosOnAllocation) {
    // Pool that zeros on allocation
    MemoryPool<128, 10, ZeroPolicy::OnAcquire> pool;
    
    // Allocate, write pattern, deallocate
    void* ptr1 = pool.allocate();
    ASSERT_NE(ptr1, nullptr);
    
    // Should be zeroed on allocation
    EXPECT_TRUE(isZeroed(ptr1, 128)) << "ZeroPolicy::OnAcquire should zero on allocation";
    
    writePattern(ptr1, 128, 0xCD);
    pool.deallocate(ptr1);
    
    // Allocate again - should get zeroed memory
    void* ptr2 = pool.allocate();
    ASSERT_EQ(ptr1, ptr2) << "Should reuse same block";
    EXPECT_TRUE(isZeroed(ptr2, 128)) << "Memory should be zeroed on reallocation";
    
    pool.deallocate(ptr2);
}

TEST(ZeroPolicy, OnReleasePolicy_ZerosOnDeallocation) {
    // Pool that zeros on deallocation
    MemoryPool<128, 10, ZeroPolicy::OnRelease> pool;
    
    // Allocate and write pattern
    void* ptr1 = pool.allocate();
    ASSERT_NE(ptr1, nullptr);
    
    // Not necessarily zeroed on allocation with OnRelease
    writePattern(ptr1, 128, 0xEF);
    
    // Deallocate - this should zero the memory
    pool.deallocate(ptr1);
    
    // Allocate again - memory was zeroed by previous deallocation
    void* ptr2 = pool.allocate();
    ASSERT_EQ(ptr1, ptr2) << "Should reuse same block";
    EXPECT_TRUE(isZeroed(ptr2, 128)) << "Memory should be zeroed from previous deallocation";
    
    pool.deallocate(ptr2);
}

TEST(ZeroPolicy, AllocateZeroed_AlwaysZeros) {
    // Test allocate_zeroed() with different policies
    
    // Even with None policy, allocate_zeroed should zero
    {
        MemoryPool<128, 10, ZeroPolicy::None> pool;
        void* ptr = pool.allocate_zeroed();
        ASSERT_NE(ptr, nullptr);
        EXPECT_TRUE(isZeroed(ptr, 128)) << "allocate_zeroed() should always zero";
        pool.deallocate(ptr);
    }
    
    // With OnAcquire, allocate_zeroed should also work (no double zeroing)
    {
        MemoryPool<128, 10, ZeroPolicy::OnAcquire> pool;
        void* ptr = pool.allocate_zeroed();
        ASSERT_NE(ptr, nullptr);
        EXPECT_TRUE(isZeroed(ptr, 128)) << "allocate_zeroed() should zero";
        pool.deallocate(ptr);
    }
}

TEST(ZeroPolicy, MixedUsage_CorrectBehavior) {
    // Test mixing regular and zeroed allocations
    MemoryPool<128, 10, ZeroPolicy::None> pool;
    
    // Regular allocation - not zeroed
    void* ptr1 = pool.allocate();
    writePattern(ptr1, 128, 0x55);
    pool.deallocate(ptr1);
    
    // Regular allocation again - gets old data
    void* ptr2 = pool.allocate();
    ASSERT_EQ(ptr1, ptr2);
    EXPECT_FALSE(isZeroed(ptr2, 128)) << "Regular allocate with None policy keeps old data";
    pool.deallocate(ptr2);
    
    // Zeroed allocation - always zeros
    void* ptr3 = pool.allocate_zeroed();
    ASSERT_EQ(ptr1, ptr3);
    EXPECT_TRUE(isZeroed(ptr3, 128)) << "allocate_zeroed always zeros";
    pool.deallocate(ptr3);
}

#ifdef TESTING
TEST(ZeroPolicy, TestAccessors_WorkCorrectly) {
    MemoryPool<128, 10, ZeroPolicy::None> pool;
    
    // Verify policy accessor
    EXPECT_EQ(pool.getPolicy(), ZeroPolicy::None);
    
    // Allocate and verify state
    void* ptr = pool.allocate();
    ASSERT_NE(ptr, nullptr);
    
    // Test isAllocated accessor
    EXPECT_TRUE(pool.isAllocated(ptr)) << "Block should be marked as allocated";
    
    pool.deallocate(ptr);
    EXPECT_FALSE(pool.isAllocated(ptr)) << "Block should be marked as free after deallocation";
}
#endif

// Performance comparison test
TEST(ZeroPolicy, PerformanceComparison) {
    const int ITERATIONS = 100000;
    
    // Measure None policy (fastest)
    {
        MemoryPool<256, 1000, ZeroPolicy::None> pool;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < ITERATIONS; ++i) {
            void* ptr = pool.allocate();
            if (ptr) pool.deallocate(ptr);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto none_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "ZeroPolicy::None: " << none_duration.count() << " μs\n";
    }
    
    // Measure OnAcquire policy
    {
        MemoryPool<256, 1000, ZeroPolicy::OnAcquire> pool;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < ITERATIONS; ++i) {
            void* ptr = pool.allocate();
            if (ptr) pool.deallocate(ptr);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto acquire_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "ZeroPolicy::OnAcquire: " << acquire_duration.count() << " μs\n";
    }
    
    // Measure OnRelease policy
    {
        MemoryPool<256, 1000, ZeroPolicy::OnRelease> pool;
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < ITERATIONS; ++i) {
            void* ptr = pool.allocate();
            if (ptr) pool.deallocate(ptr);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto release_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "ZeroPolicy::OnRelease: " << release_duration.count() << " μs\n";
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}