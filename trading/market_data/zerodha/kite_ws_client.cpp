#include "kite_ws_client.h"
#include "common/time_utils.h"
#include <chrono>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <openssl/err.h>

namespace Trading::MarketData::Zerodha {

KiteWSClient::KiteWSClient(Common::LFQueue<Common::MarketUpdate, 262144>* output_queue,
                          const Config& config)
    : IMarketDataConsumer(output_queue)
    , config_(config) {
    
    // Initialize token mappings
    for (size_t i = 0; i < MAX_INSTRUMENTS; ++i) {
        subscribed_tokens_[i].store(false);
        token_modes_[i].store(KiteMode::MODE_LTP);
        token_to_ticker_[i] = TickerId_INVALID;
    }
    
    LOG_INFO("KiteWSClient initialized with endpoint: %s", config_.ws_endpoint);
}

KiteWSClient::~KiteWSClient() {
    stop();
    cleanupSSL();
}

auto KiteWSClient::start() -> void {
    if (running_.exchange(true)) {
        return;  // Already running
    }
    
    // Initialize SSL
    if (!initSSL()) {
        LOG_ERROR("Failed to initialize SSL");
        running_ = false;
        return;
    }
    
    // Start WebSocket thread
    ws_thread_ = std::thread([this]() {
        if (config_.cpu_affinity >= 0) {
            Common::setThreadCore(config_.cpu_affinity);
        }
        // Common::setThreadName("KiteWS");
        wsThreadMain();
    });
    
    // Start heartbeat thread
    heartbeat_thread_ = std::thread([this]() {
        // Common::setThreadName("KiteHB");
        heartbeatThreadMain();
    });
    
    LOG_INFO("KiteWSClient started");
}

auto KiteWSClient::stop() -> void {
    if (!running_.exchange(false)) {
        return;  // Not running
    }
    
    disconnect();
    
    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    
    LOG_INFO("KiteWSClient stopped - Ticks: %lu, Dropped: %lu, Reconnects: %lu",
             ticks_received_.load(), ticks_dropped_.load(), reconnect_count_.load());
}

auto KiteWSClient::connect() -> bool {
    if (connected_.load()) {
        return true;
    }
    
    // Parse WebSocket URL using char arrays
    char url[256];
    char host[256];
    int port = 443;  // Default HTTPS port
    
    std::strncpy(url, config_.ws_endpoint, sizeof(url) - 1);
    url[sizeof(url) - 1] = '\0';
    
    // Extract host and port from wss://host:port/path
    const char* url_start = url;
    const char* proto_end = std::strstr(url_start, "://");
    if (proto_end) {
        url_start = proto_end + 3;
    }
    
    // Find path separator
    const char* path_start = std::strchr(url_start, '/');
    size_t host_len = 0;
    if (path_start) {
        host_len = static_cast<size_t>(path_start - url_start);
    } else {
        host_len = std::strlen(url_start);
    }
    
    if (host_len >= sizeof(host)) {
        host_len = sizeof(host) - 1;
    }
    std::strncpy(host, url_start, host_len);
    host[host_len] = '\0';
    
    // Check for port in host
    char* port_sep = std::strchr(host, ':');
    if (port_sep) {
        port = std::atoi(port_sep + 1);
        *port_sep = '\0';  // Terminate host string at colon
    }
    
    // Resolve host
    struct hostent* server = gethostbyname(host);
    if (!server) {
        LOG_ERROR("Failed to resolve host: %s", host);
        return false;
    }
    
    // Create socket
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return false;
    }
    
    // Set non-blocking mode
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);
    
    // Connect to server
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    std::memcpy(&server_addr.sin_addr.s_addr, server->h_addr, static_cast<size_t>(server->h_length));
    server_addr.sin_port = htons(static_cast<uint16_t>(port));
    
    if (::connect(socket_fd_, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        if (errno != EINPROGRESS) {
            LOG_ERROR("Failed to connect: %s", strerror(errno));
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
        
        // Wait for connection with timeout
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(socket_fd_, &fdset);
        
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        
        int nfds = static_cast<int>(socket_fd_) + 1;
        if (select(nfds, nullptr, &fdset, nullptr, &tv) <= 0) {
            LOG_ERROR("Connection timeout");
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
    }
    
    // Setup SSL
    ssl_ = SSL_new(ssl_ctx_);
    if (!ssl_) {
        LOG_ERROR("Failed to create SSL object");
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    SSL_set_fd(ssl_, socket_fd_);
    
    // Set SNI hostname for TLS
    SSL_ctrl(ssl_, SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name, 
             const_cast<char*>("ws.kite.trade"));
    
    // Perform SSL handshake with retry logic for non-blocking socket
    int ret = 0;
    unsigned int max_retries = 10;
    
    while (max_retries != 0) {
        ret = SSL_connect(ssl_);
        if (ret == 1) {
            // Success
            break;
        }
        
        int ssl_err = SSL_get_error(ssl_, ret);
        
        if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
            // Non-blocking socket needs to retry
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(socket_fd_, &fds);
            
            struct timeval timeout;
            timeout.tv_sec = 5;
            timeout.tv_usec = 0;
            
            int select_ret = 0;
            int nfds = static_cast<int>(socket_fd_) + 1;
            if (ssl_err == SSL_ERROR_WANT_READ) {
                select_ret = select(nfds, &fds, nullptr, nullptr, &timeout);
            } else {
                select_ret = select(nfds, nullptr, &fds, nullptr, &timeout);
            }
            
            if (select_ret > 0) {
                // Socket is ready, retry SSL_connect
                max_retries--;
                continue;
            } else if (select_ret == 0) {
                LOG_ERROR("SSL handshake timeout");
                SSL_free(ssl_);
                ssl_ = nullptr;
                close(socket_fd_);
                socket_fd_ = -1;
                return false;
            } else {
                LOG_ERROR("select() failed: %s", strerror(errno));
                SSL_free(ssl_);
                ssl_ = nullptr;
                close(socket_fd_);
                socket_fd_ = -1;
                return false;
            }
        } else {
            // Real error
            char err_buf[256];
            unsigned long err_code = ERR_get_error();
            ERR_error_string_n(err_code, err_buf, sizeof(err_buf));
            
            const char* err_str = "Unknown";
            switch(ssl_err) {
                case SSL_ERROR_NONE: err_str = "SSL_ERROR_NONE"; break;
                case SSL_ERROR_SSL: err_str = "SSL_ERROR_SSL"; break;
                case SSL_ERROR_SYSCALL: err_str = "SSL_ERROR_SYSCALL"; break;
                case SSL_ERROR_ZERO_RETURN: err_str = "SSL_ERROR_ZERO_RETURN"; break;
                default: err_str = "Unknown"; break;
            }
            
            LOG_ERROR("SSL handshake failed - Return: %d, Error Type: %s (%d), OpenSSL Error: %s (0x%lX)", 
                      ret, err_str, ssl_err, err_buf, err_code);
            
            if (ssl_err == SSL_ERROR_SYSCALL) {
                LOG_ERROR("System error: %s (errno=%d)", strerror(errno), errno);
            }
            
            SSL_free(ssl_);
            ssl_ = nullptr;
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
    }
    
    if (ret != 1) {
        LOG_ERROR("SSL handshake failed after %d retries", 10 - max_retries);
        SSL_free(ssl_);
        ssl_ = nullptr;
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    // Perform WebSocket handshake
    if (!performWebSocketHandshake()) {
        LOG_ERROR("WebSocket handshake failed");
        disconnect();
        return false;
    }
    
    connected_.store(true);
    last_pong_ns_.store(Common::getNanosSinceEpoch());
    
    LOG_INFO("Connected to Kite WebSocket");
    return true;
}

auto KiteWSClient::disconnect() -> void {
    connected_.store(false);
    
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    
    LOG_INFO("Disconnected from Kite WebSocket");
}

auto KiteWSClient::subscribe(Common::TickerId) -> bool {
    // This requires mapping ticker_id to instrument token
    // Implementation depends on symbol resolver
    return true;
}

auto KiteWSClient::unsubscribe(Common::TickerId) -> bool {
    return true;
}

auto KiteWSClient::subscribeTokens(const uint32_t* tokens, size_t count, KiteMode mode) -> bool {
    if (!connected_.load()) {
        LOG_WARN("Cannot subscribe - not connected");
        return false;
    }
    
    // Send subscribe message
    uint8_t buffer[4096];
    size_t msg_len = buildSubscribeMessage(tokens, count, mode, buffer);
    
    if (!sendWebSocketFrame(buffer, msg_len, 0x01)) {  // 0x01 = text frame
        LOG_ERROR("Failed to send subscribe message");
        return false;
    }
    
    // Send mode message
    char mode_msg[4096];
    const char* mode_str = "full";
    if (mode == KiteMode::MODE_LTP) mode_str = "ltp";
    else if (mode == KiteMode::MODE_QUOTE) mode_str = "quote";
    
    int mode_len = std::snprintf(mode_msg, sizeof(mode_msg), "{\"a\":\"mode\",\"v\":[\"%s\",[", mode_str);
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            mode_len += std::snprintf(mode_msg + mode_len, sizeof(mode_msg) - static_cast<size_t>(mode_len), ",%u", tokens[i]);
        } else {
            mode_len += std::snprintf(mode_msg + mode_len, sizeof(mode_msg) - static_cast<size_t>(mode_len), "%u", tokens[i]);
        }
    }
    mode_len += std::snprintf(mode_msg + mode_len, sizeof(mode_msg) - static_cast<size_t>(mode_len), "]]}");
    
    LOG_INFO("Mode message: %s", mode_msg);
    
    if (!sendWebSocketFrame(reinterpret_cast<const uint8_t*>(mode_msg), static_cast<size_t>(mode_len), 0x01)) {
        LOG_ERROR("Failed to send mode message");
        return false;
    }
    
    // Mark tokens as subscribed
    for (size_t i = 0; i < count; ++i) {
        if (tokens[i] < MAX_INSTRUMENTS) {
            subscribed_tokens_[tokens[i]].store(true);
            token_modes_[tokens[i]].store(mode);
        }
    }
    
    LOG_INFO("Subscribed to %zu tokens in mode %s", count, mode_str);
    return true;
}

auto KiteWSClient::unsubscribeTokens(const uint32_t* tokens, size_t count) -> bool {
    if (!connected_.load()) {
        return false;
    }
    
    // Build unsubscribe message
    uint8_t buffer[4096];
    buffer[0] = 0x12;  // Unsubscribe message type
    
    size_t pos = 1;
    // Add token count
    uint16_t token_count = static_cast<uint16_t>(count);
    std::memcpy(buffer + pos, &token_count, sizeof(token_count));
    pos += sizeof(token_count);
    
    // Add tokens
    for (size_t i = 0; i < count && pos + 4 <= sizeof(buffer); ++i) {
        std::memcpy(buffer + pos, &tokens[i], sizeof(uint32_t));
        pos += sizeof(uint32_t);
    }
    
    if (!sendWebSocketFrame(buffer, pos)) {
        LOG_ERROR("Failed to send unsubscribe message");
        return false;
    }
    
    // Mark tokens as unsubscribed
    for (size_t i = 0; i < count; ++i) {
        if (tokens[i] < MAX_INSTRUMENTS) {
            subscribed_tokens_[tokens[i]].store(false);
        }
    }
    
    return true;
}

auto KiteWSClient::setMode(const uint32_t* tokens, size_t count, KiteMode mode) -> bool {
    if (!connected_.load()) {
        return false;
    }
    
    // Build mode message
    uint8_t buffer[4096];
    buffer[0] = 0x13;  // Mode message type
    
    size_t pos = 1;
    // Add mode
    buffer[pos++] = static_cast<uint8_t>(mode);
    
    // Add token count
    uint16_t token_count = static_cast<uint16_t>(count);
    std::memcpy(buffer + pos, &token_count, sizeof(token_count));
    pos += sizeof(token_count);
    
    // Add tokens
    for (size_t i = 0; i < count && pos + 4 <= sizeof(buffer); ++i) {
        std::memcpy(buffer + pos, &tokens[i], sizeof(uint32_t));
        pos += sizeof(uint32_t);
    }
    
    if (!sendWebSocketFrame(buffer, pos)) {
        LOG_ERROR("Failed to send mode message");
        return false;
    }
    
    // Update token modes
    for (size_t i = 0; i < count; ++i) {
        if (tokens[i] < MAX_INSTRUMENTS) {
            token_modes_[tokens[i]].store(mode);
        }
    }
    
    return true;
}

auto KiteWSClient::wsThreadMain() -> void {
    LOG_INFO("WebSocket thread started");
    
    // Wait for initial connection to be established by main thread
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    while (running_.load()) {
        if (!connected_.load()) {
            // Only reconnect if we previously had a connection
            if (reconnect_count_.load() > 0 || ticks_received_.load() > 0) {
                handleReconnect();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.reconnect_interval_ms));
            continue;
        }
        
        // Read data from socket
        uint8_t temp_buffer[4096];
        int bytes_read = SSL_read(ssl_, temp_buffer, sizeof(temp_buffer));
        
        if (bytes_read > 0) {
            // Add to receive buffer
            if (recv_buffer_pos_ + static_cast<size_t>(bytes_read) <= RECV_BUFFER_SIZE) {
                std::memcpy(recv_buffer_ + recv_buffer_pos_, temp_buffer, static_cast<size_t>(bytes_read));
                recv_buffer_pos_ += static_cast<size_t>(bytes_read);
                
                // Process complete frames
                processReceivedData(recv_buffer_, recv_buffer_pos_);
            } else {
                LOG_WARN("Receive buffer overflow");
                recv_buffer_pos_ = 0;
            }
        } else if (bytes_read == 0) {
            // Connection closed
            LOG_WARN("Connection closed by server");
            disconnect();
        } else {
            int err = SSL_get_error(ssl_, bytes_read);
            if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                LOG_ERROR("SSL read error: %d", err);
                disconnect();
            }
        }
        
        // Small sleep to prevent CPU spinning
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    LOG_INFO("WebSocket thread stopped");
}

auto KiteWSClient::heartbeatThreadMain() -> void {
    LOG_INFO("Heartbeat thread started");
    
    while (running_.load()) {
        if (connected_.load()) {
            uint64_t now_ns = Common::getNanosSinceEpoch();
            uint64_t last_ping = last_ping_ns_.load();
            uint64_t last_pong = last_pong_ns_.load();
            
            // Send ping every ping_interval_s seconds
            if (now_ns - last_ping > config_.ping_interval_s * 1000000000ULL) {
                sendPing();
                last_ping_ns_.store(now_ns);
            }
            
            // Check for pong timeout (3x ping interval)
            if (now_ns - last_pong > config_.ping_interval_s * 3 * 1000000000ULL) {
                LOG_WARN("Pong timeout - reconnecting");
                disconnect();
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    LOG_INFO("Heartbeat thread stopped");
}

auto KiteWSClient::processReceivedData(const uint8_t* data, size_t len) -> void {
    size_t pos = 0;
    
    while (pos < len) {
        // Check for WebSocket frame
        if (pos + 2 > len) break;
        
        // uint8_t fin = (data[pos] & 0x80) >> 7;  // Currently unused
        uint8_t opcode = data[pos] & 0x0F;
        uint8_t masked = (data[pos + 1] & 0x80) >> 7;
        uint64_t payload_len = data[pos + 1] & 0x7F;
        
        pos += 2;
        
        // Extended payload length
        if (payload_len == 126) {
            if (pos + 2 > len) break;
            payload_len = (static_cast<uint64_t>(data[pos]) << 8) | data[pos + 1];
            pos += 2;
        } else if (payload_len == 127) {
            if (pos + 8 > len) break;
            payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                payload_len = (payload_len << 8) | data[pos + static_cast<size_t>(i)];
            }
            pos += 8;
        }
        
        // Skip mask if present (server->client should not be masked)
        if (masked) {
            pos += 4;
        }
        
        // Check if we have complete payload
        if (pos + payload_len > len) break;
        
        // Process based on opcode
        switch (opcode) {
            case 0x02:  // Binary frame
                LOG_INFO("Received binary frame with %lu bytes", payload_len);
                parseBinaryPacket(data + pos, static_cast<size_t>(payload_len));
                break;
                
            case 0x09:  // Ping
                // Send pong
                sendWebSocketFrame(data + pos, static_cast<size_t>(payload_len), 0x0A);
                break;
                
            case 0x0A:  // Pong
                last_pong_ns_.store(Common::getNanosSinceEpoch());
                break;
                
            case 0x08:  // Close
                LOG_WARN("Server sent close frame");
                disconnect();
                return;
                
            default:
                // Unknown opcode - ignore
                break;
        }
        
        pos += payload_len;
    }
    
    // Move remaining data to beginning of buffer
    if (pos < len) {
        std::memmove(recv_buffer_, data + pos, len - pos);
        recv_buffer_pos_ = len - pos;
    } else {
        recv_buffer_pos_ = 0;
    }
}

auto KiteWSClient::parseBinaryPacket(const uint8_t* data, size_t len) -> bool {
    if (len < 2) {
        LOG_WARN("Packet too small: %zu bytes", len);
        return false;
    }
    
    // First two bytes indicate number of packets and length
    uint16_t num_packets = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    
    LOG_INFO("Binary packet contains %u sub-packets", num_packets);
    
    size_t pos = 2;
    
    for (uint16_t i = 0; i < num_packets && pos < len; ++i) {
        if (pos + 2 > len) break;
        
        uint16_t packet_len = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
        pos += 2;
        
        if (pos + packet_len > len) {
            LOG_WARN("Incomplete packet");
            break;
        }
        
        // Parse based on packet length
        if (packet_len == 8) {
            // LTP packet
            if (pos + sizeof(KiteLTPPacket) <= len) {
                parseLTPPacket(reinterpret_cast<const KiteLTPPacket*>(data + pos));
            }
        } else if (packet_len == 44) {
            // Quote packet
            if (pos + sizeof(KiteQuotePacket) <= len) {
                parseQuotePacket(reinterpret_cast<const KiteQuotePacket*>(data + pos));
            }
        } else if (packet_len == 184) {
            // Full packet with depth
            if (pos + sizeof(KiteFullPacket) <= len) {
                parseFullPacket(reinterpret_cast<const KiteFullPacket*>(data + pos));
            }
        }
        
        pos += packet_len;
    }
    
    return true;
}

auto KiteWSClient::parseLTPPacket(const KiteLTPPacket* packet) -> void {
    auto* tick = static_cast<KiteTickData*>(tick_pool_.allocate());
    if (!tick) {
        ticks_dropped_.fetch_add(1);
        return;
    }
    
    tick->instrument_token = __builtin_bswap32(packet->instrument_token);
    tick->last_price = convertPrice(__builtin_bswap32(packet->last_price));
    tick->local_timestamp_ns = Common::getNanosSinceEpoch();
    tick->ticker_id = token_to_ticker_[tick->instrument_token];
    
    // Create market update
    Common::MarketUpdate update;
    update.update_type = MessageType::MARKET_DATA;
    update.ticker_id = tick->ticker_id;
    // update.side not available
    update.bid_price = tick->last_price;
    update.bid_qty = 0;
    // update.priority not available
    
    if (!publishUpdate(update)) {
        ticks_dropped_.fetch_add(1);
    } else {
        ticks_received_.fetch_add(1);
    }
    
    tick_pool_.deallocate(tick);
}

auto KiteWSClient::parseQuotePacket(const KiteQuotePacket* packet) -> void {
    auto* tick = static_cast<KiteTickData*>(tick_pool_.allocate());
    if (!tick) {
        ticks_dropped_.fetch_add(1);
        return;
    }
    
    tick->instrument_token = __builtin_bswap32(packet->instrument_token);
    tick->last_price = convertPrice(__builtin_bswap32(packet->last_price));
    tick->last_qty = convertQty(__builtin_bswap32(packet->last_quantity));
    tick->volume = convertQty(__builtin_bswap32(packet->volume));
    tick->open = convertPrice(__builtin_bswap32(packet->open));
    tick->high = convertPrice(__builtin_bswap32(packet->high));
    tick->low = convertPrice(__builtin_bswap32(packet->low));
    tick->close = convertPrice(__builtin_bswap32(packet->close));
    tick->local_timestamp_ns = Common::getNanosSinceEpoch();
    tick->ticker_id = token_to_ticker_[tick->instrument_token];
    
    // Create market update
    Common::MarketUpdate update;
    update.update_type = MessageType::MARKET_DATA;
    update.ticker_id = tick->ticker_id;
    // update.side not available
    update.bid_price = tick->last_price;
    update.bid_qty = tick->last_qty;
    // update.priority not available
    
    if (!publishUpdate(update)) {
        ticks_dropped_.fetch_add(1);
    } else {
        ticks_received_.fetch_add(1);
    }
    
    tick_pool_.deallocate(tick);
}

auto KiteWSClient::parseFullPacket(const KiteFullPacket* packet) -> void {
    // Parse tick data
    auto* tick = static_cast<KiteTickData*>(tick_pool_.allocate());
    if (!tick) {
        ticks_dropped_.fetch_add(1);
        return;
    }
    
    tick->instrument_token = __builtin_bswap32(packet->instrument_token);
    tick->last_price = convertPrice(__builtin_bswap32(packet->last_price));
    tick->last_qty = convertQty(__builtin_bswap32(packet->last_quantity));
    tick->volume = convertQty(__builtin_bswap32(packet->volume));
    tick->oi = convertQty(__builtin_bswap32(packet->oi));
    tick->open = convertPrice(__builtin_bswap32(packet->open));
    tick->high = convertPrice(__builtin_bswap32(packet->high));
    tick->low = convertPrice(__builtin_bswap32(packet->low));
    tick->close = convertPrice(__builtin_bswap32(packet->close));
    tick->exchange_timestamp_ns = static_cast<uint64_t>(__builtin_bswap32(packet->timestamp)) * 1000000000ULL;
    tick->local_timestamp_ns = Common::getNanosSinceEpoch();
    tick->ticker_id = token_to_ticker_[tick->instrument_token];
    
    // Parse depth data
    auto* depth = static_cast<KiteDepthUpdate*>(depth_pool_.allocate());
    if (depth) {
        depth->instrument_token = tick->instrument_token;
        depth->ticker_id = tick->ticker_id;
        depth->local_timestamp_ns = tick->local_timestamp_ns;
        
        // Parse bid levels
        depth->bid_count = 0;
        for (size_t i = 0; i < 5; ++i) {
            uint32_t qty = __builtin_bswap32(packet->bid[i].quantity);
            if (qty > 0) {
                depth->bid_prices[depth->bid_count] = convertPrice(__builtin_bswap32(packet->bid[i].price));
                depth->bid_qtys[depth->bid_count] = convertQty(qty);
                depth->bid_orders[depth->bid_count] = __builtin_bswap16(packet->bid[i].orders);
                depth->bid_count++;
            }
        }
        
        // Parse ask levels
        depth->ask_count = 0;
        for (size_t i = 0; i < 5; ++i) {
            uint32_t qty = __builtin_bswap32(packet->ask[i].quantity);
            if (qty > 0) {
                depth->ask_prices[depth->ask_count] = convertPrice(__builtin_bswap32(packet->ask[i].price));
                depth->ask_qtys[depth->ask_count] = convertQty(qty);
                depth->ask_orders[depth->ask_count] = __builtin_bswap16(packet->ask[i].orders);
                depth->ask_count++;
            }
        }
        
        // Log tick data for display
        static uint64_t tick_counter = 0;
        if (++tick_counter % 100 == 1) {  // Log every 100th tick
            LOG_INFO("[MARKET DATA] Token=%u, LTP=%.2f, Vol=%u, Bid=%.2f@%u, Ask=%.2f@%u, O=%.2f H=%.2f L=%.2f C=%.2f",
                    tick->instrument_token,
                    static_cast<double>(tick->last_price) / 100.0,  // Convert paise to rupees
                    tick->volume,
                    depth->bid_count > 0 ? static_cast<double>(depth->bid_prices[0]) / 100.0 : 0.0,
                    depth->bid_count > 0 ? depth->bid_qtys[0] : 0,
                    depth->ask_count > 0 ? static_cast<double>(depth->ask_prices[0]) / 100.0 : 0.0,
                    depth->ask_count > 0 ? depth->ask_qtys[0] : 0,
                    static_cast<double>(tick->open) / 100.0,
                    static_cast<double>(tick->high) / 100.0,
                    static_cast<double>(tick->low) / 100.0,
                    static_cast<double>(tick->close) / 100.0);
        }
        
        // Send depth updates
        for (uint8_t i = 0; i < depth->bid_count; ++i) {
            Common::MarketUpdate update;
            update.update_type = MessageType::MARKET_DATA;
            update.ticker_id = depth->ticker_id;
            // update.side not available
            update.bid_price = depth->bid_prices[i];
            update.bid_qty = depth->bid_qtys[i];
            // update.priority not available
            publishUpdate(update);
        }
        
        for (uint8_t i = 0; i < depth->ask_count; ++i) {
            Common::MarketUpdate update;
            update.update_type = MessageType::MARKET_DATA;
            update.ticker_id = depth->ticker_id;
            // update.side not available
            update.bid_price = depth->ask_prices[i];
            update.bid_qty = depth->ask_qtys[i];
            // update.priority not available
            publishUpdate(update);
        }
        
        depth_pool_.deallocate(depth);
    }
    
    // Send trade update
    Common::MarketUpdate update;
    update.update_type = MessageType::MARKET_DATA;
    update.ticker_id = tick->ticker_id;
    // update.side not available
    update.bid_price = tick->last_price;
    update.bid_qty = tick->last_qty;
    // update.priority not available
    
    if (!publishUpdate(update)) {
        ticks_dropped_.fetch_add(1);
    } else {
        ticks_received_.fetch_add(1);
    }
    
    tick_pool_.deallocate(tick);
}

auto KiteWSClient::sendWebSocketFrame(const uint8_t* data, size_t len, uint8_t opcode) -> bool {
    if (!connected_.load() || !ssl_) {
        return false;
    }
    
    uint8_t frame[4096];
    size_t frame_pos = 0;
    
    // FIN = 1, opcode
    frame[frame_pos++] = 0x80 | opcode;
    
    // Mask = 1 (client->server must be masked), payload length
    if (len < 126) {
        frame[frame_pos++] = 0x80 | static_cast<uint8_t>(len);
    } else if (len < 65536) {
        frame[frame_pos++] = 0x80 | 126;
        frame[frame_pos++] = static_cast<uint8_t>(len >> 8);
        frame[frame_pos++] = static_cast<uint8_t>(len & 0xFF);
    } else {
        LOG_ERROR("Payload too large");
        return false;
    }
    
    // Masking key (random)
    uint8_t mask[4];
    for (int i = 0; i < 4; ++i) {
        mask[i] = static_cast<uint8_t>(rand() & 0xFF);
        frame[frame_pos++] = mask[i];
    }
    
    // Masked payload
    for (size_t i = 0; i < len; ++i) {
        frame[frame_pos++] = data[i] ^ mask[i % 4];
    }
    
    // Send frame
    int written = SSL_write(ssl_, frame, static_cast<int>(frame_pos));
    return written == static_cast<int>(frame_pos);
}

auto KiteWSClient::sendPing() -> bool {
    return sendWebSocketFrame(nullptr, 0, 0x09);  // 0x09 = ping
}

auto KiteWSClient::handleReconnect() -> void {
    LOG_INFO("Attempting to reconnect to WebSocket...");
    
    if (connect()) {
        reconnect_count_.fetch_add(1);
        
        // Resubscribe to all tokens
        uint32_t tokens_to_subscribe[1000];
        size_t token_count = 0;
        
        for (size_t i = 0; i < MAX_INSTRUMENTS && token_count < 1000; ++i) {
            if (subscribed_tokens_[i].load()) {
                tokens_to_subscribe[token_count++] = static_cast<uint32_t>(i);
            }
        }
        
        if (token_count > 0) {
            subscribeTokens(tokens_to_subscribe, token_count, KiteMode::MODE_FULL);
            LOG_INFO("Resubscribed to %zu tokens", token_count);
        }
    }
}

auto KiteWSClient::initSSL() -> bool {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    ssl_ctx_ = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx_) {
        LOG_ERROR("Failed to create SSL context");
        return false;
    }
    
    // Set options for compatibility and modern TLS
    SSL_CTX_set_options(ssl_ctx_, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
    
    // Set minimum TLS version to 1.2
    SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_2_VERSION);
    
    // Load system CA certificates for verification
    if (SSL_CTX_set_default_verify_paths(ssl_ctx_) != 1) {
        LOG_ERROR("Failed to set default verify paths");
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
    }
    
    // Set verification mode
    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, nullptr);
    
    return true;
}

auto KiteWSClient::cleanupSSL() -> void {
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
}

auto KiteWSClient::performWebSocketHandshake() -> bool {
    // Build WebSocket handshake request
    char request[1024];
    std::snprintf(request, sizeof(request),
        "GET /?api_key=%s&access_token=%s HTTP/1.1\r\n"
        "Host: ws.kite.trade\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        config_.api_key, config_.access_token);
    
    if (SSL_write(ssl_, request, static_cast<int>(std::strlen(request))) <= 0) {
        LOG_ERROR("Failed to send handshake request");
        return false;
    }
    
    // Read response with retry logic
    char response[2048];
    int total_read = 0;
    unsigned int max_retries = 10;
    
    while (total_read < static_cast<int>(sizeof(response) - 1) && max_retries != 0) {
        int bytes_read = SSL_read(ssl_, response + total_read, 
                                 static_cast<int>(sizeof(response) - 1 - static_cast<size_t>(total_read)));
        
        if (bytes_read > 0) {
            total_read += bytes_read;
            response[total_read] = '\0';
            
            // Check if we've received a complete HTTP response
            if (std::strstr(response, "\r\n\r\n")) {
                break;  // Found end of HTTP headers
            }
        } else {
            int ssl_err = SSL_get_error(ssl_, bytes_read);
            
            if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
                // Non-blocking socket needs to retry
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(socket_fd_, &fds);
                
                struct timeval timeout;
                timeout.tv_sec = 5;
                timeout.tv_usec = 0;
                
                int select_ret = 0;
                int nfds = static_cast<int>(socket_fd_) + 1;
                if (ssl_err == SSL_ERROR_WANT_READ) {
                    select_ret = select(nfds, &fds, nullptr, nullptr, &timeout);
                } else {
                    select_ret = select(nfds, nullptr, &fds, nullptr, &timeout);
                }
                
                if (select_ret > 0) {
                    // Socket is ready, retry SSL_read
                    max_retries--;
                    continue;
                } else if (select_ret == 0) {
                    LOG_ERROR("WebSocket handshake read timeout");
                    return false;
                } else {
                    LOG_ERROR("select() failed during handshake read: %s", strerror(errno));
                    return false;
                }
            } else {
                LOG_ERROR("Failed to read handshake response - SSL error: %d", ssl_err);
                return false;
            }
        }
        
        max_retries--;
    }
    
    if (total_read == 0) {
        LOG_ERROR("No data received during WebSocket handshake");
        return false;
    }
    
    response[total_read] = '\0';
    
    // Check for upgrade success
    if (!std::strstr(response, "101 Switching Protocols")) {
        LOG_ERROR("WebSocket handshake failed. Response: %.500s", response);
        
        // Parse error message if present
        char* error_start = std::strstr(response, "HTTP/1.1 ");
        if (error_start) {
            char* error_end = std::strchr(error_start, '\r');
            if (error_end) {
                *error_end = '\0';
                LOG_ERROR("Server response: %s", error_start);
            }
        }
        return false;
    }
    
    LOG_INFO("WebSocket handshake successful");
    return true;
}

auto KiteWSClient::buildSubscribeMessage(const uint32_t* tokens, size_t count, KiteMode /* mode */, uint8_t* buffer) -> size_t {
    // Kite WebSocket API uses JSON for subscribe/unsubscribe
    // Mode is set separately after subscription, not in subscribe message
    char json_buffer[4096];
    int written = 0;
    
    // Build subscribe JSON message
    written = std::snprintf(json_buffer, sizeof(json_buffer), "{\"a\":\"subscribe\",\"v\":[");
    
    // Add token list
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) {
            written += std::snprintf(json_buffer + written, sizeof(json_buffer) - static_cast<size_t>(written), ",%u", tokens[i]);
        } else {
            written += std::snprintf(json_buffer + written, sizeof(json_buffer) - static_cast<size_t>(written), "%u", tokens[i]);
        }
    }
    
    written += std::snprintf(json_buffer + written, sizeof(json_buffer) - static_cast<size_t>(written), "]}");
    
    // Copy to buffer
    std::memcpy(buffer, json_buffer, static_cast<size_t>(written));
    
    LOG_INFO("Subscribe message: %s", json_buffer);
    
    // Send mode message separately
    // Mode is set separately after subscription
    
    return static_cast<size_t>(written);
}

} // namespace Trading::MarketData::Zerodha