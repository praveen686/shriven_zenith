#pragma once

#include <cstdint>
#include <cstdlib>
#include <vector>
#include <string>
#include <atomic>
#include <new>

#include "macros.h"

namespace Common {
  
  #ifndef CACHE_LINE_SIZE_DEFINED
  #define CACHE_LINE_SIZE_DEFINED
  inline constexpr std::size_t CACHE_LINE_SIZE = 64;
  #endif
  
  // Single-threaded O(1) memory pool with free-list
  template<typename T>
  class MemPool final {
  public:
    explicit MemPool(std::size_t num_elems) :
        store_(num_elems),
        free_list_(nullptr),
        num_allocated_(0),
        capacity_(num_elems) {
      
      // Initialize free list - link all blocks
      for (std::size_t i = 0; i < num_elems - 1; ++i) {
        store_[i].next = &store_[i + 1];
      }
      store_[num_elems - 1].next = nullptr;
      free_list_ = &store_[0];
    }
    
    // O(1) allocation using free-list
    template<typename... Args>
    T* allocate(Args... args) noexcept {
      if (UNLIKELY(!free_list_)) {
        ASSERT(false, "Memory pool exhausted! Allocated: " + 
               std::to_string(num_allocated_) + "/" + std::to_string(capacity_));
        return nullptr;
      }
      
      // Pop from free list
      auto* block = free_list_;
      free_list_ = free_list_->next;
      
      // Construct object using placement new
      T* obj = reinterpret_cast<T*>(&block->storage);
      new(obj) T(args...);
      
      ++num_allocated_;
      return obj;
    }
    
    // O(1) deallocation - return to free-list
    auto deallocate(T* elem) noexcept {
      if (UNLIKELY(!elem)) return;
      
      // Call destructor explicitly
      elem->~T();
      
      // Get the containing block
      auto* block = reinterpret_cast<Block*>(elem);
      
      // Push to free list
      block->next = free_list_;
      free_list_ = block;
      
      --num_allocated_;
    }
    
    auto size() const noexcept { return num_allocated_; }
    auto capacity() const noexcept { return capacity_; }
    auto available() const noexcept { return capacity_ - num_allocated_; }
    
    // Deleted default, copy & move constructors and assignment-operators
    MemPool() = delete;
    MemPool(const MemPool&) = delete;
    MemPool(const MemPool&&) = delete;
    MemPool& operator=(const MemPool&) = delete;
    MemPool& operator=(const MemPool&&) = delete;
    
  private:
    // Union to store either object or next pointer
    union Block {
      alignas(alignof(T)) std::byte storage[sizeof(T)];
      Block* next;
    };
    
    std::vector<Block> store_;
    Block* free_list_;
    std::size_t num_allocated_;
    const std::size_t capacity_;
  };
  
  // Thread-safe lock-free memory pool using CAS operations
  template<typename T>
  class LFMemPool final {
  public:
    explicit LFMemPool(std::size_t num_elems) :
        capacity_(num_elems) {
      
      // Allocate raw memory
      store_ = static_cast<Block*>(std::aligned_alloc(CACHE_LINE_SIZE, 
                                                       sizeof(Block) * num_elems));
      
      // Initialize free list with tagged pointers to prevent ABA problem
      for (std::size_t i = 0; i < num_elems - 1; ++i) {
        new (&store_[i].next) std::atomic<TaggedPtr>(TaggedPtr{&store_[i + 1], 0});
      }
      new (&store_[num_elems - 1].next) std::atomic<TaggedPtr>(TaggedPtr{nullptr, 0});
      
      free_list_.store(TaggedPtr{&store_[0], 0}, std::memory_order_relaxed);
      num_allocated_.store(0, std::memory_order_relaxed);
    }
    
    ~LFMemPool() {
      if (store_) {
        // Destroy atomics
        for (std::size_t i = 0; i < capacity_; ++i) {
          store_[i].next.~atomic();
        }
        std::free(store_);
      }
    }
    
    // Lock-free allocation
    template<typename... Args>
    T* allocate(Args... args) noexcept {
      TaggedPtr old_head, new_head;
      Block* block;
      
      do {
        old_head = free_list_.load(std::memory_order_acquire);
        if (UNLIKELY(!old_head.ptr)) {
          // Pool exhausted
          return nullptr;
        }
        
        block = old_head.ptr;
        new_head.ptr = block->next.load(std::memory_order_relaxed).ptr;
        new_head.tag = old_head.tag + 1; // Increment tag to prevent ABA
        
      } while (!free_list_.compare_exchange_weak(old_head, new_head,
                                                 std::memory_order_release,
                                                 std::memory_order_acquire));
      
      // Construct object
      T* obj = reinterpret_cast<T*>(&block->storage);
      new(obj) T(args...);
      
      num_allocated_.fetch_add(1, std::memory_order_relaxed);
      return obj;
    }
    
    // Lock-free deallocation
    auto deallocate(T* elem) noexcept {
      if (UNLIKELY(!elem)) return;
      
      // Call destructor
      elem->~T();
      
      // Get the containing block
      auto* block = reinterpret_cast<Block*>(elem);
      
      TaggedPtr old_head, new_head;
      new_head.ptr = block;
      
      do {
        old_head = free_list_.load(std::memory_order_acquire);
        block->next.store(old_head, std::memory_order_relaxed);
        new_head.tag = old_head.tag + 1; // Increment tag
        
      } while (!free_list_.compare_exchange_weak(old_head, new_head,
                                                 std::memory_order_release,
                                                 std::memory_order_acquire));
      
      num_allocated_.fetch_sub(1, std::memory_order_relaxed);
    }
    
    auto size() const noexcept { 
      return num_allocated_.load(std::memory_order_relaxed); 
    }
    auto capacity() const noexcept { return capacity_; }
    
    // Deleted default, copy & move constructors and assignment-operators
    LFMemPool() = delete;
    LFMemPool(const LFMemPool&) = delete;
    LFMemPool(const LFMemPool&&) = delete;
    LFMemPool& operator=(const LFMemPool&) = delete;
    LFMemPool& operator=(const LFMemPool&&) = delete;
    
  private:
    struct Block;
    
    // Tagged pointer to prevent ABA problem
    struct TaggedPtr {
      Block* ptr;
      std::size_t tag;
    };
    
    // Block structure with atomic next pointer
    struct alignas(CACHE_LINE_SIZE) Block {
      union {
        alignas(alignof(T)) std::byte storage[sizeof(T)];
        std::atomic<TaggedPtr> next;
      };
    };
    
    Block* store_ = nullptr;
    alignas(CACHE_LINE_SIZE) std::atomic<TaggedPtr> free_list_;
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> num_allocated_;
    const std::size_t capacity_;
  };
}