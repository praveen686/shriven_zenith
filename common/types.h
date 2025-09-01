#pragma once

#include <limits>
#include <sstream>
#include <array>
#include <vector>
#include <cstdint>
#include <atomic>
#include <type_traits>
#include <memory>
#include <immintrin.h>
#include <numa.h>

#include "macros.h"

namespace Common {

// Helper trait for atomic detection  
template<typename T>
struct is_atomic : std::false_type {};

template<typename T>
struct is_atomic<std::atomic<T>> : std::true_type {};

template<typename T>
inline constexpr bool is_atomic_v = is_atomic<T>::value;

/// Enhanced type definitions for ultra low-latency trading

/// Trading instrument identifiers - aligned for cache performance
using TickerId = uint32_t;
using OrderId = uint64_t;
using ClientId = uint32_t;
using Price = int64_t;  // Fixed-point price (scaled by 10000)
using Qty = uint64_t;
using Side = uint8_t;   // Buy=1, Sell=2
using Priority = uint64_t;

/// Constants for container sizes
constexpr size_t ME_MAX_TICKERS = 8 * 1024;
constexpr size_t ME_MAX_CLIENT_UPDATES = 256 * 1024;
constexpr size_t ME_MAX_ORDERS = 1024 * 1024;
constexpr size_t ME_MAX_NUM_CLIENTS = 256;

/// Invalid/sentinel values - chosen for branch prediction
constexpr OrderId OrderId_INVALID = std::numeric_limits<OrderId>::max();
constexpr TickerId TickerId_INVALID = std::numeric_limits<TickerId>::max();
constexpr ClientId ClientId_INVALID = std::numeric_limits<ClientId>::max();
constexpr Price Price_INVALID = std::numeric_limits<Price>::max();
constexpr Priority Priority_INVALID = std::numeric_limits<Priority>::max();

/// Cache-line aligned fundamental types
template<typename T, std::size_t Align = CACHE_LINE_SIZE>
struct alignas(Align) CacheAligned {
  static_assert(Align % alignof(T) == 0, "Align must be multiple of alignof(T)");
  static_assert(!std::is_const_v<T> || !is_atomic_v<T>, 
                "Cannot wrap const atomic types");
  
  union {
    T value;
    char storage[sizeof(T)];
  };
  
  // Calculate padding to ensure size is multiple of alignment
  static constexpr size_t padding_size = 
      (sizeof(T) < Align) ? (Align - sizeof(T)) : 
      ((sizeof(T) % Align) ? (Align - (sizeof(T) % Align)) : 0);
  [[maybe_unused]] char padding[padding_size > 0 ? padding_size : 1];
  
  // Default constructor
  CacheAligned() noexcept(std::is_nothrow_default_constructible_v<T>) {
    ::new (static_cast<void*>(std::addressof(value))) T();
  }
  
  // Universal constructor with perfect forwarding
  template<typename U>
  explicit CacheAligned(U&& u) {
    construct_init(std::forward<U>(u));
  }
  
  // Destructor
  ~CacheAligned() {
    if constexpr (!std::is_trivially_destructible_v<T>) {
      value.~T();
    }
  }
  
  // Conversion operators
  operator T&() noexcept { return value; }
  operator const T&() const noexcept { return value; }
  
  // Assignment
  template<typename U>
  CacheAligned& operator=(U&& u) noexcept(
      std::is_nothrow_assignable_v<T&, U&&>) {
    assign(std::forward<U>(u));
    return *this;
  }

private:
  template<typename U>
  void construct_init(U&& u) {
    if constexpr (is_atomic_v<T>) {
      // For atomic types, default construct then store
      ::new (static_cast<void*>(std::addressof(value))) T();
      using V = typename T::value_type;
      value.store(static_cast<V>(std::forward<U>(u)), 
                 std::memory_order_relaxed);
    } else {
      // Placement new for perfect forwarding
      ::new (static_cast<void*>(std::addressof(value))) 
          T(std::forward<U>(u));
    }
  }
  
  template<typename U>
  void assign(U&& u) {
    if constexpr (is_atomic_v<T>) {
      using V = typename T::value_type;
      value.store(static_cast<V>(std::forward<U>(u)), 
                 std::memory_order_relaxed);
    } else {
      value = std::forward<U>(u);
    }
  }
};

// Static assertions for verification
static_assert(alignof(CacheAligned<std::atomic<uint64_t>>) >= CACHE_LINE_SIZE,
              "CacheAligned must be aligned to cache line");
static_assert(sizeof(CacheAligned<std::atomic<uint64_t>>) % CACHE_LINE_SIZE == 0,
              "CacheAligned size must be multiple of cache line");
static_assert(sizeof(CacheAligned<char>) == CACHE_LINE_SIZE,
              "CacheAligned must pad to cache line size");

/// SIMD-aligned types for vectorized operations
#ifdef __AVX2__
using SimdPrice = __m256i;      // 4 x int64_t prices
using SimdQty = __m256i;        // 4 x uint64_t quantities
using SimdFloat = __m256;       // 8 x float values
using SimdDouble = __m256d;     // 4 x double values
#else
using SimdPrice = uint64_t;     // Fallback to scalar
using SimdQty = uint64_t;       // Fallback to scalar  
using SimdFloat = float;        // Fallback to scalar
using SimdDouble = double;      // Fallback to scalar
#endif

/// SIMD-aligned arrays for batch processing
template<typename T, size_t N>
struct alignas(32) SimdArray {
  T data[N];
  
  static_assert(sizeof(T) * N % 32 == 0, "Size must be multiple of 32 bytes for SIMD");
  
  T& operator[](size_t i) noexcept { return data[i]; }
  const T& operator[](size_t i) const noexcept { return data[i]; }
  
  constexpr size_t size() const noexcept { return N; }
};

/// Order book side enumeration
enum class OrderSide : uint8_t {
  INVALID = 0,
  BUY = 1,
  SELL = 2
};

/// Order types structured for switch statements
enum class OrderType : uint8_t {
  INVALID = 0,
  MARKET = 1,
  LIMIT = 2,
  STOP = 3,
  STOP_LIMIT = 4,
  IOC = 5,     // Immediate or Cancel
  FOK = 6,     // Fill or Kill
  GTD = 7      // Good Till Date
};

/// Message types for IPC
enum class MessageType : uint8_t {
  INVALID = 0,
  NEW_ORDER = 1,
  CANCEL_ORDER = 2,
  MODIFY_ORDER = 3,
  MARKET_DATA = 4,
  TRADE = 5,
  ORDER_ACK = 6,
  ORDER_REJECT = 7,
  HEARTBEAT = 8
};

/// High-performance string for symbol names (stack allocated)
template<size_t N>
struct alignas(8) FixedString {
  char data[N];
  uint8_t length = 0;
  
  FixedString() { data[0] = '\0'; }
  
  FixedString(const char* str) {
    length = std::min(static_cast<size_t>(strlen(str)), N - 1);
    memcpy(data, str, length);
    data[length] = '\0';
  }
  
  const char* c_str() const noexcept { return data; }
  size_t size() const noexcept { return length; }
  bool empty() const noexcept { return length == 0; }
  
  bool operator==(const FixedString& other) const noexcept {
    return length == other.length && memcmp(data, other.data, length) == 0;
  }
  
  // Hash function for use in containers
  size_t hash() const noexcept {
    size_t h = 0;
    for (size_t i = 0; i < length; ++i) {
      h = h * 31 + data[i];
    }
    return h;
  }
};

using Symbol = FixedString<16>;  // Most trading symbols fit in 16 chars

/// Market data tick structure - cache aligned
struct alignas(64) MarketTick {
  TickerId ticker_id;
  Price bid_price;
  Price ask_price;
  Qty bid_qty;
  Qty ask_qty;
  uint64_t timestamp;
  uint32_t sequence_number;
  uint16_t flags;
  char padding[2];  // Ensure 64-byte alignment
};
static_assert(sizeof(MarketTick) == 64, "MarketTick must be exactly one cache line");

/// Order structure - designed for order book operations
struct alignas(32) Order {
  OrderId order_id;
  TickerId ticker_id;
  ClientId client_id;
  Price price;
  Qty qty;
  Qty filled_qty;
  OrderSide side;
  OrderType type;
  uint16_t flags;
  uint64_t timestamp;
  Priority priority;
};
static_assert(sizeof(Order) <= 64, "Order should fit in cache line");

/// Trade execution record
struct alignas(32) Trade {
  OrderId buy_order_id;
  OrderId sell_order_id;
  TickerId ticker_id;
  Price price;
  Qty qty;
  uint64_t timestamp;
  ClientId buy_client_id;
  ClientId sell_client_id;
};

/// NUMA-aware allocator for containers
template<typename T>
class NumaAllocator {
public:
  using value_type = T;
  
  explicit NumaAllocator(int node = -1) : numa_node_(node) {
    if (numa_node_ < 0) {
      numa_node_ = numa_node_of_cpu(sched_getcpu());
    }
  }
  
  T* allocate(size_t n) {
    if (numa_available() >= 0) {
      return static_cast<T*>(numa_alloc_onnode(n * sizeof(T), numa_node_));
    } else {
      return static_cast<T*>(std::aligned_alloc(alignof(T), n * sizeof(T)));
    }
  }
  
  void deallocate(T* p, size_t n) {
    if (numa_available() >= 0) {
      numa_free(p, n * sizeof(T));
    } else {
      std::free(p);
    }
  }
  
  template<typename U>
  bool operator==(const NumaAllocator<U>& other) const {
    return numa_node_ == other.numa_node_;
  }
  
private:
  int numa_node_;
};

/// High-performance containers using NUMA allocator
template<typename T>
using NumaVector = std::vector<T, NumaAllocator<T>>;

/// Lock-free atomic pointer with ABA protection
template<typename T>
struct TaggedPtr {
  T* ptr;
  uint64_t tag;
  
  TaggedPtr() : ptr(nullptr), tag(0) {}
  TaggedPtr(T* p, uint64_t t) : ptr(p), tag(t) {}
  
  bool operator==(const TaggedPtr& other) const noexcept {
    return ptr == other.ptr && tag == other.tag;
  }
};

/// Performance monitoring structure
struct alignas(64) PerfCounters {
  uint64_t messages_processed = 0;
  uint64_t orders_placed = 0;
  uint64_t orders_filled = 0;
  uint64_t trades_executed = 0;
  uint64_t latency_sum_ns = 0;
  uint64_t max_latency_ns = 0;
  uint64_t cache_misses = 0;
  uint64_t cpu_cycles = 0;
};

/// Utility functions for type conversions
namespace TypeUtils {
  
  // Convert price to human readable (divide by 10000)
  inline double priceToDouble(Price p) noexcept {
    return static_cast<double>(p) / 10000.0;
  }
  
  // Convert double to fixed-point price
  inline Price doubleToPrice(double d) noexcept {
    return static_cast<Price>(d * 10000.0);
  }
  
  // Convert side enum to character
  inline char sideToChar(OrderSide side) noexcept {
    switch (side) {
      case OrderSide::BUY: return 'B';
      case OrderSide::SELL: return 'S';
      default: return '?';
    }
  }
  
  // Convert character to side enum
  inline OrderSide charToSide(char c) noexcept {
    switch (c) {
      case 'B': case 'b': return OrderSide::BUY;
      case 'S': case 's': return OrderSide::SELL;
      default: return OrderSide::INVALID;
    }
  }
}


} // namespace Common

/// Hash specialization for FixedString
namespace std {
  template<size_t N>
  struct hash<Common::FixedString<N>> {
    size_t operator()(const Common::FixedString<N>& fs) const noexcept {
      return fs.hash();
    }
  };
}