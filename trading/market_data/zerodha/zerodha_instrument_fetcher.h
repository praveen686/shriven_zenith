#pragma once

#include "trading/market_data/instrument_fetcher.h"
#include "trading/auth/zerodha/zerodha_auth.h"
#include "common/mem_pool.h"

namespace Trading::MarketData::Zerodha {

// Maximum instruments we can handle
constexpr size_t MAX_INSTRUMENTS = 100000;
constexpr size_t MAX_OPTION_CHAIN = 100;

class ZerodhaInstrumentFetcher final : public IInstrumentFetcher {
private:
    // Fixed-size storage - static to avoid stack overflow
    static Instrument instruments_[MAX_INSTRUMENTS];
    size_t instrument_count_{0};
    uint64_t last_update_time_ns_{0};
    
    // Zerodha auth instance
    Trading::Zerodha::ZerodhaAuth* auth_{nullptr};
    
    // Helper to normalize symbol names for fuzzy matching
    static auto normalizeSymbol(const char* symbol, char* normalized, size_t len) noexcept -> void {
        if (!symbol || !normalized || len == 0) return;
        
        size_t j = 0;
        for (size_t i = 0; symbol[i] && j < len - 1; ++i) {
            char c = symbol[i];
            // Convert to uppercase and skip spaces
            if (c != ' ') {
                normalized[j++] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
            }
        }
        normalized[j] = '\0';
        
        // Handle common variations
        if (strncmp(normalized, "NIFTY50", 7) == 0) {
            strncpy(normalized, "NIFTY", len - 1);
        } else if (strncmp(normalized, "NIFTYBANK", 9) == 0) {
            strncpy(normalized, "BANKNIFTY", len - 1);
        }
    }
    
    // Fuzzy match for symbols
    static auto fuzzyMatch(const char* s1, const char* s2) noexcept -> bool {
        char norm1[32], norm2[32];
        normalizeSymbol(s1, norm1, sizeof(norm1));
        normalizeSymbol(s2, norm2, sizeof(norm2));
        
        // Check if one contains the other
        return strstr(norm1, norm2) != nullptr || strstr(norm2, norm1) != nullptr;
    }
    
    // Parse expiry date from YYYY-MM-DD format
    static auto parseExpiryDate(const char* date_str) noexcept -> uint64_t {
        if (!date_str || !date_str[0]) return 0;
        
        struct tm tm{};
        if (sscanf(date_str, "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3) {
            tm.tm_year -= 1900;  // Years since 1900
            tm.tm_mon -= 1;       // 0-based month
            tm.tm_hour = 15;      // Market close at 3:30 PM
            tm.tm_min = 30;
            
            time_t expiry_time = mktime(&tm);
            if (expiry_time != -1) {
                return static_cast<uint64_t>(expiry_time) * 1000000000ULL;
            }
        }
        return 0;
    }
    
    // Parse instrument type from string
    static auto parseInstrumentType(const char* type_str) noexcept -> InstrumentType {
        if (!type_str) return InstrumentType::UNKNOWN;
        
        if (strcmp(type_str, "EQ") == 0) return InstrumentType::EQUITY;
        if (strcmp(type_str, "FUT") == 0) return InstrumentType::FUTURE;
        if (strcmp(type_str, "CE") == 0) return InstrumentType::OPTION_CALL;
        if (strcmp(type_str, "PE") == 0) return InstrumentType::OPTION_PUT;
        if (strcmp(type_str, "INDEX") == 0) return InstrumentType::INDEX;
        
        return InstrumentType::UNKNOWN;
    }
    
    // Parse exchange from string
    static auto parseExchange(const char* exchange_str) noexcept -> Exchange {
        if (!exchange_str) return Exchange::UNKNOWN;
        
        if (strcmp(exchange_str, "NSE") == 0) return Exchange::NSE;
        if (strcmp(exchange_str, "BSE") == 0) return Exchange::BSE;
        if (strcmp(exchange_str, "NFO") == 0) return Exchange::NFO;
        if (strcmp(exchange_str, "MCX") == 0) return Exchange::MCX;
        if (strcmp(exchange_str, "CDS") == 0) return Exchange::CDS;
        
        return Exchange::UNKNOWN;
    }
    
public:
    explicit ZerodhaInstrumentFetcher(Trading::Zerodha::ZerodhaAuth* auth) noexcept 
        : auth_(auth) {
        LOG_INFO("ZerodhaInstrumentFetcher initialized");
    }
    
    ~ZerodhaInstrumentFetcher() noexcept override = default;
    
    // Fetch all instruments from Zerodha
    [[nodiscard]] auto fetchAllInstruments(
        char* buffer, 
        size_t buffer_size
    ) noexcept -> bool override;
    
    // Parse CSV data into instruments
    [[nodiscard]] auto parseInstruments(
        const char* csv_data,
        size_t data_len
    ) noexcept -> size_t override {
        if (!csv_data || data_len == 0) return 0;
        
        instrument_count_ = 0;
        const char* line_start = csv_data;
        const char* line_end = nullptr;
        bool skip_header = true;
        
        while ((line_end = strchr(line_start, '\n')) != nullptr && 
               instrument_count_ < MAX_INSTRUMENTS) {
            
            if (skip_header) {
                skip_header = false;
                line_start = line_end + 1;
                continue;
            }
            
            // Parse CSV line
            // Format: instrument_token,exchange_token,tradingsymbol,name,last_price,
            //         expiry,strike,tick_size,lot_size,instrument_type,segment,exchange
            
            Instrument& inst = instruments_[instrument_count_];
            inst = Instrument{};  // Value-initialize instead of memset
            
            // Simple CSV parsing (would be more robust in production)
            char line[1024];
            size_t line_len = static_cast<size_t>(line_end - line_start);
            if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
            memcpy(line, line_start, line_len);
            line[line_len] = '\0';
            
            // Parse fields (simplified - production would handle quotes, escapes, etc.)
            char* fields[20];
            int field_count = 0;
            char* token = strtok(line, ",");
            while (token && field_count < 20) {
                fields[field_count++] = token;
                token = strtok(nullptr, ",");
            }
            
            if (field_count >= 12) {
                // Copy basic fields
                strncpy(inst.instrument_token, fields[0], sizeof(inst.instrument_token) - 1);
                strncpy(inst.trading_symbol, fields[2], sizeof(inst.trading_symbol) - 1);
                strncpy(inst.name, fields[3], sizeof(inst.name) - 1);
                
                // Parse numeric fields
                inst.last_price = static_cast<Price>(atof(fields[4]) * 100);  // Convert to paisa
                inst.expiry_timestamp_ns = parseExpiryDate(fields[5]);
                inst.strike_price = static_cast<Price>(atof(fields[6]) * 100);
                inst.tick_size = static_cast<Price>(atof(fields[7]) * 100);
                inst.lot_size = static_cast<Qty>(atoi(fields[8]));
                
                // Parse type and exchange
                inst.type = parseInstrumentType(fields[9]);
                inst.exchange = parseExchange(fields[11]);
                
                // Extract underlying from trading symbol
                // For derivatives, underlying is usually the base symbol
                if (inst.type == InstrumentType::FUTURE || 
                    inst.type == InstrumentType::OPTION_CALL ||
                    inst.type == InstrumentType::OPTION_PUT) {
                    // Extract base symbol (e.g., "NIFTY" from "NIFTY25SEPFUT")
                    // Look for first digit or known date patterns
                    size_t i = 0;
                    size_t max_len = strlen(inst.trading_symbol);
                    
                    // Copy until we hit a digit or year pattern (2X for 20XX)
                    while (i < max_len && i < sizeof(inst.underlying) - 1) {
                        char c = inst.trading_symbol[i];
                        
                        // Stop at digit, but handle special cases
                        if (c >= '0' && c <= '9') {
                            // Check if this might be part of the symbol (like NIFTY50)
                            if (i > 0 && inst.trading_symbol[i-1] >= 'A' && inst.trading_symbol[i-1] <= 'Z') {
                                // Check next few chars - if they're also digits followed by letters, it's part of name
                                if (i + 1 < max_len && inst.trading_symbol[i+1] >= '0' && inst.trading_symbol[i+1] <= '9') {
                                    // Likely a year like "25"
                                    break;
                                }
                                // Single digit might be part of symbol
                                inst.underlying[i] = c;
                                i++;
                            } else {
                                break;
                            }
                        } else {
                            inst.underlying[i] = c;
                            i++;
                        }
                    }
                    inst.underlying[i] = '\0';
                    
                    // Special handling for common indices
                    if (strncmp(inst.underlying, "NIFTY", 5) == 0 && inst.underlying[5] == '\0') {
                        // Just "NIFTY" without any suffix
                    } else if (strncmp(inst.underlying, "BANKNIFTY", 9) == 0 && inst.underlying[9] == '\0') {
                        // Just "BANKNIFTY" without any suffix  
                    }
                } else {
                    strncpy(inst.underlying, inst.trading_symbol, sizeof(inst.underlying) - 1);
                }
                
                inst.is_tradeable = true;
                inst.last_updated_ns = last_update_time_ns_;
                
                instrument_count_++;
            }
            
            line_start = line_end + 1;
        }
        
        LOG_INFO("Parsed %zu instruments", instrument_count_);
        return instrument_count_;
    }
    
    // Find instruments by underlying
    [[nodiscard]] auto findByUnderlying(
        const char* underlying,
        Instrument* instruments,
        size_t max_count
    ) noexcept -> size_t override {
        if (!underlying || !instruments || max_count == 0) return 0;
        
        size_t found = 0;
        char normalized[32];
        normalizeSymbol(underlying, normalized, sizeof(normalized));
        
        for (size_t i = 0; i < instrument_count_ && found < max_count; ++i) {
            if (fuzzyMatch(instruments_[i].underlying, normalized)) {
                instruments[found++] = instruments_[i];
            }
        }
        
        LOG_INFO("Found %zu instruments for underlying: %s", found, underlying);
        return found;
    }
    
    // Find nearest expiry future
    [[nodiscard]] auto findNearestFuture(
        const char* underlying,
        Instrument& instrument
    ) noexcept -> bool override {
        if (!underlying) return false;
        
        char normalized[32];
        normalizeSymbol(underlying, normalized, sizeof(normalized));
        
        bool found = false;
        int32_t min_days = INT32_MAX;
        
        for (size_t i = 0; i < instrument_count_; ++i) {
            const auto& inst = instruments_[i];
            
            if (inst.type == InstrumentType::FUTURE &&
                inst.exchange == Exchange::NFO &&
                fuzzyMatch(inst.underlying, normalized)) {
                
                int32_t days = inst.getDaysToExpiry();
                if (days >= 0 && days < min_days) {
                    min_days = days;
                    instrument = inst;
                    found = true;
                }
            }
        }
        
        if (found) {
            LOG_INFO("Found nearest future for %s: %s expiring in %d days",
                    underlying, instrument.trading_symbol, min_days);
        } else {
            LOG_WARN("No future found for %s", underlying);
        }
        
        return found;
    }
    
    // Find option chain for nearest expiry
    [[nodiscard]] auto findOptionChain(
        const char* underlying,
        uint32_t strike_count,
        Instrument* options,
        size_t max_count
    ) noexcept -> size_t override {
        if (!underlying || !options || max_count == 0) return 0;
        
        // First find the spot price to determine ATM
        Instrument spot;
        if (!findSpot(underlying, spot)) {
            LOG_WARN("Could not find spot price for %s", underlying);
            return 0;
        }
        
        Price atm_price = spot.last_price;
        
        // Find nearest expiry
        uint64_t nearest_expiry = UINT64_MAX;
        for (size_t i = 0; i < instrument_count_; ++i) {
            const auto& inst = instruments_[i];
            if ((inst.type == InstrumentType::OPTION_CALL || 
                 inst.type == InstrumentType::OPTION_PUT) &&
                fuzzyMatch(inst.underlying, underlying) &&
                !inst.isExpired()) {
                
                if (inst.expiry_timestamp_ns < nearest_expiry) {
                    nearest_expiry = inst.expiry_timestamp_ns;
                }
            }
        }
        
        if (nearest_expiry == UINT64_MAX) {
            LOG_WARN("No options found for %s", underlying);
            return 0;
        }
        
        // Collect options for nearest expiry around ATM
        size_t found = 0;
        constexpr size_t MAX_OPTIONS_PER_EXPIRY = 1000;
        size_t option_indices[MAX_OPTIONS_PER_EXPIRY];
        size_t option_count = 0;
        
        for (size_t i = 0; i < instrument_count_ && option_count < MAX_OPTIONS_PER_EXPIRY; ++i) {
            const auto& inst = instruments_[i];
            if ((inst.type == InstrumentType::OPTION_CALL || 
                 inst.type == InstrumentType::OPTION_PUT) &&
                inst.expiry_timestamp_ns == nearest_expiry &&
                fuzzyMatch(inst.underlying, underlying)) {
                
                option_indices[option_count++] = i;
            }
        }
        
        // Simple bubble sort by strike price (sufficient for small arrays)
        for (size_t i = 0; i < option_count - 1; ++i) {
            for (size_t j = 0; j < option_count - i - 1; ++j) {
                if (instruments_[option_indices[j]].strike_price > 
                    instruments_[option_indices[j + 1]].strike_price) {
                    size_t temp = option_indices[j];
                    option_indices[j] = option_indices[j + 1];
                    option_indices[j + 1] = temp;
                }
            }
        }
        
        // Find ATM strike index
        size_t atm_index = 0;
        Price min_diff = INT64_MAX;
        for (size_t i = 0; i < option_count; ++i) {
            Price diff = (instruments_[option_indices[i]].strike_price > atm_price) ?
                        (instruments_[option_indices[i]].strike_price - atm_price) :
                        (atm_price - instruments_[option_indices[i]].strike_price);
            if (diff < min_diff) {
                min_diff = diff;
                atm_index = i;
            }
        }
        
        // Collect strikes around ATM
        int start = static_cast<int>(atm_index) - static_cast<int>(strike_count);
        int end = static_cast<int>(atm_index) + static_cast<int>(strike_count) + 1;
        
        if (start < 0) start = 0;
        if (end > static_cast<int>(option_count)) {
            end = static_cast<int>(option_count);
        }
        
        for (int i = start; i < end && found < max_count; ++i) {
            options[found++] = instruments_[option_indices[static_cast<size_t>(i)]];
        }
        
        LOG_INFO("Found %zu options for %s around ATM", found, underlying);
        return found;
    }
    
    // Get spot instrument
    [[nodiscard]] auto findSpot(
        const char* symbol,
        Instrument& instrument
    ) noexcept -> bool override {
        if (!symbol) return false;
        
        char normalized[32];
        normalizeSymbol(symbol, normalized, sizeof(normalized));
        
        for (size_t i = 0; i < instrument_count_; ++i) {
            const auto& inst = instruments_[i];
            
            if ((inst.type == InstrumentType::EQUITY || inst.type == InstrumentType::INDEX) &&
                (inst.exchange == Exchange::NSE || inst.exchange == Exchange::BSE) &&
                fuzzyMatch(inst.trading_symbol, normalized)) {
                
                instrument = inst;
                LOG_INFO("Found spot for %s: %s at %.2f", 
                        symbol, inst.trading_symbol, static_cast<double>(inst.last_price) / 100.0);
                return true;
            }
        }
        
        LOG_WARN("No spot instrument found for %s", symbol);
        return false;
    }
    
    // Save to CSV
    [[nodiscard]] auto saveToCSV(const char* filepath) noexcept -> bool override {
        FILE* file = fopen(filepath, "w");  // AUDIT_IGNORE: Init-time only
        if (!file) {
            LOG_ERROR("Failed to open file for writing: %s", filepath);
            return false;
        }
        
        // Write header
        fprintf(file, "instrument_token,trading_symbol,underlying,name,type,exchange,"
                     "tick_size,lot_size,expiry,strike,last_price,volume,oi\n");
        
        // Write instruments
        for (size_t i = 0; i < instrument_count_; ++i) {
            const auto& inst = instruments_[i];
            
            const char* type_str = "UNKNOWN";
            switch (inst.type) {
                case InstrumentType::EQUITY: type_str = "EQ"; break;
                case InstrumentType::FUTURE: type_str = "FUT"; break;
                case InstrumentType::OPTION_CALL: type_str = "CE"; break;
                case InstrumentType::OPTION_PUT: type_str = "PE"; break;
                case InstrumentType::INDEX: type_str = "INDEX"; break;
                default: break;
            }
            
            const char* exchange_str = "UNKNOWN";
            switch (inst.exchange) {
                case Exchange::NSE: exchange_str = "NSE"; break;
                case Exchange::BSE: exchange_str = "BSE"; break;
                case Exchange::NFO: exchange_str = "NFO"; break;
                case Exchange::MCX: exchange_str = "MCX"; break;
                case Exchange::CDS: exchange_str = "CDS"; break;
                default: break;
            }
            
            fprintf(file, "%s,%s,%s,%s,%s,%s,%.2f,%lu,%llu,%.2f,%.2f,%lu,%lu\n",
                   inst.instrument_token,
                   inst.trading_symbol,
                   inst.underlying,
                   inst.name,
                   type_str,
                   exchange_str,
                   static_cast<double>(inst.tick_size) / 100.0,
                   inst.lot_size,
                   static_cast<unsigned long long>(inst.expiry_timestamp_ns),
                   static_cast<double>(inst.strike_price) / 100.0,
                   static_cast<double>(inst.last_price) / 100.0,
                   inst.volume,
                   inst.open_interest);
        }
        
        fclose(file);
        LOG_INFO("Saved %zu instruments to %s", instrument_count_, filepath);
        return true;
    }
    
    // Load from CSV
    [[nodiscard]] auto loadFromCSV(const char* filepath) noexcept -> bool override {
        FILE* file = fopen(filepath, "r");  // AUDIT_IGNORE: Init-time only
        if (!file) {
            LOG_ERROR("Failed to open file for reading: %s", filepath);
            return false;
        }
        
        char line[1024];
        instrument_count_ = 0;
        
        // Skip header
        if (!fgets(line, sizeof(line), file)) {
            fclose(file);
            return false;
        }
        
        // Read instruments
        while (fgets(line, sizeof(line), file) && instrument_count_ < MAX_INSTRUMENTS) {
            Instrument& inst = instruments_[instrument_count_];
            inst = Instrument{};  // Value-initialize instead of memset
            
            char type_str[16], exchange_str[16];
            double tick_size, strike, last_price;
            unsigned long long expiry_ns;
            
            int fields = sscanf(line, "%31[^,],%31[^,],%31[^,],%63[^,],%15[^,],%15[^,],"
                                     "%lf,%lu,%llu,%lf,%lf,%lu,%lu",
                               inst.instrument_token,
                               inst.trading_symbol,
                               inst.underlying,
                               inst.name,
                               type_str,
                               exchange_str,
                               &tick_size,
                               &inst.lot_size,
                               &expiry_ns,
                               &strike,
                               &last_price,
                               &inst.volume,
                               &inst.open_interest);
            
            if (fields >= 11) {
                inst.type = parseInstrumentType(type_str);
                inst.exchange = parseExchange(exchange_str);
                inst.tick_size = static_cast<Price>(tick_size * 100);
                inst.expiry_timestamp_ns = expiry_ns;
                inst.strike_price = static_cast<Price>(strike * 100);
                inst.last_price = static_cast<Price>(last_price * 100);
                inst.is_tradeable = true;
                inst.last_updated_ns = last_update_time_ns_;
                
                instrument_count_++;
            }
        }
        
        fclose(file);
        LOG_INFO("Loaded %zu instruments from %s", instrument_count_, filepath);
        return true;
    }
    
    // Get instrument count
    [[nodiscard]] auto getInstrumentCount() const noexcept -> size_t override {
        return instrument_count_;
    }
    
    // Get instrument by index
    [[nodiscard]] auto getInstrument(size_t index) const noexcept -> const Instrument* {
        if (index < instrument_count_) {
            return &instruments_[index];
        }
        return nullptr;
    }
    
    // Get last update time
    [[nodiscard]] auto getLastUpdateTime() const noexcept -> uint64_t override {
        return last_update_time_ns_;
    }
    
    // Count instruments by type
    struct TypeCounts {
        size_t equity = 0;
        size_t futures = 0;
        size_t option_calls = 0;
        size_t option_puts = 0;
        size_t currency = 0;
        size_t commodity = 0;
        size_t index = 0;
        size_t unknown = 0;
        size_t total = 0;
    };
    
    [[nodiscard]] auto countByType() const noexcept -> TypeCounts {
        TypeCounts counts{};
        
        for (size_t i = 0; i < instrument_count_; ++i) {
            const auto& inst = instruments_[i];
            switch (inst.type) {
                case InstrumentType::EQUITY:
                    counts.equity++;
                    break;
                case InstrumentType::FUTURE:
                    counts.futures++;
                    break;
                case InstrumentType::OPTION_CALL:
                    counts.option_calls++;
                    break;
                case InstrumentType::OPTION_PUT:
                    counts.option_puts++;
                    break;
                case InstrumentType::CURRENCY:
                    counts.currency++;
                    break;
                case InstrumentType::COMMODITY:
                    counts.commodity++;
                    break;
                case InstrumentType::INDEX:
                    counts.index++;
                    break;
                default:
                    counts.unknown++;
                    break;
            }
            counts.total++;
        }
        
        return counts;
    }
};

// Helper function to fetch and cache instruments (exported for tests)
auto fetchAndCacheInstruments(ZerodhaInstrumentFetcher* fetcher) noexcept -> bool;

} // namespace Trading::MarketData::Zerodha