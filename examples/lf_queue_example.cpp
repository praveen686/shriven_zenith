#include "lf_queue_examples.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

#include "../common/lf_queue.h"
#include "../common/thread_utils.h"
#include "../common/macros.h"

using namespace Common;
using namespace std::chrono;

// Example 1: Simple SPSC Queue Usage
void simpleSPSCExample() {
  std::cout << "\n=== SPSC Queue Example ===" << std::endl;
  
  SPSCLFQueue<int> queue(1024);
  
  // Producer
  for (int i = 0; i < 10; ++i) {
    auto* slot = queue.getNextToWriteTo();
    if (slot) {
      *slot = i;
      queue.updateWriteIndex();
      std::cout << "Produced: " << i << std::endl;
    }
  }
  
  // Consumer
  while (auto* value = queue.getNextToRead()) {
    std::cout << "Consumed: " << *value << std::endl;
    queue.updateReadIndex();
  }
}

// Example 2: Multi-threaded SPSC
void threadedSPSCExample() {
  std::cout << "\n=== Threaded SPSC Queue Example ===" << std::endl;
  
  SPSCLFQueue<int> queue(256);
  std::atomic<bool> done{false};
  constexpr int NUM_ITEMS = 1000;
  
  // Producer thread
  auto producer = createAndStartThread(1, "SPSCProducer", 
    [&queue, &done, NUM_ITEMS]() {
      for (int i = 0; i < NUM_ITEMS; ++i) {
        while (!queue.getNextToWriteTo()) {
          cpuPause(); // Wait for space
        }
        *queue.getNextToWriteTo() = i;
        queue.updateWriteIndex();
      }
      done.store(true);
      std::cout << "Producer finished" << std::endl;
    });
  
  // Consumer thread
  auto consumer = createAndStartThread(2, "SPSCConsumer", 
    [&queue, &done, NUM_ITEMS]() {
      int count = 0;
      int sum = 0;
      while (count < NUM_ITEMS) {
        if (auto* value = queue.getNextToRead()) {
          sum += *value;
          count++;
          queue.updateReadIndex();
        } else if (!done.load()) {
          cpuPause();
        }
      }
      std::cout << "Consumer finished. Sum: " << sum 
                << " (expected: " << (NUM_ITEMS * (NUM_ITEMS - 1) / 2) << ")" 
                << std::endl;
    });
  
  producer->join();
  consumer->join();
}

// Example 3: MPMC Queue Usage
void mpmcExample() {
  std::cout << "\n=== MPMC Queue Example ===" << std::endl;
  
  MPMCLFQueue<int> queue(1024);
  constexpr int NUM_PRODUCERS = 2;
  constexpr int NUM_CONSUMERS = 2;
  constexpr int ITEMS_PER_PRODUCER = 100;
  
  std::atomic<int> total_produced{0};
  std::atomic<int> total_consumed{0};
  
  std::vector<std::unique_ptr<std::thread>> producers;
  std::vector<std::unique_ptr<std::thread>> consumers;
  
  // Create producers
  for (int p = 0; p < NUM_PRODUCERS; ++p) {
    producers.push_back(createAndStartThread(p, "MPMCProducer" + std::to_string(p),
      [&queue, &total_produced, p, ITEMS_PER_PRODUCER]() {
        for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) {
          int value = p * 1000 + i;
          while (!queue.enqueue(value)) {
            cpuPause();
          }
          total_produced.fetch_add(1);
        }
      }));
  }
  
  // Create consumers
  for (int c = 0; c < NUM_CONSUMERS; ++c) {
    consumers.push_back(createAndStartThread(c + NUM_PRODUCERS, 
      "MPMCConsumer" + std::to_string(c),
      [&queue, &total_consumed, &total_produced, NUM_PRODUCERS, ITEMS_PER_PRODUCER]() {
        int local_count = 0;
        while (total_consumed.load() < NUM_PRODUCERS * ITEMS_PER_PRODUCER) {
          int value;
          if (queue.dequeue(value)) {
            local_count++;
            total_consumed.fetch_add(1);
          } else if (total_produced.load() >= NUM_PRODUCERS * ITEMS_PER_PRODUCER) {
            // All items produced, try once more
            if (!queue.dequeue(value)) {
              break;
            }
            local_count++;
            total_consumed.fetch_add(1);
          } else {
            cpuPause();
          }
        }
        std::cout << "Consumer processed " << local_count << " items" << std::endl;
      }));
  }
  
  // Wait for completion
  for (auto& p : producers) p->join();
  for (auto& c : consumers) c->join();
  
  std::cout << "Total produced: " << total_produced.load() << std::endl;
  std::cout << "Total consumed: " << total_consumed.load() << std::endl;
}

// Example 4: Performance measurement
void performanceExample() {
  std::cout << "\n=== Queue Performance Measurement ===" << std::endl;
  
  constexpr size_t ITERATIONS = 1'000'000;
  SPSCLFQueue<size_t> queue(65536);
  
  auto start = high_resolution_clock::now();
  
  // Measure single-threaded throughput
  for (size_t i = 0; i < ITERATIONS; ++i) {
    auto* slot = queue.getNextToWriteTo();
    *slot = i;
    queue.updateWriteIndex();
    
    auto* value = queue.getNextToRead();
    [[maybe_unused]] auto v = *value;
    queue.updateReadIndex();
  }
  
  auto end = high_resolution_clock::now();
  auto duration = duration_cast<nanoseconds>(end - start).count();
  
  std::cout << "Single-threaded round-trip: " << ITERATIONS << " iterations" << std::endl;
  std::cout << "Total time: " << static_cast<double>(duration) / 1000000.0 << " ms" << std::endl;
  std::cout << "Latency per operation: " << static_cast<double>(duration) / ITERATIONS << " ns" << std::endl;
  std::cout << "Throughput: " << (ITERATIONS * 1000000000.0 / static_cast<double>(duration)) << " ops/sec" << std::endl;
}

void lockFreeQueueExamples() {
  std::cout << "=== Lock-Free Queue V2 Examples ===" << std::endl;
  std::cout << "Cache line size: " << CACHE_LINE_SIZE << " bytes" << std::endl;
  
  simpleSPSCExample();
  threadedSPSCExample();
  mpmcExample();
  performanceExample();
  
  std::cout << "\n=== Lock-Free Queue examples completed ===" << std::endl;
}