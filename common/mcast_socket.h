#pragma once

#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/net_tstamp.h>

#include "socket_utils.h"
#include "logging.h"

namespace Common {


/// Size of multicast receive buffer (larger for burst handling)
constexpr size_t McastBufferSize = 32 * 1024 * 1024; // 32MB

struct UltraMcastSocket {
  explicit UltraMcastSocket(Logger&  logger) : logger_(logger) {}

  /// Join multicast group with all optimizations
  auto join(const std::string &ip, const std::string &iface, int port) -> int;

  /// High-performance receive with hardware timestamping
  auto recv(void *data, size_t max_len, uint64_t *hw_timestamp = nullptr) noexcept -> ssize_t;

  /// Receive multiple messages in single call (Linux)
  auto recvMultiple(struct mmsghdr *msgvec, unsigned int vlen) noexcept -> int;

  /// Send multicast data (if needed)
  auto send(const void *data, size_t len) noexcept -> ssize_t;

  /// Enable source-specific multicast (IGMPv3)
  auto joinSource(const std::string &group_ip, const std::string &source_ip) -> bool;

  /// Leave multicast group
  auto leave() -> bool;

  /// Set receive callback
  void setRecvCallback(std::function<void(const void*, size_t, uint64_t)> callback) {
    recv_callback_ = callback;
  }

  /// Get statistics
  struct Stats {
    uint64_t packets_received = 0;
    uint64_t bytes_received = 0;
    uint64_t drops = 0;
    uint64_t errors = 0;
  } stats_;

  // Deleted constructors
  UltraMcastSocket() = delete;
  UltraMcastSocket(const UltraMcastSocket &) = delete;
  UltraMcastSocket(const UltraMcastSocket &&) = delete;
  UltraMcastSocket &operator=(const UltraMcastSocket &) = delete;
  UltraMcastSocket &operator=(const UltraMcastSocket &&) = delete;

private:
  int socket_fd_ = -1;
  struct sockaddr_in mcast_addr_{};
  std::string group_ip_;
  std::string interface_;
  
  /// Hardware timestamp support
  bool hw_timestamp_enabled_ = false;
  
  /// Callback for received data
  std::function<void(const void*, size_t, uint64_t)> recv_callback_;
  
  /// Receive buffer (NUMA-aware)
  char* recv_buffer_ = nullptr;
  
  Logger&  logger_;
};

/// Specialized multicast receiver for market data feeds
class MarketDataReceiver {
public:
  explicit MarketDataReceiver(Logger&  logger, int numa_node = -1) 
    : logger_(logger), numa_node_(numa_node) {
    
    // Allocate receive buffer on specified NUMA node
    if (numa_node >= 0 && numa_available() >= 0) {
      buffer_ = static_cast<char*>(numa_alloc_onnode(BUFFER_SIZE, numa_node));
    } else {
      buffer_ = static_cast<char*>(aligned_alloc(64, BUFFER_SIZE));
    }
    ASSERT(buffer_ != nullptr, "Failed to allocate receive buffer");
  }
  
  ~MarketDataReceiver() {
    if (buffer_) {
      if (numa_node_ >= 0 && numa_available() >= 0) {
        numa_free(buffer_, BUFFER_SIZE);
      } else {
        free(buffer_);
      }
    }
  }
  
  // Subscribe to market data feed
  bool subscribe(const std::string& mcast_ip, int port, const std::string& interface = "") {
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd_ < 0) return false;
    
    // Ultra-low latency settings
    SocketCfg cfg;
    cfg.is_udp_ = true;
    cfg.busy_poll_ = true;
    cfg.busy_poll_us_ = 10; // Very aggressive
    cfg.needs_so_timestamp_ = true;
    cfg.numa_node_ = numa_node_;
    
    int fd = createSocket(logger_, cfg);
    if (fd < 0) return false;
    socket_fd_ = fd;
    
    // Bind to port
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      close(socket_fd_);
      return false;
    }
    
    // Join multicast group
    return join(socket_fd_, mcast_ip);
  }
  
  // Receive market data with minimal latency
  ssize_t receiveMarketData(void* data, size_t max_len, uint64_t* hw_timestamp) {
    struct msghdr msg{};
    struct iovec iov = { data, max_len };
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    // Control buffer for timestamps
    char control[256];
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    
    ssize_t received = recvmsg(socket_fd_, &msg, MSG_DONTWAIT);
    
    // Extract hardware timestamp
    if (received > 0 && hw_timestamp) {
      *hw_timestamp = extractHWTimestamp(&msg);
      stats_.packets_received++;
      stats_.bytes_received += received;
    }
    
    return received;
  }
  
  const auto& getStats() const { return stats_; }
  
private:
  static constexpr size_t BUFFER_SIZE = 64 * 1024; // 64KB buffer
  
  uint64_t extractHWTimestamp(struct msghdr* msg) {
    struct cmsghdr* cmsg;
    for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
      if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMPING) {
        struct timespec* ts = (struct timespec*)CMSG_DATA(cmsg);
        // Hardware timestamp is at index 2
        return ts[2].tv_sec * 1000000000ULL + ts[2].tv_nsec;
      }
    }
    return 0; // No hardware timestamp available
  }
  
  int socket_fd_ = -1;
  Logger&  logger_;
  int numa_node_;
  char* buffer_;
  
  struct Stats {
    uint64_t packets_received = 0;
    uint64_t bytes_received = 0;
    uint64_t hw_timestamps = 0;
  } stats_;
};


} // namespace Common