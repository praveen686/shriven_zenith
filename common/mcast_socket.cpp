#include "mcast_socket.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace Common {


int UltraMcastSocket::join(const char* ip, const char* iface, int port) {
    logger_.log("Joining multicast group %:% on interface %\n", ip, port, iface);
    
    strncpy(group_ip_, ip, sizeof(group_ip_) - 1);
    group_ip_[sizeof(group_ip_) - 1] = '\0';
    strncpy(interface_, iface, sizeof(interface_) - 1);
    interface_[sizeof(interface_) - 1] = '\0';
    
    // Create UDP socket with all optimizations
    SocketCfg cfg;
    cfg.ip_ = ip;
    cfg.iface_ = iface;
    cfg.port_ = port;
    cfg.is_udp_ = true;
    cfg.needs_so_timestamp_ = true;
    cfg.busy_poll_ = true;
    cfg.busy_poll_us_ = 5;  // Very aggressive for multicast
    
    socket_fd_ = createSocket(logger_, cfg);
    if (socket_fd_ < 0) {
        logger_.log("Failed to create multicast socket\n");
        return -1;
    }
    
    // Allow multiple sockets to bind to the same multicast address
    int reuse = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        logger_.log("SO_REUSEADDR failed: %\n", strerror(errno));
        close(socket_fd_);
        return -1;
    }
    
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
        logger_.log("SO_REUSEPORT failed: %\n", strerror(errno));
        close(socket_fd_);
        return -1;
    }
    
    // Bind to the multicast address and port
    mcast_addr_.sin_family = AF_INET;
    mcast_addr_.sin_addr.s_addr = INADDR_ANY;  // Bind to any interface
    mcast_addr_.sin_port = htons(port);
    
    if (bind(socket_fd_, (struct sockaddr*)&mcast_addr_, sizeof(mcast_addr_)) < 0) {
        logger_.log("Bind failed: %\n", strerror(errno));
        close(socket_fd_);
        return -1;
    }
    
    // Join multicast group
    struct ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(ip.c_str());
    
    if (!iface.empty()) {
        // Bind to specific interface
        mreq.imr_interface.s_addr = inet_addr(getIfaceIP(iface).c_str());
    } else {
        mreq.imr_interface.s_addr = INADDR_ANY;
    }
    
    if (setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        logger_.log("IP_ADD_MEMBERSHIP failed: %\n", strerror(errno));
        close(socket_fd_);
        return -1;
    }
    
    // Allocate receive buffer with NUMA awareness
    if (numa_available() >= 0) {
        int numa_node = numa_node_of_cpu(sched_getcpu());
        recv_buffer_ = static_cast<char*>(numa_alloc_onnode(McastBufferSize, numa_node));
    } else {
        recv_buffer_ = static_cast<char*>(aligned_alloc(64, McastBufferSize));
    }
    
    if (!recv_buffer_) {
        logger_.log("Failed to allocate receive buffer\n");
        close(socket_fd_);
        return -1;
    }
    
    // Enable hardware timestamping if available
    hw_timestamp_enabled_ = setHWTimestamp(socket_fd_);
    if (hw_timestamp_enabled_) {
        logger_.log("Hardware timestamping enabled\n");
    }
    
    logger_.log("Successfully joined multicast group, fd=%\n", socket_fd_);
    return socket_fd_;
}

ssize_t UltraMcastSocket::recv(void* data, size_t max_len, uint64_t* hw_timestamp) noexcept {
    struct msghdr msg{};
    struct iovec iov = { data, max_len };
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    // Control buffer for timestamps
    alignas(8) char control[256];
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    
    ssize_t received = recvmsg(socket_fd_, &msg, MSG_DONTWAIT);
    if (received > 0) {
        stats_.packets_received++;
        stats_.bytes_received += received;
        
        // Extract hardware timestamp if requested
        if (hw_timestamp && hw_timestamp_enabled_) {
            *hw_timestamp = 0;
            struct cmsghdr* cmsg;
            for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
                if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMPING) {
                    struct timespec* ts = (struct timespec*)CMSG_DATA(cmsg);
                    // Hardware timestamp is at index 2
                    *hw_timestamp = ts[2].tv_sec * 1000000000ULL + ts[2].tv_nsec;
                    break;
                }
            }
        }
        
        // Invoke callback if set
        if (recv_callback_) {
            uint64_t timestamp = hw_timestamp ? *hw_timestamp : rdtsc();
            recv_callback_(data, received, timestamp);
        }
    } else if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            stats_.errors++;
        }
    }
    
    return received;
}

int UltraMcastSocket::recvMultiple(struct mmsghdr* msgvec, unsigned int vlen) noexcept {
    int received = recvmmsg(socket_fd_, msgvec, vlen, MSG_DONTWAIT, nullptr);
    if (received > 0) {
        stats_.packets_received += received;
        
        // Calculate total bytes received
        for (int i = 0; i < received; ++i) {
            stats_.bytes_received += msgvec[i].msg_len;
        }
    } else if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        stats_.errors++;
    }
    
    return received;
}

ssize_t UltraMcastSocket::send(const void* data, size_t len) noexcept {
    ssize_t sent = sendto(socket_fd_, data, len, MSG_DONTWAIT, 
                         (struct sockaddr*)&mcast_addr_, sizeof(mcast_addr_));
    
    if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        stats_.errors++;
    }
    
    return sent;
}

bool UltraMcastSocket::joinSource(const char* group_ip, const char* source_ip) {
    struct ip_mreq_source mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(group_ip);
    mreq.imr_sourceaddr.s_addr = inet_addr(source_ip);
    mreq.imr_interface.s_addr = INADDR_ANY;
    
    if (setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        logger_.log("IP_ADD_SOURCE_MEMBERSHIP failed: %\n", strerror(errno));
        return false;
    }
    
    logger_.log("Joined source-specific multicast: group=%, source=%\n", group_ip, source_ip);
    return true;
}

bool UltraMcastSocket::leave() {
    if (socket_fd_ < 0) return true;
    
    // Leave multicast group
    if (!group_ip_.empty()) {
        struct ip_mreq mreq{};
        mreq.imr_multiaddr.s_addr = inet_addr(group_ip_.c_str());
        
        if (!interface_.empty()) {
            mreq.imr_interface.s_addr = inet_addr(getIfaceIP(interface_).c_str());
        } else {
            mreq.imr_interface.s_addr = INADDR_ANY;
        }
        
        if (setsockopt(socket_fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            logger_.log("IP_DROP_MEMBERSHIP failed: %\n", strerror(errno));
        }
    }
    
    // Close socket
    close(socket_fd_);
    socket_fd_ = -1;
    
    // Free receive buffer
    if (recv_buffer_) {
        if (numa_available() >= 0) {
            numa_free(recv_buffer_, McastBufferSize);
        } else {
            free(recv_buffer_);
        }
        recv_buffer_ = nullptr;
    }
    
    logger_.log("Left multicast group\n");
    return true;
}


} // namespace Common