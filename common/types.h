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
#include <cassert>

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
    if constexpr (std::is_trivially_default_constructible_v<T>) {
      // For trivial types, zero-initialize
      memset(static_cast<void*>(std::addressof(value)), 0, sizeof(T));
    } else {
      // Use in-place construction without new operator
      std::construct_at(std::addressof(value));
    }
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
      // For atomic types, construct then store
      std::construct_at(std::addressof(value));
      using V = typename T::value_type;
      value.store(static_cast<V>(std::forward<U>(u)), 
                 std::memory_order_relaxed);
    } else {
      // In-place construction without new operator
      std::construct_at(std::addressof(value), std::forward<U>(u));
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

/// Order status for trading
enum class OrderStatus : uint8_t {
  INVALID = 0,
  PENDING_NEW = 1,
  NEW = 2,
  PARTIALLY_FILLED = 3,
  FILLED = 4,
  PENDING_CANCEL = 5,
  CANCELLED = 6,
  REJECTED = 7,
  EXPIRED = 8
};

/// Exchange type for multi-exchange support
enum class Exchange : uint8_t {
  INVALID = 0,
  NSE = 1,      // National Stock Exchange (India)
  BSE = 2,      // Bombay Stock Exchange
  NFO = 3,      // NSE Futures & Options
  CDS = 4,      // Currency Derivatives
  MCX = 5,      // Multi Commodity Exchange
  BINANCE = 10, // Binance crypto
  COINBASE = 11 // Coinbase crypto
};

/// Product type (for Indian markets)
enum class Product : uint8_t {
  INVALID = 0,
  CNC = 1,    // Cash and Carry (delivery)
  MIS = 2,    // Margin Intraday Square-off
  NRML = 3,   // Normal (F&O)
  CO = 4,     // Cover Order
  BO = 5      // Bracket Order
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

/// Position tracking for trading
struct alignas(64) Position {
  TickerId ticker_id{TickerId_INVALID};
  int32_t position{0};  // Positive=long, negative=short
  int64_t realized_pnl{0};     // In cents/pips (fixed point)
  int64_t unrealized_pnl{0};   // In cents/pips (fixed point)
  int64_t total_pnl{0};         // In cents/pips (fixed point)
  std::array<int64_t, 2> open_vwap{0, 0};  // [BUY, SELL] VWAP as fixed point
  Qty volume{0};
  Price last_price{Price_INVALID};
  
  [[nodiscard]] auto isFlat() const noexcept -> bool { return position == 0; }
  [[nodiscard]] auto isLong() const noexcept -> bool { return position > 0; }
  [[nodiscard]] auto isShort() const noexcept -> bool { return position < 0; }
  
  auto reset() noexcept -> void {
    ticker_id = TickerId_INVALID;
    position = 0;
    realized_pnl = 0;
    unrealized_pnl = 0;
    total_pnl = 0;
    open_vwap[0] = 0;
    open_vwap[1] = 0;
    volume = 0;
    last_price = Price_INVALID;
  }
};

/// Order request for sending to exchange
struct alignas(64) OrderRequest {
  OrderId order_id{OrderId_INVALID};
  TickerId ticker_id{TickerId_INVALID};
  ClientId client_id{ClientId_INVALID};
  OrderSide side{OrderSide::INVALID};
  OrderType type{OrderType::INVALID};
  Price price{Price_INVALID};
  Price stop_price{Price_INVALID};
  Qty qty{0};
  uint64_t timestamp{0};
  
  auto reset() noexcept -> void {
    order_id = OrderId_INVALID;
    ticker_id = TickerId_INVALID;
    client_id = ClientId_INVALID;
    side = OrderSide::INVALID;
    type = OrderType::INVALID;
    price = Price_INVALID;
    stop_price = Price_INVALID;
    qty = 0;
    timestamp = 0;
  }
};

/// Order response from exchange
struct alignas(64) OrderResponse {
  OrderId order_id{OrderId_INVALID};
  OrderId exchange_order_id{OrderId_INVALID};
  TickerId ticker_id{TickerId_INVALID};
  ClientId client_id{ClientId_INVALID};
  OrderSide side{OrderSide::INVALID};
  Price exec_price{Price_INVALID};
  Qty exec_qty{0};
  Qty leaves_qty{0};
  MessageType type{MessageType::INVALID};
  uint64_t timestamp{0};
  
  auto reset() noexcept -> void {
    order_id = OrderId_INVALID;
    exchange_order_id = OrderId_INVALID;
    ticker_id = TickerId_INVALID;
    client_id = ClientId_INVALID;
    side = OrderSide::INVALID;
    exec_price = Price_INVALID;
    exec_qty = 0;
    leaves_qty = 0;
    type = MessageType::INVALID;
    timestamp = 0;
  }
};

/// Market update (extends MarketTick with additional fields)
struct alignas(64) MarketUpdate : public MarketTick {
  MessageType update_type{MessageType::MARKET_DATA};
  uint32_t depth_level{0};  // For depth updates
  
  auto reset() noexcept -> void {
    ticker_id = TickerId_INVALID;
    bid_price = Price_INVALID;
    ask_price = Price_INVALID;
    bid_qty = 0;
    ask_qty = 0;
    timestamp = 0;
    sequence_number = 0;
    flags = 0;
    update_type = MessageType::MARKET_DATA;
    depth_level = 0;
  }
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

/// High-performance fixed-size containers using NUMA allocator
template<typename T, size_t N>
struct NumaArray {
public:
  using value_type = T;
  using size_type = size_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = T*;
  using const_iterator = const T*;
  
  explicit NumaArray(int numa_node = -1) : allocator_(numa_node), size_(0) {
    data_ = allocator_.allocate(N);
    // Initialize all elements
    for (size_t i = 0; i < N; ++i) {
      std::construct_at(&data_[i]);
    }
  }
  
  ~NumaArray() {
    // Destroy all constructed elements
    for (size_t i = 0; i < size_; ++i) {
      std::destroy_at(&data_[i]);
    }
    allocator_.deallocate(data_, N);
  }
  
  // Non-copyable but moveable
  NumaArray(const NumaArray&) = delete;
  NumaArray& operator=(const NumaArray&) = delete;
  NumaArray(NumaArray&&) = default;
  NumaArray& operator=(NumaArray&&) = default;
  
  reference operator[](size_type pos) noexcept { return data_[pos]; }
  const_reference operator[](size_type pos) const noexcept { return data_[pos]; }
  
  reference at(size_type pos) {
    // Assert in debug, undefined behavior in release for performance
    assert(pos < size_ && "NumaArray::at index out of range");
    return data_[pos];
  }
  
  const_reference at(size_type pos) const {
    // Assert in debug, undefined behavior in release for performance
    assert(pos < size_ && "NumaArray::at index out of range");
    return data_[pos];
  }
  
  iterator begin() noexcept { return data_; }
  const_iterator begin() const noexcept { return data_; }
  iterator end() noexcept { return data_ + size_; }
  const_iterator end() const noexcept { return data_ + size_; }
  
  bool empty() const noexcept { return size_ == 0; }
  size_type size() const noexcept { return size_; }
  constexpr size_type max_size() const noexcept { return N; }
  
  [[nodiscard]] bool push_back(const T& value) noexcept {
    if (size_ >= N) return false;  // Array full
    data_[size_] = value;
    ++size_;
    return true;
  }
  
  [[nodiscard]] bool push_back(T&& value) noexcept {
    if (size_ >= N) return false;  // Array full
    data_[size_] = std::move(value);
    ++size_;
    return true;
  }
  
  void pop_back() {
    if (size_ > 0) {
      --size_;
      std::destroy_at(&data_[size_]);
      std::construct_at(&data_[size_]);
    }
  }
  
  void clear() noexcept {
    for (size_t i = 0; i < size_; ++i) {
      std::destroy_at(&data_[i]);
      std::construct_at(&data_[i]);
    }
    size_ = 0;
  }
  
private:
  NumaAllocator<T> allocator_;
  T* data_;
  size_t size_;
};

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