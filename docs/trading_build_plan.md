# Trading System Build Plan

## Table of Contents
1. [Reference Trading System Analysis](#reference-trading-system-analysis)
2. [Production Trading System Blueprint](#production-trading-system-blueprint)
3. [Implementation Roadmap](#implementation-roadmap)

---

## Reference Trading System Analysis

### Architecture Overview

The reference trading system consists of **three main components**:

1. **Trade Engine** - Brain of the trading system
2. **Order Gateway** - Connects to exchange for order management
3. **Market Data Consumer** - Receives real-time market data

### Data Flow Architecture

```
[MARKET DATA] → Multicast UDP → [Market Data Consumer]
                                         ↓
                                   Lock-Free Queue
                                         ↓
                                   [TRADE ENGINE]
                                   • FeatureEngine
                                   • OrderManager
                                   • RiskManager
                                   • Strategy (MM/Taker)
                                   • PositionKeeper
                                         ↓
                                   Lock-Free Queue
                                         ↓
[EXCHANGE] ← TCP ← [Order Gateway]
```

### Key Components Breakdown

#### 1. Trade Engine (Core)
- **FeatureEngine**: Calculates trading signals (BBO, spread, etc.)
- **OrderManager**: Tracks active orders and their states
- **RiskManager**: Pre-trade risk checks (position limits, loss limits)
- **PositionKeeper**: Tracks positions, PnL, volume
- **Strategies**: MarketMaker or LiquidityTaker algorithms

#### 2. Order Gateway
- TCP connection to exchange
- Sequence number management
- Order serialization/deserialization

#### 3. Market Data Consumer
- Multicast UDP subscription
- Snapshot/Incremental synchronization
- Gap detection and recovery

### Critical Design Patterns to Preserve

1. **Lock-free communication** between components
2. **Memory pools** for order objects
3. **Pre-allocated containers** (arrays not vectors)
4. **Thread isolation** (each component in own thread)
5. **Event-driven architecture**

---

## Production Trading System Blueprint

### Architecture for Zerodha/Binance

```cpp
namespace Trading {
    
    // ============== CORE INFRASTRUCTURE ==============
    
    // Replace std::vector with fixed arrays
    template<typename T, size_t SIZE>
    using FixedArray = std::array<T, SIZE>;
    
    // All communication via our lock-free queues
    using OrderRequestQueue = Common::LFQueue<OrderRequest, 65536>;
    using OrderResponseQueue = Common::LFQueue<OrderResponse, 65536>;
    using MarketDataQueue = Common::LFQueue<MarketUpdate, 262144>;
    
    // ============== API CONNECTORS ==============
    
    class ZerodhaConnector {
        // Authentication
        struct Credentials {
            char api_key[64];
            char api_secret[128];
            char user_id[32];
            char password[64];
            char totp_secret[64];
        };
        
        // WebSocket for market data
        class MarketDataHandler {
            Common::TCPSocket websocket_;
            MarketDataQueue* output_queue_;
            Common::MemoryPool<MarketUpdate, 1024> update_pool_;
            
            void onTick(const char* data, size_t len) {
                // Parse binary/JSON tick
                // Allocate from pool, not heap
                auto* update = update_pool_.allocate();
                parseTickData(data, len, update);
                output_queue_->enqueue(update);
            }
        };
        
        // REST API for orders
        class OrderHandler {
            Common::TCPSocket https_socket_;
            OrderRequestQueue* input_queue_;
            OrderResponseQueue* output_queue_;
            
            // Pre-allocated buffers for REST API
            char request_buffer_[4096];
            char response_buffer_[8192];
            
            void placeOrder(const OrderRequest* req) {
                // Build REST request (no std::string)
                buildHttpRequest(req, request_buffer_);
                https_socket_.send(request_buffer_);
                // Parse response
                https_socket_.recv(response_buffer_);
                parseOrderResponse(response_buffer_);
            }
        };
    };
    
    class BinanceConnector {
        // Similar structure but for Binance API
        // HMAC-SHA256 signing for authentication
        // WebSocket streams for market data
        // REST API for order management
        
        struct HmacSigner {
            uint8_t secret_key_[64];
            
            void sign(const char* payload, char* signature) {
                // HMAC-SHA256 implementation
                // No OpenSSL - use optimized implementation
            }
        };
    };
    
    // ============== TRADE ENGINE (REFACTORED) ==============
    
    class TradeEngine {
    private:
        // Replace all std containers with fixed-size
        Common::MemoryPool<Order, 10000> order_pool_;
        Common::MemoryPool<Position, 1000> position_pool_;
        
        // Market data storage - no std::map
        struct OrderBook {
            Common::CacheAligned<Price> bid_prices[MAX_DEPTH];
            Common::CacheAligned<Qty> bid_qtys[MAX_DEPTH];
            Common::CacheAligned<Price> ask_prices[MAX_DEPTH];
            Common::CacheAligned<Qty> ask_qtys[MAX_DEPTH];
            std::atomic<uint64_t> last_update_ns{0};
        };
        
        FixedArray<OrderBook, MAX_SYMBOLS> order_books_;
        
        // Risk management with atomics
        struct RiskLimits {
            std::atomic<int64_t> max_position{100000};
            std::atomic<int64_t> max_loss{50000};
            std::atomic<uint32_t> max_order_rate{100}; // per second
        };
        
        // Feature calculation without heap allocation
        struct Features {
            double mid_price;
            double spread;
            double book_imbalance;
            double volatility;
            // ... more features
        };
        
        void calculateFeatures(const OrderBook& book, Features* out) {
            // All calculations in-place, no allocations
        }
        
        // Strategy interface
        class Strategy {
        public:
            virtual void onMarketData(const MarketUpdate* update) = 0;
            virtual void onOrderUpdate(const OrderResponse* response) = 0;
            virtual void onTimer(uint64_t nanos) = 0;
        };
        
        // Main event loop
        void run() {
            while (running_) {
                // Process market data
                if (auto* update = market_queue_->dequeue()) {
                    updateOrderBook(update);
                    strategy_->onMarketData(update);
                    market_pool_.deallocate(update);
                }
                
                // Process order responses
                if (auto* response = order_response_queue_->dequeue()) {
                    updatePosition(response);
                    strategy_->onOrderUpdate(response);
                    response_pool_.deallocate(response);
                }
                
                // Timer events
                auto now = Common::getCurrentNanos();
                if (now - last_timer_ > timer_interval_ns_) {
                    strategy_->onTimer(now);
                    last_timer_ = now;
                }
            }
        }
    };
    
    // ============== INTEGRATION LAYER ==============
    
    class TradingSystem {
    private:
        // Components
        std::unique_ptr<TradeEngine> engine_;
        std::unique_ptr<ZerodhaConnector> zerodha_;
        std::unique_ptr<BinanceConnector> binance_;
        
        // Thread management
        std::thread engine_thread_;
        std::thread zerodha_thread_;
        std::thread binance_thread_;
        
    public:
        void initialize(const Config& cfg) {
            // Set CPU affinity
            Common::ThreadUtils::setCurrentThreadAffinity(0);
            
            // Initialize components with our Common library
            engine_ = std::make_unique<TradeEngine>();
            
            if (cfg.enable_zerodha) {
                zerodha_ = std::make_unique<ZerodhaConnector>();
                zerodha_thread_ = std::thread([this] {
                    Common::ThreadUtils::setCurrentThreadAffinity(1);
                    zerodha_->run();
                });
            }
            
            if (cfg.enable_binance) {
                binance_ = std::make_unique<BinanceConnector>();
                binance_thread_ = std::thread([this] {
                    Common::ThreadUtils::setCurrentThreadAffinity(2);
                    binance_->run();
                });
            }
            
            // Start engine
            engine_thread_ = std::thread([this] {
                Common::ThreadUtils::setCurrentThreadAffinity(3);
                Common::ThreadUtils::setRealTimePriority(99);
                engine_->run();
            });
        }
    };
}
```

### Key Migration Points

#### PHILOSOPHY: Single Source of Truth
**CRITICAL CHANGE**: ALL types and infrastructure are in Common library ONLY
- NO duplicate type definitions anywhere
- NO Trading::Types namespace
- Business logic uses Common types directly
- Common library is the ONLY place for type definitions

#### From Reference → To Production

| Reference Component | Production Component | Changes Required |
|---------------------|---------------------|------------------|
| `std::vector` | `Common::FixedArray` | Pre-allocate all containers |
| `std::string` | `char[]` buffers | No dynamic strings |
| `std::map` | Hash arrays | O(1) lookup, no allocation |
| `new/delete` | `Common::MemoryPool` | Pool-based allocation |
| TCP to Exchange | REST/WebSocket APIs | Protocol adaptation |
| Multicast Market Data | WebSocket streams | Different transport |
| Generic logging | `Common::Logger` | Lock-free async logging |

### Zerodha Integration Specifics

```cpp
class ZerodhaKiteConnect {
    // Authentication flow
    // 1. Login with credentials
    // 2. Generate TOTP
    // 3. Get access token
    // 4. Subscribe to WebSocket
    
    // Order placement
    // POST /orders/regular
    {
        "exchange": "NSE",
        "symbol": "RELIANCE",
        "transaction_type": "BUY",
        "quantity": 100,
        "order_type": "LIMIT",
        "price": 2450.50
    }
    
    // Market data subscription
    ws.subscribe([256265]); // NIFTY
    ws.on('ticks', (ticks) => {
        // Process ticks
    });
};
```

### Binance Integration Specifics

```cpp
class BinanceAPIClient {
    // REST endpoints
    // POST /api/v3/order
    // - symbol=BTCUSDT
    // - side=BUY
    // - type=LIMIT
    // - quantity=0.01
    // - price=40000
    // - signature=HMAC-SHA256(query)
    
    // WebSocket streams
    // wss://stream.binance.com:9443/ws/btcusdt@depth
    // wss://stream.binance.com:9443/ws/btcusdt@trade
};
```

---

## Implementation Roadmap

### Phase 1: Core Infrastructure (Week 1)
- [x] Common library (DONE)
- [ ] Create Trading namespace structure
- [ ] Implement fixed-size containers
- [ ] Set up memory pools for orders/positions

### Phase 2: API Connectors (Week 2)
- [ ] Zerodha authentication (TOTP, OAuth)
- [ ] Binance HMAC signing
- [ ] WebSocket handlers (no dynamic allocation)
- [ ] REST API clients (pre-allocated buffers)

### Phase 3: Trade Engine (Week 3)
- [ ] Port OrderManager without `std::map`
- [ ] Port RiskManager with atomics
- [ ] Port PositionKeeper with fixed arrays
- [ ] Integrate with `Common::LFQueue`

### Phase 4: Integration (Week 4)
- [ ] Connect all components
- [ ] Test with paper trading
- [ ] Performance benchmarking
- [ ] Production deployment

### Performance Targets

| Component | Target Latency | Notes |
|-----------|---------------|-------|
| Market Data Processing | < 1μs | From receipt to strategy |
| Order Placement | < 10μs | From signal to gateway |
| Risk Checks | < 100ns | Pre-trade validation |
| Memory Allocation | 0ns | Everything pre-allocated |

### Testing Strategy

1. **Unit Tests**: Each component in isolation
2. **Integration Tests**: Component interactions
3. **Stress Tests**: Maximum throughput scenarios
4. **Latency Tests**: 99.99th percentile measurements
5. **Paper Trading**: Real market conditions, fake money
6. **Production Rollout**: Gradual with small positions

---

## Conclusion

The reference trading system provides a **solid architectural foundation** but needs **complete rewrite** to meet our performance standards. Every `std::` container must be replaced with our Common library primitives to achieve the sub-microsecond latencies required for competitive trading.

**Key Success Factors:**
- Zero dynamic allocation
- Lock-free data structures
- Cache-line alignment
- CPU affinity and real-time scheduling
- Pre-allocated memory pools

This blueprint ensures we can compete with the fastest trading systems while maintaining the flexibility to trade on both traditional (Zerodha/NSE) and crypto (Binance) markets.