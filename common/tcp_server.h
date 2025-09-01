#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#ifdef BLDG_BLOCKS_URING_SUPPORT
#include <liburing.h>
#endif  // io_uring support (Linux 5.1+)

#include "tcp_socket.h"
#include "socket_utils.h"
#include "logging.h"
#include "mem_pool.h"
#include "thread_utils.h"

namespace Common {


/// Connection pool for zero-allocation client handling
template<size_t MAX_CONNECTIONS>
class ConnectionPool {
public:
  struct Connection {
    UltraTCPSocket socket;
    ClientId client_id = ClientId_INVALID;
    uint64_t last_activity = 0;
    bool is_active = false;
    
    Connection(Logger&  logger) : socket(logger) {}
  };
  
  ConnectionPool(Logger&  logger) : logger_(logger) {
    // Initialize all connections using placement new on static storage
    for (size_t i = 0; i < MAX_CONNECTIONS; ++i) {
      ::new (static_cast<void*>(&connections_[i])) Connection(logger_);
    }
  }
  
  Connection* acquire() noexcept {
    for (auto& conn : connections_) {
      if (!conn->is_active) {
        conn->is_active = true;
        conn->client_id = next_client_id_++;
        return conn.get();
      }
    }
    return nullptr; // Pool exhausted
  }
  
  void release(Connection* conn) noexcept {
    if (conn) {
      conn->is_active = false;
      conn->client_id = ClientId_INVALID;
      conn->last_activity = 0;
    }
  }
  
  size_t activeConnections() const noexcept {
    size_t count = 0;
    for (const auto& conn : connections_) {
      if (conn->is_active) ++count;
    }
    return count;
  }
  
private:
  Logger&  logger_;
  std::array<std::unique_ptr<Connection>, MAX_CONNECTIONS> connections_;
  std::atomic<ClientId> next_client_id_{1};
};

/// Ultra-high performance TCP server with io_uring
class UltraTCPServer {
public:
  static constexpr size_t MAX_CONNECTIONS = 1024;
  static constexpr size_t MAX_EVENTS = 256;
  static constexpr size_t RING_SIZE = 4096;
  
  explicit UltraTCPServer(Logger&  logger, int numa_node = -1) 
    : logger_(logger), numa_node_(numa_node), connection_pool_(logger) {
    
    // Pin to NUMA node if specified
    if (numa_node_ >= 0 && numa_available() >= 0) {
      numa_run_on_node(numa_node_);
    }
  }
  
  ~UltraTCPServer() {
    stop();
  }
  
  // Start server with enhanced performance settings
  bool start(const char* ip, int port, const char* interface = nullptr);
  
  // Stop server gracefully
  void stop() noexcept;
  
  // Main event loop with io_uring (Linux 5.1+)
  void runEventLoop() noexcept;
  
  // Fallback epoll-based event loop
  void runEpollLoop() noexcept;
  
  // Set callback for new connections
  void setConnectionCallback(std::function<void(ClientId, bool)> callback) {
    connection_callback_ = callback;
  }
  
  // Set callback for received data
  void setDataCallback(std::function<void(ClientId, const void*, size_t)> callback) {
    data_callback_ = callback;
  }
  
  // Send data to specific client
  bool sendToClient(ClientId client_id, const void* data, size_t len) noexcept;
  
  // Broadcast data to all clients
  void broadcast(const void* data, size_t len) noexcept;
  
  // Get server statistics
  struct Stats {
    uint64_t connections_accepted = 0;
    uint64_t connections_dropped = 0;
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    uint64_t messages_processed = 0;
    uint64_t epoll_events = 0;
    uint64_t io_uring_completions = 0;
  } stats_;
  
  const Stats& getStats() const noexcept { return stats_; }
  
private:
  // Accept new connection with zero-copy if possible
  void acceptConnection() noexcept;
  
  // Handle client data with minimal copying
  void handleClientData(typename ConnectionPool<MAX_CONNECTIONS>::Connection* conn) noexcept;
  
  // Cleanup disconnected client
  void cleanupClient(typename ConnectionPool<MAX_CONNECTIONS>::Connection* conn) noexcept;
  
  // Setup io_uring if available
  bool setupIoUring() noexcept;
  
  // Setup epoll as fallback
  bool setupEpoll() noexcept;
  
  Logger&  logger_;
  int numa_node_;
  
  // Server socket
  int listen_fd_ = -1;
  struct sockaddr_in server_addr_{};
  
  // Connection management
  ConnectionPool<MAX_CONNECTIONS> connection_pool_;
  
  // io_uring support (Linux 5.1+)
  #ifdef BLDG_BLOCKS_URING_SUPPORT
  struct io_uring ring_{};
#endif
  bool io_uring_supported_ = false;
  
  // Epoll fallback
  int epoll_fd_ = -1;
  std::array<struct epoll_event, MAX_EVENTS> epoll_events_;
  
  // Event notification
  int event_fd_ = -1;  // For graceful shutdown
  std::atomic<bool> running_{false};
  
  // Callbacks
  std::function<void(ClientId, bool)> connection_callback_;       // client_id, connected
  std::function<void(ClientId, const void*, size_t)> data_callback_; // client_id, data, len
  
  // Client lookup for fast access
  std::unordered_map<int, typename ConnectionPool<MAX_CONNECTIONS>::Connection*> fd_to_connection_;
  std::unordered_map<ClientId, typename ConnectionPool<MAX_CONNECTIONS>::Connection*> client_to_connection_;
};

/// Specialized market data server for broadcasting
class MarketDataServer {
public:
  explicit MarketDataServer(Logger&  logger, int numa_node = -1) 
    : server_(logger, numa_node), logger_(logger) {
    
    // Set connection callback
    server_.setConnectionCallback([this](ClientId client_id, bool connected) {
      if (connected) {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        subscribers_.insert(client_id);
        logger_.log("Client % subscribed to market data\n", client_id);
      } else {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        subscribers_.erase(client_id);
        logger_.log("Client % unsubscribed from market data\n", client_id);
      }
    });
  }
  
  bool start(int port) {
    return server_.start("0.0.0.0", port);
  }
  
  void stop() {
    server_.stop();
  }
  
  // Broadcast market data to all subscribers
  void broadcastMarketData(const MarketTick& tick) noexcept {
    server_.broadcast(&tick, sizeof(tick));
    broadcasts_sent_++;
  }
  
  // Broadcast multiple ticks efficiently
  void broadcastMarketData(const MarketTick* ticks, size_t count) noexcept {
    server_.broadcast(ticks, count * sizeof(MarketTick));
    broadcasts_sent_ += count;
  }
  
  void runEventLoop() {
    server_.runEventLoop();
  }
  
  size_t getSubscriberCount() const {
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    return subscribers_.size();
  }
  
  uint64_t getBroadcastsSent() const { return broadcasts_sent_; }
  
private:
  UltraTCPServer server_;
  Logger&  logger_;
  
  mutable std::mutex subscribers_mutex_;
  std::unordered_set<ClientId> subscribers_;
  
  std::atomic<uint64_t> broadcasts_sent_{0};
};

/// Order management server for receiving orders
class OrderServer {
public:
  explicit OrderServer(Logger&  logger, int numa_node = -1) 
    : server_(logger, numa_node), logger_(logger) {
    
    // Set data callback to parse orders
    server_.setDataCallback([this](ClientId client_id, const void* data, size_t len) {
      handleOrderData(client_id, data, len);
    });
  }
  
  bool start(int port) {
    return server_.start("0.0.0.0", port);
  }
  
  void stop() {
    server_.stop();
  }
  
  void runEventLoop() {
    server_.runEventLoop();
  }
  
  // Set callback for parsed orders
  void setOrderCallback(std::function<void(ClientId, const Order&)> callback) {
    order_callback_ = callback;
  }
  
  // Send order acknowledgment back to client
  void sendOrderAck(ClientId client_id, OrderId order_id, bool accepted) noexcept {
    struct OrderAck {
      MessageType type = MessageType::ORDER_ACK;
      OrderId order_id;
      bool accepted;
    } ack{MessageType::ORDER_ACK, order_id, accepted};
    
    server_.sendToClient(client_id, &ack, sizeof(ack));
  }
  
private:
  void handleOrderData(ClientId client_id, const void* data, size_t len) noexcept {
    if (len < sizeof(MessageType)) return;
    
    const auto* msg_type = static_cast<const MessageType*>(data);
    
    switch (*msg_type) {
      case MessageType::NEW_ORDER:
        if (len >= sizeof(Order)) {
          const auto* order = static_cast<const Order*>(data);
          if (order_callback_) {
            order_callback_(client_id, *order);
          }
        }
        break;
      
      // Handle other message types...
      default:
        logger_.log("Unknown message type from client %\n", client_id);
        break;
    }
  }
  
  UltraTCPServer server_;
  Logger&  logger_;
  
  std::function<void(ClientId, const Order&)> order_callback_;
};


} // namespace Common