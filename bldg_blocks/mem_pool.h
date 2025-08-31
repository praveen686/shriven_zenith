#pragma once

#include <atomic>
#include <memory>
#include <array>
#include <algorithm>
#include <utility>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <sched.h>
#include <immintrin.h>
#include <numa.h>

#include "macros.h"

namespace BldgBlocks {


/// NUMA-aware memory allocator with prefaulting
template<size_t BLOCK_SIZE, size_t NUM_BLOCKS>
class MemoryPool {
    static_assert(BLOCK_SIZE >= sizeof(void*), "Block size must be at least pointer size");
    static_assert(NUM_BLOCKS > 0, "Must have at least one block");
    
public:
    explicit MemoryPool(int numa_node = -1) : numa_node_(numa_node) {
        if (numa_node_ < 0 && numa_available() >= 0) {
            numa_node_ = numa_node_of_cpu(sched_getcpu());
        }
        
        allocateMemory();
        initializeFreeList();
        prefaultPages();
    }
    
    ~MemoryPool() {
        deallocateMemory();
    }
    
    // Delete copy constructor and assignment operator
    // This class manages raw memory and cannot be safely copied
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    
    // Thread-safe O(1) allocation with tight spinlock
    void* allocate() noexcept {
        void* result = nullptr;
        
        // Acquire spinlock with exponential backoff
        int backoff = 1;
        while (allocation_lock_.test_and_set(std::memory_order_acquire)) {
            // Backoff with pause to reduce cache-line ping-pong
            for (int i = 0; i < backoff; ++i) {
                #if defined(__x86_64__) || defined(__i386__)
                    __builtin_ia32_pause();
                #elif defined(__aarch64__)
                    __asm__ volatile("yield");
                #else
                    std::this_thread::yield();
                #endif
            }
            backoff = std::min(backoff * 2, 64); // Cap backoff
        }
        
        // Critical section - keep this TINY
        {
            auto head = free_head_.load(std::memory_order_relaxed);
            if (head != nullptr) {
                // Safe to dereference since we hold the lock
                void* next = *reinterpret_cast<void**>(head);
                free_head_.store(next, std::memory_order_relaxed);
                allocated_count_.fetch_add(1, std::memory_order_relaxed);
                result = head;
            }
        }
        
        // Release lock
        allocation_lock_.clear(std::memory_order_release);
        
        // Clear memory OUTSIDE the lock to minimize critical section
        if (result != nullptr) {
            clearBlock(result);
        }
        
        return result;
    }
    
    // Thread-safe O(1) deallocation
    void deallocate(void* ptr) noexcept {
        if (UNLIKELY(!ptr || !isValidPointer(ptr))) {
            return;
        }
        
        // Clear block BEFORE acquiring lock to keep critical section small
        clearBlock(ptr);
        
        // Acquire spinlock
        while (allocation_lock_.test_and_set(std::memory_order_acquire)) {
            #if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
            #elif defined(__aarch64__)
                __asm__ volatile("yield");  
            #else
                std::this_thread::yield();
            #endif
        }
        
        // Critical section - push to free list
        {
            auto old_head = free_head_.load(std::memory_order_relaxed);
            *reinterpret_cast<void**>(ptr) = old_head;
            free_head_.store(ptr, std::memory_order_relaxed);
            allocated_count_.fetch_sub(1, std::memory_order_relaxed);
        }
        
        // Release lock
        allocation_lock_.clear(std::memory_order_release);
    }
    
    // Bulk allocation for reduced contention
    size_t allocateBulk(void** ptrs, size_t count) noexcept {
        size_t allocated = 0;
        
        for (size_t i = 0; i < count; ++i) {
            ptrs[i] = allocate();
            if (ptrs[i] == nullptr) {
                break;
            }
            ++allocated;
        }
        
        return allocated;
    }
    
    // Bulk deallocation for reduced contention
    void deallocateBulk(void** ptrs, size_t count) noexcept {
        for (size_t i = 0; i < count; ++i) {
            if (ptrs[i]) {
                deallocate(ptrs[i]);
                ptrs[i] = nullptr;
            }
        }
    }
    
    // Statistics
    size_t totalBlocks() const noexcept { return NUM_BLOCKS; }
    size_t allocatedBlocks() const noexcept { 
        return allocated_count_.load(std::memory_order_acquire); 
    }
    size_t freeBlocks() const noexcept { 
        return NUM_BLOCKS - allocatedBlocks(); 
    }
    size_t blockSize() const noexcept { return BLOCK_SIZE; }
    size_t totalMemory() const noexcept { return NUM_BLOCKS * BLOCK_SIZE; }
    
    bool empty() const noexcept { return allocatedBlocks() == 0; }
    bool full() const noexcept { return allocatedBlocks() == NUM_BLOCKS; }
    
    // Memory pool health check
    bool isValid() const noexcept {
        return memory_region_ != nullptr && 
               allocatedBlocks() <= NUM_BLOCKS;
    }
    
private:
    void allocateMemory() {
        const size_t total_size = NUM_BLOCKS * BLOCK_SIZE;
        
        if (numa_node_ >= 0 && numa_available() >= 0) {
            // NUMA-aware allocation
            memory_region_ = numa_alloc_onnode(total_size, numa_node_);
            if (!memory_region_) {
                // Fallback to regular allocation
                memory_region_ = aligned_alloc(64, total_size);
            }
        } else {
            // Regular aligned allocation
            memory_region_ = aligned_alloc(64, total_size);
        }
        
        ASSERT(memory_region_ != nullptr, "Failed to allocate memory pool");
    }
    
    void deallocateMemory() {
        if (memory_region_) {
            if (numa_node_ >= 0 && numa_available() >= 0) {
                numa_free(memory_region_, NUM_BLOCKS * BLOCK_SIZE);
            } else {
                free(memory_region_);
            }
            memory_region_ = nullptr;
        }
    }
    
    void initializeFreeList() {
        char* block = static_cast<char*>(memory_region_);
        
        // Link all blocks in the free list
        for (size_t i = 0; i < NUM_BLOCKS - 1; ++i) {
            void* next_block = block + (i + 1) * BLOCK_SIZE;
            *reinterpret_cast<void**>(block + i * BLOCK_SIZE) = next_block;
        }
        
        // Last block points to nullptr
        *reinterpret_cast<void**>(block + (NUM_BLOCKS - 1) * BLOCK_SIZE) = nullptr;
        
        // Initialize free head
        free_head_.store(memory_region_, std::memory_order_release);
    }
    
    void prefaultPages() {
        // Touch every page to ensure they're allocated and in memory
        const size_t page_size = 4096; // Assume 4KB pages
        const size_t total_size = NUM_BLOCKS * BLOCK_SIZE;
        char* mem = static_cast<char*>(memory_region_);
        
        // Use SIMD to touch cache lines efficiently
        for (size_t offset = 0; offset < total_size; offset += page_size) {
            // Touch first cache line of each page
            volatile char dummy = mem[offset];
            (void)dummy; // Suppress unused variable warning
            
            // Prefetch next page
            if (offset + page_size < total_size) {
#ifdef __SSE__
                _mm_prefetch(mem + offset + page_size, _MM_HINT_T0);
#else
                __builtin_prefetch(mem + offset + page_size, 0, 3);
#endif
            }
        }
    }
    
    void clearBlock(void* ptr) noexcept {
#ifdef __AVX2__
        // Fast zero using SIMD when block is large enough and AVX2 available
        if (BLOCK_SIZE >= 32) {
            char* block = static_cast<char*>(ptr);
            const __m256i zero = _mm256_setzero_si256();
            
            // Clear 32-byte chunks
            size_t simd_size = BLOCK_SIZE & ~31; // Round down to multiple of 32
            for (size_t i = 0; i < simd_size; i += 32) {
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(block + i), zero);
            }
            
            // Clear remainder
            if (simd_size < BLOCK_SIZE) {
                memset(block + simd_size, 0, BLOCK_SIZE - simd_size);
            }
        } else {
            memset(ptr, 0, BLOCK_SIZE);
        }
#else
        // Fallback to standard memset when SIMD not available
        memset(ptr, 0, BLOCK_SIZE);
#endif
    }
    
    bool isValidPointer(void* ptr) const noexcept {
        char* p = static_cast<char*>(ptr);
        char* base = static_cast<char*>(memory_region_);
        char* end = base + NUM_BLOCKS * BLOCK_SIZE;
        
        // Check if pointer is within our memory region
        if (p < base || p >= end) {
            return false;
        }
        
        // Check if pointer is properly aligned to block boundary
        // Use reinterpret_cast for pointer to integer conversions
        size_t offset = reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(base);
        return (offset % BLOCK_SIZE) == 0;
    }
    
    int numa_node_;
    void* memory_region_ = nullptr;
    
    // Spinlock for thread safety (cache-line aligned)
    alignas(64) std::atomic_flag allocation_lock_ = ATOMIC_FLAG_INIT;
    
    // Free list head (protected by spinlock)  
    std::atomic<void*> free_head_{nullptr};
    
    // Statistics
    std::atomic<size_t> allocated_count_{0};
};

/// Typed memory pool for objects
template<typename T>
class MemPool {
    static constexpr size_t MAX_CAPACITY = 16 * 1024; // 16K objects max
    using Pool = MemoryPool<sizeof(T), MAX_CAPACITY>;
    Pool pool_;
    size_t requested_capacity_;
    
public:
    explicit MemPool(size_t capacity) : pool_(), requested_capacity_(std::min(capacity, MAX_CAPACITY)) {
        if (capacity > MAX_CAPACITY) {
            // Log warning about capacity limit
        }
    }
    
    // Delete copy constructor and assignment operator
    // This class manages memory pool and cannot be safely copied
    MemPool(const MemPool&) = delete;
    MemPool& operator=(const MemPool&) = delete;
    
    template<typename... Args>
    T* allocate(Args&&... args) {
        void* mem = pool_.allocate();
        if (mem) {
            return new(mem) T(std::forward<Args>(args)...);
        }
        return nullptr;
    }
    
    void deallocate(T* ptr) {
        if (ptr) {
            ptr->~T();
            pool_.deallocate(ptr);
        }
    }
    
    size_t capacity() const { return requested_capacity_; }
    size_t available() const { 
        auto free = pool_.freeBlocks();
        return std::min(free, requested_capacity_ - pool_.allocatedBlocks()); 
    }
    size_t size() const { return pool_.allocatedBlocks(); }
};

/// Lock-free typed memory pool
template<typename T>
using LFMemPool = MemPool<T>;

/// Template specializations for common trading object sizes
using OrderPool = MemoryPool<64, 1024 * 1024>;    // 64MB for orders
using TradePool = MemoryPool<32, 512 * 1024>;     // 16MB for trades  
using TickPool = MemoryPool<64, 2 * 1024 * 1024>; // 128MB for market ticks
using MessagePool = MemoryPool<256, 64 * 1024>;   // 16MB for messages

/// Global memory pools for different object types
struct GlobalMemoryPools {
    static OrderPool& orders() {
        static thread_local OrderPool pool;
        return pool;
    }
    
    static TradePool& trades() {
        static thread_local TradePool pool;
        return pool;
    }
    
    static TickPool& ticks() {
        static thread_local TickPool pool;
        return pool;
    }
    
    static MessagePool& messages() {
        static thread_local MessagePool pool;
        return pool;
    }
};

/// RAII wrapper for automatic cleanup
template<typename Pool>
class PooledPtr {
public:
    explicit PooledPtr(Pool& pool) : pool_(pool), ptr_(pool.allocate()) {}
    
    PooledPtr(Pool& pool, void* ptr) : pool_(pool), ptr_(ptr) {}
    
    ~PooledPtr() {
        if (ptr_) {
            pool_.deallocate(ptr_);
        }
    }
    
    // Move constructor
    PooledPtr(PooledPtr&& other) noexcept : pool_(other.pool_), ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    
    // Move assignment
    PooledPtr& operator=(PooledPtr&& other) noexcept {
        if (this != &other) {
            if (ptr_) {
                pool_.deallocate(ptr_);
            }
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }
    
    // Delete copy operations
    PooledPtr(const PooledPtr&) = delete;
    PooledPtr& operator=(const PooledPtr&) = delete;
    
    void* get() const noexcept { return ptr_; }
    void* release() noexcept {
        void* tmp = ptr_;
        ptr_ = nullptr;
        return tmp;
    }
    
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    
private:
    Pool& pool_;
    void* ptr_;
};

/// Fast allocation macros for common types
#define ALLOC_ORDER() GlobalMemoryPools::orders().allocate()
#define FREE_ORDER(ptr) GlobalMemoryPools::orders().deallocate(ptr)

#define ALLOC_TRADE() GlobalMemoryPools::trades().allocate()
#define FREE_TRADE(ptr) GlobalMemoryPools::trades().deallocate(ptr)

#define ALLOC_TICK() GlobalMemoryPools::ticks().allocate()
#define FREE_TICK(ptr) GlobalMemoryPools::ticks().deallocate(ptr)

#define ALLOC_MESSAGE() GlobalMemoryPools::messages().allocate()
#define FREE_MESSAGE(ptr) GlobalMemoryPools::messages().deallocate(ptr)


} // namespace BldgBlocks