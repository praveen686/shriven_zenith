#pragma once

#include "common/types.h"
#include "common/macros.h"
#include "common/logging.h"
#include <cstring>
#include <vector>
#include <ctime>
#include <chrono>

namespace Trading::MarketData {

// Import types from Common namespace
using Common::Price;
using Common::Qty;
using Common::TickerId;

// Instrument type enumeration
enum class InstrumentType : uint8_t {
    UNKNOWN = 0,
    EQUITY = 1,
    FUTURE = 2,
    OPTION_CALL = 3,
    OPTION_PUT = 4,
    INDEX = 5,
    CURRENCY = 6,
    COMMODITY = 7
};

// Exchange enumeration  
enum class Exchange : uint8_t {
    UNKNOWN = 0,
    NSE = 1,
    BSE = 2,
    NFO = 3,    // NSE F&O
    MCX = 4,
    CDS = 5     // Currency
};

// Fixed-size instrument structure - no dynamic allocation
struct alignas(64) Instrument {
    // Identifiers
    char instrument_token[32]{};     // Exchange instrument ID
    char trading_symbol[32]{};       // Trading symbol (e.g., "NIFTY24DEC25000CE")
    char underlying[32]{};           // Underlying (e.g., "NIFTY")
    char name[64]{};                 // Full name
    
    // Type and exchange
    InstrumentType type{InstrumentType::UNKNOWN};
    Exchange exchange{Exchange::UNKNOWN};
    
    // Contract specifications
    Price tick_size{0};              // Minimum price movement (in paisa)
    Qty lot_size{1};                 // Contract lot size
    Qty multiplier{1};               // Contract multiplier
    
    // For derivatives
    uint64_t expiry_timestamp_ns{0}; // Expiry time in nanoseconds
    Price strike_price{0};            // Strike price for options (in paisa)
    
    // Trading info
    bool is_tradeable{false};
    Price last_price{0};             // Last traded price (in paisa)
    Qty volume{0};                   // Day's volume
    Qty open_interest{0};            // Open interest for F&O
    
    // Price bands
    Price price_band_upper{0};       // Upper circuit limit
    Price price_band_lower{0};       // Lower circuit limit
    Qty max_order_qty{0};            // Max order quantity allowed
    
    // Metadata
    uint64_t last_updated_ns{0};    // Last update timestamp
    
    // Helper methods
    [[nodiscard]] auto isExpired() const noexcept -> bool {
        if (type != InstrumentType::FUTURE && 
            type != InstrumentType::OPTION_CALL && 
            type != InstrumentType::OPTION_PUT) {
            return false;
        }
        
        auto now_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        
        return now_ns > expiry_timestamp_ns;
    }
    
    [[nodiscard]] auto getDaysToExpiry() const noexcept -> int32_t {
        if (expiry_timestamp_ns == 0) return -1;
        
        auto now_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
        
        if (now_ns > expiry_timestamp_ns) return -1;
        
        uint64_t diff_ns = expiry_timestamp_ns - now_ns;
        uint64_t days = diff_ns / (24ULL * 60 * 60 * 1000000000ULL);
        return static_cast<int32_t>(days);
    }
};

// Configuration for instrument fetching
struct InstrumentConfig {
    char symbol[32]{};               // Base symbol (e.g., "NIFTY50")
    InstrumentType type{};           // Type to fetch
    Exchange exchange{};             // Exchange
    uint32_t strike_count{5};        // For options: ±strikes around ATM
    bool fetch_weekly{false};        // Fetch weekly expiries
    bool fetch_monthly{true};        // Fetch monthly expiries
};

// Instrument fetcher interface
class IInstrumentFetcher {
public:
    virtual ~IInstrumentFetcher() = default;
    
    // Fetch all instruments from exchange
    [[nodiscard]] virtual auto fetchAllInstruments(
        char* buffer, 
        size_t buffer_size
    ) noexcept -> bool = 0;
    
    // Parse and store instruments
    [[nodiscard]] virtual auto parseInstruments(
        const char* csv_data,
        size_t data_len
    ) noexcept -> size_t = 0;
    
    // Find instruments by underlying
    [[nodiscard]] virtual auto findByUnderlying(
        const char* underlying,
        Instrument* instruments,
        size_t max_count
    ) noexcept -> size_t = 0;
    
    // Find nearest expiry future
    [[nodiscard]] virtual auto findNearestFuture(
        const char* underlying,
        Instrument& instrument
    ) noexcept -> bool = 0;
    
    // Find option chain for nearest expiry
    [[nodiscard]] virtual auto findOptionChain(
        const char* underlying,
        uint32_t strike_count,
        Instrument* options,
        size_t max_count
    ) noexcept -> size_t = 0;
    
    // Get spot instrument
    [[nodiscard]] virtual auto findSpot(
        const char* symbol,
        Instrument& instrument
    ) noexcept -> bool = 0;
    
    // Save to CSV file
    [[nodiscard]] virtual auto saveToCSV(
        const char* filepath
    ) noexcept -> bool = 0;
    
    // Load from CSV file
    [[nodiscard]] virtual auto loadFromCSV(
        const char* filepath
    ) noexcept -> bool = 0;
    
    // Get instrument count
    [[nodiscard]] virtual auto getInstrumentCount() const noexcept -> size_t = 0;
    
    // Get last update time
    [[nodiscard]] virtual auto getLastUpdateTime() const noexcept -> uint64_t = 0;
};

} // namespace Trading::MarketData