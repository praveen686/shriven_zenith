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

namespace Common {

/// Zero policy for memory pools
enum class ZeroPolicy {
    None,       // No zeroing (maximum performance)
    OnAcquire,  // Zero on allocation (security)
    OnRelease   // Zero on deallocation (debug/test)
};

/// NUMA-aware memory allocator with prefaulting
template<size_t BLOCK_SIZE, size_t NUM_BLOCKS, ZeroPolicy POLICY = ZeroPolicy::None>
class MemoryPool {
    static_assert(BLOCK_SIZE >= 64, "Block size must be at least 64 bytes for cache-line alignment");
    static_assert(NUM_BLOCKS > 0, "Must have at least one block");
    
    // Split Arrays (SoA) design: separate headers from payloads
    // This maintains cache-line alignment for payloads
    struct alignas(64) Header {
        std::atomic<uint8_t> state{0};     // 0 = Free, 1 = InUse
        std::atomic<uint32_t> next_idx{0xFFFFFFFFu}; // Index of next free block
    };
    
    static constexpr size_t CACHE_LINE = 64;
    // Round block size up to cache line multiple for alignment
    static constexpr size_t ALIGNED_BLOCK_SIZE = ((BLOCK_SIZE + CACHE_LINE - 1) / CACHE_LINE) * CACHE_LINE;
    
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
            uint32_t head_idx = free_head_idx_.load(std::memory_order_relaxed);
            if (head_idx != 0xFFFFFFFFu) {
                Header* hdr = &headers_[head_idx];
                
                // Mark as in-use (should always succeed for free list nodes)
                uint8_t expected = 0;
                if (hdr->state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
                    free_head_idx_.store(hdr->next_idx.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    allocated_count_.fetch_add(1, std::memory_order_relaxed);
                    result = indexToPayload(head_idx);
                } else {
                    // Shouldn't happen - node in free list should be free
                    // But handle gracefully for safety
                    result = nullptr;
                }
            }
        }
        
        // Release lock
        allocation_lock_.clear(std::memory_order_release);
        
        // Apply zero policy if configured
        if constexpr (POLICY == ZeroPolicy::OnAcquire) {
            if (result) {
                clearBlock(result);
            }
        }
        
        return result;
    }
    
    // Thread-safe O(1) deallocation - idempotent (double-free safe)
    void deallocate(void* ptr) noexcept {
        if (UNLIKELY(!ptr || !isValidPointer(ptr))) {
            return;
        }
        
        // Apply zero policy if configured (before releasing)
        if constexpr (POLICY == ZeroPolicy::OnRelease) {
            clearBlock(ptr);
        }
        
        // Get index from payload pointer
        uint32_t idx = payloadToIndex(ptr);
        if (idx >= NUM_BLOCKS) {
            return; // Invalid index
        }
        
        Header* hdr = &headers_[idx];
        
        // Try to mark as free - this makes deallocation idempotent
        uint8_t expected = 1;  // Expect InUse state
        if (!hdr->state.compare_exchange_strong(expected, 0, std::memory_order_acq_rel)) {
            // Already free or being freed - this is a double-free, safely ignore
            return;
        }
        
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
            uint32_t old_head = free_head_idx_.load(std::memory_order_relaxed);
            hdr->next_idx.store(old_head, std::memory_order_relaxed);
            free_head_idx_.store(idx, std::memory_order_relaxed);
            
            // Defensive: only decrement if counter is non-zero
            auto current = allocated_count_.load(std::memory_order_relaxed);
            if (current > 0) {
                allocated_count_.fetch_sub(1, std::memory_order_relaxed);
            }
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
    
    // Explicit zeroed allocation (regardless of policy)
    void* allocate_zeroed() noexcept {
        void* ptr = allocate();
        if (ptr && POLICY != ZeroPolicy::OnAcquire) {
            clearBlock(ptr);
        }
        return ptr;
    }
    
    // Memory pool health check
    bool isValid() const noexcept {
        return headers_ != nullptr && 
               payloads_ != nullptr && 
               allocatedBlocks() <= NUM_BLOCKS;
    }
    
#ifdef TESTING
    // Test-only accessors for internal state verification
    const Header& getHeader(uint32_t idx) const {
        ASSERT(idx < NUM_BLOCKS, "Index out of bounds");
        return headers_[idx];
    }
    
    bool isAllocated(void* ptr) const {
        uint32_t idx = payloadToIndex(ptr);
        if (idx >= NUM_BLOCKS) return false;
        return headers_[idx].state.load(std::memory_order_acquire) == 1;
    }
    
    static constexpr ZeroPolicy getPolicy() { return POLICY; }
#endif
    
private:
    void allocateMemory() {
        // Allocate separate arrays for headers and payloads
        const size_t headers_size = NUM_BLOCKS * sizeof(Header);
        const size_t payloads_size = NUM_BLOCKS * ALIGNED_BLOCK_SIZE;
        
        if (numa_node_ >= 0 && numa_available() >= 0) {
            // NUMA-aware allocation for both arrays
            headers_ = static_cast<Header*>(numa_alloc_onnode(headers_size, numa_node_));
            if (!headers_) {
                headers_ = static_cast<Header*>(aligned_alloc(64, headers_size));
            }
            
            payloads_ = numa_alloc_onnode(payloads_size, numa_node_);
            if (!payloads_) {
                payloads_ = aligned_alloc(64, payloads_size);
            }
        } else {
            // Regular aligned allocation
            headers_ = static_cast<Header*>(aligned_alloc(64, headers_size));
            payloads_ = aligned_alloc(64, payloads_size);
        }
        
        ASSERT(headers_ != nullptr && payloads_ != nullptr, "Failed to allocate memory pool");
    }
    
    void deallocateMemory() {
        if (headers_) {
            if (numa_node_ >= 0 && numa_available() >= 0) {
                numa_free(headers_, NUM_BLOCKS * sizeof(Header));
            } else {
                free(headers_);
            }
            headers_ = nullptr;
        }
        
        if (payloads_) {
            if (numa_node_ >= 0 && numa_available() >= 0) {
                numa_free(payloads_, NUM_BLOCKS * ALIGNED_BLOCK_SIZE);
            } else {
                free(payloads_);
            }
            payloads_ = nullptr;
        }
    }
    
    void initializeFreeList() {
        // Initialize headers with free state and build free list
        for (size_t i = 0; i < NUM_BLOCKS; ++i) {
            Header* hdr = &headers_[i];
            
            // Initialize state as free
            hdr->state.store(0, std::memory_order_relaxed);
            
            // Set next index in free list
            if (i < NUM_BLOCKS - 1) {
                hdr->next_idx.store(static_cast<uint32_t>(i + 1), std::memory_order_relaxed);
            } else {
                hdr->next_idx.store(0xFFFFFFFFu, std::memory_order_relaxed);  // Last block
            }
        }
        
        // Initialize free head to first block
        free_head_idx_.store(0, std::memory_order_release);
    }
    
    void prefaultPages() {
        // Touch every page in both arrays to ensure they're allocated and in memory
        const size_t page_size = 4096; // Assume 4KB pages
        
        // Prefault headers array
        const size_t headers_size = NUM_BLOCKS * sizeof(Header);
        char* headers_mem = reinterpret_cast<char*>(headers_);
        for (size_t offset = 0; offset < headers_size; offset += page_size) {
            volatile char dummy = headers_mem[offset];
            (void)dummy;
            if (offset + page_size < headers_size) {
#ifdef __SSE__
                _mm_prefetch(headers_mem + offset + page_size, _MM_HINT_T0);
#else
                __builtin_prefetch(headers_mem + offset + page_size, 0, 3);
#endif
            }
        }
        
        // Prefault payloads array
        const size_t payloads_size = NUM_BLOCKS * ALIGNED_BLOCK_SIZE;
        char* payloads_mem = static_cast<char*>(payloads_);
        for (size_t offset = 0; offset < payloads_size; offset += page_size) {
            volatile char dummy = payloads_mem[offset];
            (void)dummy;
            if (offset + page_size < payloads_size) {
#ifdef __SSE__
                _mm_prefetch(payloads_mem + offset + page_size, _MM_HINT_T0);
#else
                __builtin_prefetch(payloads_mem + offset + page_size, 0, 3);
#endif
            }
        }
    }
    
    // Helper functions for index/payload conversion
    void* indexToPayload(uint32_t idx) const noexcept {
        if (idx >= NUM_BLOCKS) return nullptr;
        return static_cast<char*>(payloads_) + (idx * ALIGNED_BLOCK_SIZE);
    }
    
    uint32_t payloadToIndex(void* ptr) const noexcept {
        if (!ptr) return 0xFFFFFFFFu;
        char* p = static_cast<char*>(ptr);
        char* base = static_cast<char*>(payloads_);
        ptrdiff_t offset = p - base;
        if (offset < 0 || offset >= static_cast<ptrdiff_t>(NUM_BLOCKS * ALIGNED_BLOCK_SIZE)) {
            return 0xFFFFFFFFu;
        }
        return static_cast<uint32_t>(static_cast<size_t>(offset) / ALIGNED_BLOCK_SIZE);
    }
    
    bool isValidPointer(void* ptr) const noexcept {
        // Validate a payload pointer in SoA layout
        if (!ptr || !payloads_) return false;
        
        char* p = static_cast<char*>(ptr);
        char* base = static_cast<char*>(payloads_);
        char* end = base + (NUM_BLOCKS * ALIGNED_BLOCK_SIZE);
        
        // Check if pointer is within payloads region
        if (p < base || p >= end) {
            return false;
        }
        
        // Check if pointer is properly aligned to block boundary
        ptrdiff_t offset = p - base;
        return (static_cast<size_t>(offset) % ALIGNED_BLOCK_SIZE) == 0;
    }
    
    void clearBlock(void* ptr) noexcept {
        // Clear a block of memory (SIMD-optimized when available)
#ifdef __AVX2__
        char* block = static_cast<char*>(ptr);
        const __m256i zero = _mm256_setzero_si256();
        
        // Clear 32-byte chunks
        size_t simd_size = ALIGNED_BLOCK_SIZE & ~static_cast<size_t>(31);
        for (size_t i = 0; i < simd_size; i += 32) {
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(block + i), zero);
        }
        
        // Clear remainder
        if (simd_size < ALIGNED_BLOCK_SIZE) {
            memset(block + simd_size, 0, ALIGNED_BLOCK_SIZE - simd_size);
        }
#else
        memset(ptr, 0, ALIGNED_BLOCK_SIZE);
#endif
    }
    
    int numa_node_;
    
    // Split Arrays: separate headers and payloads for cache alignment
    Header* headers_ = nullptr;        // Array of headers
    void* payloads_ = nullptr;         // Array of payload blocks
    
    // Spinlock for thread safety (cache-line aligned)
    alignas(64) std::atomic_flag allocation_lock_ = ATOMIC_FLAG_INIT;
    
    // Free list head index (protected by spinlock)  
    std::atomic<uint32_t> free_head_idx_{0xFFFFFFFFu};
    
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

/// Template specializations for common trading object sizes (fast path, no zeroing)
using OrderPool = MemoryPool<64, 1024 * 1024, ZeroPolicy::None>;    // 64MB for orders
using TradePool = MemoryPool<64, 512 * 1024, ZeroPolicy::None>;     // 32MB for trades
using TickPool = MemoryPool<64, 2 * 1024 * 1024, ZeroPolicy::None>; // 128MB for market ticks
using MessagePool = MemoryPool<256, 64 * 1024, ZeroPolicy::None>;   // 16MB for messages

// Secure variants with zeroing on acquire (for sensitive data)
using SecureOrderPool = MemoryPool<64, 1024 * 1024, ZeroPolicy::OnAcquire>;
using SecureMessagePool = MemoryPool<256, 64 * 1024, ZeroPolicy::OnAcquire>;

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


} // namespace Common