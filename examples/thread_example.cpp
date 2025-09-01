#include "thread_examples.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <atomic>
#include <iomanip>

#include "../common/thread_utils.h"
#include "../common/macros.h"

using namespace Common;
using namespace std::chrono;

// Example 1: CPU Affinity Setting
void cpuAffinityExample() {
  std::cout << "\n=== CPU Affinity Example ===" << std::endl;
  
  // Get number of CPU cores
  int num_cores = static_cast<int>(std::thread::hardware_concurrency());
  std::cout << "System has " << num_cores << " CPU cores" << std::endl;
  
  // Create threads pinned to specific cores
  std::vector<std::unique_ptr<std::thread>> threads;
  
  for (int core = 0; core < std::min(4, num_cores); ++core) {
    threads.push_back(createAndStartThread(core, "PinnedThread" + std::to_string(core),
      [core]() {
        std::cout << "Thread running on core " << core 
                  << ", thread ID: " << std::this_thread::get_id() << std::endl;
        
        // Do some work to verify we stay on the core
        volatile uint64_t sum = 0;
        for (uint64_t i = 0; i < 100'000'000; ++i) {
          sum += i;
        }
        
        std::cout << "Thread on core " << core << " completed work" << std::endl;
      }));
  }
  
  // Wait for all threads
  for (auto& t : threads) {
    t->join();
  }
}

// Example 2: Thread Pool Usage
void threadPoolExample() {
  std::cout << "\n=== Thread Pool Example ===" << std::endl;
  
  // Create thread pool with 4 workers on cores 0-3
  ThreadPool pool({0, 1, 2, 3});
  
  // Submit various tasks
  std::vector<std::future<int>> futures;
  
  for (int i = 0; i < 10; ++i) {
    futures.push_back(pool.enqueue([i]() {
      std::cout << "Task " << i << " running on thread " 
                << std::this_thread::get_id() << std::endl;
      std::this_thread::sleep_for(milliseconds(10));
      return i * i;
    }));
  }
  
  // Get results
  std::cout << "Results: ";
  for (auto& f : futures) {
    std::cout << f.get() << " ";
  }
  std::cout << std::endl;
}

// Example 3: RDTSC Timing
void rdtscExample() {
  std::cout << "\n=== RDTSC Timing Example ===" << std::endl;
  
  // Measure overhead of rdtsc itself
  uint64_t start = rdtsc();
  uint64_t end = rdtsc();
  std::cout << "RDTSC overhead: " << (end - start) << " cycles" << std::endl;
  
  // Measure various operations
  volatile int dummy = 0;
  
  // Measure simple addition
  start = rdtsc();
  for (int i = 0; i < 1000; ++i) {
    dummy += i;
  }
  end = rdtsc();
  std::cout << "1000 additions: " << (end - start) << " cycles" << std::endl;
  
  // Measure memory fence
  start = rdtsc();
  memoryFence();
  end = rdtsc();
  std::cout << "Memory fence: " << (end - start) << " cycles" << std::endl;
  
  // Measure pause instruction
  start = rdtsc();
  for (int i = 0; i < 100; ++i) {
    cpuPause();
  }
  end = rdtsc();
  std::cout << "100 pause instructions: " << (end - start) << " cycles" << std::endl;
}

// Example 4: Comparison with old thread creation
void threadCreationComparison() {
  std::cout << "\n=== Thread Creation Comparison ===" << std::endl;
  
  constexpr int NUM_THREADS = 5;
  
  // New method - fast thread creation with affinity
  {
    auto start = high_resolution_clock::now();
    
    std::vector<std::unique_ptr<std::thread>> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
      threads.push_back(createAndStartThread(i % 4, "FastThread",
        []() {
          // Minimal work
          std::atomic<int> x{0};
          x.fetch_add(1);
        }));
    }
    
    for (auto& t : threads) {
      t->join();
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start).count();
    std::cout << "New method (with affinity): " << duration << " μs for " 
              << NUM_THREADS << " threads" << std::endl;
    std::cout << "Average per thread: " << duration / NUM_THREADS << " μs" << std::endl;
  }
  
  // Standard method - no affinity
  {
    auto start = high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
      threads.emplace_back([]() {
        // Minimal work
        std::atomic<int> x{0};
        x.fetch_add(1);
      });
    }
    
    for (auto& t : threads) {
      t.join();
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start).count();
    std::cout << "Standard method (no affinity): " << duration << " μs for " 
              << NUM_THREADS << " threads" << std::endl;
    std::cout << "Average per thread: " << duration / NUM_THREADS << " μs" << std::endl;
  }
  
  // Thread pool - pre-created threads
  {
    ThreadPool pool({0, 1, 2, 3});
    
    auto start = high_resolution_clock::now();
    
    std::vector<std::future<void>> futures;
    for (int i = 0; i < NUM_THREADS; ++i) {
      futures.push_back(pool.enqueue([]() {
        // Minimal work
        std::atomic<int> x{0};
        x.fetch_add(1);
      }));
    }
    
    for (auto& f : futures) {
      f.get();
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start).count();
    std::cout << "Thread pool (pre-created): " << duration << " μs for " 
              << NUM_THREADS << " tasks" << std::endl;
    std::cout << "Average per task: " << duration / NUM_THREADS << " μs" << std::endl;
  }
}

// Example 5: Spinlock implementation using pause
void spinlockExample() {
  std::cout << "\n=== Spinlock Example ===" << std::endl;
  
  std::atomic<bool> lock{false};
  std::atomic<int> counter{0};
  constexpr int ITERATIONS = 100000;
  
  auto spinlock_acquire = [&lock]() {
    bool expected = false;
    while (!lock.compare_exchange_weak(expected, true, 
                                       std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
      expected = false;
      cpuPause(); // Reduce contention
    }
  };
  
  auto spinlock_release = [&lock]() {
    lock.store(false, std::memory_order_release);
  };
  
  auto start = high_resolution_clock::now();
  
  // Create competing threads
  std::vector<std::unique_ptr<std::thread>> threads;
  for (int t = 0; t < 4; ++t) {
    threads.push_back(createAndStartThread(t, "SpinlockThread",
      [&spinlock_acquire, &spinlock_release, &counter, ITERATIONS]() {
        for (int i = 0; i < ITERATIONS; ++i) {
          spinlock_acquire();
          counter.fetch_add(1, std::memory_order_relaxed);
          spinlock_release();
        }
      }));
  }
  
  for (auto& t : threads) {
    t->join();
  }
  
  auto end = high_resolution_clock::now();
  auto duration = duration_cast<microseconds>(end - start).count();
  
  std::cout << "Spinlock test completed" << std::endl;
  std::cout << "Counter value: " << counter.load() << " (expected: " << 4 * ITERATIONS << ")" << std::endl;
  std::cout << "Total time: " << duration << " μs" << std::endl;
  std::cout << "Average per lock/unlock: " << (static_cast<double>(duration) * 1000.0) / (4 * ITERATIONS) << " ns" << std::endl;
}

void threadUtilsExamples() {
  std::cout << "=== Thread Utils V2 Examples ===" << std::endl;
  std::cout << "Hardware concurrency: " << std::thread::hardware_concurrency() << std::endl;
  
  cpuAffinityExample();
  threadPoolExample();
  rdtscExample();
  threadCreationComparison();
  spinlockExample();
  
  std::cout << "\n=== Thread Utils examples completed ===" << std::endl;
}