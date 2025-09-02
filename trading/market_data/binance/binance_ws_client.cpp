#include "binance_ws_client.h"

#include <cstring>
#include <cstdio>
#include <chrono>

namespace Trading::MarketData::Binance {

// ============================================================================
// WebSocket Protocol Configuration
// ============================================================================

static struct lws_protocols protocols[] = {
    {
        "binance-ws-protocol",
        BinanceWSClient::wsCallback,
        0,
        65536,  // rx buffer size
    },
    { nullptr, nullptr, 0, 0 }  // terminator
};

// ============================================================================
// Initialization
// ============================================================================

bool BinanceWSClient::init(const Config& config) {
    config_ = config;
    
    // Create WebSocket context
    struct lws_context_creation_info info;
    std::memset(&info, 0, sizeof(info));
    
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.user = this;  // Pass this as user data
    
    ws_context_ = lws_create_context(&info);
    if (!ws_context_) {
        LOG_ERROR("Failed to create WebSocket context");
        return false;
    }
    
    LOG_INFO("BinanceWSClient initialized: url=%s, testnet=%d", 
             config_.use_testnet ? config_.testnet_url : config_.ws_url,
             config_.use_testnet);
    
    return true;
}

// ============================================================================
// Start/Stop
// ============================================================================

bool BinanceWSClient::start() {
    if (running_.load(std::memory_order_acquire)) {
        LOG_WARN("BinanceWSClient already running");
        return true;
    }
    
    running_.store(true, std::memory_order_release);
    
    // Start WebSocket thread
    ws_thread_ = std::thread([this]() {
        if (config_.cpu_affinity >= 0) {
            Common::setThreadCore(config_.cpu_affinity);
        }
        pthread_setname_np(pthread_self(), "binance-ws");
        wsThreadFunc();
    });
    
    // Start processor thread
    processor_thread_ = std::thread([this]() {
        if (config_.cpu_affinity >= 0) {
            Common::setThreadCore(config_.cpu_affinity + 1);
        }
        pthread_setname_np(pthread_self(), "binance-proc");
        processorThreadFunc();
    });
    
    LOG_INFO("BinanceWSClient started");
    return true;
}

void BinanceWSClient::stop() {
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }
    
    LOG_INFO("Stopping BinanceWSClient...");
    running_.store(false, std::memory_order_release);
    
    // Close WebSocket connection
    auto* conn = ws_connection_.load(std::memory_order_acquire);
    if (conn) {
        lws_callback_on_writable(conn);
    }
    
    // Wait for threads
    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
    if (processor_thread_.joinable()) {
        processor_thread_.join();
    }
    
    // Destroy context
    if (ws_context_) {
        lws_context_destroy(ws_context_);
        ws_context_ = nullptr;
    }
    
    LOG_INFO("BinanceWSClient stopped: received=%lu, dropped=%lu",
             messages_received_.load(), messages_dropped_.load());
}

// ============================================================================
// WebSocket Thread
// ============================================================================

void BinanceWSClient::wsThreadFunc() {
    LOG_INFO("WebSocket thread started");
    
    // Initialize to past time to trigger immediate connection
    auto last_reconnect_attempt = std::chrono::steady_clock::now() - 
                                  std::chrono::milliseconds(config_.reconnect_interval_ms * 2);
    
    while (running_.load(std::memory_order_acquire)) {
        // Check if we need to reconnect
        if (!connected_.load(std::memory_order_acquire) && 
            !ws_connection_.load(std::memory_order_acquire)) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_reconnect_attempt).count();
            
            if (static_cast<uint32_t>(elapsed_ms) >= config_.reconnect_interval_ms) {
                LOG_INFO("Attempting to reconnect to WebSocket...");
                
                // Connect to WebSocket
                struct lws_client_connect_info ccinfo;
                std::memset(&ccinfo, 0, sizeof(ccinfo));
                
                const char* url = config_.use_testnet ? config_.testnet_url : config_.ws_url;
                
                // Parse URL
                char address[256];
                char path[256];
                int port = 443;
                
                if (sscanf(url, "wss://%255[^:]:%d%255s", address, &port, path) < 2) {
                    sscanf(url, "wss://%255[^/]%255s", address, path);
                }
                
                ccinfo.context = ws_context_;
                ccinfo.address = address;
                ccinfo.port = port;
                ccinfo.path = path;
                ccinfo.host = address;
                ccinfo.origin = address;
                ccinfo.protocol = "binance-ws-protocol";
                ccinfo.ssl_connection = LCCSCF_USE_SSL;
                ccinfo.userdata = this;
                
                auto* new_conn = lws_client_connect_via_info(&ccinfo);
                if (!new_conn) {
                    LOG_ERROR("Failed to connect to WebSocket");
                } else {
                    ws_connection_.store(new_conn, std::memory_order_release);
                    LOG_INFO("WebSocket connection initiated");
                }
                
                last_reconnect_attempt = now;
            }
        }
        
        // Send ping if needed (every 30 seconds)
        auto* conn = ws_connection_.load(std::memory_order_acquire);
        if (conn && connected_.load(std::memory_order_acquire)) {
            uint64_t now_ns = Common::getNanosSinceEpoch();
            uint64_t last_ping = last_ping_time_.load(std::memory_order_relaxed);
            if (now_ns - last_ping > 30000000000ULL) {  // 30 seconds
                unsigned char ping_data[LWS_PRE + 8] = {0};
                lws_write(conn, &ping_data[LWS_PRE], 0, LWS_WRITE_PING);
                last_ping_time_.store(now_ns, std::memory_order_relaxed);
            }
        }
        
        // Service the WebSocket
        if (ws_context_) {
            lws_service(ws_context_, 50);  // 50ms timeout
        }
    }
    
    LOG_INFO("WebSocket thread stopped");
}

// ============================================================================
// Processor Thread
// ============================================================================

void BinanceWSClient::processorThreadFunc() {
    LOG_INFO("Processor thread started");
    
    while (running_.load(std::memory_order_acquire)) {
        bool processed = false;
        
        // Process tick queue
        const auto* tick_ptr = tick_queue_.getNextToRead();
        if (tick_ptr) {
            BinanceTickData* tick = *const_cast<BinanceTickData**>(tick_ptr);
            if (tick && tick_callback_) {
                tick_callback_(tick);
            }
            if (tick) {
                tick_pool_.deallocate(tick);
            }
            tick_queue_.updateReadIndex();
            processed = true;
        }
        
        // Process depth queue
        const auto* depth_ptr = depth_queue_.getNextToRead();
        if (depth_ptr) {
            BinanceDepthUpdate* depth = *const_cast<BinanceDepthUpdate**>(depth_ptr);
            if (depth && depth_callback_) {
                depth_callback_(depth);
            }
            if (depth) {
                depth_pool_.deallocate(depth);
            }
            depth_queue_.updateReadIndex();
            processed = true;
        }
        
        // Avoid spinning - brief pause if queues empty
        if (!processed) {
            std::this_thread::yield();
        }
    }
    
    LOG_INFO("Processor thread stopped");
}

// ============================================================================
// WebSocket Callback
// ============================================================================

int BinanceWSClient::wsCallback(struct lws* wsi, enum lws_callback_reasons reason,
                                void* /* user */, void* in, size_t len) {
    auto* client = static_cast<BinanceWSClient*>(lws_context_user(lws_get_context(wsi)));
    if (!client) return 0;
    
    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        LOG_INFO("WebSocket connected");
        client->connected_.store(true, std::memory_order_release);
        client->last_ping_time_.store(Common::getNanosSinceEpoch(), std::memory_order_relaxed);
        
        // Subscribe to streams after connection
        for (size_t i = 0; i < client->symbol_count_; ++i) {
            char stream[128];
            snprintf(stream, sizeof(stream), "%s@trade", client->symbols_[i]);
            client->sendSubscribeMessage(stream);
        }
        break;
        
    case LWS_CALLBACK_CLIENT_RECEIVE:
        if (in && len > 0) {
            // Get timestamp immediately for lowest latency
            uint64_t local_ts = Common::getNanosSinceEpoch();
            
            // Rate limiting check
            uint64_t current_sec = local_ts / 1000000000ULL;
            uint64_t last_sec = client->current_second_.load(std::memory_order_relaxed);
            if (current_sec != last_sec) {
                client->messages_this_second_.store(0, std::memory_order_relaxed);
                client->current_second_.store(current_sec, std::memory_order_relaxed);
            }
            
            uint32_t msg_count = client->messages_this_second_.fetch_add(1, std::memory_order_relaxed);
            if (msg_count >= BinanceWSClient::MAX_MESSAGES_PER_SECOND) {
                client->messages_rate_limited_.fetch_add(1, std::memory_order_relaxed);
                break;  // Drop message due to rate limit
            }
            
            // Bounds check and copy to buffer if it fits
            if (len > RX_BUFFER_SIZE || client->rx_buffer_pos_ + len >= RX_BUFFER_SIZE) {
                // Buffer would overflow - reset
                LOG_WARN("RX buffer overflow, resetting (pos=%zu, len=%zu)", 
                         client->rx_buffer_pos_, len);
                client->rx_buffer_pos_ = 0;
                break;
            }
            
            if (client->rx_buffer_pos_ + len < RX_BUFFER_SIZE) {
                std::memcpy(client->rx_buffer_ + client->rx_buffer_pos_, in, len);
                client->rx_buffer_pos_ += len;
                
                // Check if message is complete (look for closing brace)
                if (client->rx_buffer_[client->rx_buffer_pos_ - 1] == '}') {
                    // Parse message
                    client->rx_buffer_[client->rx_buffer_pos_] = '\0';
                    
                    // Determine message type by looking for key fields
                    if (strstr(client->rx_buffer_, "\"e\":\"trade\"")) {
                        // Trade tick message
                        auto* tick = static_cast<BinanceTickData*>(client->tick_pool_.allocate());
                        if (tick && client->parseTickMessage(client->rx_buffer_, 
                                                            client->rx_buffer_pos_, tick)) {
                            tick->local_timestamp_ns = local_ts;
                            
                            // Try to enqueue using SPSC API
                            auto* slot = client->tick_queue_.getNextToWriteTo();
                            if (slot) {
                                *slot = tick;
                                client->tick_queue_.updateWriteIndex();
                                client->messages_received_.fetch_add(1, std::memory_order_relaxed);
                            } else {
                                client->tick_pool_.deallocate(tick);
                                client->messages_dropped_.fetch_add(1, std::memory_order_relaxed);
                            }
                        } else if (tick) {
                            client->tick_pool_.deallocate(tick);
                        }
                    } else if (strstr(client->rx_buffer_, "\"e\":\"depthUpdate\"")) {
                        // Depth update message
                        auto* depth = static_cast<BinanceDepthUpdate*>(client->depth_pool_.allocate());
                        if (depth && client->parseDepthMessage(client->rx_buffer_,
                                                              client->rx_buffer_pos_, depth)) {
                            depth->local_timestamp_ns = local_ts;
                            
                            // Try to enqueue using SPSC API
                            auto* slot = client->depth_queue_.getNextToWriteTo();
                            if (slot) {
                                *slot = depth;
                                client->depth_queue_.updateWriteIndex();
                                client->messages_received_.fetch_add(1, std::memory_order_relaxed);
                            } else {
                                client->depth_pool_.deallocate(depth);
                                client->messages_dropped_.fetch_add(1, std::memory_order_relaxed);
                            }
                        } else if (depth) {
                            client->depth_pool_.deallocate(depth);
                        }
                    }
                    
                    // Reset buffer
                    client->rx_buffer_pos_ = 0;
                }
            }
        }
        break;
        
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        LOG_ERROR("WebSocket connection error - will reconnect");
        client->connected_.store(false, std::memory_order_release);
        client->ws_connection_.store(nullptr, std::memory_order_release);
        client->reconnect_count_.fetch_add(1, std::memory_order_relaxed);
        return -1;
        
    case LWS_CALLBACK_CLIENT_CLOSED:
        LOG_INFO("WebSocket disconnected - will reconnect");
        client->connected_.store(false, std::memory_order_release);
        client->ws_connection_.store(nullptr, std::memory_order_release);
        client->reconnect_count_.fetch_add(1, std::memory_order_relaxed);
        return -1;
        
    default:
        break;
    }
    
    return 0;
}

// ============================================================================
// Subscription Management
// ============================================================================

bool BinanceWSClient::subscribeTicker(const char* symbol) {
    if (symbol_count_ >= MAX_SYMBOLS) {
        LOG_ERROR("Max symbols reached: %zu", MAX_SYMBOLS);
        return false;
    }
    
    symbols_[symbol_count_++] = symbol;
    
    if (connected_.load(std::memory_order_acquire)) {
        char stream[128];
        snprintf(stream, sizeof(stream), "%s@trade", symbol);
        return sendSubscribeMessage(stream);
    }
    
    return true;  // Will subscribe on connection
}

bool BinanceWSClient::subscribeDepth(const char* symbol, int levels) {
    if (connected_.load(std::memory_order_acquire)) {
        char stream[128];
        snprintf(stream, sizeof(stream), "%s@depth%d@100ms", symbol, levels);
        return sendSubscribeMessage(stream);
    }
    return true;
}

bool BinanceWSClient::sendSubscribeMessage(const char* stream) {
    char subscribe_msg[512];
    int len = snprintf(subscribe_msg, sizeof(subscribe_msg),
                      "{\"method\":\"SUBSCRIBE\",\"params\":[\"%s\"],\"id\":1}",
                      stream);
    
    if (ws_connection_) {
        unsigned char buf[LWS_PRE + 512];
        std::memcpy(&buf[LWS_PRE], subscribe_msg, static_cast<size_t>(len));
        
        int written = lws_write(ws_connection_, &buf[LWS_PRE], 
                               static_cast<size_t>(len), LWS_WRITE_TEXT);
        if (written < len) {
            LOG_ERROR("Failed to send subscribe message");
            return false;
        }
        
        LOG_INFO("Subscribed to stream: %s", stream);
        return true;
    }
    
    return false;
}

// ============================================================================
// JSON Parsing - Zero Allocation
// ============================================================================

bool BinanceWSClient::parseTickMessage(const char* json, size_t /* len */, BinanceTickData* tick) {
    tick->reset();
    
    // Example message:
    // {"e":"trade","E":1234567890123,"s":"BTCUSDT","t":12345,"p":"50000.12",
    //  "q":"0.001","T":1234567890123,"m":true,"M":true}
    
    char value[64];
    
    // Extract symbol
    if (extractJsonValue(json, "\"s\"", value, sizeof(value))) {
        strncpy(tick->symbol, value, sizeof(tick->symbol) - 1);
    }
    
    // Extract price
    if (extractJsonValue(json, "\"p\"", value, sizeof(value))) {
        double price;
        if (parseDouble(value, price)) {
            tick->price = static_cast<Price>(price * PRICE_MULTIPLIER);
        }
    }
    
    // Extract quantity
    if (extractJsonValue(json, "\"q\"", value, sizeof(value))) {
        double qty;
        if (parseDouble(value, qty)) {
            tick->qty = static_cast<Qty>(qty * QTY_MULTIPLIER);
        }
    }
    
    // Extract timestamp
    if (extractJsonValue(json, "\"T\"", value, sizeof(value))) {
        uint64_t ts;
        if (parseLong(value, ts)) {
            tick->exchange_timestamp_ns = ts * 1000000;  // ms to ns
        }
    }
    
    // Extract buyer maker flag
    if (extractJsonValue(json, "\"m\"", value, sizeof(value))) {
        tick->is_buyer_maker = (value[0] == 't');
    }
    
    return tick->price != Price_INVALID && tick->qty != Qty_INVALID;
}

bool BinanceWSClient::parseDepthMessage(const char* json, size_t len, BinanceDepthUpdate* depth) {
    if (!json || !depth || len == 0 || len > JSON_BUFFER_SIZE) {
        return false;
    }
    
    depth->reset();
    
    // Parse depth update
    // This is a simplified version - full implementation would parse all levels
    
    char value[64];
    
    // Extract symbol
    if (extractJsonValue(json, "\"s\"", value, sizeof(value))) {
        // Map symbol to ticker_id (would need symbol mapping)
        depth->ticker_id = 0;  // Placeholder
    }
    
    // Extract update ID
    if (extractJsonValue(json, "\"u\"", value, sizeof(value))) {
        parseLong(value, depth->last_update_id);
    }
    
    // Parse bids array - simplified for example
    // Full implementation would parse the complete arrays
    
    return depth->last_update_id != 0;
}

bool BinanceWSClient::extractJsonValue(const char* json, const char* key, 
                                       char* out, size_t out_len) {
    const char* pos = strstr(json, key);
    if (!pos) return false;
    
    pos += strlen(key);
    
    // Skip whitespace and colon
    while (*pos && (*pos == ' ' || *pos == ':')) pos++;
    
    // Check if value is string (quoted) or number
    bool is_string = (*pos == '"');
    if (is_string) pos++;  // Skip opening quote
    
    size_t i = 0;
    while (*pos && i < out_len - 1) {
        if (is_string && *pos == '"') break;  // End of string
        if (!is_string && (*pos == ',' || *pos == '}')) break;  // End of number
        out[i++] = *pos++;
    }
    out[i] = '\0';
    
    return i > 0;
}

} // namespace Trading::MarketData::Binance