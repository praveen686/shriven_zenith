#pragma once

#include "../order_gateway.h"
#include "trading/auth/zerodha/zerodha_auth.h"
#include "common/types.h"
#include "common/logging.h"
#include "common/thread_utils.h"
#include "common/mem_pool.h"
#include <thread>
#include <curl/curl.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

namespace Trading::Zerodha {

using namespace Common;

/// Zerodha-specific order request with additional fields
struct ZerodhaOrderRequest {
    char exchange[16]{};       // NSE, BSE, NFO, etc.
    char tradingsymbol[32]{};  // RELIANCE, NIFTY25DEC21FUT
    char transaction_type[8]{}; // BUY, SELL
    char order_type[16]{};     // LIMIT, MARKET, SL, SL-M
    char product[16]{};        // CNC, MIS, NRML
    char validity[16]{};       // DAY, IOC
    Price price{0};
    Price trigger_price{0};
    Qty quantity{0};
    Qty disclosed_quantity{0};
    OrderId tag{0};            // Client order ID for tracking
};

/// Zerodha Order Gateway - implements REST API for order management
class alignas(CACHE_LINE_SIZE) ZerodhaOrderGateway : public IOrderGateway {
public:
    ZerodhaOrderGateway(SPSCLFQueue<OrderRequest*, 65536>* request_queue,
                        SPSCLFQueue<OrderResponse*, 65536>* response_queue,
                        ZerodhaAuth* auth);
    
    ~ZerodhaOrderGateway() override;
    
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
    
    void setSymbolMapping(TickerId ticker_id, const char* exchange, const char* symbol) {
        if (ticker_id < ME_MAX_TICKERS) {
            auto& mapping = symbol_mappings_[ticker_id];
            strncpy(mapping.exchange, exchange, sizeof(mapping.exchange) - 1);
            strncpy(mapping.tradingsymbol, symbol, sizeof(mapping.tradingsymbol) - 1);
        }
    }
    
private:
    // Symbol mapping for ticker ID to exchange symbol
    struct SymbolMapping {
        char exchange[16]{};
        char tradingsymbol[32]{};
    };
    
    // Order tracking
    struct OrderInfo {
        OrderId client_order_id{OrderId_INVALID};
        OrderId exchange_order_id{OrderId_INVALID};
        char zerodha_order_id[32]{};
        TickerId ticker_id{TickerId_INVALID};
        OrderSide side{OrderSide::INVALID};
        Price price{Price_INVALID};
        Qty quantity{0};
        Qty filled_qty{0};
        char status[16]{};
        uint64_t timestamp_ns{0};
        std::atomic<bool> active{false};
    };
    
    // Thread function for processing order requests
    void runOrderProcessor() noexcept;
    
    // Thread function for polling order status
    void runStatusPoller() noexcept;
    
    // REST API methods
    bool placeOrder(const ZerodhaOrderRequest& req, char* order_id_out);
    bool cancelOrderApi(const char* order_id);
    bool modifyOrderApi(const char* order_id, Price new_price, Qty new_qty);
    bool fetchOrderStatus(const char* order_id, OrderInfo& info);
    
    // HTTP helpers
    bool sendHttpRequest(const char* method, const char* endpoint, 
                        const char* payload, char* response_buffer, size_t buffer_size);
    static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
    
    // Convert internal types to Zerodha format
    void convertToZerodhaRequest(const OrderRequest& internal, ZerodhaOrderRequest& zerodha);
    void parseOrderResponse(const char* json, OrderResponse& response);
    void parseOrderStatus(const char* json, OrderInfo& info);
    
    // Helper to publish responses
    bool publishResponse(OrderResponse* response);
    
    // Response data structure for CURL
    struct WriteData {
        char* buffer;
        size_t size;
        size_t used;
    };
    
    // Member variables
    ZerodhaAuth* auth_;
    char api_url_[256]{"https://api.kite.trade"};
    
    // Symbol mappings
    std::array<SymbolMapping, ME_MAX_TICKERS> symbol_mappings_;
    
    // Order tracking - fixed size, no std::map
    static constexpr size_t MAX_ORDERS = 10000;
    std::array<OrderInfo, MAX_ORDERS> orders_;
    std::atomic<uint64_t> next_order_idx_{0};
    
    // Memory pools
    MemoryPool<64, 10000> request_pool_;
    MemoryPool<64, 10000> response_pool_;
    
    // Threads
    std::thread order_processor_thread_;
    std::thread status_poller_thread_;
    
    // Statistics
    std::atomic<uint64_t> orders_sent_{0};
    std::atomic<uint64_t> orders_filled_{0};
    std::atomic<uint64_t> orders_rejected_{0};
    std::atomic<uint64_t> orders_canceled_{0};
    
    // CURL handle (reused for performance)
    CURL* curl_{nullptr};
    struct curl_slist* headers_{nullptr};
    
    // Rate limiting
    std::atomic<uint64_t> last_request_time_ns_{0};
    static constexpr uint64_t MIN_REQUEST_INTERVAL_NS = 100000000; // 100ms = 10 req/sec
    
    // Helper to find order by exchange ID
    OrderInfo* findOrderByExchangeId(const char* exchange_order_id) noexcept;
    OrderInfo* findOrderByClientId(OrderId client_order_id) noexcept;
};

} // namespace Trading::Zerodha