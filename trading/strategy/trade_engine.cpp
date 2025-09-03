#include "trade_engine.h"
#include "common/time_utils.h"
#include "common/thread_utils.h"

namespace Trading {

using namespace Common;

TradeEngine::TradeEngine(ClientId client_id,
                        ClientRequestQueue* order_requests_out,
                        ClientResponseQueue* order_responses_in,
                        MarketUpdateQueue* market_updates_in)
    : client_id_(client_id),
      order_requests_out_(order_requests_out),
      order_responses_in_(order_responses_in),
      market_updates_in_(market_updates_in),
      request_pool_(-1),  // Use default NUMA node
      response_pool_(-1),
      update_pool_(-1) {
    
    // Initialize components
    order_manager_ = std::make_unique<OrderManager>(this, nullptr);
    risk_manager_ = std::make_unique<RiskManager>();
    position_keeper_ = std::make_unique<PositionKeeper>();
    feature_engine_ = std::make_unique<FeatureEngine>();
    market_maker_ = std::make_unique<MarketMaker>(order_manager_.get(), 
                                                    feature_engine_.get(),
                                                    risk_manager_.get(), 
                                                    position_keeper_.get());
    liquidity_taker_ = std::make_unique<LiquidityTaker>(order_manager_.get(), 
                                                          feature_engine_.get(),
                                                          risk_manager_.get(), 
                                                          position_keeper_.get());
}

TradeEngine::~TradeEngine() {
    stop();
}

bool TradeEngine::start() {
    if (running_.exchange(true)) {
        return false; // Already running
    }
    
    engine_thread_ = std::thread([this] {
        Common::setThreadCore(3); // Core 3 for trade engine
        // TODO: setRealTimePriority(99);
        // TODO: setCurrentThreadName("TradeEngine");
        
        LOG_INFO("TradeEngine started on core 3");
        run();
    });
    
    return true;
}

void TradeEngine::stop() {
    if (!running_.exchange(false)) {
        return; // Already stopped
    }
    
    if (engine_thread_.joinable()) {
        engine_thread_.join();
    }
    
    LOG_INFO("TradeEngine stopped - processed %lu messages", 
             messages_processed_.load(std::memory_order_relaxed));
}

void TradeEngine::run() noexcept {
    while (running_.load(std::memory_order_acquire)) {
        // Process market data with higher priority
        bool processed = false;
        
        // Process up to 100 market updates per iteration
        for (int i = 0; i < 100; ++i) {
            if (const auto* update_ptr = market_updates_in_->getNextToRead()) {
                onMarketUpdate(*update_ptr);  // Dereference the pointer to pointer
                market_updates_in_->updateReadIndex();
                processed = true;
            } else {
                break;
            }
        }
        
        // Process order responses
        for (int i = 0; i < 10; ++i) {
            if (const auto* response_ptr = order_responses_in_->getNextToRead()) {
                onOrderResponse(*response_ptr);  // Dereference the pointer to pointer
                order_responses_in_->updateReadIndex();
                processed = true;
            } else {
                break;
            }
        }
        
        // If nothing processed, yield CPU
        if (!processed) {
            __builtin_ia32_pause(); // CPU pause instruction
        }
    }
}

void TradeEngine::sendOrderRequest(const ClientRequest* request) noexcept {
    if (!request) return;
    
    // Risk check first
    auto risk_result = risk_manager_->checkOrder(
        request->ticker_id, 
        request->side,
        request->price,
        request->quantity
    );
    
    if (risk_result != RiskCheckResult::PASS) {
        LOG_WARN("Order rejected by risk check: ticker=%u, reason=%u",
                request->ticker_id, static_cast<uint8_t>(risk_result));
        return;
    }
    
    // Send to exchange
    if (auto* slot = order_requests_out_->getNextToWriteTo()) {
        *slot = const_cast<ClientRequest*>(request);
        order_requests_out_->updateWriteIndex();
        orders_sent_.fetch_add(1, std::memory_order_relaxed);
    } else {
        LOG_ERROR("Failed to enqueue order request - queue full");
    }
}

void TradeEngine::sendOrder(TickerId ticker_id, Side side, Price price, Qty quantity) noexcept {
    // Allocate request from pool
    auto* request = reinterpret_cast<ClientRequest*>(request_pool_.allocate());
    if (!request) {
        LOG_ERROR("Failed to allocate order request - pool exhausted");
        return;
    }
    
    // Initialize request
    request->type = ClientRequest::NEW_ORDER;
    request->client_id = client_id_;
    request->ticker_id = ticker_id;
    request->order_id = OrderId_INVALID; // Will be assigned by exchange
    request->side = side;
    request->price = price;
    request->quantity = quantity;
    request->timestamp_ns = Common::getNanosSinceEpoch();
    
    // Send to exchange
    sendOrderRequest(request);
    
    LOG_DEBUG("Sent order: ticker=%u, side=%u, px=%lu, qty=%u", 
             ticker_id, side, price, quantity);
}

void TradeEngine::onMarketUpdate(const MarketUpdate* update) noexcept {
    if (!update || update->ticker_id >= ME_MAX_TICKERS) return;
    
    messages_processed_.fetch_add(1, std::memory_order_relaxed);
    last_event_time_ns_.store(Common::getNanosSinceEpoch(), std::memory_order_relaxed);
    
    // Update order book
    updateOrderBook(update);
    
    // Update feature engine with order book
    const auto& book = order_books_[update->ticker_id];
    feature_engine_->onOrderBookUpdate(update->ticker_id, &book);
    
    // Update strategies with order book
    market_maker_->onOrderBookUpdate(update->ticker_id, &book);
    
    // Update position keeper with market price
    if (update->type == MarketUpdate::TRADE) {
        position_keeper_->updateMarketPrice(update->ticker_id, update->price);
        
        // Update feature engine with trade
        feature_engine_->onTradeUpdate(update->ticker_id, update->side, 
                                       update->price, update->quantity);
        
        // Update strategies with trade
        market_maker_->onTradeUpdate(update->ticker_id, update->side, 
                                     update->price, update->quantity);
        liquidity_taker_->onTradeUpdate(update->ticker_id, update->side, 
                                        update->price, update->quantity);
    }
    
    // Check for trading signals (may be redundant now)
    checkSignals(update->ticker_id);
}

void TradeEngine::onOrderResponse(const ClientResponse* response) noexcept {
    if (!response) return;
    
    messages_processed_.fetch_add(1, std::memory_order_relaxed);
    
    switch (response->type) {
        case ClientResponse::ORDER_ACK:
            LOG_DEBUG("Order acknowledged: id=%lu", response->order_id);
            order_manager_->onOrderUpdate(
                response->order_id, 
                OrderState::LIVE,
                0, 
                response->quantity
            );
            break;
            
        case ClientResponse::ORDER_FILL: {
            LOG_INFO("Order filled: id=%lu, qty=%u, px=%lu",
                    response->order_id, response->quantity, response->price);
            
            // Update order manager
            order_manager_->onOrderUpdate(
                response->order_id,
                response->leaves_qty == 0 ? OrderState::FILLED : OrderState::LIVE,
                response->quantity,
                response->leaves_qty
            );
            
            // Update position keeper
            position_keeper_->onFill(
                response->ticker_id,
                response->side,
                response->quantity,
                response->price
            );
            
            // Update risk manager
            risk_manager_->updatePosition(
                response->ticker_id,
                response->side,
                response->quantity,
                response->price
            );
            break;
        }
            
        case ClientResponse::ORDER_CANCEL:
            LOG_INFO("Order canceled: id=%lu", response->order_id);
            order_manager_->onOrderUpdate(
                response->order_id,
                OrderState::CANCELED,
                0, 0
            );
            break;
            
        case ClientResponse::ORDER_REJECT:
            LOG_WARN("Order rejected: id=%lu", response->order_id);
            order_manager_->onOrderUpdate(
                response->order_id,
                OrderState::REJECTED,
                0, 0
            );
            break;
            
        default:
            LOG_ERROR("Unknown order response type: %u", static_cast<uint8_t>(response->type));
            break;
    }
}

void TradeEngine::updateOrderBook(const MarketUpdate* update) noexcept {
    auto& book = order_books_[update->ticker_id];
    
    switch (update->type) {
        case MarketUpdate::BID_UPDATE:
            // Update at level 0 (best bid) with 1 order
            book.updateBid(update->price, update->quantity, 1, 0);
            break;
            
        case MarketUpdate::ASK_UPDATE:
            // Update at level 0 (best ask) with 1 order
            book.updateAsk(update->price, update->quantity, 1, 0);
            break;
            
        case MarketUpdate::TRADE:
            // Just track last trade for now
            break;
            
        default:
            LOG_WARN("Unknown market update type: %u", static_cast<uint8_t>(update->type));
            break;
    }
    
    book.updateTimestamp(update->timestamp_ns);
}

void TradeEngine::checkSignals(TickerId ticker_id) noexcept {
    // This is where strategy logic would go
    // For now, just a placeholder
    
    const auto& book = order_books_[ticker_id];
    const Price best_bid = book.getBestBid();
    const Price best_ask = book.getBestAsk();
    
    if (best_bid == Price_INVALID || best_ask == Price_INVALID) {
        return;
    }
    
    const Qty bid_qty = book.getBestBidQty();
    const Qty ask_qty = book.getBestAskQty();
    
    if (best_bid != Price_INVALID && best_ask != Price_INVALID) {
        const Price spread = best_ask - best_bid;
        const Price min_spread = 100; // 1 rupee minimum spread
        
        if (spread > min_spread) {
            // Wide spread - potential market making opportunity
            // Calculate our quotes based on available liquidity
            const Qty min_qty = std::min(bid_qty, ask_qty);
            
            if (min_qty > 0) {
                // Place orders at better prices than current BBO
                const Price our_bid = best_bid + 1;  // 1 tick better
                const Price our_ask = best_ask - 1;  // 1 tick better
                const Qty our_qty = std::min(min_qty / 2, Qty(100)); // Conservative size
                
                // Create market making orders
                auto* buy_order = order_manager_->createOrder(ticker_id, 1, our_bid, our_qty);  // BUY
                auto* sell_order = order_manager_->createOrder(ticker_id, 2, our_ask, our_qty); // SELL
                
                if (buy_order && sell_order) {
                    LOG_DEBUG("Created MM orders: buy@%ld, sell@%ld, qty=%lu", 
                             our_bid, our_ask, our_qty);
                }
                
                LOG_DEBUG("MM opportunity: ticker=%u, spread=%ld, bid_qty=%lu, ask_qty=%lu",
                         ticker_id, spread, bid_qty, ask_qty);
            }
        }
    }
}

int64_t TradeEngine::getPosition(TickerId ticker_id) const noexcept {
    return position_keeper_->getPosition(ticker_id);
}

int64_t TradeEngine::getTotalPnL() const noexcept {
    return position_keeper_->getTotalPnL();
}

} // namespace Trading