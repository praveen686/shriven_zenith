#pragma once

#include <functional>
#include <sys/socket.h>
#include <sys/uio.h>
#include <immintrin.h>

#include "socket_utils.h"
#include "logging.h"

namespace BldgBlocks {


/// Size of ring buffers in bytes (16MB for ultra low latency)
constexpr size_t TCPBufferSize = 16 * 1024 * 1024;

/// Cache-aligned ring buffer template
template<size_t SIZE>
class alignas(64) TCPRingBuffer {
  static_assert((SIZE & (SIZE - 1)) == 0, "Size must be power of 2");
  
public:
  TCPRingBuffer() {
    // NUMA-aware allocation
    if (numa_available() >= 0) {
      data_ = static_cast<char*>(numa_alloc_onnode(SIZE, numa_node_of_cpu(sched_getcpu())));
    } else {
      data_ = static_cast<char*>(aligned_alloc(64, SIZE));
    }
    ASSERT(data_ != nullptr, "Failed to allocate ring buffer");
  }
  
  ~TCPRingBuffer() {
    if (data_) {
      if (numa_available() >= 0) {
        numa_free(data_, SIZE);
      } else {
        free(data_);
      }
    }
  }
  
  // Write data to ring buffer (producer)
  size_t write(const void* src, size_t len) noexcept {
    const auto write_idx = write_idx_.load(std::memory_order_relaxed);
    
    // Check available space
    if (UNLIKELY(read_idx_cache_ == UINT64_MAX)) {
      read_idx_cache_ = read_idx_.load(std::memory_order_acquire);
    }
    
    const size_t available = (read_idx_cache_ - write_idx - 1) & (SIZE - 1);
    if (UNLIKELY(len > available)) {
      read_idx_cache_ = read_idx_.load(std::memory_order_acquire);
      const size_t new_available = (read_idx_cache_ - write_idx - 1) & (SIZE - 1);
      if (len > new_available) {
        return 0; // Buffer full
      }
    }
    
    const auto masked_write = write_idx & (SIZE - 1);
    
    // Handle wrap-around with vectorized copy
    if (masked_write + len > SIZE) {
      const size_t first_part = SIZE - masked_write;
      memcpy(data_ + masked_write, src, first_part);
      memcpy(data_, static_cast<const char*>(src) + first_part, len - first_part);
    } else {
      memcpy(data_ + masked_write, src, len);
    }
    
    write_idx_.store(write_idx + len, std::memory_order_release);
    read_idx_cache_ = UINT64_MAX; // Invalidate cache
    return len;
  }
  
  // Read data from ring buffer (consumer)
  size_t read(void* dst, size_t max_len) noexcept {
    const auto read_idx = read_idx_.load(std::memory_order_relaxed);
    
    // Check available data
    if (UNLIKELY(write_idx_cache_ == UINT64_MAX)) {
      write_idx_cache_ = write_idx_.load(std::memory_order_acquire);
    }
    
    const size_t available = (write_idx_cache_ - read_idx) & (SIZE - 1);
    if (UNLIKELY(available == 0)) {
      write_idx_cache_ = write_idx_.load(std::memory_order_acquire);
      const size_t new_available = (write_idx_cache_ - read_idx) & (SIZE - 1);
      if (new_available == 0) {
        return 0; // Buffer empty
      }
    }
    
    const size_t to_read = std::min(available, max_len);
    const auto masked_read = read_idx & (SIZE - 1);
    
    // Handle wrap-around
    if (masked_read + to_read > SIZE) {
      const size_t first_part = SIZE - masked_read;
      memcpy(dst, data_ + masked_read, first_part);
      memcpy(static_cast<char*>(dst) + first_part, data_, to_read - first_part);
    } else {
      memcpy(dst, data_ + masked_read, to_read);
    }
    
    read_idx_.store(read_idx + to_read, std::memory_order_release);
    write_idx_cache_ = UINT64_MAX; // Invalidate cache
    return to_read;
  }
  
  size_t size() const noexcept {
    const auto write = write_idx_.load(std::memory_order_acquire);
    const auto read = read_idx_.load(std::memory_order_acquire);
    return (write - read) & (SIZE - 1);
  }
  
  bool empty() const noexcept { return size() == 0; }
  bool full() const noexcept { return size() == SIZE - 1; }
  
private:
  char* data_;
  
  // Producer side (cache-line aligned)
  alignas(64) std::atomic<uint64_t> write_idx_{0};
  alignas(64) mutable uint64_t read_idx_cache_ = 0;
  
  // Consumer side (cache-line aligned)
  alignas(64) std::atomic<uint64_t> read_idx_{0};
  alignas(64) mutable uint64_t write_idx_cache_ = UINT64_MAX;
};

struct UltraTCPSocket {
  explicit UltraTCPSocket(Logger&  logger) : logger_(logger) {}

  /// Create TCP socket with all performance settings
  auto connect(const std::string &ip, const std::string &iface, int port, bool is_listening) -> int;

  /// High-performance send/receive loop
  auto sendAndRecv() noexcept -> bool;

  /// Queue data for sending
  auto send(const void *data, size_t len) noexcept -> size_t;

  /// Zero-copy send using MSG_ZEROCOPY (Linux 4.14+)
  auto sendZeroCopy(const void *data, size_t len) noexcept -> bool;

  /// Vectored I/O send
  auto sendVectorized(const struct iovec *iov, int iovcnt) noexcept -> ssize_t;

  /// Read received data
  auto recv(void *data, size_t max_len) noexcept -> size_t;

  /// Get hardware timestamp if available
  auto getLastHWTimestamp() const noexcept -> uint64_t { return last_hw_timestamp_; }

  /// Set callback for received data
  void setRecvCallback(std::function<void(const void*, size_t)> callback) {
    recv_callback_ = callback;
  }

  // Deleted constructors
  UltraTCPSocket() = delete;
  UltraTCPSocket(const UltraTCPSocket &) = delete;
  UltraTCPSocket(const UltraTCPSocket &&) = delete;
  UltraTCPSocket &operator=(const UltraTCPSocket &) = delete;
  UltraTCPSocket &operator=(const UltraTCPSocket &&) = delete;

  /// Socket file descriptor
  int socket_fd_ = -1;

  /// High-performance ring buffers
  TCPRingBuffer<TCPBufferSize> outbound_buffer_;
  TCPRingBuffer<TCPBufferSize> inbound_buffer_;

  /// Socket attributes
  struct sockaddr_in socket_attrib_{};

  /// Statistics
  uint64_t bytes_sent_ = 0;
  uint64_t bytes_received_ = 0;
  uint64_t send_count_ = 0;
  uint64_t recv_count_ = 0;

  /// Hardware timestamp support
  uint64_t last_hw_timestamp_ = 0;
  bool hw_timestamp_enabled_ = false;

  /// Callback for received data
  std::function<void(const void*, size_t)> recv_callback_;

  Logger&  logger_;
};


} // namespace BldgBlocks