#include "tcp_server.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace Common {


bool UltraTCPServer::start(const char* ip, int port, const char* interface) {
    logger_.log("Starting UltraTCPServer on %:%\n", ip, port);
    
    // Create server socket with optimizations
    SocketCfg cfg;
    cfg.ip_ = ip;
    cfg.iface_ = interface;
    cfg.port_ = port;
    cfg.is_listening_ = true;
    cfg.needs_so_timestamp_ = true;
    cfg.busy_poll_ = true;
    cfg.busy_poll_us_ = 10;  // Ultra-aggressive polling
    cfg.numa_node_ = numa_node_;
    
    listen_fd_ = createSocket(logger_, cfg);
    if (listen_fd_ < 0) {
        logger_.log("Failed to create listening socket\n");
        return false;
    }
    
    // Bind to address
    server_addr_.sin_family = AF_INET;
    server_addr_.sin_addr.s_addr = inet_addr(ip.c_str());
    server_addr_.sin_port = htons(port);
    
    if (bind(listen_fd_, (struct sockaddr*)&server_addr_, sizeof(server_addr_)) < 0) {
        logger_.log("Failed to bind socket: %\n", strerror(errno));
        close(listen_fd_);
        return false;
    }
    
    // Start listening
    if (listen(listen_fd_, MaxTCPServerBacklog) < 0) {
        logger_.log("Failed to listen: %\n", strerror(errno));
        close(listen_fd_);
        return false;
    }
    
    // Setup io_uring or fallback to epoll
    if (!setupIoUring()) {
        logger_.log("io_uring not available, falling back to epoll\n");
        if (!setupEpoll()) {
            logger_.log("Failed to setup epoll\n");
            close(listen_fd_);
            return false;
        }
    }
    
    // Create eventfd for shutdown signaling
    event_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (event_fd_ < 0) {
        logger_.log("Failed to create eventfd\n");
        close(listen_fd_);
        return false;
    }
    
    running_.store(true, std::memory_order_release);
    logger_.log("UltraTCPServer started successfully\n");
    return true;
}

void UltraTCPServer::stop() noexcept {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }
    
    running_.store(false, std::memory_order_release);
    
    // Signal shutdown via eventfd
    if (event_fd_ >= 0) {
        uint64_t val = 1;
        write(event_fd_, &val, sizeof(val));
        close(event_fd_);
        event_fd_ = -1;
    }
    
    // Close listening socket
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    
    // Close epoll if used
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
    
    // Cleanup io_uring if used
    if (io_uring_supported_) {
        io_uring_queue_exit(&ring_);
        io_uring_supported_ = false;
    }
    
    // Cleanup all connections
    for (auto& [fd, conn] : fd_to_connection_) {
        if (conn->socket.socket_fd_ >= 0) {
            close(conn->socket.socket_fd_);
        }
        connection_pool_.release(conn);
    }
    fd_to_connection_.clear();
    client_to_connection_.clear();
    
    logger_.log("UltraTCPServer stopped\n");
}

void UltraTCPServer::runEventLoop() noexcept {
    if (io_uring_supported_) {
        logger_.log("Using io_uring event loop\n");
        // io_uring implementation would go here
        // For now, fall back to epoll
        runEpollLoop();
    } else {
        runEpollLoop();
    }
}

void UltraTCPServer::runEpollLoop() noexcept {
    logger_.log("Starting epoll event loop\n");
    
    while (running_.load(std::memory_order_acquire)) {
        int nfds = epoll_wait(epoll_fd_, epoll_events_.data(), MAX_EVENTS, 1);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            logger_.log("epoll_wait failed: %\n", strerror(errno));
            break;
        }
        
        for (int i = 0; i < nfds; ++i) {
            const auto& event = epoll_events_[i];
            stats_.epoll_events++;
            
            if (event.data.fd == listen_fd_) {
                // New connection
                acceptConnection();
            } else if (event.data.fd == event_fd_) {
                // Shutdown signal
                logger_.log("Received shutdown signal\n");
                return;
            } else {
                // Client data
                auto it = fd_to_connection_.find(event.data.fd);
                if (it != fd_to_connection_.end()) {
                    if (event.events & (EPOLLERR | EPOLLHUP)) {
                        cleanupClient(it->second);
                    } else if (event.events & EPOLLIN) {
                        handleClientData(it->second);
                    }
                }
            }
        }
    }
    
    logger_.log("Epoll event loop stopped\n");
}

void UltraTCPServer::acceptConnection() noexcept {
    struct sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    
    int client_fd = accept4(listen_fd_, (struct sockaddr*)&client_addr, &addr_len, 
                           SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            logger_.log("accept4 failed: %\n", strerror(errno));
        }
        return;
    }
    
    // Get connection from pool
    auto* conn = connection_pool_.acquire();
    if (!conn) {
        logger_.log("Connection pool exhausted, rejecting client\n");
        close(client_fd);
        stats_.connections_dropped++;
        return;
    }
    
    // Setup client socket with optimizations
    conn->socket.socket_fd_ = client_fd;
    conn->socket.socket_attrib_ = client_addr;
    conn->last_activity = rdtsc();
    
    // Apply socket optimizations
    disableNagle(client_fd);
    enableQuickAck(client_fd);
    setBusyPoll(client_fd, 10);
    setLargeBuffers(client_fd);
    
    // Add to epoll
    struct epoll_event event{};
    event.events = EPOLLIN | EPOLLET; // Edge-triggered
    event.data.fd = client_fd;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event) < 0) {
        logger_.log("epoll_ctl ADD failed: %\n", strerror(errno));
        connection_pool_.release(conn);
        close(client_fd);
        return;
    }
    
    // Add to lookup maps
    fd_to_connection_[client_fd] = conn;
    client_to_connection_[conn->client_id] = conn;
    
    stats_.connections_accepted++;
    
    // Notify callback
    if (connection_callback_) {
        connection_callback_(conn->client_id, true);
    }
    
    logger_.log("Accepted connection from %, client_id=%\n", 
                inet_ntoa(client_addr.sin_addr), conn->client_id);
}

void UltraTCPServer::handleClientData(typename ConnectionPool<MAX_CONNECTIONS>::Connection* conn) noexcept {
    constexpr size_t BUFFER_SIZE = 64 * 1024;
    alignas(64) char buffer[BUFFER_SIZE];
    
    ssize_t received = recv(conn->socket.socket_fd_, buffer, BUFFER_SIZE, MSG_DONTWAIT);
    if (received <= 0) {
        if (received == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            // Connection closed or error
            cleanupClient(conn);
        }
        return;
    }
    
    conn->last_activity = rdtsc();
    stats_.bytes_received += received;
    stats_.messages_processed++;
    
    // Process received data through callback
    if (data_callback_) {
        data_callback_(conn->client_id, buffer, received);
    }
}

void UltraTCPServer::cleanupClient(typename ConnectionPool<MAX_CONNECTIONS>::Connection* conn) noexcept {
    const int client_fd = conn->socket.socket_fd_;
    const ClientId client_id = conn->client_id;
    
    // Remove from epoll
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    
    // Close socket
    if (client_fd >= 0) {
        close(client_fd);
        conn->socket.socket_fd_ = -1;
    }
    
    // Remove from lookup maps
    fd_to_connection_.erase(client_fd);
    client_to_connection_.erase(client_id);
    
    // Return to pool
    connection_pool_.release(conn);
    
    // Notify callback
    if (connection_callback_) {
        connection_callback_(client_id, false);
    }
    
    logger_.log("Cleaned up client %, fd=%\n", client_id, client_fd);
}

bool UltraTCPServer::sendToClient(ClientId client_id, const void* data, size_t len) noexcept {
    auto it = client_to_connection_.find(client_id);
    if (it == client_to_connection_.end()) {
        return false;
    }
    
    auto* conn = it->second;
    ssize_t sent = send(conn->socket.socket_fd_, data, len, MSG_DONTWAIT | MSG_NOSIGNAL);
    
    if (sent > 0) {
        stats_.bytes_sent += sent;
        return sent == static_cast<ssize_t>(len);
    }
    
    return false;
}

void UltraTCPServer::broadcast(const void* data, size_t len) noexcept {
    for (const auto& [client_id, conn] : client_to_connection_) {
        sendToClient(client_id, data, len);
    }
}

bool UltraTCPServer::setupIoUring() noexcept {
    // Try to setup io_uring (Linux 5.1+)
    int ret = io_uring_queue_init(RING_SIZE, &ring_, 0);
    if (ret < 0) {
        logger_.log("io_uring_queue_init failed: %\n", strerror(-ret));
        return false;
    }
    
    // Check if io_uring features we need are supported
    struct io_uring_probe* probe = io_uring_get_probe_ring(&ring_);
    if (!probe) {
        io_uring_queue_exit(&ring_);
        return false;
    }
    
    bool has_accept = io_uring_opcode_supported(probe, IORING_OP_ACCEPT);
    bool has_recv = io_uring_opcode_supported(probe, IORING_OP_RECV);
    bool has_send = io_uring_opcode_supported(probe, IORING_OP_SEND);
    
    free(probe);
    
    if (!has_accept || !has_recv || !has_send) {
        logger_.log("Required io_uring opcodes not supported\n");
        io_uring_queue_exit(&ring_);
        return false;
    }
    
    io_uring_supported_ = true;
    logger_.log("io_uring setup successful\n");
    return true;
}

bool UltraTCPServer::setupEpoll() noexcept {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        logger_.log("epoll_create1 failed: %\n", strerror(errno));
        return false;
    }
    
    // Add listening socket to epoll
    struct epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = listen_fd_;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event) < 0) {
        logger_.log("epoll_ctl ADD listen_fd failed: %\n", strerror(errno));
        close(epoll_fd_);
        return false;
    }
    
    // Add event fd for shutdown signaling
    event.data.fd = event_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &event) < 0) {
        logger_.log("epoll_ctl ADD event_fd failed: %\n", strerror(errno));
        close(epoll_fd_);
        return false;
    }
    
    logger_.log("Epoll setup successful\n");
    return true;
}


} // namespace Common