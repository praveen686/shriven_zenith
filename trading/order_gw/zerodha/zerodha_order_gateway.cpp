#include "zerodha_order_gateway.h"
#include "common/time_utils.h"
#include <cstring>
#include <unistd.h>

namespace Trading::Zerodha {

using namespace Common;

ZerodhaOrderGateway::ZerodhaOrderGateway(SPSCLFQueue<OrderRequest*, 65536>* request_queue,
                                         SPSCLFQueue<OrderResponse*, 65536>* response_queue,
                                         ZerodhaAuth* auth)
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
    
    LOG_INFO("ZerodhaOrderGateway initialized");
}

ZerodhaOrderGateway::~ZerodhaOrderGateway() {
    stop();
    
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
    if (headers_) {
        curl_slist_free_all(headers_);
    }
    
    LOG_INFO("ZerodhaOrderGateway destroyed");
}

void ZerodhaOrderGateway::start() {
    if (running_.exchange(true)) {
        LOG_WARN("ZerodhaOrderGateway already running");
        return;
    }
    
    // Start order processor thread
    order_processor_thread_ = std::thread([this] {
        Common::setThreadCore(4); // Core 4 for Zerodha orders
        runOrderProcessor();
    });
    
    // Start status poller thread
    status_poller_thread_ = std::thread([this] {
        Common::setThreadCore(4); // Same core, different priority
        runStatusPoller();
    });
    
    LOG_INFO("ZerodhaOrderGateway started");
}

void ZerodhaOrderGateway::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    if (order_processor_thread_.joinable()) {
        order_processor_thread_.join();
    }
    if (status_poller_thread_.joinable()) {
        status_poller_thread_.join();
    }
    
    LOG_INFO("ZerodhaOrderGateway stopped - Orders: sent=%lu, filled=%lu, rejected=%lu",
             orders_sent_.load(), orders_filled_.load(), orders_rejected_.load());
}

bool ZerodhaOrderGateway::connect() {
    // Verify authentication
    if (!auth_ || !auth_->isAuthenticated()) {
        LOG_ERROR("Not authenticated with Zerodha");
        return false;
    }
    
    // Setup authorization header
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), 
             "Authorization: token %s:%s", 
             auth_->getApiKey(), auth_->getAccessToken());
    
    headers_ = curl_slist_append(headers_, auth_header);
    headers_ = curl_slist_append(headers_, "Content-Type: application/x-www-form-urlencoded");
    
    LOG_INFO("Connected to Zerodha API");
    return true;
}

void ZerodhaOrderGateway::disconnect() {
    LOG_INFO("Disconnecting from Zerodha API");
}

bool ZerodhaOrderGateway::sendOrder(const OrderRequest& request) {
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

bool ZerodhaOrderGateway::cancelOrder(OrderId order_id) {
    // Find the order
    auto* order = findOrderByClientId(order_id);
    if (!order || !order->active.load()) {
        LOG_WARN("Order %lu not found for cancel", order_id);
        return false;
    }
    
    // Call Zerodha API to cancel
    return cancelOrderApi(order->zerodha_order_id);
}

bool ZerodhaOrderGateway::modifyOrder(OrderId order_id, Price new_price, Qty new_qty) {
    // Find the order
    auto* order = findOrderByClientId(order_id);
    if (!order || !order->active.load()) {
        LOG_WARN("Order %lu not found for modify", order_id);
        return false;
    }
    
    // Call Zerodha API to modify
    return modifyOrderApi(order->zerodha_order_id, new_price, new_qty);
}

void ZerodhaOrderGateway::runOrderProcessor() noexcept {
    LOG_INFO("Order processor thread started");
    
    while (running_.load()) {
        // Process order requests from queue
        auto* request_ptr = order_requests_queue_->getNextToRead();
        if (request_ptr && *request_ptr) {
            const auto* request = *request_ptr;
            
            // Rate limiting
            const uint64_t now_ns = getNanosSinceEpoch();
            const uint64_t elapsed_ns = now_ns - last_request_time_ns_.load();
            if (elapsed_ns < MIN_REQUEST_INTERVAL_NS) {
                const uint64_t sleep_ns = MIN_REQUEST_INTERVAL_NS - elapsed_ns;
                std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
            }
            
            // Convert to Zerodha format
            ZerodhaOrderRequest zerodha_req;
            convertToZerodhaRequest(*request, zerodha_req);
            
            // Place the order
            char order_id[32]{};
            if (placeOrder(zerodha_req, order_id)) {
                // Track the order
                const size_t idx = next_order_idx_.fetch_add(1) % MAX_ORDERS;
                auto& order = orders_[idx];
                order.client_order_id = request->order_id;
                strncpy(order.zerodha_order_id, order_id, sizeof(order.zerodha_order_id) - 1);
                order.ticker_id = request->ticker_id;
                order.side = request->side;
                order.price = request->price;
                order.quantity = request->qty;
                order.filled_qty = 0;
                order.timestamp_ns = getNanosSinceEpoch();
                order.active.store(true);
                
                orders_sent_.fetch_add(1, std::memory_order_relaxed);
                
                // Send acknowledgment
                auto* response = response_pool_.allocate();
                if (response) {
                    response->order_id = request->order_id;
                    response->ticker_id = request->ticker_id;
                    response->client_id = request->client_id;
                    response->side = request->side;
                    response->type = MessageType::ORDER_ACK;
                    response->timestamp = getNanosSinceEpoch();
                    
                    publishResponse(response);
                }
                
                LOG_INFO("Order placed: client_id=%lu, zerodha_id=%s", 
                        request->order_id, order_id);
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
            
            last_request_time_ns_.store(getNanosSinceEpoch());
            order_requests_queue_->updateReadIndex();
        }
        
        // Small sleep to avoid busy spinning
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    LOG_INFO("Order processor thread stopped");
}

void ZerodhaOrderGateway::runStatusPoller() noexcept {
    LOG_INFO("Status poller thread started");
    
    while (running_.load()) {
        // Poll active orders for status updates
        for (auto& order : orders_) {
            if (!order.active.load()) continue;
            
            OrderInfo updated_info;
            if (fetchOrderStatus(order.zerodha_order_id, updated_info)) {
                // Check for fills
                if (updated_info.filled_qty > order.filled_qty) {
                    const Qty fill_qty = updated_info.filled_qty - order.filled_qty;
                    order.filled_qty = updated_info.filled_qty;
                    
                    // Send fill notification
                    auto* response = response_pool_.allocate();
                    if (response) {
                        response->order_id = order.client_order_id;
                        response->ticker_id = order.ticker_id;
                        response->client_id = ClientId_INVALID; // TODO: track client ID
                        response->side = order.side;
                        response->exec_price = order.price; // TODO: get actual exec price
                        response->exec_qty = fill_qty;
                        response->leaves_qty = order.quantity - order.filled_qty;
                        response->type = (order.filled_qty >= order.quantity) ? 
                                       MessageType::ORDER_FILLED : MessageType::ORDER_PARTIAL_FILL;
                        response->timestamp = getNanosSinceEpoch();
                        
                        publishResponse(response);
                    }
                    
                    if (order.filled_qty >= order.quantity) {
                        order.active.store(false);
                        orders_filled_.fetch_add(1, std::memory_order_relaxed);
                        LOG_INFO("Order filled: client_id=%lu, zerodha_id=%s", 
                                order.client_order_id, order.zerodha_order_id);
                    }
                }
                
                // Check for cancellation/rejection
                if (strcmp(updated_info.status, "CANCELLED") == 0 || 
                    strcmp(updated_info.status, "REJECTED") == 0) {
                    order.active.store(false);
                    
                    auto* response = response_pool_.allocate();
                    if (response) {
                        response->order_id = order.client_order_id;
                        response->ticker_id = order.ticker_id;
                        response->side = order.side;
                        response->type = (strcmp(updated_info.status, "CANCELLED") == 0) ?
                                       MessageType::ORDER_CANCELED : MessageType::ORDER_REJECT;
                        response->timestamp = getNanosSinceEpoch();
                        
                        publishResponse(response);
                    }
                    
                    orders_canceled_.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        
        // Poll every 500ms
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    LOG_INFO("Status poller thread stopped");
}

bool ZerodhaOrderGateway::placeOrder(const ZerodhaOrderRequest& req, char* order_id_out) {
    char payload[1024];
    snprintf(payload, sizeof(payload),
             "exchange=%s&tradingsymbol=%s&transaction_type=%s&"
             "order_type=%s&quantity=%lu&product=%s&validity=%s&"
             "price=%.2f&trigger_price=%.2f&disclosed_quantity=%lu&tag=%lu",
             req.exchange, req.tradingsymbol, req.transaction_type,
             req.order_type, req.quantity, req.product, req.validity,
             req.price / 100.0, req.trigger_price / 100.0, 
             req.disclosed_quantity, req.tag);
    
    char response[4096];
    if (!sendHttpRequest("POST", "/orders/regular", payload, response, sizeof(response))) {
        return false;
    }
    
    // Parse response for order ID
    rapidjson::Document doc;
    doc.Parse(response);
    
    if (doc.HasMember("status") && strcmp(doc["status"].GetString(), "success") == 0) {
        if (doc.HasMember("data") && doc["data"].HasMember("order_id")) {
            strncpy(order_id_out, doc["data"]["order_id"].GetString(), 31);
            return true;
        }
    }
    
    LOG_ERROR("Order placement failed: %s", response);
    return false;
}

bool ZerodhaOrderGateway::cancelOrderApi(const char* order_id) {
    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/orders/regular/%s", order_id);
    
    char response[4096];
    return sendHttpRequest("DELETE", endpoint, nullptr, response, sizeof(response));
}

bool ZerodhaOrderGateway::modifyOrderApi(const char* order_id, Price new_price, Qty new_qty) {
    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/orders/regular/%s", order_id);
    
    char payload[256];
    snprintf(payload, sizeof(payload), "price=%.2f&quantity=%lu", 
             new_price / 100.0, new_qty);
    
    char response[4096];
    return sendHttpRequest("PUT", endpoint, payload, response, sizeof(response));
}

bool ZerodhaOrderGateway::fetchOrderStatus(const char* order_id, OrderInfo& info) {
    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), "/orders/%s", order_id);
    
    char response[4096];
    if (!sendHttpRequest("GET", endpoint, nullptr, response, sizeof(response))) {
        return false;
    }
    
    parseOrderStatus(response, info);
    return true;
}

bool ZerodhaOrderGateway::sendHttpRequest(const char* method, const char* endpoint,
                                          const char* payload, char* response_buffer, 
                                          size_t buffer_size) {
    if (!curl_ || !auth_->isAuthenticated()) {
        return false;
    }
    
    // Build full URL
    char url[512];
    snprintf(url, sizeof(url), "%s%s", api_url_, endpoint);
    
    // Setup CURL options
    curl_easy_setopt(curl_, CURLOPT_URL, url);
    curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
    
    if (payload) {
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, payload);
    }
    
    // Response handling
    struct WriteData {
        char* buffer;
        size_t size;
        size_t used;
    } write_data = {response_buffer, buffer_size, 0};
    
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

size_t ZerodhaOrderGateway::curlWriteCallback(void* contents, size_t size, 
                                               size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    auto* write_data = static_cast<WriteData*>(userp);
    
    size_t to_copy = std::min(total_size, write_data->size - write_data->used - 1);
    memcpy(write_data->buffer + write_data->used, contents, to_copy);
    write_data->used += to_copy;
    write_data->buffer[write_data->used] = '\0';
    
    return total_size;
}

void ZerodhaOrderGateway::convertToZerodhaRequest(const OrderRequest& internal, 
                                                  ZerodhaOrderRequest& zerodha) {
    // Get symbol mapping
    if (internal.ticker_id < ME_MAX_TICKERS) {
        const auto& mapping = symbol_mappings_[internal.ticker_id];
        strncpy(zerodha.exchange, mapping.exchange, sizeof(zerodha.exchange) - 1);
        strncpy(zerodha.tradingsymbol, mapping.tradingsymbol, sizeof(zerodha.tradingsymbol) - 1);
    }
    
    // Transaction type
    strncpy(zerodha.transaction_type, 
            internal.side == OrderSide::BUY ? "BUY" : "SELL",
            sizeof(zerodha.transaction_type) - 1);
    
    // Order type
    switch (internal.type) {
        case OrderType::MARKET:
            strncpy(zerodha.order_type, "MARKET", sizeof(zerodha.order_type) - 1);
            break;
        case OrderType::LIMIT:
            strncpy(zerodha.order_type, "LIMIT", sizeof(zerodha.order_type) - 1);
            break;
        case OrderType::STOP:
            strncpy(zerodha.order_type, "SL", sizeof(zerodha.order_type) - 1);
            zerodha.trigger_price = internal.stop_price;
            break;
        default:
            strncpy(zerodha.order_type, "LIMIT", sizeof(zerodha.order_type) - 1);
    }
    
    // Other fields
    zerodha.price = internal.price;
    zerodha.quantity = internal.qty;
    strncpy(zerodha.product, "MIS", sizeof(zerodha.product) - 1); // Intraday
    strncpy(zerodha.validity, "DAY", sizeof(zerodha.validity) - 1);
    zerodha.tag = internal.order_id; // Use client order ID as tag
}

void ZerodhaOrderGateway::parseOrderStatus(const char* json, OrderInfo& info) {
    rapidjson::Document doc;
    doc.Parse(json);
    
    if (doc.HasMember("data") && doc["data"].IsArray() && doc["data"].Size() > 0) {
        const auto& order = doc["data"][0];
        
        if (order.HasMember("order_id")) {
            strncpy(info.zerodha_order_id, order["order_id"].GetString(), 
                   sizeof(info.zerodha_order_id) - 1);
        }
        if (order.HasMember("status")) {
            strncpy(info.status, order["status"].GetString(), 
                   sizeof(info.status) - 1);
        }
        if (order.HasMember("filled_quantity")) {
            info.filled_qty = order["filled_quantity"].GetUint64();
        }
        if (order.HasMember("price")) {
            info.price = static_cast<Price>(order["price"].GetDouble() * 100);
        }
        if (order.HasMember("quantity")) {
            info.quantity = order["quantity"].GetUint64();
        }
    }
}

OrderInfo* ZerodhaOrderGateway::findOrderByExchangeId(const char* exchange_order_id) noexcept {
    for (auto& order : orders_) {
        if (order.active.load() && 
            strcmp(order.zerodha_order_id, exchange_order_id) == 0) {
            return &order;
        }
    }
    return nullptr;
}

OrderInfo* ZerodhaOrderGateway::findOrderByClientId(OrderId client_order_id) noexcept {
    for (auto& order : orders_) {
        if (order.active.load() && order.client_order_id == client_order_id) {
            return &order;
        }
    }
    return nullptr;
}

bool ZerodhaOrderGateway::publishResponse(OrderResponse* response) {
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

} // namespace Trading::Zerodha