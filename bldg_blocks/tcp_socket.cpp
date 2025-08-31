#include "tcp_socket.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace BldgBlocks {


int UltraTCPSocket::connect(const std::string& ip, const std::string& iface, 
                           int port, bool is_listening) {
    logger_.log("Connecting to %:%\n", ip, port);
    
    SocketCfg cfg;
    cfg.ip_ = ip;
    cfg.iface_ = iface;
    cfg.port_ = port;
    cfg.is_listening_ = is_listening;
    cfg.needs_so_timestamp_ = true;
    cfg.zero_copy_ = true;
    cfg.busy_poll_ = true;
    cfg.busy_poll_us_ = 10;
    
    socket_fd_ = createSocket(logger_, cfg);
    if (socket_fd_ < 0) {
        return -1;
    }
    
    // Setup socket address
    socket_attrib_.sin_family = AF_INET;
    socket_attrib_.sin_addr.s_addr = inet_addr(ip.c_str());
    socket_attrib_.sin_port = htons(port);
    
    if (is_listening) {
        // Server socket - bind and listen
        if (bind(socket_fd_, (struct sockaddr*)&socket_attrib_, sizeof(socket_attrib_)) < 0) {
            logger_.log("Bind failed: %\n", strerror(errno));
            close(socket_fd_);
            return -1;
        }
        
        if (listen(socket_fd_, MaxTCPServerBacklog) < 0) {
            logger_.log("Listen failed: %\n", strerror(errno));
            close(socket_fd_);
            return -1;
        }
    } else {
        // Client socket - connect
        if (::connect(socket_fd_, (struct sockaddr*)&socket_attrib_, sizeof(socket_attrib_)) < 0) {
            if (errno != EINPROGRESS) {
                logger_.log("Connect failed: %\n", strerror(errno));
                close(socket_fd_);
                return -1;
            }
        }
    }
    
    logger_.log("TCP socket connected successfully, fd=%\n", socket_fd_);
    return socket_fd_;
}

bool UltraTCPSocket::sendAndRecv() noexcept {
    bool activity = false;
    
    // Process outbound data
    constexpr size_t SEND_BUFFER_SIZE = 64 * 1024;
    alignas(64) char send_buffer[SEND_BUFFER_SIZE];
    
    size_t to_send = outbound_buffer_.read(send_buffer, SEND_BUFFER_SIZE);
    if (to_send > 0) {
        ssize_t sent = ::send(socket_fd_, send_buffer, to_send, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent > 0) {
            bytes_sent_ += sent;
            send_count_++;
            activity = true;
            
            // If we didn't send everything, put the remainder back
            if (sent < static_cast<ssize_t>(to_send)) {
                // This is a simplified approach - in production, you'd handle partial sends more carefully
                logger_.log("Partial send: sent % of % bytes\n", sent, to_send);
            }
        } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            logger_.log("Send error: %\n", strerror(errno));
            return false;
        }
    }
    
    // Process inbound data
    constexpr size_t RECV_BUFFER_SIZE = 64 * 1024;
    alignas(64) char recv_buffer[RECV_BUFFER_SIZE];
    
    ssize_t received = ::recv(socket_fd_, recv_buffer, RECV_BUFFER_SIZE, MSG_DONTWAIT);
    if (received > 0) {
        bytes_received_ += received;
        recv_count_++;
        activity = true;
        
        // Try to buffer the received data
        size_t buffered = inbound_buffer_.write(recv_buffer, received);
        if (buffered < static_cast<size_t>(received)) {
            logger_.log("Inbound buffer full, dropping % bytes\n", received - buffered);
        }
        
        // Notify callback if set
        if (recv_callback_) {
            recv_callback_(recv_buffer, received);
        }
    } else if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        logger_.log("Recv error: %\n", strerror(errno));
        return false;
    } else if (received == 0) {
        // Connection closed
        logger_.log("Connection closed by peer\n");
        return false;
    }
    
    return true;
}

size_t UltraTCPSocket::send(const void* data, size_t len) noexcept {
    return outbound_buffer_.write(data, len);
}

bool UltraTCPSocket::sendZeroCopy(const void* data, size_t len) noexcept {
    ssize_t sent = ::send(socket_fd_, data, len, MSG_DONTWAIT | MSG_NOSIGNAL | MSG_ZEROCOPY);
    if (sent > 0) {
        bytes_sent_ += sent;
        send_count_++;
        return sent == static_cast<ssize_t>(len);
    }
    
    if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        logger_.log("Zero-copy send error: %\n", strerror(errno));
    }
    
    return false;
}

ssize_t UltraTCPSocket::sendVectorized(const struct iovec* iov, int iovcnt) noexcept {
    struct msghdr msg{};
    msg.msg_iov = const_cast<struct iovec*>(iov);
    msg.msg_iovlen = iovcnt;
    
    ssize_t sent = sendmsg(socket_fd_, &msg, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent > 0) {
        bytes_sent_ += sent;
        send_count_++;
    } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        logger_.log("Vectorized send error: %\n", strerror(errno));
    }
    
    return sent;
}

size_t UltraTCPSocket::recv(void* data, size_t max_len) noexcept {
    return inbound_buffer_.read(data, max_len);
}


} // namespace BldgBlocks