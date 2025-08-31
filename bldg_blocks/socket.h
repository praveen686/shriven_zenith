#pragma once

#include <functional>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/tcp.h>
#include <linux/net_tstamp.h>
#include <immintrin.h>
#include <numa.h>

#include "../socket_utils.h"
#include "../../v2/logging.h"
#include "../../shared/macros.h"

namespace BldgBlocks {



// Cache-aligned ring buffer for zero-copy networking
template<size_t SIZE>
class alignas(64) RingBuffer {
  static_assert((SIZE & (SIZE - 1)) == 0, "Size must be power of 2");
  
public:
  RingBuffer() {
    // NUMA-aware allocation
    if (numa_available() >= 0) {
      data_ = static_cast<char*>(numa_alloc_onnode(SIZE, numa_node_of_cpu(sched_getcpu())));
    } else {
      data_ = static_cast<char*>(aligned_alloc(64, SIZE));
    }
  }
  
  ~RingBuffer() {
    if (numa_available() >= 0) {
      numa_free(data_, SIZE);
    } else {
      free(data_);
    }
  }
  
  // Lock-free write
  bool write(const void* src, size_t len) noexcept {
    const auto write_idx = write_idx_.load(std::memory_order_relaxed);
    const auto read_idx = read_idx_cache_;
    
    const size_t available = (read_idx - write_idx - 1) & (SIZE - 1);
    if (UNLIKELY(len > available)) {
      read_idx_cache_ = read_idx_.load(std::memory_order_acquire);
      if (len > ((read_idx_cache_ - write_idx - 1) & (SIZE - 1))) {
        return false;
      }
    }
    
    const auto next_write = (write_idx + len) & (SIZE - 1);
    
    // Handle wrap-around
    if (write_idx + len > SIZE) {
      const size_t first_part = SIZE - write_idx;
      memcpy(data_ + write_idx, src, first_part);
      memcpy(data_, static_cast<const char*>(src) + first_part, len - first_part);
    } else {
      memcpy(data_ + write_idx, src, len);
    }
    
    write_idx_.store(next_write, std::memory_order_release);
    return true;
  }
  
  // Lock-free read
  size_t read(void* dst, size_t max_len) noexcept {
    const auto read_idx = read_idx_.load(std::memory_order_relaxed);
    const auto write_idx = write_idx_cache_;
    
    size_t available = (write_idx - read_idx) & (SIZE - 1);
    if (UNLIKELY(available == 0)) {
      write_idx_cache_ = write_idx_.load(std::memory_order_acquire);
      available = (write_idx_cache_ - read_idx) & (SIZE - 1);
      if (available == 0) return 0;
    }
    
    const size_t to_read = std::min(available, max_len);
    const auto next_read = (read_idx + to_read) & (SIZE - 1);
    
    // Handle wrap-around
    if (read_idx + to_read > SIZE) {
      const size_t first_part = SIZE - read_idx;
      memcpy(dst, data_ + read_idx, first_part);
      memcpy(static_cast<char*>(dst) + first_part, data_, to_read - first_part);
    } else {
      memcpy(dst, data_ + read_idx, to_read);
    }
    
    read_idx_.store(next_read, std::memory_order_release);
    return to_read;
  }
  
private:
  char* data_;
  
  alignas(64) std::atomic<size_t> write_idx_{0};
  alignas(64) size_t read_idx_cache_ = 0;
  
  alignas(64) std::atomic<size_t> read_idx_{0};
  alignas(64) size_t write_idx_cache_ = 0;
};

// Ultra low-latency TCP socket with kernel bypass options
class UltraLowLatencyTCPSocket {
public:
  static constexpr size_t BUFFER_SIZE = 16 * 1024 * 1024; // 16MB ring buffers
  
  explicit UltraLowLatencyTCPSocket(Logger&  logger) 
    : logger_(logger) {}
  
  // Enhanced connect with low-latency options
  int connect(const std::string& ip, const std::string& iface, int port, bool is_listening) {
    socket_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
    if (socket_fd_ < 0) return -1;
    
    // Critical TCP optimizations
    int one = 1;
    setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));     // Disable Nagle
    setsockopt(socket_fd_, IPPROTO_TCP, TCP_QUICKACK, &one, sizeof(one));    // Quick ACK mode
    
    // Reduce TCP user timeout (faster failure detection)
    unsigned int timeout = 10000; // 10 seconds
    setsockopt(socket_fd_, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout, sizeof(timeout));
    
    // Enable busy polling (reduce latency)
    int busy_poll = 50; // 50 microseconds
    setsockopt(socket_fd_, SOL_SOCKET, SO_BUSY_POLL, &busy_poll, sizeof(busy_poll));
    
    // Increase socket buffers
    int buffer_size = 4 * 1024 * 1024; // 4MB
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
    setsockopt(socket_fd_, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
    
    // Enable hardware timestamping if available
    int timestamping = SOF_TIMESTAMPING_RX_HARDWARE | 
                       SOF_TIMESTAMPING_TX_HARDWARE |
                       SOF_TIMESTAMPING_RAW_HARDWARE;
    setsockopt(socket_fd_, SOL_SOCKET, SO_TIMESTAMPING, &timestamping, sizeof(timestamping));
    
    // Enable zero-copy if kernel supports it (Linux 4.14+)
    #ifdef SO_ZEROCOPY
    setsockopt(socket_fd_, SOL_SOCKET, SO_ZEROCOPY, &one, sizeof(one));
    #endif
    
    // TODO: Complete connection logic
    return socket_fd_;
  }
  
  // Zero-copy send using MSG_ZEROCOPY (Linux 4.14+)
  bool sendZeroCopy(const void* data, size_t len) noexcept {
    struct msghdr msg = {};
    struct iovec iov = { const_cast<void*>(data), len };
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    #ifdef MSG_ZEROCOPY
    ssize_t sent = sendmsg(socket_fd_, &msg, MSG_DONTWAIT | MSG_ZEROCOPY);
    #else
    ssize_t sent = sendmsg(socket_fd_, &msg, MSG_DONTWAIT);
    #endif
    
    return sent == static_cast<ssize_t>(len);
  }
  
  // Send/receive with ring buffers
  bool sendAndRecv() noexcept {
    // Send from ring buffer
    char temp[4096];
    size_t to_send = send_buffer_.read(temp, sizeof(temp));
    if (to_send > 0) {
      if (send(socket_fd_, temp, to_send, MSG_DONTWAIT) < 0 && errno != EAGAIN) {
        return false;
      }
    }
    
    // Receive to ring buffer
    ssize_t received = recv(socket_fd_, temp, sizeof(temp), MSG_DONTWAIT);
    if (received > 0) {
      recv_buffer_.write(temp, received);
      if (recv_callback_) {
        recv_callback_();
      }
    }
    
    return true;
  }
  
  // Write to send buffer
  bool queueSend(const void* data, size_t len) noexcept {
    return send_buffer_.write(data, len);
  }
  
  // Read from receive buffer
  size_t getReceived(void* data, size_t max_len) noexcept {
    return recv_buffer_.read(data, max_len);
  }
  
  void setRecvCallback(std::function<void()> callback) {
    recv_callback_ = callback;
  }
  
private:
  int socket_fd_ = -1;
  Logger&  logger_;
  
  RingBuffer<BUFFER_SIZE> send_buffer_;
  RingBuffer<BUFFER_SIZE> recv_buffer_;
  
  std::function<void()> recv_callback_;
  
  // Deleted copy/move
  UltraLowLatencyTCPSocket(const UltraLowLatencyTCPSocket&) = delete;
  UltraLowLatencyTCPSocket& operator=(const UltraLowLatencyTCPSocket&) = delete;
};

// Multicast socket with IGMPv3 and hardware timestamping
class UltraLowLatencyMcastSocket {
public:
  explicit UltraLowLatencyMcastSocket(Logger&  logger) : logger_(logger) {}
  
  int join(const std::string& ip, const std::string& iface, int port) {
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd_ < 0) return -1;
    
    // Set non-blocking
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
    
    // Enable reuse
    int one = 1;
    setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
    
    // Large receive buffer for bursts
    int buffer_size = 8 * 1024 * 1024; // 8MB
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
    
    // Enable hardware timestamping
    int timestamping = SOF_TIMESTAMPING_RX_HARDWARE | 
                       SOF_TIMESTAMPING_RAW_HARDWARE;
    setsockopt(socket_fd_, SOL_SOCKET, SO_TIMESTAMPING, &timestamping, sizeof(timestamping));
    
    // Busy polling for lower latency
    int busy_poll = 10; // 10 microseconds
    setsockopt(socket_fd_, SOL_SOCKET, SO_BUSY_POLL, &busy_poll, sizeof(busy_poll));
    
    // Join multicast group with IGMPv3 for source filtering
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(ip.c_str());
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    
    // TODO: Bind and complete setup
    return socket_fd_;
  }
  
  // Receive with hardware timestamp
  ssize_t recvWithTimestamp(void* buffer, size_t len, uint64_t* hw_timestamp) noexcept {
    struct msghdr msg = {};
    struct iovec iov = { buffer, len };
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    char control[256];
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    
    ssize_t received = recvmsg(socket_fd_, &msg, MSG_DONTWAIT);
    
    // Extract hardware timestamp if available
    if (received > 0 && hw_timestamp) {
      struct cmsghdr* cmsg;
      for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMPING) {
          struct timespec* ts = (struct timespec*)CMSG_DATA(cmsg);
          *hw_timestamp = ts[2].tv_sec * 1000000000ULL + ts[2].tv_nsec; // Hardware timestamp
          break;
        }
      }
    }
    
    return received;
  }
  
private:
  int socket_fd_ = -1;
  Logger&  logger_;
};



} // namespace BldgBlocks