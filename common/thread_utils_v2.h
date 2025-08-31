#pragma once

#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>
#include <numa.h>

namespace Common {
  
  /// Set affinity for current thread to be pinned to the provided core_id
  inline auto setThreadCore(int core_id) noexcept -> bool {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    
    return (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0);
  }
  
  /// Set NUMA node affinity for memory allocation
  inline auto setNumaNode(int node_id) noexcept -> bool {
    if (numa_available() < 0) {
      return false; // NUMA not available
    }
    
    numa_set_preferred(node_id);
    return true;
  }
  
  /// Creates a thread with immediate affinity setting (no sleep)
  template<typename T, typename... A>
  inline auto createAndStartThread(int core_id, const std::string& name, 
                                   T&& func, A&&... args) noexcept {
    std::atomic<bool> thread_ready{false};
    std::mutex mtx;
    std::condition_variable cv;
    
    auto t = std::make_unique<std::thread>([&thread_ready, &cv, core_id, name, 
                                            func = std::forward<T>(func), 
                                            ...args = std::forward<A>(args)]() mutable {
      // Set thread affinity immediately
      if (core_id >= 0) {
        if (!setThreadCore(core_id)) {
          std::cerr << "Failed to set core affinity for " << name 
                   << " " << pthread_self() << " to " << core_id << std::endl;
          exit(EXIT_FAILURE);
        }
        std::cerr << "Set core affinity for " << name 
                 << " " << pthread_self() << " to " << core_id << std::endl;
      }
      
      // Set thread name for debugging
      pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
      
      // Signal that thread is ready
      thread_ready.store(true, std::memory_order_release);
      cv.notify_one();
      
      // Execute the function
      std::forward<T>(func)(std::forward<A>(args)...);
    });
    
    // Wait for thread to be ready (much faster than sleep)
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&thread_ready] { 
      return thread_ready.load(std::memory_order_acquire); 
    });
    
    return t;
  }
  
  /// Thread pool for pre-created threads with core affinity
  class ThreadPool {
  public:
    explicit ThreadPool(const std::vector<int>& core_ids) {
      for (int core_id : core_ids) {
        workers_.emplace_back([this, core_id] {
          if (core_id >= 0) {
            setThreadCore(core_id);
          }
          
          while (!stop_.load(std::memory_order_acquire)) {
            std::function<void()> task;
            
            {
              std::unique_lock<std::mutex> lock(queue_mutex_);
              condition_.wait(lock, [this] { 
                return stop_.load(std::memory_order_acquire) || !tasks_.empty(); 
              });
              
              if (stop_.load(std::memory_order_acquire) && tasks_.empty()) {
                return;
              }
              
              task = std::move(tasks_.front());
              tasks_.pop_front();
            }
            
            task();
          }
        });
      }
    }
    
    ~ThreadPool() {
      stop_.store(true, std::memory_order_release);
      condition_.notify_all();
      
      for (auto& worker : workers_) {
        if (worker.joinable()) {
          worker.join();
        }
      }
    }
    
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) {
      auto task = std::make_shared<std::packaged_task<decltype(f(args...))()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
      );
      
      auto res = task->get_future();
      
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_.load(std::memory_order_acquire)) {
          throw std::runtime_error("enqueue on stopped ThreadPool");
        }
        
        tasks_.emplace_back([task]() { (*task)(); });
      }
      
      condition_.notify_one();
      return res;
    }
    
  private:
    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> tasks_;
    
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};
  };
  
  /// Get current CPU cycle count for precise timing
  inline uint64_t rdtsc() noexcept {
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return static_cast<uint64_t>(hi) << 32 | lo;
  }
  
  /// Memory fence for ordering
  inline void memoryFence() noexcept {
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }
  
  /// Pause instruction for spinlock optimization
  inline void cpuPause() noexcept {
    __builtin_ia32_pause();
  }
}