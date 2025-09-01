#pragma once

#include <cstring>
#include <iostream>
#include <immintrin.h>

// Original macros
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

// Enhanced macros for ultra low latency

// Prefetch hints for cache optimization
#define PREFETCH_READ(addr) __builtin_prefetch(addr, 0, 3)
#define PREFETCH_WRITE(addr) __builtin_prefetch(addr, 1, 3)
#define PREFETCH_L1(addr) __builtin_prefetch(addr, 0, 3)
#define PREFETCH_L2(addr) __builtin_prefetch(addr, 0, 2)
#define PREFETCH_L3(addr) __builtin_prefetch(addr, 0, 1)

// Compiler barriers
#define COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")
#define MEMORY_BARRIER() __sync_synchronize()
#define LOAD_BARRIER() __asm__ __volatile__("lfence" ::: "memory")
#define STORE_BARRIER() __asm__ __volatile__("sfence" ::: "memory")
#define FULL_BARRIER() __asm__ __volatile__("mfence" ::: "memory")

// CPU pause for spinlocks
#define CPU_PAUSE() __builtin_ia32_pause()
#define SPIN_PAUSE() do { CPU_PAUSE(); CPU_PAUSE(); CPU_PAUSE(); } while(0)

// Force inline for critical functions
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define NEVER_INLINE __attribute__((noinline))
#define HOT_FUNCTION __attribute__((hot))
#define COLD_FUNCTION __attribute__((cold))

// Cache line alignment
#define CACHE_LINE_SIZE 64
#define CACHE_ALIGNED alignas(CACHE_LINE_SIZE)
#define CACHE_PADDED(type) \
    union { type data; char padding[CACHE_LINE_SIZE]; }

// Restrict pointer aliasing for optimization
#define RESTRICT __restrict__

// Assume for optimizer hints (GCC 13+)
#if __GNUC__ >= 13
  #define ASSUME(cond) [[assume(cond)]]
#else
  #define ASSUME(cond) do { if (!(cond)) __builtin_unreachable(); } while(0)
#endif

// Assertions with better optimization
inline auto ASSERT(bool cond, const std::string &msg) noexcept {
  if (UNLIKELY(!cond)) {
    std::cerr << "ASSERT : " << msg << std::endl;
    __builtin_trap(); // Faster than exit()
  }
}

inline auto FATAL(const std::string &msg) noexcept {
  std::cerr << "FATAL : " << msg << std::endl;
  __builtin_trap();
}

// Debug-only assertions (compiled out in release)
#ifdef NDEBUG
  #define DEBUG_ASSERT(cond, msg) ((void)0)
#else
  #define DEBUG_ASSERT(cond, msg) ASSERT(cond, msg)
#endif

// Unroll loops for better performance
#define UNROLL_2 _Pragma("GCC unroll 2")
#define UNROLL_4 _Pragma("GCC unroll 4")
#define UNROLL_8 _Pragma("GCC unroll 8")
#define UNROLL_16 _Pragma("GCC unroll 16")

// Vectorization hints
#define VECTORIZE _Pragma("GCC ivdep")
#define NO_VECTORIZE _Pragma("GCC novector")

// Profile-guided optimization hints
#define LIKELY_VALUE(x, val) __builtin_expect_with_probability(x, val, 0.9)
#define UNLIKELY_VALUE(x, val) __builtin_expect_with_probability(x, val, 0.1)