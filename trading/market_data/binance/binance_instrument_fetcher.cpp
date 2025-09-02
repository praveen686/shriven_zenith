// ============================================================================
// binance_instrument_fetcher.cpp - Binance Symbol/Instrument Fetcher Implementation
// ============================================================================

#include "trading/market_data/binance/binance_instrument_fetcher.h"
#include "config/config_manager.h"
#include "common/logging.h"
#include <cstring>
#include <cstdio>
#include <ctime>
#include <algorithm>

namespace Trading::MarketData::Binance {

// Static member definition
Symbol BinanceInstrumentFetcher::symbols_[MAX_SYMBOLS];

// ============================================================================
// JSON Parsing Helpers
// ============================================================================

// Helper to extract string value from JSON
static bool extractJsonString(const char* json, const char* key, char* value, size_t value_size) {
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\":\"", key);
    
    const char* start = strstr(json, search_key);
    if (!start) {
        // Try without quotes for nested objects
        snprintf(search_key, sizeof(search_key), "\"%s\":", key);
        start = strstr(json, search_key);
        if (!start) return false;
        start += strlen(search_key);
        if (*start == '"') start++;
    } else {
        start += strlen(search_key);
    }
    
    const char* end = strchr(start, '"');
    if (!end) return false;
    
    size_t len = static_cast<size_t>(end - start);
    if (len >= value_size) len = value_size - 1;
    
    strncpy(value, start, len);
    value[len] = '\0';
    
    return true;
}

// Helper to extract double value from JSON
static bool extractJsonDouble(const char* json, const char* key, double* value) {
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\":\"", key);
    
    const char* start = strstr(json, search_key);
    if (!start) {
        // Try without quotes for numbers
        snprintf(search_key, sizeof(search_key), "\"%s\":", key);
        start = strstr(json, search_key);
        if (!start) return false;
        start += strlen(search_key);
    } else {
        start += strlen(search_key);
    }
    
    *value = strtod(start, nullptr);
    return true;
}

// ============================================================================
// BinanceInstrumentFetcher Implementation
// ============================================================================

bool BinanceInstrumentFetcher::parseSymbol(const char* symbol_json, Symbol& symbol) noexcept {
    // Extract basic symbol info
    if (!extractJsonString(symbol_json, "symbol", symbol.symbol, sizeof(symbol.symbol))) {
        return false;
    }
    
    extractJsonString(symbol_json, "baseAsset", symbol.base_asset, sizeof(symbol.base_asset));
    extractJsonString(symbol_json, "quoteAsset", symbol.quote_asset, sizeof(symbol.quote_asset));
    extractJsonString(symbol_json, "status", symbol.status, sizeof(symbol.status));
    
    // Check permissions
    const char* permissions = strstr(symbol_json, "\"permissions\":");
    if (permissions) {
        symbol.spot_trading = strstr(permissions, "\"SPOT\"") != nullptr;
        symbol.margin_trading = strstr(permissions, "\"MARGIN\"") != nullptr;
    }
    
    // Parse filters
    const char* filters = strstr(symbol_json, "\"filters\":");
    if (filters) {
        // PRICE_FILTER
        const char* price_filter = strstr(filters, "\"PRICE_FILTER\"");
        if (price_filter) {
            extractJsonDouble(price_filter, "minPrice", &symbol.min_price);
            extractJsonDouble(price_filter, "maxPrice", &symbol.max_price);
            extractJsonDouble(price_filter, "tickSize", &symbol.tick_size);
        }
        
        // LOT_SIZE
        const char* lot_size = strstr(filters, "\"LOT_SIZE\"");
        if (lot_size) {
            extractJsonDouble(lot_size, "minQty", &symbol.min_qty);
            extractJsonDouble(lot_size, "maxQty", &symbol.max_qty);
            extractJsonDouble(lot_size, "stepSize", &symbol.step_size);
        }
        
        // MIN_NOTIONAL
        const char* min_notional = strstr(filters, "\"MIN_NOTIONAL\"");
        if (min_notional) {
            extractJsonDouble(min_notional, "minNotional", &symbol.min_notional);
        }
    }
    
    return true;
}

size_t BinanceInstrumentFetcher::parseExchangeInfo(const char* json_data, size_t json_len) noexcept {
    if (!json_data || json_len == 0) return 0;
    
    size_t parsed_count = 0;
    
    // Find symbols array
    const char* symbols_start = strstr(json_data, "\"symbols\":[");
    if (!symbols_start) {
        LOG_ERROR("No symbols array found in exchange info");
        return 0;
    }
    
    symbols_start += 11; // Skip "symbols":["
    
    // Parse each symbol
    const char* current = symbols_start;
    while (current && parsed_count < MAX_SYMBOLS) {
        // Find next symbol object
        const char* symbol_start = strchr(current, '{');
        if (!symbol_start) break;
        
        // Find end of this symbol object
        const char* symbol_end = symbol_start + 1;
        const char* json_end = json_data + json_len;
        int brace_count = 1;
        while (symbol_end < json_end && brace_count > 0) {
            if (*symbol_end == '{') brace_count++;
            else if (*symbol_end == '}') brace_count--;
            symbol_end++;
        }
        
        if (symbol_end >= json_end || brace_count != 0) break;
        
        // Parse this symbol
        Symbol temp_symbol;
        if (parseSymbol(symbol_start, temp_symbol)) {
            // Only add tradeable spot symbols
            if (temp_symbol.isTradeable()) {
                symbols_[parsed_count++] = temp_symbol;
            }
        }
        
        current = symbol_end;
    }
    
    symbol_count_.store(parsed_count);
    LOG_INFO("Parsed %zu tradeable symbols from exchange info", parsed_count);
    
    return parsed_count;
}

size_t BinanceInstrumentFetcher::parseTicker24hr(const char* json_data, size_t json_len) noexcept {
    if (!json_data || json_len == 0) return 0;
    
    size_t updated_count = 0;
    size_t total_symbols = symbol_count_.load();
    
    // Parse ticker array
    const char* current = json_data;
    const char* json_end = json_data + json_len;
    
    // Check if it's an array or single object
    bool is_array = json_data[0] == '[';
    
    while (current < json_end) {
        const char* ticker_start = strchr(current, '{');
        if (!ticker_start || ticker_start >= json_end) break;
        
        // Find end of this ticker object
        const char* ticker_end = strchr(ticker_start, '}');
        if (!ticker_end || ticker_end >= json_end) break;
        
        // Extract symbol name
        char symbol_name[20];
        if (extractJsonString(ticker_start, "symbol", symbol_name, sizeof(symbol_name))) {
            // Find matching symbol in our list
            for (size_t i = 0; i < total_symbols; ++i) {
                if (strcmp(symbols_[i].symbol, symbol_name) == 0) {
                    // Update market data
                    extractJsonDouble(ticker_start, "lastPrice", &symbols_[i].last_price);
                    extractJsonDouble(ticker_start, "volume", &symbols_[i].volume_24h);
                    extractJsonDouble(ticker_start, "quoteVolume", &symbols_[i].quote_volume_24h);
                    updated_count++;
                    break;
                }
            }
        }
        
        current = ticker_end + 1;
        
        // If not an array, we're done after first object
        if (!is_array) break;
    }
    
    LOG_INFO("Updated market data for %zu symbols", updated_count);
    return updated_count;
}

bool BinanceInstrumentFetcher::fetchTopSymbols(char* buffer, size_t buffer_size) noexcept {
    if (!buffer || buffer_size == 0 || !auth_) {
        LOG_ERROR("Invalid parameters for fetchTopSymbols");
        return false;
    }
    
    LOG_INFO("Fetching top symbols from Binance...");
    
    // Clear existing symbols
    clear();
    
    // Define top trading pairs we want to track
    const char* top_symbols[] = {
        // Major USDT pairs
        "BTCUSDT", "ETHUSDT", "BNBUSDT", "XRPUSDT", "SOLUSDT",
        "ADAUSDT", "DOGEUSDT", "AVAXUSDT", "SHIBUSDT", "DOTUSDT",
        "MATICUSDT", "LINKUSDT", "UNIUSDT", "LTCUSDT", "ATOMUSDT",
        // Major BTC pairs
        "ETHBTC", "BNBBTC", "XRPBTC", "ADABTC", "SOLBTC",
        // Major ETH pairs
        "BNBETH", "XRPETH", "ADAETH", "SOLETH", "MATICETH"
    };
    
    size_t symbol_count = 0;
    
    // Fetch ticker data for each symbol
    for (const char* symbol_name : top_symbols) {
        memset(buffer, 0, buffer_size);
        
        if (auth_->fetchTicker24hr(symbol_name, buffer, buffer_size)) {
            // Parse this ticker
            Symbol& sym = symbols_[symbol_count];
            
            // Set symbol name
            strncpy(sym.symbol, symbol_name, sizeof(sym.symbol) - 1);
            
            // Extract base and quote assets from symbol name
            if (strstr(symbol_name, "USDT")) {
                strncpy(sym.quote_asset, "USDT", sizeof(sym.quote_asset) - 1);
                size_t base_len = strlen(symbol_name) - 4;
                strncpy(sym.base_asset, symbol_name, base_len);
                sym.base_asset[base_len] = '\0';
            } else if (strstr(symbol_name, "BTC")) {
                strncpy(sym.quote_asset, "BTC", sizeof(sym.quote_asset) - 1);
                size_t base_len = strlen(symbol_name) - 3;
                strncpy(sym.base_asset, symbol_name, base_len);
                sym.base_asset[base_len] = '\0';
            } else if (strstr(symbol_name, "ETH")) {
                strncpy(sym.quote_asset, "ETH", sizeof(sym.quote_asset) - 1);
                size_t base_len = strlen(symbol_name) - 3;
                strncpy(sym.base_asset, symbol_name, base_len);
                sym.base_asset[base_len] = '\0';
            }
            
            // Set as trading
            strncpy(sym.status, "TRADING", sizeof(sym.status) - 1);
            sym.spot_trading = true;
            
            // Parse ticker data
            extractJsonDouble(buffer, "lastPrice", &sym.last_price);
            extractJsonDouble(buffer, "volume", &sym.volume_24h);
            extractJsonDouble(buffer, "quoteVolume", &sym.quote_volume_24h);
            
            // Set reasonable defaults for trading rules (would need exchange info for exact values)
            sym.min_price = 0.00000001;
            sym.max_price = 1000000.0;
            sym.tick_size = 0.00000001;
            sym.min_qty = 0.00001;
            sym.max_qty = 10000000.0;
            sym.step_size = 0.00001;
            sym.min_notional = 10.0;
            
            symbol_count++;
            LOG_INFO("Fetched %s: price=%.2f, volume=%.2f", 
                     symbol_name, sym.last_price, sym.quote_volume_24h);
        } else {
            LOG_WARN("Failed to fetch ticker for %s", symbol_name);
        }
    }
    
    symbol_count_.store(symbol_count);
    LOG_INFO("Fetched %zu top symbols from Binance", symbol_count);
    
    // Update timestamp
    last_update_time_ns_.store(static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    ));
    
    return symbol_count > 0;
}

bool BinanceInstrumentFetcher::fetchAllSymbols(char* buffer, size_t buffer_size) noexcept {
    if (!buffer || buffer_size == 0 || !auth_) {
        LOG_ERROR("Invalid parameters for fetchAllSymbols");
        return false;
    }
    
    LOG_INFO("Fetching symbols from Binance exchange info...");
    
    // Clear existing symbols
    clear();
    
    // Fetch exchange info (contains all symbols and trading rules)
    if (!auth_->fetchExchangeInfo(buffer, buffer_size)) {
        LOG_ERROR("Failed to fetch exchange info");
        return false;
    }
    
    // Parse the response
    size_t parsed = parseExchangeInfo(buffer, strlen(buffer));
    if (parsed == 0) {
        LOG_ERROR("Failed to parse any symbols");
        return false;
    }
    
    LOG_INFO("Fetched %zu symbols from Binance", parsed);
    
    // Now fetch 24hr ticker data for all symbols to get prices/volumes
    LOG_INFO("Fetching 24hr ticker data...");
    
    memset(buffer, 0, buffer_size);
    if (auth_->fetchTicker24hr("", buffer, buffer_size)) {
        parseTicker24hr(buffer, strlen(buffer));
    } else {
        LOG_WARN("Failed to fetch ticker data, prices will be 0");
    }
    
    // Update timestamp
    last_update_time_ns_.store(static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    ));
    
    return true;
}

bool BinanceInstrumentFetcher::updateMarketData(char* buffer, size_t buffer_size) noexcept {
    if (!buffer || buffer_size == 0 || !auth_) {
        LOG_ERROR("Invalid parameters for updateMarketData");
        return false;
    }
    
    if (symbol_count_.load() == 0) {
        LOG_ERROR("No symbols loaded, fetch symbols first");
        return false;
    }
    
    LOG_INFO("Updating market data for all symbols...");
    
    memset(buffer, 0, buffer_size);
    if (!auth_->fetchTicker24hr("", buffer, buffer_size)) {
        LOG_ERROR("Failed to fetch ticker data");
        return false;
    }
    
    size_t updated = parseTicker24hr(buffer, strlen(buffer));
    
    // Update timestamp
    last_update_time_ns_.store(static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    ));
    
    return updated > 0;
}

bool BinanceInstrumentFetcher::saveToCSV(const char* filename) const noexcept {
    if (!filename) return false;
    
    FILE* file = fopen(filename, "w");  // AUDIT_IGNORE: Cache file I/O
    if (!file) {
        LOG_ERROR("Failed to open file for writing: %s", filename);
        return false;
    }
    
    // Write header
    fprintf(file, "symbol,base_asset,quote_asset,status,min_price,max_price,tick_size,"
                  "min_qty,max_qty,step_size,min_notional,spot_trading,margin_trading,"
                  "last_price,volume_24h,quote_volume_24h\n");
    
    // Write symbols
    size_t count = symbol_count_.load();
    for (size_t i = 0; i < count; ++i) {
        const Symbol& sym = symbols_[i];
        fprintf(file, "%s,%s,%s,%s,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,%d,%d,%.8f,%.2f,%.2f\n",
                sym.symbol, sym.base_asset, sym.quote_asset, sym.status,
                sym.min_price, sym.max_price, sym.tick_size,
                sym.min_qty, sym.max_qty, sym.step_size,
                sym.min_notional,
                sym.spot_trading ? 1 : 0,
                sym.margin_trading ? 1 : 0,
                sym.last_price, sym.volume_24h, sym.quote_volume_24h);
    }
    
    fclose(file);
    LOG_INFO("Saved %zu symbols to %s", count, filename);
    return true;
}

bool BinanceInstrumentFetcher::loadFromCSV(const char* filename) noexcept {
    if (!filename) return false;
    
    FILE* file = fopen(filename, "r");  // AUDIT_IGNORE: Cache file I/O
    if (!file) {
        LOG_ERROR("Failed to open file for reading: %s", filename);
        return false;
    }
    
    clear();
    
    char line[1024];
    // Skip header
    if (!fgets(line, sizeof(line), file)) {
        fclose(file);
        return false;
    }
    
    size_t loaded = 0;
    while (fgets(line, sizeof(line), file) && loaded < MAX_SYMBOLS) {
        Symbol& sym = symbols_[loaded];
        
        int spot, margin;
        int parsed = sscanf(line, "%19[^,],%9[^,],%9[^,],%15[^,],%lf,%lf,%lf,%lf,%lf,%lf,%lf,%d,%d,%lf,%lf,%lf",
                           sym.symbol, sym.base_asset, sym.quote_asset, sym.status,
                           &sym.min_price, &sym.max_price, &sym.tick_size,
                           &sym.min_qty, &sym.max_qty, &sym.step_size,
                           &sym.min_notional, &spot, &margin,
                           &sym.last_price, &sym.volume_24h, &sym.quote_volume_24h);
        
        if (parsed >= 13) {  // At least the required fields
            sym.spot_trading = (spot != 0);
            sym.margin_trading = (margin != 0);
            loaded++;
        }
    }
    
    fclose(file);
    
    symbol_count_.store(loaded);
    
    // Update timestamp
    last_update_time_ns_.store(static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    ));
    
    LOG_INFO("Loaded %zu symbols from %s", loaded, filename);
    return loaded > 0;
}

bool BinanceInstrumentFetcher::findSymbol(const char* symbol_name, Symbol& out_symbol) const noexcept {
    if (!symbol_name) return false;
    
    size_t count = symbol_count_.load();
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(symbols_[i].symbol, symbol_name) == 0) {
            out_symbol = symbols_[i];
            return true;
        }
    }
    
    return false;
}

size_t BinanceInstrumentFetcher::findByBaseAsset(const char* base_asset, Symbol* out_symbols, size_t max_count) const noexcept {
    if (!base_asset || !out_symbols || max_count == 0) return 0;
    
    size_t found = 0;
    size_t count = symbol_count_.load();
    
    for (size_t i = 0; i < count && found < max_count; ++i) {
        if (symbols_[i].hasBaseAsset(base_asset)) {
            out_symbols[found++] = symbols_[i];
        }
    }
    
    return found;
}

size_t BinanceInstrumentFetcher::findByQuoteAsset(const char* quote_asset, Symbol* out_symbols, size_t max_count) const noexcept {
    if (!quote_asset || !out_symbols || max_count == 0) return 0;
    
    size_t found = 0;
    size_t count = symbol_count_.load();
    
    for (size_t i = 0; i < count && found < max_count; ++i) {
        if (symbols_[i].hasQuoteAsset(quote_asset)) {
            out_symbols[found++] = symbols_[i];
        }
    }
    
    return found;
}

size_t BinanceInstrumentFetcher::findTopUSDTPairs(Symbol* out_symbols, size_t max_count) const noexcept {
    if (!out_symbols || max_count == 0) return 0;
    
    // First collect all USDT pairs
    Symbol usdt_pairs[MAX_SYMBOLS];
    size_t usdt_count = findByQuoteAsset("USDT", usdt_pairs, MAX_SYMBOLS);
    
    if (usdt_count == 0) return 0;
    
    // Sort by 24h quote volume (descending)
    std::sort(usdt_pairs, usdt_pairs + usdt_count, 
              [](const Symbol& a, const Symbol& b) {
                  return a.quote_volume_24h > b.quote_volume_24h;
              });
    
    // Copy top pairs
    size_t copy_count = std::min(usdt_count, max_count);
    memcpy(out_symbols, usdt_pairs, copy_count * sizeof(Symbol));
    
    return copy_count;
}

// Helper function implementation
bool fetchAndCacheSymbols(BinanceInstrumentFetcher* fetcher) noexcept {
    if (!fetcher) return false;
    
    // Allocate buffer for JSON data (20MB for exchange info + ticker data)
    constexpr size_t BUFFER_SIZE = 20 * 1024 * 1024;
    char* json_buffer = new char[BUFFER_SIZE];  // AUDIT_IGNORE: Init-time only
    memset(json_buffer, 0, BUFFER_SIZE);
    
    bool success = false;
    
    // Try to fetch from API
    if (fetcher->fetchAllSymbols(json_buffer, BUFFER_SIZE)) {
        // Save to cache file with today's date
        char filename[512];
        time_t now = time(nullptr);
        struct tm* tm_info = localtime(&now);
        
        snprintf(filename, sizeof(filename), "%s/binance_symbols_%04d%02d%02d.csv",
                Trading::ConfigManager::getInstrumentsDataDir(),
                tm_info->tm_year + 1900,
                tm_info->tm_mon + 1,
                tm_info->tm_mday);
        
        if (fetcher->saveToCSV(filename)) {
            LOG_INFO("Saved Binance symbols to cache: %s", filename);
            success = true;
        }
    } else {
        LOG_WARN("Failed to fetch from API, trying to load from cache");
        
        // Try to load from most recent cache file
        char filename[512];
        time_t now = time(nullptr);
        struct tm* tm_info = localtime(&now);
        
        // Try today's file first
        snprintf(filename, sizeof(filename), "%s/binance_symbols_%04d%02d%02d.csv",
                Trading::ConfigManager::getInstrumentsDataDir(),
                tm_info->tm_year + 1900,
                tm_info->tm_mon + 1,
                tm_info->tm_mday);
        
        if (fetcher->loadFromCSV(filename)) {
            LOG_INFO("Loaded Binance symbols from cache: %s", filename);
            success = true;
        } else {
            // Try yesterday's file
            now -= 86400;  // Subtract one day
            tm_info = localtime(&now);
            
            snprintf(filename, sizeof(filename), "%s/binance_symbols_%04d%02d%02d.csv",
                    Trading::ConfigManager::getInstrumentsDataDir(),
                    tm_info->tm_year + 1900,
                    tm_info->tm_mon + 1,
                    tm_info->tm_mday);
            
            if (fetcher->loadFromCSV(filename)) {
                LOG_INFO("Loaded Binance symbols from yesterday's cache: %s", filename);
                success = true;
            }
        }
    }
    
    delete[] json_buffer;  // AUDIT_IGNORE: Init-time only
    return success;
}

} // namespace Trading::MarketData::Binance