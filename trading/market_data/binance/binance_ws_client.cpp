#include "binance_ws_client.h"
#include "../order_book.h"

#include <cstring>
#include <strings.h>  // For strcasecmp
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
            
            // Update order book if manager is set
            if (depth && order_book_manager_) {
                // Cast to OrderBookManager and update
                auto* book_mgr = static_cast<Trading::MarketData::OrderBookManager<1000>*>(order_book_manager_);
                
                // Register instrument if not already done
                auto* order_book = book_mgr->getOrderBook(depth->ticker_id);
                if (!order_book) {
                    order_book = book_mgr->registerInstrument(depth->ticker_id, 
                                                              static_cast<Common::TickerId>(depth->ticker_id));
                }
                
                if (order_book) {
                    // Clear and update bid levels
                    order_book->clearBids();
                    for (uint8_t i = 0; i < depth->bid_count && i < 20; ++i) {
                        order_book->updateBid(depth->bid_prices[i], depth->bid_qtys[i], 1, i);
                    }
                    
                    // Clear and update ask levels
                    order_book->clearAsks();
                    for (uint8_t i = 0; i < depth->ask_count && i < 20; ++i) {
                        order_book->updateAsk(depth->ask_prices[i], depth->ask_qtys[i], 1, i);
                    }
                    
                    // Update timestamp
                    order_book->updateTimestamp(depth->local_timestamp_ns);
                }
            }
            
            // Also call callback if set
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
            const auto& sym = client->symbols_[i];
            if (sym.has_ticker) {
                char stream[128];
                snprintf(stream, sizeof(stream), "%s@trade", sym.symbol);
                client->sendSubscribeMessage(stream);
            }
            if (sym.has_depth) {
                char stream[128];
                snprintf(stream, sizeof(stream), "%s@depth10@100ms", sym.symbol);
                client->sendSubscribeMessage(stream);
            }
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
                            
                            // Log market data for display
                            static uint64_t tick_counter = 0;
                            if (++tick_counter % 100 == 1) {  // Log every 100th tick
                                LOG_INFO("[BINANCE TICK] %s: Price=%.8f, Qty=%.8f, Side=%s",
                                        tick->symbol,
                                        static_cast<double>(tick->price) / 1e8,  // Convert from satoshi
                                        static_cast<double>(tick->qty) / 1e8,
                                        tick->is_buyer_maker ? "SELL" : "BUY");
                            }
                            
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
                    } else if (strstr(client->rx_buffer_, "\"lastUpdateId\"") && 
                              strstr(client->rx_buffer_, "\"bids\"")) {
                        // Partial book snapshot (from depth5/10/20 streams)
                        auto* depth = static_cast<BinanceDepthUpdate*>(client->depth_pool_.allocate());
                        if (depth && client->parsePartialBookMessage(client->rx_buffer_,
                                                              client->rx_buffer_pos_, depth)) {
                            depth->local_timestamp_ns = local_ts;
                            
                            // Log depth data for display
                            static uint64_t depth_counter = 0;
                            if (++depth_counter % 100 == 1) {  // Log every 100th depth update
                                LOG_INFO("[BINANCE PARTIAL BOOK] UpdateID=%lu, Bids=%d, Asks=%d, BestBid=%.8f@%.8f, BestAsk=%.8f@%.8f",
                                        depth->last_update_id,
                                        depth->bid_count,
                                        depth->ask_count,
                                        depth->bid_count > 0 ? static_cast<double>(depth->bid_prices[0]) / 1e8 : 0.0,
                                        depth->bid_count > 0 ? static_cast<double>(depth->bid_qtys[0]) / 1e8 : 0.0,
                                        depth->ask_count > 0 ? static_cast<double>(depth->ask_prices[0]) / 1e8 : 0.0,
                                        depth->ask_count > 0 ? static_cast<double>(depth->ask_qtys[0]) / 1e8 : 0.0);
                            }
                            
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
                    } else if (strstr(client->rx_buffer_, "\"e\":\"depthUpdate\"")) {
                        // Incremental depth update (from @depth stream)
                        auto* depth = static_cast<BinanceDepthUpdate*>(client->depth_pool_.allocate());
                        if (depth && client->parseDepthMessage(client->rx_buffer_,
                                                              client->rx_buffer_pos_, depth)) {
                            depth->local_timestamp_ns = local_ts;
                            
                            // Log depth data for display
                            static uint64_t depth_counter = 0;
                            if (++depth_counter % 100 == 1) {  // Log every 100th depth update
                                LOG_INFO("[BINANCE DEPTH] UpdateID=%lu, Bids=%d, Asks=%d, BestBid=%.8f@%.8f, BestAsk=%.8f@%.8f",
                                        depth->last_update_id,
                                        depth->bid_count,
                                        depth->ask_count,
                                        depth->bid_count > 0 ? static_cast<double>(depth->bid_prices[0]) / 1e8 : 0.0,
                                        depth->bid_count > 0 ? static_cast<double>(depth->bid_qtys[0]) / 1e8 : 0.0,
                                        depth->ask_count > 0 ? static_cast<double>(depth->ask_prices[0]) / 1e8 : 0.0,
                                        depth->ask_count > 0 ? static_cast<double>(depth->ask_qtys[0]) / 1e8 : 0.0);
                            }
                            
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
// Symbol Management
// ============================================================================

void BinanceWSClient::registerSymbol(const char* symbol, uint32_t ticker_id) {
    if (symbol_map_count_ >= MAX_SYMBOLS) {
        LOG_WARN("Symbol map full, cannot register %s", symbol);
        return;
    }
    
    // Check if already registered
    for (size_t i = 0; i < symbol_map_count_; ++i) {
        if (strcmp(symbol_map_[i].symbol, symbol) == 0) {
            symbol_map_[i].ticker_id = ticker_id;  // Update existing
            return;
        }
    }
    
    // Add new mapping
    strncpy(symbol_map_[symbol_map_count_].symbol, symbol, 
            sizeof(symbol_map_[symbol_map_count_].symbol) - 1);
    symbol_map_[symbol_map_count_].symbol[sizeof(symbol_map_[0].symbol) - 1] = '\0';
    symbol_map_[symbol_map_count_].ticker_id = ticker_id;
    symbol_map_count_++;
    
    LOG_INFO("Registered symbol %s -> ticker_id %u", symbol, ticker_id);
}

uint32_t BinanceWSClient::getTickerId(const char* symbol) const {
    for (size_t i = 0; i < symbol_map_count_; ++i) {
        if (strcasecmp(symbol_map_[i].symbol, symbol) == 0) {
            return symbol_map_[i].ticker_id;
        }
    }
    return 0;  // Not found
}

// ============================================================================
// Subscription Management
// ============================================================================

bool BinanceWSClient::subscribeTicker(const char* symbol, uint32_t ticker_id) {
    registerSymbol(symbol, ticker_id);
    
    if (symbol_count_ >= MAX_SYMBOLS) {
        LOG_ERROR("Max symbols reached: %zu", MAX_SYMBOLS);
        return false;
    }
    
    // Store symbol info
    auto& sym_info = symbols_[symbol_count_++];
    strncpy(sym_info.symbol, symbol, sizeof(sym_info.symbol) - 1);
    sym_info.symbol[sizeof(sym_info.symbol) - 1] = '\0';
    sym_info.ticker_id = ticker_id;
    sym_info.has_ticker = true;
    sym_info.has_depth = false;
    
    if (connected_.load(std::memory_order_acquire)) {
        char stream[128];
        snprintf(stream, sizeof(stream), "%s@trade", symbol);
        return sendSubscribeMessage(stream);
    }
    
    return true;  // Will subscribe on connection
}

bool BinanceWSClient::subscribeDepth(const char* symbol, uint32_t ticker_id, int levels) {
    registerSymbol(symbol, ticker_id);
    
    // Find or add symbol info
    bool found = false;
    for (size_t i = 0; i < symbol_count_; ++i) {
        if (strcmp(symbols_[i].symbol, symbol) == 0) {
            symbols_[i].has_depth = true;
            found = true;
            break;
        }
    }
    
    if (!found && symbol_count_ < MAX_SYMBOLS) {
        auto& sym_info = symbols_[symbol_count_++];
        strncpy(sym_info.symbol, symbol, sizeof(sym_info.symbol) - 1);
        sym_info.symbol[sizeof(sym_info.symbol) - 1] = '\0';
        sym_info.ticker_id = ticker_id;
        sym_info.has_ticker = false;
        sym_info.has_depth = true;
    }
    
    if (connected_.load(std::memory_order_acquire)) {
        char stream[128];
        snprintf(stream, sizeof(stream), "%s@depth%d@100ms", symbol, levels);
        return sendSubscribeMessage(stream);
    }
    return true;
}

bool BinanceWSClient::subscribeSymbol(const char* symbol, uint32_t ticker_id, 
                                      bool ticker, bool depth, int depth_levels) {
    registerSymbol(symbol, ticker_id);
    
    // Update or add symbol info
    bool found = false;
    for (size_t i = 0; i < symbol_count_; ++i) {
        if (strcmp(symbols_[i].symbol, symbol) == 0) {
            symbols_[i].has_ticker = symbols_[i].has_ticker || ticker;
            symbols_[i].has_depth = symbols_[i].has_depth || depth;
            found = true;
            break;
        }
    }
    
    if (!found && symbol_count_ < MAX_SYMBOLS) {
        auto& sym_info = symbols_[symbol_count_++];
        strncpy(sym_info.symbol, symbol, sizeof(sym_info.symbol) - 1);
        sym_info.symbol[sizeof(sym_info.symbol) - 1] = '\0';
        sym_info.ticker_id = ticker_id;
        sym_info.has_ticker = ticker;
        sym_info.has_depth = depth;
    }
    
    bool success = true;
    if (connected_.load(std::memory_order_acquire)) {
        if (ticker) {
            char stream[128];
            snprintf(stream, sizeof(stream), "%s@trade", symbol);
            success &= sendSubscribeMessage(stream);
        }
        if (depth) {
            char stream[128];
            snprintf(stream, sizeof(stream), "%s@depth%d@100ms", symbol, depth_levels);
            success &= sendSubscribeMessage(stream);
        }
    }
    
    return success;
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
    
    char value[256];
    
    // Extract symbol and map to ticker_id
    if (extractJsonValue(json, "\"s\"", value, sizeof(value))) {
        // Map known symbols to fixed ticker IDs
        if (strcmp(value, "BTCUSDT") == 0) {
            depth->ticker_id = 1001;
        } else if (strcmp(value, "ETHUSDT") == 0) {
            depth->ticker_id = 1002;
        } else {
            // For other symbols, use hash
            uint32_t hash = 0;
            for (const char* p = value; *p; ++p) {
                hash = hash * 31 + static_cast<uint32_t>(*p);
            }
            depth->ticker_id = 2000 + (hash % 1000);  // Map to range 2000-2999
        }
    }
    
    // Extract update ID (could be "u" for incremental or "lastUpdateId" for partial)
    if (extractJsonValue(json, "\"u\"", value, sizeof(value))) {
        parseLong(value, depth->last_update_id);
    } else if (extractJsonValue(json, "\"lastUpdateId\"", value, sizeof(value))) {
        parseLong(value, depth->last_update_id);
    }
    
    // Parse bids array
    const char* bids_start = strstr(json, "\"b\":[");
    if (bids_start) {
        bids_start += 5;  // Skip "b":[ 
        depth->bid_count = 0;
        
        const char* pos = bids_start;
        while (*pos && depth->bid_count < BinanceDepthUpdate::MAX_DEPTH) {
            // Skip to next [
            while (*pos && *pos != '[') {
                if (*pos == ']') break;  // End of bids array
                pos++;
            }
            if (*pos != '[') break;
            pos++;  // Skip [
            
            // Parse price
            char price_str[64];
            size_t i = 0;
            while (*pos && *pos != '"' && *pos != ',') {
                if (*pos == '"') pos++;  // Skip quotes
                else if (i < sizeof(price_str) - 1) price_str[i++] = *pos++;
                else pos++;
            }
            price_str[i] = '\0';
            
            double price_val = 0.0;
            if (parseDouble(price_str, price_val)) {
                depth->bid_prices[depth->bid_count] = static_cast<Price>(price_val * 100000000);  // Convert to fixed point
            }
            
            // Skip comma and quotes
            while (*pos && (*pos == ',' || *pos == '"')) pos++;
            
            // Parse quantity
            char qty_str[64];
            i = 0;
            while (*pos && *pos != '"' && *pos != ']') {
                if (*pos == '"') pos++;
                else if (i < sizeof(qty_str) - 1) qty_str[i++] = *pos++;
                else pos++;
            }
            qty_str[i] = '\0';
            
            double qty_val = 0.0;
            if (parseDouble(qty_str, qty_val)) {
                depth->bid_qtys[depth->bid_count] = static_cast<Qty>(qty_val * 100000000);
                depth->bid_count++;
            }
            
            // Skip to next level
            while (*pos && *pos != '[' && *pos != ']') pos++;
            if (*pos == ']' && *(pos+1) == ']') break;  // End of bids
        }
    }
    
    // Parse asks array
    const char* asks_start = strstr(json, "\"a\":[");
    if (asks_start) {
        asks_start += 5;  // Skip "a":[
        depth->ask_count = 0;
        
        const char* pos = asks_start;
        while (*pos && depth->ask_count < BinanceDepthUpdate::MAX_DEPTH) {
            // Skip to next [
            while (*pos && *pos != '[') {
                if (*pos == ']') break;  // End of asks array
                pos++;
            }
            if (*pos != '[') break;
            pos++;  // Skip [
            
            // Parse price
            char price_str[64];
            size_t i = 0;
            while (*pos && *pos != '"' && *pos != ',') {
                if (*pos == '"') pos++;
                else if (i < sizeof(price_str) - 1) price_str[i++] = *pos++;
                else pos++;
            }
            price_str[i] = '\0';
            
            double price_val = 0.0;
            if (parseDouble(price_str, price_val)) {
                depth->ask_prices[depth->ask_count] = static_cast<Price>(price_val * 100000000);
            }
            
            // Skip comma and quotes
            while (*pos && (*pos == ',' || *pos == '"')) pos++;
            
            // Parse quantity
            char qty_str[64];
            i = 0;
            while (*pos && *pos != '"' && *pos != ']') {
                if (*pos == '"') pos++;
                else if (i < sizeof(qty_str) - 1) qty_str[i++] = *pos++;
                else pos++;
            }
            qty_str[i] = '\0';
            
            double qty_val = 0.0;
            if (parseDouble(qty_str, qty_val)) {
                depth->ask_qtys[depth->ask_count] = static_cast<Qty>(qty_val * 100000000);
                depth->ask_count++;
            }
            
            // Skip to next level
            while (*pos && *pos != '[' && *pos != ']') pos++;
            if (*pos == ']' && *(pos+1) == ']') break;  // End of asks
        }
    }
    
    return depth->last_update_id != 0 && (depth->bid_count > 0 || depth->ask_count > 0);
}

bool BinanceWSClient::parsePartialBookMessage(const char* json, size_t len, BinanceDepthUpdate* depth) {
    if (!json || !depth || len == 0 || len > JSON_BUFFER_SIZE) {
        return false;
    }
    
    depth->reset();
    
    char value[256];
    
    // Check if this is a combined stream message with stream field
    const char* stream_start = strstr(json, "\"stream\":\"");
    if (stream_start) {
        stream_start += 10;  // Skip "stream":"
        char stream_name[64];
        size_t i = 0;
        while (*stream_start && *stream_start != '"' && i < sizeof(stream_name) - 1) {
            stream_name[i++] = *stream_start++;
        }
        stream_name[i] = '\0';
        
        // Extract symbol from stream name (e.g., "btcusdt@depth10@100ms")
        char symbol[16];
        i = 0;
        const char* p = stream_name;
        while (*p && *p != '@' && i < sizeof(symbol) - 1) {
            symbol[i++] = (*p >= 'a' && *p <= 'z') ? (*p - 32) : *p;  // Convert to uppercase
            p++;
        }
        symbol[i] = '\0';
        
        // Look up ticker_id from symbol
        depth->ticker_id = getTickerId(symbol);
        if (depth->ticker_id == 0) {
            // Unknown symbol, try to detect from price range
            if (strstr(json, "111") != nullptr) {
                depth->ticker_id = 1001;  // BTCUSDT
            } else if (strstr(json, "4.3") != nullptr || strstr(json, "2.3") != nullptr) {
                depth->ticker_id = 1002;  // ETHUSDT
            }
        }
        
        // Find the actual data object
        const char* data_start = strstr(json, "\"data\":{");
        if (data_start) {
            json = data_start + 7;  // Point to actual payload
        }
    } else {
        // Direct stream, use price detection for now
        if (strstr(json, "111") != nullptr) {
            depth->ticker_id = 1001;  // BTCUSDT
        } else if (strstr(json, "4.3") != nullptr || strstr(json, "2.3") != nullptr) {
            depth->ticker_id = 1002;  // ETHUSDT  
        }
    }
    
    // Extract lastUpdateId
    if (extractJsonValue(json, "\"lastUpdateId\"", value, sizeof(value))) {
        parseLong(value, depth->last_update_id);
    }
    
    // Message validated, proceed with parsing
    
    // Parse bids array - format is "bids":[["price","qty"],...]
    const char* bids_start = strstr(json, "\"bids\":[");
    if (bids_start) {
        bids_start += 8;  // Skip "bids":[ 
        if (*bids_start == '[') bids_start++;  // Skip opening [ of first element
        depth->bid_count = 0;
        
        const char* pos = bids_start;
        while (*pos && depth->bid_count < BinanceDepthUpdate::MAX_DEPTH) {
            // Skip whitespace
            while (*pos && (*pos == ' ' || *pos == '\n' || *pos == '\r' || *pos == '\t')) pos++;
            
            // Check for end of array
            if (*pos == ']') break;
            
            // We expect ["price","qty"]
            // Skip opening quote
            if (*pos == '"') pos++;
            
            // Parse price
            char price_str[32];
            size_t i = 0;
            while (*pos && *pos != '"' && i < sizeof(price_str) - 1) {
                price_str[i++] = *pos++;
            }
            price_str[i] = '\0';
            
            if (i == 0) break;  // No more data
            
            double price_val = 0.0;
            if (parseDouble(price_str, price_val)) {
                depth->bid_prices[depth->bid_count] = static_cast<Price>(price_val * 100000000);
            }
            
            // Skip quote, comma, quote: "," -> ,
            if (*pos == '"') pos++;  // closing quote of price
            if (*pos == ',') pos++;   // comma
            if (*pos == '"') pos++;   // opening quote of quantity
            
            // Parse quantity  
            char qty_str[32];
            i = 0;
            while (*pos && *pos != '"' && i < sizeof(qty_str) - 1) {
                qty_str[i++] = *pos++;
            }
            qty_str[i] = '\0';
            
            double qty_val = 0.0;
            if (parseDouble(qty_str, qty_val)) {
                depth->bid_qtys[depth->bid_count] = static_cast<Qty>(qty_val * 100000000);
                depth->bid_count++;
            }
            
            // Skip to next element: "]," or "]]"
            if (*pos == '"') pos++;  // closing quote of quantity
            if (*pos == ']') {
                pos++;  // closing bracket of this element
                if (*pos == ']') break;  // End of entire bids array ]]
                if (*pos == ',') {
                    pos++;  // comma between elements
                    if (*pos == '[') pos++;  // opening bracket of next element
                }
            }
        }
    }
    
    // Parse asks array - same format
    const char* asks_start = strstr(json, "\"asks\":[");
    if (asks_start) {
        asks_start += 8;  // Skip "asks":[
        if (*asks_start == '[') asks_start++;  // Skip opening [ of first element
        depth->ask_count = 0;
        
        // Removed debug logging
        
        const char* pos = asks_start;
        while (*pos && depth->ask_count < BinanceDepthUpdate::MAX_DEPTH) {
            // Skip whitespace
            while (*pos && (*pos == ' ' || *pos == '\n' || *pos == '\r' || *pos == '\t')) pos++;
            
            // Check for end of array
            if (*pos == ']') break;
            
            // We expect ["price","qty"]
            // Skip opening quote
            if (*pos == '"') pos++;
            
            // Parse price
            char price_str[32];
            size_t i = 0;
            while (*pos && *pos != '"' && i < sizeof(price_str) - 1) {
                price_str[i++] = *pos++;
            }
            price_str[i] = '\0';
            
            if (i == 0) break;  // No more data
            
            double price_val = 0.0;
            if (parseDouble(price_str, price_val)) {
                depth->ask_prices[depth->ask_count] = static_cast<Price>(price_val * 100000000);
            }
            
            // Skip quote, comma, quote: "," -> ,
            if (*pos == '"') pos++;  // closing quote of price
            if (*pos == ',') pos++;   // comma
            if (*pos == '"') pos++;   // opening quote of quantity
            
            // Parse quantity  
            char qty_str[32];
            i = 0;
            while (*pos && *pos != '"' && i < sizeof(qty_str) - 1) {
                qty_str[i++] = *pos++;
            }
            qty_str[i] = '\0';
            
            double qty_val = 0.0;
            if (parseDouble(qty_str, qty_val)) {
                depth->ask_qtys[depth->ask_count] = static_cast<Qty>(qty_val * 100000000);
                depth->ask_count++;
            }
            
            // Skip to next element: "]," or "]]"
            if (*pos == '"') pos++;  // closing quote of quantity
            if (*pos == ']') {
                pos++;  // closing bracket of this element
                if (*pos == ']') break;  // End of entire asks array ]]
                if (*pos == ',') {
                    pos++;  // comma between elements
                    if (*pos == '[') pos++;  // opening bracket of next element
                }
            }
        }
    }
    
    return depth->last_update_id != 0 && (depth->bid_count > 0 || depth->ask_count > 0);
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

// ============================================================================
// Health Monitoring
// ============================================================================

BinanceWSClient::HealthStatus BinanceWSClient::getHealthStatus() const {
    HealthStatus status;
    
    status.connected = connected_.load(std::memory_order_acquire);
    status.messages_received = messages_received_.load(std::memory_order_relaxed);
    status.messages_dropped = messages_dropped_.load(std::memory_order_relaxed);
    status.reconnect_count = reconnect_count_.load(std::memory_order_relaxed);
    
    // Calculate rates
    if (status.messages_received > 0) {
        status.drop_rate = static_cast<double>(status.messages_dropped) / 
                           static_cast<double>(status.messages_received + status.messages_dropped);
    } else {
        status.drop_rate = 0.0;
    }
    
    // Get current time for uptime calculation
    uint64_t now_ns = Common::rdtsc();
    status.last_message_time_ns = now_ns;  // Would track actual last message time in production
    status.uptime_seconds = 0;  // Would calculate from start time
    
    // Calculate message rate (would need time window tracking in production)
    status.message_rate_per_sec = 0.0;
    
    return status;
}

} // namespace Trading::MarketData::Binance