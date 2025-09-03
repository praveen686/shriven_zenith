#pragma once

#include "common/types.h"
#include "common/macros.h"
#include <atomic>
#include <cstring>

namespace Trading::MarketData {

using namespace Common;

// Fixed-size order book with zero allocation
template<size_t MAX_LEVELS = 20>
class OrderBook {
public:
    OrderBook() noexcept {
        reset();
    }
    
    // Reset order book to empty state
    auto reset() noexcept -> void {
        instrument_token_ = 0;
        ticker_id_ = TickerId_INVALID;
        bid_depth_ = 0;
        ask_depth_ = 0;
        last_update_ns_.store(0);
        total_bid_qty_ = 0;
        total_ask_qty_ = 0;
        
        std::memset(bid_prices_, 0, sizeof(bid_prices_));
        std::memset(bid_qtys_, 0, sizeof(bid_qtys_));
        std::memset(bid_orders_, 0, sizeof(bid_orders_));
        std::memset(ask_prices_, 0, sizeof(ask_prices_));
        std::memset(ask_qtys_, 0, sizeof(ask_qtys_));
        std::memset(ask_orders_, 0, sizeof(ask_orders_));
    }
    
    // Update bid side - O(1) for specific level
    [[gnu::always_inline]]
    inline auto updateBid(Price price, Qty qty, uint16_t orders, uint8_t level) noexcept -> void {
        if (level < MAX_LEVELS) {
            bid_prices_[level] = price;
            bid_qtys_[level] = qty;
            bid_orders_[level] = orders;
            if (level >= bid_depth_) {
                bid_depth_ = level + 1;
            }
            recalculateBidQty();
        }
    }
    
    // Update ask side - O(1) for specific level
    [[gnu::always_inline]]
    inline auto updateAsk(Price price, Qty qty, uint16_t orders, uint8_t level) noexcept -> void {
        if (level < MAX_LEVELS) {
            ask_prices_[level] = price;
            ask_qtys_[level] = qty;
            ask_orders_[level] = orders;
            if (level >= ask_depth_) {
                ask_depth_ = level + 1;
            }
            recalculateAskQty();
        }
    }
    
    // Clear bid levels
    auto clearBids() noexcept -> void {
        bid_depth_ = 0;
        total_bid_qty_ = 0;
        std::memset(bid_prices_, 0, sizeof(bid_prices_));
        std::memset(bid_qtys_, 0, sizeof(bid_qtys_));
        std::memset(bid_orders_, 0, sizeof(bid_orders_));
    }
    
    // Clear ask levels
    auto clearAsks() noexcept -> void {
        ask_depth_ = 0;
        total_ask_qty_ = 0;
        std::memset(ask_prices_, 0, sizeof(ask_prices_));
        std::memset(ask_qtys_, 0, sizeof(ask_qtys_));
        std::memset(ask_orders_, 0, sizeof(ask_orders_));
    }
    
    // Getters - all O(1)
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getBestBid() const noexcept -> Price {
        return bid_depth_ > 0 ? bid_prices_[0] : Price_INVALID;
    }
    
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getBestAsk() const noexcept -> Price {
        return ask_depth_ > 0 ? ask_prices_[0] : Price_INVALID;
    }
    
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getBestBidQty() const noexcept -> Qty {
        return bid_depth_ > 0 ? bid_qtys_[0] : 0;
    }
    
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getBestAskQty() const noexcept -> Qty {
        return ask_depth_ > 0 ? ask_qtys_[0] : 0;
    }
    
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getSpread() const noexcept -> Price {
        if (bid_depth_ > 0 && ask_depth_ > 0) {
            return ask_prices_[0] - bid_prices_[0];
        }
        return Price_INVALID;
    }
    
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getMidPrice() const noexcept -> Price {
        if (bid_depth_ > 0 && ask_depth_ > 0) {
            return (bid_prices_[0] + ask_prices_[0]) / 2;
        }
        return Price_INVALID;
    }
    
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getTotalBidQty() const noexcept -> Qty {
        return total_bid_qty_;
    }
    
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getTotalAskQty() const noexcept -> Qty {
        return total_ask_qty_;
    }
    
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getImbalance() const noexcept -> double {
        if (total_bid_qty_ + total_ask_qty_ == 0) return 0.0;
        return static_cast<double>(total_bid_qty_ - total_ask_qty_) / 
               static_cast<double>(total_bid_qty_ + total_ask_qty_);
    }
    
    // Get specific level
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getBidLevel(uint8_t level) const noexcept -> std::tuple<Price, Qty, uint16_t> {
        if (level < bid_depth_) {
            return {bid_prices_[level], bid_qtys_[level], bid_orders_[level]};
        }
        return {Price_INVALID, 0, 0};
    }
    
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getAskLevel(uint8_t level) const noexcept -> std::tuple<Price, Qty, uint16_t> {
        if (level < ask_depth_) {
            return {ask_prices_[level], ask_qtys_[level], ask_orders_[level]};
        }
        return {Price_INVALID, 0, 0};
    }
    
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getBidDepth() const noexcept -> uint8_t { return bid_depth_; }
    
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getAskDepth() const noexcept -> uint8_t { return ask_depth_; }
    
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getLastUpdateNs() const noexcept -> uint64_t { 
        return last_update_ns_.load(std::memory_order_acquire); 
    }
    
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getInstrumentToken() const noexcept -> uint32_t { return instrument_token_; }
    
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getTickerId() const noexcept -> TickerId { return ticker_id_; }
    
    // Setters
    auto setInstrumentToken(uint32_t token) noexcept -> void { instrument_token_ = token; }
    auto setTickerId(TickerId id) noexcept -> void { ticker_id_ = id; }
    auto updateTimestamp(uint64_t ns) noexcept -> void { 
        last_update_ns_.store(ns, std::memory_order_release); 
    }
    
    // Snapshot for persistence
    struct Snapshot {
        uint32_t instrument_token;
        TickerId ticker_id;
        uint64_t timestamp_ns;
        uint8_t bid_depth;
        uint8_t ask_depth;
        Price bid_prices[MAX_LEVELS];
        Qty bid_qtys[MAX_LEVELS];
        uint16_t bid_orders[MAX_LEVELS];
        Price ask_prices[MAX_LEVELS];
        Qty ask_qtys[MAX_LEVELS];
        uint16_t ask_orders[MAX_LEVELS];
        Qty total_bid_qty;
        Qty total_ask_qty;
    };
    
    [[nodiscard]] auto getSnapshot() const noexcept -> Snapshot {
        Snapshot snap;
        snap.instrument_token = instrument_token_;
        snap.ticker_id = ticker_id_;
        snap.timestamp_ns = last_update_ns_.load(std::memory_order_acquire);
        snap.bid_depth = bid_depth_;
        snap.ask_depth = ask_depth_;
        snap.total_bid_qty = total_bid_qty_;
        snap.total_ask_qty = total_ask_qty_;
        
        std::memcpy(snap.bid_prices, bid_prices_, sizeof(bid_prices_));
        std::memcpy(snap.bid_qtys, bid_qtys_, sizeof(bid_qtys_));
        std::memcpy(snap.bid_orders, bid_orders_, sizeof(bid_orders_));
        std::memcpy(snap.ask_prices, ask_prices_, sizeof(ask_prices_));
        std::memcpy(snap.ask_qtys, ask_qtys_, sizeof(ask_qtys_));
        std::memcpy(snap.ask_orders, ask_orders_, sizeof(ask_orders_));
        
        return snap;
    }
    
    auto loadSnapshot(const Snapshot& snap) noexcept -> void {
        instrument_token_ = snap.instrument_token;
        ticker_id_ = snap.ticker_id;
        last_update_ns_.store(snap.timestamp_ns, std::memory_order_release);
        bid_depth_ = snap.bid_depth;
        ask_depth_ = snap.ask_depth;
        total_bid_qty_ = snap.total_bid_qty;
        total_ask_qty_ = snap.total_ask_qty;
        
        std::memcpy(bid_prices_, snap.bid_prices, sizeof(bid_prices_));
        std::memcpy(bid_qtys_, snap.bid_qtys, sizeof(bid_qtys_));
        std::memcpy(bid_orders_, snap.bid_orders, sizeof(bid_orders_));
        std::memcpy(ask_prices_, snap.ask_prices, sizeof(ask_prices_));
        std::memcpy(ask_qtys_, snap.ask_qtys, sizeof(ask_qtys_));
        std::memcpy(ask_orders_, snap.ask_orders, sizeof(ask_orders_));
    }
    
private:
    // Cache-aligned arrays for performance
    alignas(CACHE_LINE_SIZE) Price bid_prices_[MAX_LEVELS];
    alignas(CACHE_LINE_SIZE) Qty bid_qtys_[MAX_LEVELS];
    alignas(CACHE_LINE_SIZE) uint16_t bid_orders_[MAX_LEVELS];
    alignas(CACHE_LINE_SIZE) Price ask_prices_[MAX_LEVELS];
    alignas(CACHE_LINE_SIZE) Qty ask_qtys_[MAX_LEVELS];
    alignas(CACHE_LINE_SIZE) uint16_t ask_orders_[MAX_LEVELS];
    
    // Metadata
    uint32_t instrument_token_;
    TickerId ticker_id_;
    uint8_t bid_depth_;
    uint8_t ask_depth_;
    Qty total_bid_qty_;
    Qty total_ask_qty_;
    
    // Atomic timestamp for thread safety
    std::atomic<uint64_t> last_update_ns_;
    
    // Helper methods
    auto recalculateBidQty() noexcept -> void {
        total_bid_qty_ = 0;
        for (uint8_t i = 0; i < bid_depth_; ++i) {
            total_bid_qty_ += bid_qtys_[i];
        }
    }
    
    auto recalculateAskQty() noexcept -> void {
        total_ask_qty_ = 0;
        for (uint8_t i = 0; i < ask_depth_; ++i) {
            total_ask_qty_ += ask_qtys_[i];
        }
    }
};

// Manager for multiple order books
template<size_t MAX_INSTRUMENTS = 1000>
class OrderBookManager {
public:
    OrderBookManager() noexcept {
        // Initialize token to index mapping
        for (size_t i = 0; i < MAX_TOKEN_VALUE; ++i) {
            token_to_index_[i] = INVALID_INDEX;
        }
        next_index_ = 0;
    }
    
    // Register an instrument and get its order book
    auto registerInstrument(uint32_t token, TickerId ticker_id) noexcept -> OrderBook<20>* {
        if (token >= MAX_TOKEN_VALUE || next_index_ >= MAX_INSTRUMENTS) {
            return nullptr;
        }
        
        // Check if already registered
        if (token_to_index_[token] != INVALID_INDEX) {
            return &order_books_[token_to_index_[token]];
        }
        
        // Register new instrument
        size_t index = next_index_++;
        token_to_index_[token] = static_cast<uint32_t>(index);
        
        auto& book = order_books_[index];
        book.reset();
        book.setInstrumentToken(token);
        book.setTickerId(ticker_id);
        
        LOG_INFO("Registered order book for token %u at index %zu", token, index);
        return &book;
    }
    
    // Get order book by token - O(1)
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getOrderBook(uint32_t token) noexcept -> OrderBook<20>* {
        if (token < MAX_TOKEN_VALUE && token_to_index_[token] != INVALID_INDEX) {
            return &order_books_[token_to_index_[token]];
        }
        return nullptr;
    }
    
    [[nodiscard]] [[gnu::always_inline]]
    inline auto getOrderBookConst(uint32_t token) const noexcept -> const OrderBook<20>* {
        if (token < MAX_TOKEN_VALUE && token_to_index_[token] != INVALID_INDEX) {
            return &order_books_[token_to_index_[token]];
        }
        return nullptr;
    }
    
    // Get active order books - returns count and fills provided array
    struct ActiveBooks {
        const OrderBook<20>* books[MAX_INSTRUMENTS];
        size_t count;
    };
    
    [[nodiscard]] auto getActiveBooks(ActiveBooks& active) const noexcept -> size_t {
        active.count = 0;
        for (size_t i = 0; i < next_index_ && active.count < MAX_INSTRUMENTS; ++i) {
            active.books[active.count++] = &order_books_[i];
        }
        return active.count;
    }
    
    [[nodiscard]] auto getBookCount() const noexcept -> size_t {
        return next_index_;
    }
    
private:
    static constexpr uint32_t MAX_TOKEN_VALUE = 100000;
    static constexpr uint32_t INVALID_INDEX = std::numeric_limits<uint32_t>::max();
    
    // Fixed-size storage
    OrderBook<20> order_books_[MAX_INSTRUMENTS];
    
    // Direct index mapping for O(1) access
    uint32_t token_to_index_[MAX_TOKEN_VALUE];
    
    std::atomic<size_t> next_index_;
};

} // namespace Trading::MarketData