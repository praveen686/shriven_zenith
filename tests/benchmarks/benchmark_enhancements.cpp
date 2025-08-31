#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <iomanip>
#include <future>
#include <deque>

#include "bldg_blocks/lf_queue.h"
#include "bldg_blocks/mem_pool.h"
#include "bldg_blocks/thread_utils.h"
#include "bldg_blocks/macros.h"

using namespace BldgBlocks;
using namespace std::chrono;

// Forward declarations
static void benchmarkSPSCQueue();
static void benchmarkMemPool();
static void benchmarkThreadCreation();

// Test data structure
struct TestObject {
  uint64_t id;
  uint64_t timestamp;
  char padding[48]; // Make it cache-line sized
  
  TestObject(uint64_t i = 0) : id(i), timestamp(rdtsc()) {}
};

// Benchmark SPSC Queue
static void benchmarkSPSCQueue() {
  std::cout << "\n=== SPSC Queue Benchmark ===" << std::endl;
  
  constexpr size_t QUEUE_SIZE = 1024 * 1024; // 1M elements
  constexpr size_t NUM_ITEMS = 10'000'000;
  
  SPSCLFQueue<TestObject> queue(QUEUE_SIZE);
  std::atomic<bool> producer_done{false};
  
  auto start = high_resolution_clock::now();
  
  // Producer thread
  auto producer = createAndStartThread(1, "Producer", [&queue, &producer_done, NUM_ITEMS]() {
    for (size_t i = 0; i < NUM_ITEMS; ++i) {
      while (!queue.getNextToWriteTo()) {
        cpuPause(); // Spin wait
      }
      *queue.getNextToWriteTo() = TestObject(i);
      queue.updateWriteIndex();
    }
    producer_done.store(true, std::memory_order_release);
  });
  
  // Consumer thread
  auto consumer = createAndStartThread(2, "Consumer", [&queue, &producer_done, NUM_ITEMS]() {
    size_t consumed = 0;
    while (consumed < NUM_ITEMS) {
      if (queue.getNextToRead()) {
        consumed++;
        queue.updateReadIndex();
      } else if (producer_done.load(std::memory_order_acquire)) {
        cpuPause();
      }
    }
  });
  
  producer->join();
  consumer->join();
  
  auto end = high_resolution_clock::now();
  auto duration = duration_cast<microseconds>(end - start).count();
  
  std::cout << "Processed " << NUM_ITEMS << " items in " << duration << " μs" << std::endl;
  std::cout << "Throughput: " << std::fixed << std::setprecision(2) 
            << (NUM_ITEMS * 1000000.0 / static_cast<double>(duration)) << " items/sec" << std::endl;
  std::cout << "Latency per item: " << (static_cast<double>(duration) * 1000.0 / NUM_ITEMS) << " ns" << std::endl;
}

// Benchmark Memory Pool
static void benchmarkMemPool() {
  std::cout << "\n=== Memory Pool Benchmark ===" << std::endl;
  
  constexpr size_t POOL_SIZE = 100'000;
  constexpr size_t NUM_ALLOCS = 1'000'000;
  
  // Single-threaded pool
  {
    MemPool<TestObject> pool(POOL_SIZE);
    std::vector<TestObject*> allocated;
    allocated.reserve(POOL_SIZE);
    
    auto start = high_resolution_clock::now();
    
    for (size_t i = 0; i < NUM_ALLOCS; ++i) {
      // Allocate
      auto* obj = pool.allocate(i);
      allocated.push_back(obj);
      
      // Deallocate half when full
      if (allocated.size() >= POOL_SIZE) {
        for (size_t j = 0; j < POOL_SIZE / 2; ++j) {
          pool.deallocate(allocated[j]);
        }
        allocated.erase(allocated.begin(), allocated.begin() + POOL_SIZE / 2);
      }
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<nanoseconds>(end - start).count();
    
    std::cout << "Single-threaded: " << NUM_ALLOCS << " allocations in " 
              << duration << " ns" << std::endl;
    std::cout << "Average allocation time: " << (static_cast<double>(duration) / NUM_ALLOCS) << " ns" << std::endl;
  }
  
  // Lock-free pool
  {
    LFMemPool<TestObject> pool(POOL_SIZE);
    std::atomic<size_t> total_allocs{0};
    
    auto start = high_resolution_clock::now();
    
    // Multiple threads allocating/deallocating
    std::vector<std::unique_ptr<std::thread>> threads;
    for (int i = 0; i < 4; ++i) {
      threads.push_back(createAndStartThread(i, "Worker" + std::to_string(i), 
        [&pool, &total_allocs, NUM_ALLOCS]() {
          std::vector<TestObject*> local_allocs;
          while (total_allocs.fetch_add(1) < NUM_ALLOCS) {
            auto* obj = pool.allocate(rdtsc());
            if (obj) {
              local_allocs.push_back(obj);
              if (local_allocs.size() > 10) {
                // Deallocate some
                pool.deallocate(local_allocs.back());
                local_allocs.pop_back();
              }
            }
          }
          // Clean up
          for (auto* obj : local_allocs) {
            pool.deallocate(obj);
          }
        }
      ));
    }
    
    for (auto& t : threads) {
      t->join();
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<nanoseconds>(end - start).count();
    
    std::cout << "Lock-free (4 threads): " << NUM_ALLOCS << " allocations in " 
              << duration << " ns" << std::endl;
    std::cout << "Average allocation time: " << (static_cast<double>(duration) / NUM_ALLOCS) << " ns" << std::endl;
  }
}

// Benchmark Thread Creation
static void benchmarkThreadCreation() {
  std::cout << "\n=== Thread Creation Benchmark ===" << std::endl;
  
  constexpr int NUM_THREADS = 10;
  
  // Old method (simulated with sleep)
  {
    auto start = high_resolution_clock::now();
    std::vector<std::thread> threads;
    
    for (int i = 0; i < NUM_THREADS; ++i) {
      threads.emplace_back([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Simulating old method
      });
    }
    
    for (auto& t : threads) {
      t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(end - start).count();
    std::cout << "Old method (with sleep): " << duration << " ms for " 
              << NUM_THREADS << " threads" << std::endl;
  }
  
  // New method (no sleep)
  {
    auto start = high_resolution_clock::now();
    std::vector<std::unique_ptr<std::thread>> threads;
    
    for (int i = 0; i < NUM_THREADS; ++i) {
      threads.push_back(createAndStartThread(i % 4, "TestThread", []() {
        // Thread work
        std::this_thread::sleep_for(std::chrono::microseconds(1));
      }));
    }
    
    for (auto& t : threads) {
      t->join();
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start).count();
    std::cout << "New method (no sleep): " << duration << " μs for " 
              << NUM_THREADS << " threads" << std::endl;
  }
  
  // Thread pool
  {
    auto start = high_resolution_clock::now();
    ThreadPool pool({0, 1, 2, 3}); // 4 threads on cores 0-3
    
    std::vector<std::future<void>> futures;
    for (int i = 0; i < NUM_THREADS; ++i) {
      futures.push_back(pool.enqueue([]() {
        std::this_thread::sleep_for(std::chrono::microseconds(1));
      }));
    }
    
    for (auto& f : futures) {
      f.get();
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start).count();
    std::cout << "Thread pool (pre-created): " << duration << " μs for " 
              << NUM_THREADS << " tasks" << std::endl;
  }
}

int main() {
  std::cout << "=== Testing Enhanced Components ===" << std::endl;
  std::cout << "Cache line size: " << CACHE_LINE_SIZE << " bytes" << std::endl;
  
  benchmarkSPSCQueue();
  benchmarkMemPool();
  benchmarkThreadCreation();
  
  std::cout << "\n=== All tests completed ===" << std::endl;
  return 0;
}