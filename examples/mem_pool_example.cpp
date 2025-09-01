#include "mem_pool_examples.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>

#include "../common/mem_pool.h"
#include "../common/thread_utils.h"
#include "../common/macros.h"
#include "../common/logging.h"

using namespace Common;
using namespace std::chrono;

// Test object with some data
struct TestObject {
  uint64_t id;
  double value;
  char data[48]; // Pad to cache line
  
  TestObject(uint64_t i = 0, double v = 0.0) : id(i), value(v) {}
  ~TestObject() {
    // Destructor to verify it's called
  }
};

// Example 1: Basic Memory Pool Usage
void basicMemPoolExample() {
  std::cout << "\n=== Basic Memory Pool Example ===" << std::endl;
  
  MemPool<TestObject> pool(10);
  
  std::cout << "Pool capacity: " << pool.capacity() << std::endl;
  std::cout << "Initial available: " << pool.available() << std::endl;
  
  // Allocate objects
  std::vector<TestObject*> objects;
  for (size_t i = 0; i < 5; ++i) {
    auto* obj = pool.allocate(i, static_cast<double>(i) * 1.5);
    objects.push_back(obj);
    std::cout << "Allocated object " << i << ", available: " << pool.available() << std::endl;
  }
  
  // Use objects
  for (auto* obj : objects) {
    std::cout << "Object id: " << obj->id << ", value: " << obj->value << std::endl;
  }
  
  // Deallocate some
  for (size_t i = 0; i < 3; ++i) {
    pool.deallocate(objects[i]);
    std::cout << "Deallocated object " << i << ", available: " << pool.available() << std::endl;
  }
  
  // Allocate again (should reuse memory)
  auto* reused = pool.allocate(uint64_t{99}, 99.9);
  std::cout << "Allocated new object, id: " << reused->id << std::endl;
  
  // Cleanup
  pool.deallocate(reused);
  pool.deallocate(objects[3]);
  pool.deallocate(objects[4]);
}

// Example 2: Lock-Free Memory Pool
void lockFreeMemPoolExample() {
  std::cout << "\n=== Lock-Free Memory Pool Example ===" << std::endl;
  
  LFMemPool<TestObject> pool(100);
  
  constexpr int NUM_THREADS = 4;
  constexpr int OPS_PER_THREAD = 1000;
  std::atomic<int> success_count{0};
  std::atomic<int> fail_count{0};
  
  std::vector<std::unique_ptr<std::thread>> threads;
  
  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.push_back(createAndStartThread(t, "Worker" + std::to_string(t),
      [&pool, &success_count, &fail_count, t, OPS_PER_THREAD]() {
        std::vector<TestObject*> local_objects;
        
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
          // Allocate
          auto* obj = pool.allocate(static_cast<uint64_t>(t * 1000 + i), static_cast<double>(i) * 0.5);
          if (obj) {
            local_objects.push_back(obj);
            success_count.fetch_add(1);
            
            // Deallocate some to create churn
            if (local_objects.size() > 10 && i % 3 == 0) {
              pool.deallocate(local_objects.back());
              local_objects.pop_back();
            }
          } else {
            fail_count.fetch_add(1);
          }
        }
        
        // Cleanup
        for (auto* obj : local_objects) {
          pool.deallocate(obj);
        }
      }));
  }
  
  // Wait for threads
  for (auto& t : threads) {
    t->join();
  }
  
  std::cout << "Successful allocations: " << success_count.load() << std::endl;
  std::cout << "Failed allocations: " << fail_count.load() << std::endl;
  std::cout << "Final pool size: " << pool.size() << std::endl;
}

// Example 3: Performance Comparison
void performanceComparison() {
  std::cout << "\n=== Memory Pool Performance Comparison ===" << std::endl;
  
  constexpr size_t POOL_SIZE = 1000;
  constexpr size_t ITERATIONS = 1000;  // Reduced for reasonable test duration
  
  // Test standard memory pool
  {
    MemPool<TestObject> pool(POOL_SIZE);
    std::vector<TestObject*> objects;
    objects.reserve(POOL_SIZE);
    
    auto start = high_resolution_clock::now();
    
    for (size_t i = 0; i < ITERATIONS; ++i) {
      // Allocate batch
      for (size_t j = 0; j < POOL_SIZE / 2; ++j) {
        auto* obj = pool.allocate(i, static_cast<double>(j) * 1.0);
        if (obj != nullptr) {
          objects.push_back(obj);
        } else {
          // Pool exhausted - this shouldn't happen with proper sizing
          break;
        }
      }
      
      // Deallocate batch
      for (auto* obj : objects) {
        if (obj != nullptr) {
          pool.deallocate(obj);
        }
      }
      objects.clear();
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start).count();
    
    std::cout << "Standard MemPool: " << duration << " μs for " 
              << ITERATIONS * POOL_SIZE / 2 << " allocations" << std::endl;
    std::cout << "Average: " << (static_cast<double>(duration) * 1000.0) / (ITERATIONS * POOL_SIZE / 2) 
              << " ns per allocation" << std::endl;
  }
  
  // Test heap allocation for comparison
  {
    std::vector<TestObject*> objects;
    objects.reserve(POOL_SIZE);
    
    auto start = high_resolution_clock::now();
    
    for (size_t i = 0; i < ITERATIONS; ++i) {
      // Allocate batch
      for (size_t j = 0; j < POOL_SIZE / 2; ++j) {
        objects.push_back(new TestObject(i, static_cast<double>(j) * 1.0));
      }
      
      // Deallocate batch
      for (auto* obj : objects) {
        delete obj;
      }
      objects.clear();
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start).count();
    
    std::cout << "Heap allocation: " << duration << " μs for " 
              << ITERATIONS * POOL_SIZE / 2 << " allocations" << std::endl;
    std::cout << "Average: " << (static_cast<double>(duration) * 1000.0) / (ITERATIONS * POOL_SIZE / 2) 
              << " ns per allocation" << std::endl;
  }
}

// Example 4: Memory Pool Stress Test
void stressTest() {
  std::cout << "\n=== Memory Pool Stress Test ===" << std::endl;
  
  LFMemPool<TestObject> pool(1000);
  constexpr int NUM_THREADS = 8;
  constexpr int DURATION_MS = 1000;
  
  std::atomic<bool> stop{false};
  std::atomic<uint64_t> total_ops{0};
  
  std::vector<std::unique_ptr<std::thread>> threads;
  
  for (int t = 0; t < NUM_THREADS; ++t) {
    threads.push_back(createAndStartThread(t % 4, "Stress" + std::to_string(t),
      [&pool, &stop, &total_ops, t]() {
        uint64_t local_ops = 0;
        std::vector<TestObject*> local_objects;
        
        while (!stop.load()) {
          // Random allocation/deallocation pattern
          if (local_objects.size() < 50 || (rand() % 2 == 0)) {
            auto* obj = pool.allocate(local_ops, static_cast<double>(t) * 1.0);
            if (obj) {
              local_objects.push_back(obj);
              local_ops++;
            }
          } else if (!local_objects.empty()) {
            size_t idx = static_cast<size_t>(rand()) % local_objects.size();
            pool.deallocate(local_objects[idx]);
            local_objects.erase(local_objects.begin() + static_cast<std::vector<TestObject*>::difference_type>(idx));
            local_ops++;
          }
        }
        
        // Cleanup
        for (auto* obj : local_objects) {
          pool.deallocate(obj);
        }
        
        total_ops.fetch_add(local_ops);
      }));
  }
  
  // Run for specified duration
  std::this_thread::sleep_for(milliseconds(DURATION_MS));
  stop.store(true);
  
  // Wait for threads
  for (auto& t : threads) {
    t->join();
  }
  
  std::cout << "Total operations in " << DURATION_MS << "ms: " << total_ops.load() << std::endl;
  std::cout << "Operations per second: " << (static_cast<double>(total_ops.load()) * 1000.0 / DURATION_MS) << std::endl;
  std::cout << "Final pool utilization: " << pool.size() << "/" << pool.capacity() << std::endl;
}

void memPoolExamples() {
  std::cout << "=== Memory Pool V2 Examples ===" << std::endl;
  std::cout << "Cache line size: " << CACHE_LINE_SIZE << " bytes" << std::endl;
  std::cout << "TestObject size: " << sizeof(TestObject) << " bytes" << std::endl;
  
  LOG_INFO("Starting Memory Pool examples");
  LOG_INFO("Cache line size: %d bytes", CACHE_LINE_SIZE);
  LOG_INFO("TestObject size: %zu bytes", sizeof(TestObject));
  
  basicMemPoolExample();
  LOG_INFO("Basic memory pool example completed");
  
  lockFreeMemPoolExample();
  LOG_INFO("Lock-free memory pool example completed");
  
  performanceComparison();
  LOG_INFO("Performance comparison completed");
  
  stressTest();
  LOG_INFO("Stress test completed");
  
  std::cout << "\n=== Memory Pool examples completed ===" << std::endl;
  LOG_INFO("All memory pool examples completed successfully");
}