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
#include <deque>
#include <future>
#include <cstring>
#include "macros.h"
#include <type_traits>
#include <tuple>
#include "time_utils.h"
#include "mem_pool.h"

namespace Common {

  
  /// Set affinity for current thread to be pinned to the provided core_id
  inline auto setThreadCore(int core_id) noexcept -> bool {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(static_cast<size_t>(core_id), &cpuset);
    
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
  inline auto createAndStartThread(int core_id, const char* name, 
                                   T&& func, A&&... args) noexcept {
    std::atomic<bool> thread_ready{false};
    std::mutex mtx;
    std::condition_variable cv;
    
    // Use placement new with static storage instead of make_unique
    alignas(std::thread) static char thread_storage[sizeof(std::thread)];
    auto* t = new (thread_storage) std::thread([&thread_ready, &cv, core_id, name, 
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
      char truncated_name[16];
      strncpy(truncated_name, name, 15);
      truncated_name[15] = '\0';
      pthread_setname_np(pthread_self(), truncated_name);
      
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
    static constexpr size_t MAX_THREADS = 64;
  public:
    explicit ThreadPool(const int* core_ids, size_t num_cores) 
      : num_workers_(num_cores), tasks_{}, queue_mutex_{}, condition_{}, stopped_{false} {
      ASSERT(num_cores <= MAX_THREADS, "Too many threads requested");
      for (size_t i = 0; i < num_cores; ++i) {
        int core_id = core_ids[i];
        new (&workers_[i]) std::thread([this, core_id] {
          // Try to set thread affinity, but don't terminate on failure
          if (core_id >= 0) {
            bool affinity_set = setThreadCore(core_id);
            // Log failure but continue execution (don't exit)
            if (!affinity_set) {
              // Worker continues without affinity - acceptable for error handling
            }
          }
          
          while (true) {
            std::function<void()> task;
            
            {
              std::unique_lock<std::mutex> lock(queue_mutex_);
              condition_.wait(lock, [this] { 
                return stopped_.load(std::memory_order_acquire) || !tasks_.empty(); 
              });
              
              if (stopped_.load(std::memory_order_acquire) && tasks_.empty()) {
                break;
              }
              
              if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop_front();
              } else {
                continue;  // Spurious wakeup
              }
            }
            
            if (task) {
              task();
            }
          }
        });
      }
    }
    
    ~ThreadPool() {
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stopped_.store(true, std::memory_order_release);
      }
      condition_.notify_all();
      
      for (size_t i = 0; i < num_workers_; ++i) {
        if (workers_[i].joinable()) {
          workers_[i].join();
        }
        workers_[i].~thread();  // Explicit destructor call
      }
    }
    
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
      using R = std::invoke_result_t<F, Args...>;
      
      if (stopped_.load(std::memory_order_acquire)) {
        // Return a future with an exception already set
        std::promise<R> error_promise;
        error_promise.set_exception(std::make_exception_ptr(
          std::runtime_error("enqueue on stopped ThreadPool")));
        return error_promise.get_future();
      }
      
      // Use memory pool allocation instead of make_shared
      auto* task_mem = GlobalMemoryPools::messages().allocate();
      if (!task_mem) {
        // Return a future with an exception already set
        std::promise<R> error_promise;
        error_promise.set_exception(std::make_exception_ptr(
          std::runtime_error("Failed to allocate task memory from pool")));
        return error_promise.get_future();
      }
      
      auto* task = new (task_mem) std::packaged_task<R()>(
        [fn = std::forward<F>(f), 
         args_tuple = std::make_tuple(std::forward<Args>(args)...)]() mutable -> R {
          if constexpr (std::is_void_v<R>) {
            std::apply([&fn](auto&&... captured_args) {
              std::invoke(std::move(fn), std::forward<decltype(captured_args)>(captured_args)...);
            }, std::move(args_tuple));
          } else {
            return std::apply([&fn](auto&&... captured_args) {
              return std::invoke(std::move(fn), std::forward<decltype(captured_args)>(captured_args)...);
            }, std::move(args_tuple));
          }
        }
      );
      
      // Create a shared wrapper that manages pool deallocation
      auto task_deleter = [](std::packaged_task<R()>* t) {
        if (t) {
          t->~packaged_task();
          GlobalMemoryPools::messages().deallocate(t);
        }
      };
      
      std::shared_ptr<std::packaged_task<R()>> shared_task(task, task_deleter);
      
      auto res = shared_task->get_future();
      
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stopped_.load(std::memory_order_acquire)) {
          // Return a future with an exception already set
          std::promise<R> error_promise;
          error_promise.set_exception(std::make_exception_ptr(
            std::runtime_error("enqueue on stopped ThreadPool")));
          return error_promise.get_future();
        }
        
        tasks_.emplace_back([shared_task]() { (*shared_task)(); });
      }
      
      condition_.notify_one();
      return res;
    }
    
    /// Try to enqueue a task, returns false if stopped
    template<typename F, typename... Args>
    [[nodiscard]] auto try_enqueue(F&& f, Args&&... args) noexcept -> bool {
      if (stopped_.load(std::memory_order_acquire)) {
        return false;
      }
      
      std::function<void()> task = [fn = std::forward<F>(f), 
                                    args_tuple = std::make_tuple(std::forward<Args>(args)...)]() mutable {
        std::apply([&fn](auto&&... captured_args) {
          std::invoke(std::move(fn), std::forward<decltype(captured_args)>(captured_args)...);
        }, std::move(args_tuple));
      };
      
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stopped_.load(std::memory_order_acquire)) {
          return false;
        }
        
        tasks_.emplace_back(std::move(task));
      }
      
      condition_.notify_one();
      return true;
    }
    
    /// Check if the ThreadPool is stopped
    [[nodiscard]] auto is_stopped() const noexcept -> bool {
      return stopped_.load(std::memory_order_acquire);
    }
    
  private:
    alignas(64) std::thread workers_[MAX_THREADS];
    size_t num_workers_;
    std::deque<std::function<void()>> tasks_;
    
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stopped_;
  };
  
  /// Get current CPU cycle count for precise timing
  
  /// Memory fence for ordering
  inline void memoryFence() noexcept {
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }
  
  /// Pause instruction for spinlock optimization
  inline void cpuPause() noexcept {
    __builtin_ia32_pause();
  }
}