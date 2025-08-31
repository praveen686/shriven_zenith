#pragma once

#include <iostream>
#include <vector>
#include <atomic>
#include <cstddef>
#include <new>

#include "macros.h"
#include "types.h"

namespace BldgBlocks {

  
  // Cache line size defined in macros.h
  
  
  // Single Producer Single Consumer lock-free queue
  template<typename T>
  class SPSCLFQueue final {
  public:
    explicit SPSCLFQueue(std::size_t num_elems) :
        store_(num_elems, T()),
        capacity_(num_elems),
        mask_(num_elems - 1) {
      // Ensure power of 2 for efficient modulo operations
      ASSERT((capacity_ & (capacity_ - 1)) == 0, 
             "Queue size must be power of 2 for optimal performance");
    }
    
    // Producer side - only called by producer thread
    auto getNextToWriteTo() noexcept -> T* {
      // Check if queue is full using count
      const auto current_count = count_.value.load(std::memory_order_acquire);
      if (UNLIKELY(current_count >= capacity_)) {
        return nullptr; // Queue full (N slots used)
      }
      
      const auto write_idx = write_index_.value.load(std::memory_order_relaxed);
      return &store_[write_idx & mask_];
    }
    
    auto updateWriteIndex() noexcept {
      const auto write_idx = write_index_.value.load(std::memory_order_relaxed);
      write_index_.value.store((write_idx + 1) & mask_, 
                              std::memory_order_release);
      // Increment count after write is committed
      count_.value.fetch_add(1, std::memory_order_release);
    }
    
    // Consumer side - only called by consumer thread
    auto getNextToRead() noexcept -> const T* {
      // Check if queue is empty using count
      const auto current_count = count_.value.load(std::memory_order_acquire);
      if (UNLIKELY(current_count == 0)) {
        return nullptr; // Queue empty
      }
      
      const auto read_idx = read_index_.value.load(std::memory_order_relaxed);
      return &store_[read_idx & mask_];
    }
    
    auto updateReadIndex() noexcept {
      const auto read_idx = read_index_.value.load(std::memory_order_relaxed);
      read_index_.value.store((read_idx + 1) & mask_, 
                             std::memory_order_release);
      // Decrement count after read is committed
      count_.value.fetch_sub(1, std::memory_order_release);
    }
    
    // Exact size using count
    auto size() const noexcept {
      return count_.value.load(std::memory_order_acquire);
    }
    
    auto capacity() const noexcept { return capacity_; }
    
    // Deleted default, copy & move constructors and assignment-operators
    SPSCLFQueue() = delete;
    SPSCLFQueue(const SPSCLFQueue&) = delete;
    SPSCLFQueue(const SPSCLFQueue&&) = delete;
    SPSCLFQueue& operator=(const SPSCLFQueue&) = delete;
    SPSCLFQueue& operator=(const SPSCLFQueue&&) = delete;
    
  private:
    // Storage
    std::vector<T> store_;
    const std::size_t capacity_;
    const std::size_t mask_;
    
    // Producer side data (cache-line aligned)
    alignas(CACHE_LINE_SIZE) CacheAligned<std::atomic<std::size_t>> write_index_{0};
    alignas(CACHE_LINE_SIZE) std::size_t read_index_cache_ = 0;
    
    // Consumer side data (cache-line aligned)  
    alignas(CACHE_LINE_SIZE) CacheAligned<std::atomic<std::size_t>> read_index_{0};
    alignas(CACHE_LINE_SIZE) std::size_t write_index_cache_ = 0;
    
    // Count for distinguishing full from empty when indices are equal
    alignas(CACHE_LINE_SIZE) CacheAligned<std::atomic<std::size_t>> count_{0};
  };
  
  // Multi Producer Multi Consumer lock-free queue
  template<typename T>
  class MPMCLFQueue final {
  public:
    explicit MPMCLFQueue(std::size_t num_elems) :
        store_(num_elems),
        capacity_(num_elems) {
      ASSERT((capacity_ & (capacity_ - 1)) == 0, 
             "Queue size must be power of 2 for optimal performance");
      
      // Initialize sequence numbers
      for (std::size_t i = 0; i < capacity_; ++i) {
        store_[i].sequence.store(i, std::memory_order_relaxed);
      }
    }
    
    // Producer side - can be called by multiple threads
    bool enqueue(const T& item) noexcept {
      std::size_t pos = write_index_.value.load(std::memory_order_relaxed);
      
      for (;;) {
        auto& cell = store_[pos & (capacity_ - 1)];
        auto seq = cell.sequence.load(std::memory_order_acquire);
        auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);
        
        if (diff == 0) {
          // Try to claim this slot
          if (write_index_.value.compare_exchange_weak(pos, pos + 1, 
                                                       std::memory_order_relaxed)) {
            cell.data = item;
            cell.sequence.store(pos + 1, std::memory_order_release);
            return true;
          }
        } else if (diff < 0) {
          // Queue is full
          return false;
        } else {
          // Another thread got this slot, try next
          pos = write_index_.value.load(std::memory_order_relaxed);
        }
      }
    }
    
    // Consumer side - can be called by multiple threads
    bool dequeue(T& item) noexcept {
      std::size_t pos = read_index_.value.load(std::memory_order_relaxed);
      
      for (;;) {
        auto& cell = store_[pos & (capacity_ - 1)];
        auto seq = cell.sequence.load(std::memory_order_acquire);
        auto diff = static_cast<std::intptr_t>(seq) - 
                   static_cast<std::intptr_t>(pos + 1);
        
        if (diff == 0) {
          // Try to claim this slot
          if (read_index_.value.compare_exchange_weak(pos, pos + 1, 
                                                     std::memory_order_relaxed)) {
            item = cell.data;
            cell.sequence.store(pos + capacity_, std::memory_order_release);
            return true;
          }
        } else if (diff < 0) {
          // Queue is empty
          return false;
        } else {
          // Another thread got this slot, try next
          pos = read_index_.value.load(std::memory_order_relaxed);
        }
      }
    }
    
    // Deleted default, copy & move constructors and assignment-operators
    MPMCLFQueue() = delete;
    MPMCLFQueue(const MPMCLFQueue&) = delete;
    MPMCLFQueue(const MPMCLFQueue&&) = delete;
    MPMCLFQueue& operator=(const MPMCLFQueue&) = delete;
    MPMCLFQueue& operator=(const MPMCLFQueue&&) = delete;
    
  private:
    struct Cell {
      alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> sequence;
      T data;
      
      Cell() : sequence(0), data() {}
    };
    
    std::vector<Cell> store_;
    const std::size_t capacity_;
    
    // Producer and consumer indices (cache-line aligned)
    alignas(CACHE_LINE_SIZE) CacheAligned<std::atomic<std::size_t>> write_index_{0};
    alignas(CACHE_LINE_SIZE) CacheAligned<std::atomic<std::size_t>> read_index_{0};
  };
  
  // Backwards compatibility alias
  template<typename T>
  using LFQueue = SPSCLFQueue<T>;
}