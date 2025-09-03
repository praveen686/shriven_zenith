# Zerodha WebSocket & Order Book Implementation Plan

**Project**: Shriven Zenith - Zerodha Trading System
**Start Date**: 2025-09-03
**Target**: Production-ready NIFTY50 trading with spot, futures & options

## Executive Summary

Implement Zerodha WebSocket client for real-time market data with order book building for NIFTY50 index components (spot, futures, option chain). System will persist tick data and maintain order books following the zero-allocation, ultra-low latency design patterns already established in the codebase.

## Phase 1: Core WebSocket Implementation (Week 1)

### 1.1 Kite WebSocket Binary Protocol Parser
**File**: `/trading/market_data/zerodha/kite_ws_client.h/.cpp`
**Reuses**: 
- `Common::MemoryPool` for zero allocation
- `Common::LFQueue` for lock-free message passing
- Pattern from `binance_ws_client.cpp`

**Key Components**:
```cpp
class KiteWSClient : public IMarketDataConsumer {
    // Binary packet structures (Kite protocol)
    struct KitePacket {
        uint16_t num_packets;
        uint16_t packet_length;
        // Followed by binary data
    };
    
    struct KiteTick {
        uint32_t instrument_token;
        uint32_t last_price;      // Price * 100
        uint32_t last_quantity;
        // ... other fields per Kite spec
    };
    
    // Memory pools - NO HEAP ALLOCATION
    MemoryPool<KiteTick, 100000> tick_pool_;
    MemoryPool<KiteDepth, 10000> depth_pool_;
};
```

### 1.2 Symbol Resolution & Subscription Manager
**File**: `/trading/market_data/zerodha/kite_symbol_resolver.h/.cpp`
**Purpose**: Map NIFTY50 components to Kite instrument tokens

**Key Functions**:
- `resolveNifty50Components()` - Get spot constituents
- `getNearestFuturesExpiry()` - Find current month futures
- `getOptionChain()` - Get strikes around spot price
- `buildSubscriptionList()` - Create subscription message

### 1.3 WebSocket Connection Management
**Requirements**:
- Automatic reconnection on disconnect
- Heartbeat/pong every 3 seconds (Kite requirement)
- Rate limiting (max 3000 subscriptions)
- Binary protocol handling

## Phase 2: Order Book Builder (Week 1-2)

### 2.1 Order Book Data Structure
**File**: `/trading/market_data/order_book.h`
**Design**: Fixed-size, cache-aligned, zero-allocation

```cpp
template<size_t MAX_LEVELS = 20>
struct OrderBook {
    // Cache-aligned arrays for performance
    alignas(64) Price bid_prices[MAX_LEVELS];
    alignas(64) Qty bid_qtys[MAX_LEVELS];
    alignas(64) Price ask_prices[MAX_LEVELS];
    alignas(64) Qty ask_qtys[MAX_LEVELS];
    
    std::atomic<uint64_t> last_update_ns{0};
    uint32_t instrument_token;
    uint8_t bid_depth;
    uint8_t ask_depth;
    
    // O(1) operations only
    void updateBid(Price px, Qty qty, uint8_t level);
    void updateAsk(Price px, Qty qty, uint8_t level);
    Price getBestBid() const { return bid_prices[0]; }
    Price getBestAsk() const { return ask_prices[0]; }
};
```

### 2.2 Order Book Manager
**File**: `/trading/market_data/zerodha/kite_orderbook_manager.h/.cpp`
**Manages**: All order books for subscribed instruments

```cpp
class KiteOrderBookManager {
    // Fixed-size storage - no std::map
    static constexpr size_t MAX_INSTRUMENTS = 500;
    OrderBook<20> futures_books_[MAX_INSTRUMENTS];
    OrderBook<5> options_books_[MAX_INSTRUMENTS];
    
    // Direct index mapping for O(1) access
    uint32_t token_to_index_[MAX_TOKEN_VALUE];
};
```

## Phase 3: Data Persistence (Week 2)

### 3.1 Tick Data Persistence
**File**: `/trading/market_data/zerodha/kite_tick_writer.h/.cpp`
**Format**: Binary format for speed, CSV for analysis

```cpp
class KiteTickWriter {
    // Memory-mapped file for zero-copy writes
    struct TickRecord {
        uint32_t instrument_token;
        uint64_t exchange_timestamp_ns;
        uint64_t local_timestamp_ns;
        Price last_price;
        Qty volume;
        Qty oi;  // Open interest for F&O
    } __attribute__((packed));
    
    // Async writing to avoid blocking
    LFQueue<TickRecord, 1000000> write_queue_;
};
```

### 3.2 Order Book Snapshots
**File**: `/trading/market_data/zerodha/kite_orderbook_snapshot.h/.cpp`
**Purpose**: Periodic snapshots for recovery/analysis

## Phase 4: Integration & Testing (Week 2)

### 4.1 Integration Points
1. **Config Integration**
   - Read indices from config.toml
   - Apply subscription filters
   
2. **Instrument Fetcher Integration**
   - Use existing `ZerodhaInstrumentFetcher`
   - Cache instrument metadata

3. **Trading Engine Integration**
   - Publish to `market_updates_queue_`
   - Maintain latency < 1μs target

### 4.2 Testing Requirements
- Unit tests for binary parser
- Integration test with mock WebSocket
- Performance benchmarks (target: < 100ns per tick)
- Stress test: 10,000 ticks/second

## Implementation Checklist

### Week 1 Sprint
- [ ] Implement KiteWSClient base structure
- [ ] Binary protocol parser for Kite packets
- [ ] WebSocket connection with libwebsockets
- [ ] Symbol resolver for NIFTY50 components
- [ ] Basic order book structure
- [ ] Unit tests for parser

### Week 2 Sprint  
- [ ] Order book manager implementation
- [ ] Tick data persistence
- [ ] Order book snapshots
- [ ] Integration with existing system
- [ ] Performance benchmarks
- [ ] End-to-end testing

## Design Principles (FROM CLAUDE.md)

1. **ZERO HEAP ALLOCATION**
   - All memory pre-allocated in pools
   - Fixed-size containers only
   
2. **EXPLICIT TYPE CONVERSIONS**
   ```cpp
   Price px = static_cast<Price>(tick.last_price) / 100;
   ```

3. **CACHE ALIGNMENT**
   ```cpp
   alignas(64) Price bid_prices[MAX_LEVELS];
   ```

4. **NO EXCEPTIONS IN HOT PATH**
   - Return codes only
   - Pre-validated data

5. **PERFORMANCE TARGETS**
   - Tick processing: < 100ns
   - Order book update: < 50ns
   - Queue operations: < 45ns

## File Structure (Following Existing Patterns)

```
trading/market_data/zerodha/
├── kite_ws_client.h           # Main WebSocket client
├── kite_ws_client.cpp
├── kite_packet_parser.h       # Binary protocol parser
├── kite_packet_parser.cpp
├── kite_symbol_resolver.h     # Symbol to token mapping
├── kite_symbol_resolver.cpp
├── kite_orderbook_manager.h   # Order book management
├── kite_orderbook_manager.cpp
├── kite_tick_writer.h         # Data persistence
└── kite_tick_writer.cpp
```

## Dependencies

### Existing (Reuse)
- `Common::MemoryPool`
- `Common::LFQueue`
- `Common::Logger`
- `ZerodhaInstrumentFetcher`
- `ZerodhaAuth`

### New (Required)
- libwebsockets (for WebSocket - already used by Binance)
- No other external dependencies

## Risk Mitigation

1. **Binary Protocol Complexity**
   - Start with basic tick parsing
   - Add depth updates incrementally
   
2. **Connection Stability**
   - Implement exponential backoff
   - Queue messages during reconnect
   
3. **Data Integrity**
   - Checksums on persisted data
   - Sequence number tracking

## Success Criteria

1. **Functional**
   - Successfully connects to Kite WebSocket
   - Receives and parses NIFTY50 ticks
   - Maintains accurate order books
   - Persists data without loss

2. **Performance**
   - Tick processing < 100ns
   - Zero heap allocations
   - Handles 10,000 ticks/sec

3. **Reliability**
   - Auto-reconnection works
   - No memory leaks
   - Graceful error handling

## Notes

- NO std::map or dynamic containers
- NO implicit type conversions  
- NO exceptions in trading path
- MUST compile with zero warnings
- MUST pass audit with 0 Tier A violations

---
**Status**: READY FOR IMPLEMENTATION
**Estimated Effort**: 2 weeks
**Priority**: CRITICAL for Zerodha trading