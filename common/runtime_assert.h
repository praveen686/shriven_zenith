// runtime_assert.h - Production-safe assertions for ultra-low latency
// Last updated: Testing auditor on save functionality
#pragma once
#include <cstdint>
#include <cstdlib>
#include "macros.h"  // For LIKELY/UNLIKELY

// Production assertions are ON by default for safety
#if !defined(PROD_ASSERTS_ON)
#define PROD_ASSERTS_ON 1
#endif

namespace Common {

#if PROD_ASSERTS_ON
  // Fast fail handler - minimal overhead, no string formatting
  // Marked cold and noinline to keep it out of hot path
  [[gnu::cold, gnu::noinline]] 
  inline void fast_fail(const char* expr, const char* file, int line) noexcept {
    // Minimal logging - no heavy formatting
    // Hook to metrics/logger if needed, but keep it lean
    
    // Use __builtin_trap() for immediate termination
    // This generates a SIGILL which is catchable in debugger
    __builtin_trap();
    
    // Alternative: std::abort() if you prefer SIGABRT
    // std::abort();
  }

  // HARD_ASSERT: For Tier A invariants that must never fail
  // Uses __builtin_expect for branch prediction (cold path)
  #define HARD_ASSERT(cond) do {                                          \
      if (UNLIKELY(!(cond))) {                                            \
        ::Common::fast_fail(#cond, __FILE__, __LINE__);                   \
      }                                                                    \
    } while(0)

  // SOFT_CHECK: For Tier B conditions that can be recovered from
  // Executes recovery statement without aborting
  #define SOFT_CHECK(cond, on_fail_stmt) do {                             \
      if (UNLIKELY(!(cond))) {                                            \
        on_fail_stmt;                                                      \
      }                                                                    \
    } while(0)

  // DEBUG_ASSERT: Only active in debug builds
  #ifdef DEBUG
    #define DEBUG_ASSERT(cond) HARD_ASSERT(cond)
  #else
    #define DEBUG_ASSERT(cond) do {} while(0)
  #endif

#else
  // Production assertions disabled - all become no-ops
  #define HARD_ASSERT(cond)             do {} while(0)
  #define SOFT_CHECK(cond, stmt)        do {} while(0)
  #define DEBUG_ASSERT(cond)            do {} while(0)
#endif

// Usage examples and guidelines:
//
// Tier A (HARD_ASSERT) - Critical invariants:
//   - Null pointer checks before dereference
//   - Array bounds in ring buffers
//   - Memory ownership invariants
//   - Lock-free queue ABA guards
// Example:
//   HARD_ASSERT(order_ptr != nullptr);
//   HARD_ASSERT(index < capacity_);
//
// Tier B (SOFT_CHECK) - Recoverable conditions:
//   - Queue full/empty conditions
//   - Network timeouts
//   - Optional feature availability
// Example:
//   SOFT_CHECK(queue.size() < max_size, return ErrorCode::QUEUE_FULL);
//   SOFT_CHECK(socket.is_connected(), LOG_WARN("Socket disconnected"); return);
//
// Tier C (DEBUG_ASSERT) - Development/debugging only:
//   - Expensive consistency checks
//   - Pedagogical assertions
// Example:
//   DEBUG_ASSERT(validate_order_book_consistency());

} // namespace Common

// Optional: Replace standard assert with our implementation in hot paths
// Uncomment if you want to override standard assert
/*
#ifdef assert
  #undef assert
#endif
#define assert(x) HARD_ASSERT(x)
*/

#endif // RUNTIME_ASSERT_H