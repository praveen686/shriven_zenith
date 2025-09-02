// ============================================================================
// binance_instrument_fetcher.h - Binance Symbol/Instrument Fetcher
// ============================================================================

#pragma once

#include "common/types.h"
#include "common/macros.h"
#include "trading/auth/binance/binance_auth.h"
#include <cstring>
#include <atomic>

namespace Trading::MarketData::Binance {

// ============================================================================
// Binance Symbol Structure
// ============================================================================
struct Symbol {
    char symbol[20];           // e.g., "BTCUSDT"
    char base_asset[10];       // e.g., "BTC"
    char quote_asset[10];      // e.g., "USDT"
    char status[16];           // "TRADING", "BREAK", etc.
    
    // Trading rules
    double min_price;
    double max_price;
    double tick_size;          // Price precision
    
    double min_qty;
    double max_qty;
    double step_size;          // Quantity precision
    
    double min_notional;       // Minimum order value
    
    // Permissions
    bool spot_trading;
    bool margin_trading;
    
    // Market data
    double last_price;         // From 24hr ticker
    double volume_24h;         // 24hr volume in quote asset
    double quote_volume_24h;   // 24hr volume in quote currency
    
    Symbol() {
        memset(symbol, 0, sizeof(symbol));
        memset(base_asset, 0, sizeof(base_asset));
        memset(quote_asset, 0, sizeof(quote_asset));
        memset(status, 0, sizeof(status));
        
        min_price = 0.0;
        max_price = 0.0;
        tick_size = 0.0;
        
        min_qty = 0.0;
        max_qty = 0.0;
        step_size = 0.0;
        
        min_notional = 0.0;
        
        spot_trading = false;
        margin_trading = false;
        
        last_price = 0.0;
        volume_24h = 0.0;
        quote_volume_24h = 0.0;
    }
    
    // Check if symbol is tradeable
    bool isTradeable() const noexcept {
        return strcmp(status, "TRADING") == 0 && spot_trading;
    }
    
    // Check if symbol matches a base asset
    bool hasBaseAsset(const char* asset) const noexcept {
        return strcmp(base_asset, asset) == 0;
    }
    
    // Check if symbol matches a quote asset
    bool hasQuoteAsset(const char* asset) const noexcept {
        return strcmp(quote_asset, asset) == 0;
    }
};

// ============================================================================
// Binance Instrument Fetcher
// ============================================================================
class BinanceInstrumentFetcher {
private:
    // Maximum symbols we can handle
    static constexpr size_t MAX_SYMBOLS = 5000;
    
    // Static storage for symbols (no dynamic allocation)
    static Symbol symbols_[MAX_SYMBOLS];
    
    // Current number of symbols
    std::atomic<size_t> symbol_count_{0};
    
    // Last update timestamp
    std::atomic<uint64_t> last_update_time_ns_{0};
    
    // Binance auth instance
    Trading::Binance::BinanceAuth* auth_;
    
    // Parse exchange info JSON response
    size_t parseExchangeInfo(const char* json_data, size_t json_len) noexcept;
    
    // Parse ticker data and update prices
    size_t parseTicker24hr(const char* json_data, size_t json_len) noexcept;
    
    // Helper to parse a single symbol from JSON
    bool parseSymbol(const char* symbol_json, Symbol& symbol) noexcept;
    
public:
    explicit BinanceInstrumentFetcher(Trading::Binance::BinanceAuth* auth) noexcept 
        : auth_(auth) {}
    
    // Fetch all symbols from Binance
    bool fetchAllSymbols(char* buffer, size_t buffer_size) noexcept;
    
    // Fetch only top symbols (more efficient for production)
    bool fetchTopSymbols(char* buffer, size_t buffer_size) noexcept;
    
    // Update market data (prices, volumes)
    bool updateMarketData(char* buffer, size_t buffer_size) noexcept;
    
    // Save symbols to CSV file
    bool saveToCSV(const char* filename) const noexcept;
    
    // Load symbols from CSV file
    bool loadFromCSV(const char* filename) noexcept;
    
    // Get symbol count
    size_t getSymbolCount() const noexcept { return symbol_count_.load(); }
    
    // Find symbol by name
    bool findSymbol(const char* symbol_name, Symbol& out_symbol) const noexcept;
    
    // Find all symbols with base asset
    size_t findByBaseAsset(const char* base_asset, Symbol* out_symbols, size_t max_count) const noexcept;
    
    // Find all symbols with quote asset
    size_t findByQuoteAsset(const char* quote_asset, Symbol* out_symbols, size_t max_count) const noexcept;
    
    // Find most liquid USDT pairs
    size_t findTopUSDTPairs(Symbol* out_symbols, size_t max_count) const noexcept;
    
    // Get symbol by index
    const Symbol* getSymbol(size_t index) const noexcept {
        if (index >= symbol_count_.load()) return nullptr;
        return &symbols_[index];
    }
    
    // Get last update time
    uint64_t getLastUpdateTimeNs() const noexcept { return last_update_time_ns_.load(); }
    
    // Clear all symbols
    void clear() noexcept {
        symbol_count_.store(0);
        last_update_time_ns_.store(0);
    }
};

// Helper function to fetch and cache symbols
bool fetchAndCacheSymbols(BinanceInstrumentFetcher* fetcher) noexcept;

} // namespace Trading::MarketData::Binance