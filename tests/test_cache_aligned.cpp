#include "../bldg_blocks/types.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <thread>
#include <chrono>

using namespace BldgBlocks;

void test_basic_construction() {
    std::cout << "Testing basic construction..." << std::endl;
    
    // Test default construction
    {
        CacheAligned<int> x;
        assert(x.value == 0);
    }
    
    // Test value construction
    {
        CacheAligned<int> x{42};
        assert(x.value == 42);
    }
    
    // Test with different types
    {
        CacheAligned<double> d{3.14};
        assert(d.value == 3.14);
        
        CacheAligned<char> c{'A'};
        assert(c.value == 'A');
    }
    
    std::cout << "  ✓ Basic construction tests passed" << std::endl;
}

void test_atomic_types() {
    std::cout << "Testing atomic types..." << std::endl;
    
    // Test atomic construction with different integer types
    {
        CacheAligned<std::atomic<uint64_t>> counter{0};  // int literal
        assert(counter.value.load() == 0);
        
        CacheAligned<std::atomic<uint64_t>> counter2{uint64_t{100}};
        assert(counter2.value.load() == 100);
    }
    
    // Test atomic operations
    {
        CacheAligned<std::atomic<uint64_t>> counter{0};
        counter.value.fetch_add(1);
        assert(counter.value.load() == 1);
        
        counter.value.store(10);
        assert(counter.value.load() == 10);
    }
    
    std::cout << "  ✓ Atomic type tests passed" << std::endl;
}

void test_alignment() {
    std::cout << "Testing alignment properties..." << std::endl;
    
    // Test single object alignment
    {
        CacheAligned<char> x;
        assert(reinterpret_cast<uintptr_t>(&x) % CACHE_LINE_SIZE == 0);
        assert(alignof(decltype(x)) >= CACHE_LINE_SIZE);
    }
    
    // Test array stride maintains alignment
    {
        CacheAligned<char> items[4];
        for (int i = 0; i < 3; ++i) {
            auto* p1 = &items[i];
            auto* p2 = &items[i+1];
            ptrdiff_t diff = reinterpret_cast<char*>(p2) - reinterpret_cast<char*>(p1);
            assert(diff == CACHE_LINE_SIZE);
            assert(reinterpret_cast<uintptr_t>(p1) % CACHE_LINE_SIZE == 0);
        }
    }
    
    // Test size is multiple of cache line
    {
        assert(sizeof(CacheAligned<char>) == CACHE_LINE_SIZE);
        assert(sizeof(CacheAligned<int>) == CACHE_LINE_SIZE);
        assert(sizeof(CacheAligned<double>) == CACHE_LINE_SIZE);
        assert(sizeof(CacheAligned<std::atomic<uint64_t>>) == CACHE_LINE_SIZE);
    }
    
    std::cout << "  ✓ Alignment tests passed" << std::endl;
}

void test_assignment() {
    std::cout << "Testing assignment operations..." << std::endl;
    
    // Non-atomic assignment
    {
        CacheAligned<int> x{10};
        x = 20;
        assert(x.value == 20);
    }
    
    // Atomic assignment
    {
        CacheAligned<std::atomic<uint64_t>> counter{10};
        counter = 20;  // Should use store with relaxed ordering
        assert(counter.value.load() == 20);
    }
    
    std::cout << "  ✓ Assignment tests passed" << std::endl;
}

void test_false_sharing_prevention() {
    std::cout << "Testing false sharing prevention..." << std::endl;
    
    struct TestData {
        CacheAligned<std::atomic<uint64_t>> counter1{0};
        CacheAligned<std::atomic<uint64_t>> counter2{0};
    };
    
    TestData data;
    const int iterations = 1'000'000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::thread t1([&data, iterations]() {
        for (int i = 0; i < iterations; ++i) {
            data.counter1.value.fetch_add(1, std::memory_order_relaxed);
        }
    });
    
    std::thread t2([&data, iterations]() {
        for (int i = 0; i < iterations; ++i) {
            data.counter2.value.fetch_add(1, std::memory_order_relaxed);
        }
    });
    
    t1.join();
    t2.join();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    assert(data.counter1.value.load() == iterations);
    assert(data.counter2.value.load() == iterations);
    
    std::cout << "  ✓ False sharing prevention test passed" << std::endl;
    std::cout << "    Time for " << iterations << " iterations: " << duration << " μs" << std::endl;
    
    // Verify counters are on different cache lines
    auto* addr1 = &data.counter1.value;
    auto* addr2 = &data.counter2.value;
    ptrdiff_t diff = reinterpret_cast<char*>(addr2) - reinterpret_cast<char*>(addr1);
    assert(std::abs(diff) >= CACHE_LINE_SIZE);
    std::cout << "    Address separation: " << std::abs(diff) << " bytes (>= " << CACHE_LINE_SIZE << ")" << std::endl;
}

void test_perfect_forwarding() {
    std::cout << "Testing perfect forwarding..." << std::endl;
    
    // Test with rvalue
    {
        std::string s = "test";
        CacheAligned<std::string> x{std::move(s)};
        assert(x.value == "test");
        assert(s.empty()); // Should have been moved from
    }
    
    // Test with temporary
    {
        CacheAligned<std::vector<int>> v{std::vector<int>{1, 2, 3}};
        assert(v.value.size() == 3);
        assert(v.value[0] == 1);
    }
    
    std::cout << "  ✓ Perfect forwarding tests passed" << std::endl;
}

int main() {
    std::cout << "\n=== CacheAligned Test Suite ===" << std::endl;
    std::cout << "Cache line size: " << CACHE_LINE_SIZE << " bytes" << std::endl;
    std::cout << "sizeof(CacheAligned<char>): " << sizeof(CacheAligned<char>) << " bytes" << std::endl;
    std::cout << "sizeof(CacheAligned<atomic<uint64_t>>): " << sizeof(CacheAligned<std::atomic<uint64_t>>) << " bytes\n" << std::endl;
    
    test_basic_construction();
    test_atomic_types();
    test_alignment();
    test_assignment();
    test_false_sharing_prevention();
    test_perfect_forwarding();
    
    std::cout << "\n✅ All CacheAligned tests passed successfully!" << std::endl;
    return 0;
}