#pragma once

#include "../order_gateway.h"
#include "trading/auth/binance/binance_auth.h"
#include "common/types.h"
#include "common/logging.h"
#include "common/thread_utils.h"
#include "common/mem_pool.h"
#include <thread>
#include <curl/curl.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

namespace Trading::Binance {

using namespace Common;

/// Binance-specific order request with additional fields
struct BinanceOrderRequest {
    char symbol[32]{};          // BTCUSDT, ETHUSDT
    char side[8]{};             // BUY, SELL
    char type[16]{};            // LIMIT, MARKET, STOP_LOSS_LIMIT
    char timeInForce[8]{};      // GTC, IOC, FOK
    char quantity[32]{};        // Decimal string
    char price[32]{};           // Decimal string  
    char stopPrice[32]{};       // For stop orders
    OrderId newClientOrderId{0}; // Client order ID
    uint64_t recvWindow{5000};  // Request validity window
    uint64_t timestamp{0};       // Request timestamp
};

/// Binance Order Gateway - implements REST API and WebSocket for order management
class alignas(CACHE_LINE_SIZE) BinanceOrderGateway : public IOrderGateway {
public:
    BinanceOrderGateway(SPSCLFQueue<OrderRequest*, 65536>* request_queue,
                        SPSCLFQueue<OrderResponse*, 65536>* response_queue,
                        BinanceAuth* auth);
    
    ~BinanceOrderGateway() override;
    
    // Core interface implementation
    auto start() -> void override;
    auto stop() -> void override;
    auto connect() -> bool override;
    auto disconnect() -> void override;
    
    // Order operations - these are called by external threads
    auto sendOrder(const OrderRequest& request) -> bool override;
    auto cancelOrder(OrderId order_id) -> bool override;
    auto modifyOrder(OrderId order_id, Price new_price, Qty new_qty) -> bool override;
    
    // Configuration
    void setApiUrl(const char* url) {
        strncpy(api_url_, url, sizeof(api_url_) - 1);
    }
    
    void setWsUrl(const char* url) {
        strncpy(ws_url_, url, sizeof(ws_url_) - 1);
    }
    
    void setSymbolMapping(TickerId ticker_id, const char* symbol) {
        if (ticker_id < ME_MAX_TICKERS) {
            strncpy(symbol_mappings_[ticker_id], symbol, sizeof(symbol_mappings_[ticker_id]) - 1);
        }
    }
    
private:
    // Symbol mapping for ticker ID to Binance symbol
    std::array<char[32], ME_MAX_TICKERS> symbol_mappings_;
    
    // Order tracking
    struct OrderInfo {
        OrderId client_order_id{OrderId_INVALID};
        OrderId binance_order_id{OrderId_INVALID};
        char symbol[32]{};
        TickerId ticker_id{TickerId_INVALID};
        OrderSide side{OrderSide::INVALID};
        Price price{Price_INVALID};
        Qty quantity{0};
        Qty filled_qty{0};
        Qty cumulative_quote_qty{0};  // Total value of filled quantity
        char status[16]{};  // NEW, PARTIALLY_FILLED, FILLED, CANCELED, REJECTED
        uint64_t timestamp_ns{0};
        std::atomic<bool> active{false};
    };
    
    // Thread function for processing order requests
    void runOrderProcessor() noexcept;
    
    // Thread function for WebSocket user data stream
    void runWebSocketHandler() noexcept;
    
    // REST API methods
    bool placeOrder(const BinanceOrderRequest& req, OrderInfo& order_out);
    bool cancelOrderApi(const char* symbol, OrderId order_id);
    bool queryOrder(const char* symbol, OrderId order_id, OrderInfo& info);
    bool startUserDataStream(char* listen_key_out);
    bool keepAliveUserDataStream(const char* listen_key);
    
    // WebSocket methods
    bool connectWebSocket();
    void processWebSocketMessage(const char* message);
    void processExecutionReport(const rapidjson::Document& doc);
    
    // HTTP helpers
    bool sendHttpRequest(const char* method, const char* endpoint, 
                        const char* payload, char* response_buffer, size_t buffer_size);
    static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
    
    // Convert internal types to Binance format
    void convertToBinanceRequest(const OrderRequest& internal, BinanceOrderRequest& binance);
    void parseOrderResponse(const rapidjson::Document& doc, OrderInfo& info);
    
    // Helper to publish responses
    bool publishResponse(OrderResponse* response);
    
    // Response data structure for CURL
    struct WriteData {
        char* buffer;
        size_t size;
        size_t used;
    };
    
    // Member variables
    BinanceAuth* auth_;
    char api_url_[256]{"https://api.binance.com"};
    char ws_url_[256]{"wss://stream.binance.com:9443/ws"};
    char listen_key_[128]{};  // User data stream key
    
    // Order tracking - fixed size, no std::map
    static constexpr size_t MAX_ORDERS = 10000;
    std::array<OrderInfo, MAX_ORDERS> orders_;
    std::atomic<uint64_t> next_order_idx_{0};
    
    // Memory pools
    MemoryPool<64, 10000> request_pool_;
    MemoryPool<64, 10000> response_pool_;
    
    // Threads
    std::thread order_processor_thread_;
    std::thread websocket_thread_;
    
    // Statistics
    std::atomic<uint64_t> orders_sent_{0};
    std::atomic<uint64_t> orders_filled_{0};
    std::atomic<uint64_t> orders_rejected_{0};
    std::atomic<uint64_t> orders_canceled_{0};
    
    // CURL handles
    CURL* curl_{nullptr};
    struct curl_slist* headers_{nullptr};
    
    // WebSocket handle (simplified - in production use libwebsockets)
    int ws_socket_{-1};
    
    // Rate limiting (Binance: 1200 weight per minute)
    std::atomic<uint64_t> last_request_time_ns_{0};
    std::atomic<uint32_t> request_weight_{0};
    static constexpr uint64_t WEIGHT_RESET_INTERVAL_NS = 60000000000ULL; // 1 minute
    static constexpr uint32_t MAX_WEIGHT_PER_MINUTE = 1200;
    
    // Helper to find order by Binance order ID
    OrderInfo* findOrderByBinanceId(OrderId binance_order_id) noexcept;
    OrderInfo* findOrderByClientId(OrderId client_order_id) noexcept;
    
    // Weight management
    bool checkAndUpdateWeight(uint32_t weight);
    void resetWeightIfNeeded();
};

} // namespace Trading::Binance