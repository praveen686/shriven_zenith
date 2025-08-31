#pragma once

#include <iostream>
#include <string>
#include <unordered_set>
#include <sstream>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <linux/net_tstamp.h>
#include <numa.h>

#include "macros.h"
#include "logging.h"

namespace BldgBlocks {


struct SocketCfg {
  std::string ip_;
  std::string iface_;
  int port_ = -1;
  bool is_udp_ = false;
  bool is_listening_ = false;
  bool needs_so_timestamp_ = false;
  bool zero_copy_ = false;
  bool busy_poll_ = true;
  int busy_poll_us_ = 50;
  int numa_node_ = -1;

  auto toString() const {
    std::stringstream ss;
    ss << "SocketCfg[ip:" << ip_
       << " iface:" << iface_
       << " port:" << port_
       << " is_udp:" << is_udp_
       << " is_listening:" << is_listening_
       << " needs_SO_timestamp:" << needs_so_timestamp_
       << " zero_copy:" << zero_copy_
       << " busy_poll:" << busy_poll_
       << " numa_node:" << numa_node_
       << "]";
    return ss.str();
  }
};

/// Maximum number of pending TCP connections
constexpr int MaxTCPServerBacklog = 1024;

/// Convert interface name "eth0" to ip "123.123.123.123"
inline auto getIfaceIP(const std::string &iface) -> std::string {
  char buf[NI_MAXHOST] = {'\0'};
  ifaddrs *ifaddr = nullptr;

  if (getifaddrs(&ifaddr) != -1) {
    for (ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET && iface == ifa->ifa_name) {
        getnameinfo(ifa->ifa_addr, sizeof(sockaddr_in), buf, sizeof(buf), NULL, 0, NI_NUMERICHOST);
        break;
      }
    }
    freeifaddrs(ifaddr);
  }

  return buf;
}

/// Set socket non-blocking
inline auto setNonBlocking(int fd) -> bool {
  const auto flags = fcntl(fd, F_GETFL, 0);
  if (flags & O_NONBLOCK)
    return true;
  return (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1);
}

/// Disable Nagle's algorithm
inline auto disableNagle(int fd) -> bool {
  int one = 1;
  return (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<void *>(&one), sizeof(one)) != -1);
}

/// Enable TCP quick ACK mode
inline auto enableQuickAck(int fd) -> bool {
  int one = 1;
  return (setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, reinterpret_cast<void *>(&one), sizeof(one)) != -1);
}

/// Set TCP user timeout (faster failure detection)
inline auto setTCPUserTimeout(int fd, unsigned int timeout_ms) -> bool {
  return (setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout_ms, sizeof(timeout_ms)) != -1);
}

/// Enable busy polling for lower latency
inline auto setBusyPoll(int fd, int timeout_us) -> bool {
  return (setsockopt(fd, SOL_SOCKET, SO_BUSY_POLL, &timeout_us, sizeof(timeout_us)) != -1);
}

/// Enable hardware timestamping
inline auto setHWTimestamp(int fd) -> bool {
  int timestamping = SOF_TIMESTAMPING_RX_HARDWARE | 
                     SOF_TIMESTAMPING_TX_HARDWARE |
                     SOF_TIMESTAMPING_RAW_HARDWARE;
  return (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &timestamping, sizeof(timestamping)) != -1);
}

/// Enable zero-copy if available (Linux 4.14+)
inline auto setZeroCopy(int fd) -> bool {
#ifdef SO_ZEROCOPY
  int one = 1;
  return (setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &one, sizeof(one)) != -1);
#else
  return false; // Not supported
#endif
}

/// Set large socket buffers
inline auto setLargeBuffers(int fd, int size = 4 * 1024 * 1024) -> bool {
  bool success = true;
  success &= (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) != -1);
  success &= (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) != -1);
  return success;
}

/// Join multicast group
inline auto join(int fd, const std::string &ip) -> bool {
  const ip_mreq mreq{{inet_addr(ip.c_str())}, {htonl(INADDR_ANY)}};
  return (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != -1);
}

/// Create socket with all performance settings
[[nodiscard]] inline auto createSocket(Logger&  logger, const SocketCfg& socket_cfg) -> int {
  std::string time_str;

  const auto ip = socket_cfg.ip_.empty() ? getIfaceIP(socket_cfg.iface_) : socket_cfg.ip_;
  logger.info("%:% %() % cfg:%\n", __FILE__, __LINE__, __FUNCTION__,
             BldgBlocks::getCurrentTimeStr(&time_str), socket_cfg.toString());

  const auto socket_fd = socket(AF_INET, socket_cfg.is_udp_ ? SOCK_DGRAM : SOCK_STREAM, 
                                socket_cfg.is_udp_ ? IPPROTO_UDP : IPPROTO_TCP);
  if (socket_fd < 0) {
    logger.info("%:% %() % socket() failed. error:%\n", __FILE__, __LINE__, __FUNCTION__,
               BldgBlocks::getCurrentTimeStr(&time_str), strerror(errno));
    return -1;
  }

  // Set all performance optimizations
  if (!setNonBlocking(socket_fd)) {
    logger.info("Failed to set non-blocking\n");
  }

  if (!socket_cfg.is_udp_) {
    disableNagle(socket_fd);
    enableQuickAck(socket_fd);
    setTCPUserTimeout(socket_fd, 10000); // 10 seconds
  }

  if (socket_cfg.busy_poll_) {
    setBusyPoll(socket_fd, socket_cfg.busy_poll_us_);
  }

  if (socket_cfg.needs_so_timestamp_) {
    setHWTimestamp(socket_fd);
  }

  if (socket_cfg.zero_copy_) {
    setZeroCopy(socket_fd);
  }

  setLargeBuffers(socket_fd);

  // Enable address reuse
  int one = 1;
  setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  setsockopt(socket_fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

  // Bind to NUMA node if specified
  if (socket_cfg.numa_node_ >= 0 && numa_available() >= 0) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    // Get CPUs for this NUMA node
    struct bitmask* mask = numa_allocate_cpumask();
    numa_node_to_cpus(socket_cfg.numa_node_, mask);
    
    for (int cpu = 0; cpu < numa_num_possible_cpus(); ++cpu) {
      if (numa_bitmask_isbitset(mask, cpu)) {
        CPU_SET(cpu, &cpuset);
        break; // Just use first CPU from this node
      }
    }
    numa_free_cpumask(mask);
    
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
  }

  return socket_fd;
}


} // namespace BldgBlocks