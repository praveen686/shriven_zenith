#include "binance_order_gateway.h"
#include "common/time_utils.h"
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>

namespace Trading::Binance {

using namespace Common;

BinanceOrderGateway::BinanceOrderGateway(SPSCLFQueue<OrderRequest*, 65536>* request_queue,
                                         SPSCLFQueue<OrderResponse*, 65536>* response_queue,
                                         BinanceAuth* auth)
    : IOrderGateway(request_queue, response_queue),
      auth_(auth),
      request_pool_(-1),  // Use default NUMA node
      response_pool_(-1) {
    
    // Initialize CURL
    curl_ = curl_easy_init();
    if (!curl_) {
        LOG_ERROR("Failed to initialize CURL");
        return;
    }
    
    // Set default CURL options
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 5L); // 5 second timeout
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
    
    LOG_INFO("BinanceOrderGateway initialized");
}

BinanceOrderGateway::~BinanceOrderGateway() {
    stop();
    
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
    if (headers_) {
        curl_slist_free_all(headers_);
    }
    
    if (ws_socket_ >= 0) {
        close(ws_socket_);
    }
    
    LOG_INFO("BinanceOrderGateway destroyed");
}

void BinanceOrderGateway::start() {
    if (running_.exchange(true)) {
        LOG_WARN("BinanceOrderGateway already running");
        return;
    }
    
    // Start order processor thread
    order_processor_thread_ = std::thread([this] {
        Common::setThreadCore(5); // Core 5 for Binance orders
        runOrderProcessor();
    });
    
    // Start WebSocket thread for user data stream
    websocket_thread_ = std::thread([this] {
        Common::setThreadCore(5); // Same core, different priority
        runWebSocketHandler();
    });
    
    LOG_INFO("BinanceOrderGateway started");
}

void BinanceOrderGateway::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    if (order_processor_thread_.joinable()) {
        order_processor_thread_.join();
    }
    if (websocket_thread_.joinable()) {
        websocket_thread_.join();
    }
    
    LOG_INFO("BinanceOrderGateway stopped - Orders: sent=%lu, filled=%lu, rejected=%lu",
             orders_sent_.load(), orders_filled_.load(), orders_rejected_.load());
}

bool BinanceOrderGateway::connect() {
    // Verify authentication
    if (!auth_ || !auth_->hasValidCredentials()) {
        LOG_ERROR("Invalid Binance credentials");
        return false;
    }
    
    // Setup headers
    char api_key_header[256];
    snprintf(api_key_header, sizeof(api_key_header), "X-MBX-APIKEY: %s", auth_->getApiKey());
    headers_ = curl_slist_append(headers_, api_key_header);
    headers_ = curl_slist_append(headers_, "Content-Type: application/x-www-form-urlencoded");
    
    // Start user data stream for WebSocket
    if (!startUserDataStream(listen_key_)) {
        LOG_ERROR("Failed to start user data stream");
        return false;
    }
    
    // Connect WebSocket
    if (!connectWebSocket()) {
        LOG_ERROR("Failed to connect WebSocket");
        return false;
    }
    
    LOG_INFO("Connected to Binance API");
    return true;
}

void BinanceOrderGateway::disconnect() {
    LOG_INFO("Disconnecting from Binance API");
    
    if (ws_socket_ >= 0) {
        close(ws_socket_);
        ws_socket_ = -1;
    }
}

bool BinanceOrderGateway::sendOrder(const OrderRequest& request) {
    // This is called by external threads, so we just enqueue
    auto* req_copy = request_pool_.allocate();
    if (!req_copy) {
        LOG_ERROR("Order request pool exhausted");
        return false;
    }
    
    memcpy(req_copy, &request, sizeof(OrderRequest));
    
    if (!order_requests_queue_->getNextToWriteTo()) {
        LOG_ERROR("Order request queue full");
        request_pool_.deallocate(req_copy);
        return false;
    }
    
    *order_requests_queue_->getNextToWriteTo() = req_copy;
    order_requests_queue_->updateWriteIndex();
    
    return true;
}

bool BinanceOrderGateway::cancelOrder(OrderId order_id) {
    // Find the order
    auto* order = findOrderByClientId(order_id);
    if (!order || !order->active.load()) {
        LOG_WARN("Order %lu not found for cancel", order_id);
        return false;
    }
    
    // Call Binance API to cancel
    return cancelOrderApi(order->symbol, order->binance_order_id);
}

bool BinanceOrderGateway::modifyOrder(OrderId order_id, Price new_price, Qty new_qty) {
    // Binance doesn't support modify - must cancel and replace
    // First cancel the existing order
    if (!cancelOrder(order_id)) {
        return false;
    }
    
    // Find the original order
    auto* order = findOrderByClientId(order_id);
    if (!order) {
        return false;
    }
    
    // Create new order with updated price/qty
    OrderRequest new_request;
    new_request.order_id = order_id + 1000000; // New order ID
    new_request.ticker_id = order->ticker_id;
    new_request.side = order->side;
    new_request.type = OrderType::LIMIT;
    new_request.price = new_price;
    new_request.qty = new_qty;
    new_request.timestamp = getNanosSinceEpoch();
    
    return sendOrder(new_request);
}

void BinanceOrderGateway::runOrderProcessor() noexcept {
    LOG_INFO("Order processor thread started");
    
    while (running_.load()) {
        // Check rate limits
        resetWeightIfNeeded();
        
        // Process order requests from queue
        auto* request_ptr = order_requests_queue_->getNextToRead();
        if (request_ptr && *request_ptr) {
            const auto* request = *request_ptr;
            
            // Check rate limit
            if (!checkAndUpdateWeight(10)) { // Order placement weight = 10
                LOG_WARN("Rate limit reached, waiting...");
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            
            // Convert to Binance format
            BinanceOrderRequest binance_req;
            convertToBinanceRequest(*request, binance_req);
            
            // Place the order
            OrderInfo order_info;
            if (placeOrder(binance_req, order_info)) {
                // Track the order
                const size_t idx = next_order_idx_.fetch_add(1) % MAX_ORDERS;
                auto& order = orders_[idx];
                order = order_info;
                order.client_order_id = request->order_id;
                order.ticker_id = request->ticker_id;
                order.active.store(true);
                
                orders_sent_.fetch_add(1, std::memory_order_relaxed);
                
                // Send acknowledgment
                auto* response = response_pool_.allocate();
                if (response) {
                    response->order_id = request->order_id;
                    response->exchange_order_id = order_info.binance_order_id;
                    response->ticker_id = request->ticker_id;
                    response->client_id = request->client_id;
                    response->side = request->side;
                    response->type = MessageType::ORDER_ACK;
                    response->timestamp = getNanosSinceEpoch();
                    
                    publishResponse(response);
                }
                
                LOG_INFO("Order placed: client_id=%lu, binance_id=%lu, symbol=%s", 
                        request->order_id, order_info.binance_order_id, order_info.symbol);
            } else {
                // Send rejection
                auto* response = response_pool_.allocate();
                if (response) {
                    response->order_id = request->order_id;
                    response->ticker_id = request->ticker_id;
                    response->client_id = request->client_id;
                    response->side = request->side;
                    response->type = MessageType::ORDER_REJECT;
                    response->timestamp = getNanosSinceEpoch();
                    
                    publishResponse(response);
                }
                
                orders_rejected_.fetch_add(1, std::memory_order_relaxed);
                LOG_ERROR("Failed to place order: client_id=%lu", request->order_id);
            }
            
            order_requests_queue_->updateReadIndex();
        }
        
        // Small sleep to avoid busy spinning
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    LOG_INFO("Order processor thread stopped");
}

void BinanceOrderGateway::runWebSocketHandler() noexcept {
    LOG_INFO("WebSocket handler thread started");
    
    while (running_.load()) {
        // Keep alive the user data stream every 30 minutes
        static uint64_t last_keepalive = getNanosSinceEpoch();
        const uint64_t now = getNanosSinceEpoch();
        if (now - last_keepalive > 30ULL * 60 * 1000000000ULL) { // 30 minutes
            keepAliveUserDataStream(listen_key_);
            last_keepalive = now;
        }
        
        // Read WebSocket messages (simplified - in production use proper WebSocket library)
        char buffer[4096];
        int bytes_read = recv(ws_socket_, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            processWebSocketMessage(buffer);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    LOG_INFO("WebSocket handler thread stopped");
}

bool BinanceOrderGateway::placeOrder(const BinanceOrderRequest& req, OrderInfo& order_out) {
    // Build query string
    char payload[1024];
    int offset = snprintf(payload, sizeof(payload),
                         "symbol=%s&side=%s&type=%s&quantity=%s",
                         req.symbol, req.side, req.type, req.quantity);
    
    // Add price for LIMIT orders
    if (strcmp(req.type, "LIMIT") == 0) {
        offset += snprintf(payload + offset, sizeof(payload) - offset,
                          "&price=%s&timeInForce=%s", req.price, req.timeInForce);
    }
    
    // Add stop price for stop orders
    if (req.stopPrice[0] != '\0') {
        offset += snprintf(payload + offset, sizeof(payload) - offset,
                          "&stopPrice=%s", req.stopPrice);
    }
    
    // Add client order ID
    offset += snprintf(payload + offset, sizeof(payload) - offset,
                      "&newClientOrderId=%lu", req.newClientOrderId);
    
    // Add timestamp and signature
    const uint64_t timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    
    offset += snprintf(payload + offset, sizeof(payload) - offset,
                      "&recvWindow=%lu&timestamp=%lu", req.recvWindow, timestamp);
    
    // Generate signature
    char signature[128];
    auth_->signRequest(payload, signature, sizeof(signature));
    offset += snprintf(payload + offset, sizeof(payload) - offset,
                      "&signature=%s", signature);
    
    // Send request
    char response[4096];
    if (!sendHttpRequest("POST", "/api/v3/order", payload, response, sizeof(response))) {
        return false;
    }
    
    // Parse response
    rapidjson::Document doc;
    doc.Parse(response);
    
    if (doc.HasMember("orderId")) {
        parseOrderResponse(doc, order_out);
        return true;
    }
    
    LOG_ERROR("Order placement failed: %s", response);
    return false;
}

bool BinanceOrderGateway::cancelOrderApi(const char* symbol, OrderId order_id) {
    // Build query string
    char payload[512];
    int offset = snprintf(payload, sizeof(payload),
                         "symbol=%s&orderId=%lu", symbol, order_id);
    
    // Add timestamp and signature
    const uint64_t timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    
    offset += snprintf(payload + offset, sizeof(payload) - offset,
                      "&timestamp=%lu", timestamp);
    
    // Generate signature
    char signature[128];
    auth_->signRequest(payload, signature, sizeof(signature));
    offset += snprintf(payload + offset, sizeof(payload) - offset,
                      "&signature=%s", signature);
    
    // Send request
    char response[4096];
    return sendHttpRequest("DELETE", "/api/v3/order", payload, response, sizeof(response));
}

bool BinanceOrderGateway::startUserDataStream(char* listen_key_out) {
    char response[512];
    if (!sendHttpRequest("POST", "/api/v3/userDataStream", nullptr, response, sizeof(response))) {
        return false;
    }
    
    rapidjson::Document doc;
    doc.Parse(response);
    
    if (doc.HasMember("listenKey")) {
        strncpy(listen_key_out, doc["listenKey"].GetString(), 127);
        return true;
    }
    
    return false;
}

bool BinanceOrderGateway::keepAliveUserDataStream(const char* listen_key) {
    char payload[256];
    snprintf(payload, sizeof(payload), "listenKey=%s", listen_key);
    
    char response[256];
    return sendHttpRequest("PUT", "/api/v3/userDataStream", payload, response, sizeof(response));
}

bool BinanceOrderGateway::connectWebSocket() {
    // Simplified WebSocket connection (in production use libwebsockets)
    // This is a placeholder - actual implementation would use proper WebSocket library
    
    char url[512];
    snprintf(url, sizeof(url), "%s/%s", ws_url_, listen_key_);
    
    LOG_INFO("Connecting to WebSocket: %s", url);
    
    // TODO: Implement proper WebSocket connection
    ws_socket_ = -1; // Placeholder
    
    return true;
}

void BinanceOrderGateway::processWebSocketMessage(const char* message) {
    rapidjson::Document doc;
    doc.Parse(message);
    
    if (!doc.HasMember("e")) return;
    
    const char* event_type = doc["e"].GetString();
    
    if (strcmp(event_type, "executionReport") == 0) {
        processExecutionReport(doc);
    }
}

void BinanceOrderGateway::processExecutionReport(const rapidjson::Document& doc) {
    // Extract order info
    const OrderId client_order_id = doc["C"].GetUint64(); // clientOrderId
    const OrderId binance_order_id = doc["i"].GetUint64(); // orderId
    const char* status = doc["X"].GetString(); // order status
    const char* exec_type = doc["x"].GetString(); // execution type
    
    // Find the order
    auto* order = findOrderByClientId(client_order_id);
    if (!order) {
        order = findOrderByBinanceId(binance_order_id);
    }
    
    if (!order) {
        LOG_WARN("Received execution report for unknown order: %lu", binance_order_id);
        return;
    }
    
    // Update order status
    strncpy(order->status, status, sizeof(order->status) - 1);
    
    // Handle different execution types
    if (strcmp(exec_type, "TRADE") == 0) {
        // Trade execution
        const Qty last_filled_qty = static_cast<Qty>(std::stod(doc["l"].GetString()) * 100000000);
        const Price last_exec_price = static_cast<Price>(std::stod(doc["L"].GetString()) * 100000000);
        
        order->filled_qty += last_filled_qty;
        
        // Send fill notification
        auto* response = response_pool_.allocate();
        if (response) {
            response->order_id = order->client_order_id;
            response->exchange_order_id = order->binance_order_id;
            response->ticker_id = order->ticker_id;
            response->side = order->side;
            response->exec_price = last_exec_price;
            response->exec_qty = last_filled_qty;
            response->leaves_qty = order->quantity - order->filled_qty;
            response->type = (strcmp(status, "FILLED") == 0) ? 
                           MessageType::ORDER_FILLED : MessageType::ORDER_PARTIAL_FILL;
            response->timestamp = getNanosSinceEpoch();
            
            publishResponse(response);
        }
        
        if (strcmp(status, "FILLED") == 0) {
            order->active.store(false);
            orders_filled_.fetch_add(1, std::memory_order_relaxed);
            LOG_INFO("Order filled: client_id=%lu, binance_id=%lu", 
                    order->client_order_id, order->binance_order_id);
        }
    } else if (strcmp(exec_type, "CANCELED") == 0) {
        order->active.store(false);
        orders_canceled_.fetch_add(1, std::memory_order_relaxed);
        
        // Send cancellation notification
        auto* response = response_pool_.allocate();
        if (response) {
            response->order_id = order->client_order_id;
            response->exchange_order_id = order->binance_order_id;
            response->ticker_id = order->ticker_id;
            response->side = order->side;
            response->type = MessageType::ORDER_CANCELED;
            response->timestamp = getNanosSinceEpoch();
            
            publishResponse(response);
        }
    } else if (strcmp(exec_type, "REJECTED") == 0) {
        order->active.store(false);
        orders_rejected_.fetch_add(1, std::memory_order_relaxed);
        
        // Send rejection notification
        auto* response = response_pool_.allocate();
        if (response) {
            response->order_id = order->client_order_id;
            response->exchange_order_id = order->binance_order_id;
            response->ticker_id = order->ticker_id;
            response->side = order->side;
            response->type = MessageType::ORDER_REJECT;
            response->timestamp = getNanosSinceEpoch();
            
            publishResponse(response);
        }
    }
}

bool BinanceOrderGateway::sendHttpRequest(const char* method, const char* endpoint,
                                          const char* payload, char* response_buffer, 
                                          size_t buffer_size) {
    if (!curl_ || !auth_->hasValidCredentials()) {
        return false;
    }
    
    // Build full URL
    char url[512];
    snprintf(url, sizeof(url), "%s%s", api_url_, endpoint);
    
    // For GET/DELETE with payload, append as query string
    if (payload && (strcmp(method, "GET") == 0 || strcmp(method, "DELETE") == 0)) {
        strcat(url, "?");
        strcat(url, payload);
        payload = nullptr;
    }
    
    // Setup CURL options
    curl_easy_setopt(curl_, CURLOPT_URL, url);
    curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
    
    if (payload) {
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, payload);
    }
    
    // Response handling
    WriteData write_data = {response_buffer, buffer_size, 0};
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &write_data);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
        LOG_ERROR("CURL request failed: %s", curl_easy_strerror(res));
        return false;
    }
    
    // Check HTTP status
    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code < 200 || http_code >= 300) {
        LOG_ERROR("HTTP request failed with code %ld: %s", http_code, response_buffer);
        return false;
    }
    
    return true;
}

size_t BinanceOrderGateway::curlWriteCallback(void* contents, size_t size, 
                                              size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    auto* write_data = static_cast<WriteData*>(userp);
    
    size_t to_copy = std::min(total_size, write_data->size - write_data->used - 1);
    memcpy(write_data->buffer + write_data->used, contents, to_copy);
    write_data->used += to_copy;
    write_data->buffer[write_data->used] = '\0';
    
    return total_size;
}

void BinanceOrderGateway::convertToBinanceRequest(const OrderRequest& internal, 
                                                  BinanceOrderRequest& binance) {
    // Get symbol mapping
    if (internal.ticker_id < ME_MAX_TICKERS) {
        strncpy(binance.symbol, symbol_mappings_[internal.ticker_id], 
               sizeof(binance.symbol) - 1);
    }
    
    // Side
    strncpy(binance.side, 
            internal.side == OrderSide::BUY ? "BUY" : "SELL",
            sizeof(binance.side) - 1);
    
    // Order type
    switch (internal.type) {
        case OrderType::MARKET:
            strncpy(binance.type, "MARKET", sizeof(binance.type) - 1);
            break;
        case OrderType::LIMIT:
            strncpy(binance.type, "LIMIT", sizeof(binance.type) - 1);
            strncpy(binance.timeInForce, "GTC", sizeof(binance.timeInForce) - 1);
            break;
        case OrderType::STOP:
            strncpy(binance.type, "STOP_LOSS_LIMIT", sizeof(binance.type) - 1);
            snprintf(binance.stopPrice, sizeof(binance.stopPrice), "%.8f", 
                    internal.stop_price / 100000000.0);
            strncpy(binance.timeInForce, "GTC", sizeof(binance.timeInForce) - 1);
            break;
        default:
            strncpy(binance.type, "LIMIT", sizeof(binance.type) - 1);
            strncpy(binance.timeInForce, "GTC", sizeof(binance.timeInForce) - 1);
    }
    
    // Quantity and price (Binance uses decimal strings)
    snprintf(binance.quantity, sizeof(binance.quantity), "%.8f", 
            internal.qty / 100000000.0);
    snprintf(binance.price, sizeof(binance.price), "%.8f", 
            internal.price / 100000000.0);
    
    // Client order ID
    binance.newClientOrderId = internal.order_id;
    binance.recvWindow = 5000;
}

void BinanceOrderGateway::parseOrderResponse(const rapidjson::Document& doc, OrderInfo& info) {
    if (doc.HasMember("orderId")) {
        info.binance_order_id = doc["orderId"].GetUint64();
    }
    if (doc.HasMember("clientOrderId")) {
        info.client_order_id = std::stoull(doc["clientOrderId"].GetString());
    }
    if (doc.HasMember("symbol")) {
        strncpy(info.symbol, doc["symbol"].GetString(), sizeof(info.symbol) - 1);
    }
    if (doc.HasMember("side")) {
        const char* side = doc["side"].GetString();
        info.side = (strcmp(side, "BUY") == 0) ? OrderSide::BUY : OrderSide::SELL;
    }
    if (doc.HasMember("price")) {
        info.price = static_cast<Price>(std::stod(doc["price"].GetString()) * 100000000);
    }
    if (doc.HasMember("origQty")) {
        info.quantity = static_cast<Qty>(std::stod(doc["origQty"].GetString()) * 100000000);
    }
    if (doc.HasMember("executedQty")) {
        info.filled_qty = static_cast<Qty>(std::stod(doc["executedQty"].GetString()) * 100000000);
    }
    if (doc.HasMember("status")) {
        strncpy(info.status, doc["status"].GetString(), sizeof(info.status) - 1);
    }
    info.timestamp_ns = getNanosSinceEpoch();
}

OrderInfo* BinanceOrderGateway::findOrderByBinanceId(OrderId binance_order_id) noexcept {
    for (auto& order : orders_) {
        if (order.active.load() && order.binance_order_id == binance_order_id) {
            return &order;
        }
    }
    return nullptr;
}

OrderInfo* BinanceOrderGateway::findOrderByClientId(OrderId client_order_id) noexcept {
    for (auto& order : orders_) {
        if (order.active.load() && order.client_order_id == client_order_id) {
            return &order;
        }
    }
    return nullptr;
}

bool BinanceOrderGateway::publishResponse(OrderResponse* response) {
    if (!response) return false;
    
    auto* slot = order_responses_queue_->getNextToWriteTo();
    if (!slot) {
        LOG_ERROR("Response queue full");
        response_pool_.deallocate(response);
        return false;
    }
    
    *slot = response;
    order_responses_queue_->updateWriteIndex();
    return true;
}

bool BinanceOrderGateway::checkAndUpdateWeight(uint32_t weight) {
    const uint32_t current_weight = request_weight_.load();
    if (current_weight + weight > MAX_WEIGHT_PER_MINUTE) {
        return false;
    }
    
    request_weight_.fetch_add(weight);
    return true;
}

void BinanceOrderGateway::resetWeightIfNeeded() {
    const uint64_t now = getNanosSinceEpoch();
    const uint64_t last_request = last_request_time_ns_.load();
    
    if (now - last_request > WEIGHT_RESET_INTERVAL_NS) {
        request_weight_.store(0);
        last_request_time_ns_.store(now);
    }
}

} // namespace Trading::Binance