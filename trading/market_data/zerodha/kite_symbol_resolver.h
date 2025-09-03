#pragma once

#include "common/types.h"
#include "config/config.h"
#include "zerodha_instrument_fetcher.h"
#include <cstring>
#include <algorithm>
#include <cstdlib>

namespace Trading::MarketData::Zerodha {

using namespace Common;

// Symbol resolver for mapping indices to instrument tokens
class KiteSymbolResolver {
public:
    static constexpr size_t MAX_OPTIONS = 100;      // Max option contracts per index
    static constexpr size_t MAX_CONSTITUENTS = 50;  // Max constituent stocks per index
    
    struct IndexComponents {
        char index_name[32];
        uint32_t spot_token;
        uint32_t futures_token;
        uint32_t option_tokens[MAX_OPTIONS];
        size_t option_count;
        uint32_t constituent_tokens[MAX_CONSTITUENTS];
        size_t constituent_count;
    };
    
    explicit KiteSymbolResolver(ZerodhaInstrumentFetcher* fetcher)
        : instrument_fetcher_(fetcher) {
        LOG_INFO("KiteSymbolResolver initialized");
    }
    
    // Resolve index components based on config
    auto resolveIndexComponents(const char* index_name) -> IndexComponents {
        IndexComponents components;
        std::strncpy(components.index_name, index_name, sizeof(components.index_name) - 1);
        components.index_name[sizeof(components.index_name) - 1] = '\0';
        
        if (std::strcmp(index_name, "NIFTY50") == 0) {
            resolveNifty50Components(components);
        } else if (std::strcmp(index_name, "BANKNIFTY") == 0) {
            resolveBankNiftyComponents(components);
        } else if (std::strcmp(index_name, "FINNIFTY") == 0) {
            resolveFinniftyComponents(components);
        }
        
        LOG_INFO("Resolved %s: Spot=%u, Futures=%u, Options=%zu", 
                 index_name, components.spot_token, components.futures_token, 
                 components.option_count);
        
        return components;
    }
    
    // Get all tokens to subscribe based on config
    static constexpr size_t MAX_SUBSCRIPTION_TOKENS = 1000;
    
    struct SubscriptionList {
        uint32_t tokens[MAX_SUBSCRIPTION_TOKENS];
        size_t count;
    };
    
    auto getSubscriptionList(const Trading::TradingConfig& config) -> SubscriptionList {
        SubscriptionList list{};
        list.count = 0;
        
        for (uint32_t i = 0; i < config.zerodha.num_indices; ++i) {
            auto components = resolveIndexComponents(config.zerodha.indices[i]);
            
            // Add spot
            if (config.zerodha.fetch_spot && components.spot_token != 0 && list.count < MAX_SUBSCRIPTION_TOKENS) {
                list.tokens[list.count++] = components.spot_token;
            }
            
            // Add futures
            if (config.zerodha.fetch_futures && components.futures_token != 0 && list.count < MAX_SUBSCRIPTION_TOKENS) {
                list.tokens[list.count++] = components.futures_token;
            }
            
            // Add options
            if (config.zerodha.fetch_options) {
                for (size_t j = 0; j < components.option_count && list.count < MAX_SUBSCRIPTION_TOKENS; ++j) {
                    list.tokens[list.count++] = components.option_tokens[j];
                }
            }
            
            // Add constituents for spot indices
            if (config.zerodha.fetch_spot) {
                for (size_t j = 0; j < components.constituent_count && list.count < MAX_SUBSCRIPTION_TOKENS; ++j) {
                    list.tokens[list.count++] = components.constituent_tokens[j];
                }
            }
        }
        
        // Remove duplicates - simple O(n^2) but works for small lists
        size_t unique_count = 0;
        for (size_t i = 0; i < list.count; ++i) {
            bool is_duplicate = false;
            for (size_t j = 0; j < unique_count; ++j) {
                if (list.tokens[j] == list.tokens[i]) {
                    is_duplicate = true;
                    break;
                }
            }
            if (!is_duplicate) {
                list.tokens[unique_count++] = list.tokens[i];
            }
        }
        list.count = unique_count;
        
        LOG_INFO("Subscription list: %zu unique tokens", list.count);
        return list;
    }
    
private:
    ZerodhaInstrumentFetcher* instrument_fetcher_;
    
    // NIFTY 50 specific resolution
    auto resolveNifty50Components(IndexComponents& components) -> void {
        // NIFTY 50 spot index token
        components.spot_token = 256265;  // NSE:NIFTY 50
        
        // Get current date for expiry calculation
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        struct tm* tm_now = std::localtime(&time_t);
        
        // Find nearest Thursday expiry
        int days_to_thursday = (4 - tm_now->tm_wday + 7) % 7;
        if (days_to_thursday == 0 && tm_now->tm_hour >= 15) {
            days_to_thursday = 7;  // If today is Thursday after market close, get next Thursday
        }
        
        // Search for NIFTY futures and options
        if (instrument_fetcher_ && instrument_fetcher_->getInstrumentCount() > 0) {
            for (size_t i = 0; i < instrument_fetcher_->getInstrumentCount(); ++i) {
                const Instrument* inst = instrument_fetcher_->getInstrument(i);
                if (!inst) continue;
                
                // Check for NIFTY instruments
                if (std::strstr(inst->trading_symbol, "NIFTY") != nullptr) {
                    // Futures
                    if (inst->type == InstrumentType::FUTURE && components.futures_token == 0) {
                        // Check for current month expiry
                        if (std::strstr(inst->trading_symbol, "NIFTY") && 
                            !std::strstr(inst->trading_symbol, "BANKNIFTY") &&
                            !std::strstr(inst->trading_symbol, "FINNIFTY")) {
                            components.futures_token = static_cast<uint32_t>(std::strtoul(inst->instrument_token, nullptr, 10));
                        }
                    }
                    // Options
                    else if (inst->type == InstrumentType::OPTION_CALL || inst->type == InstrumentType::OPTION_PUT) {
                        // Get ATM and nearby strikes
                        if (std::strstr(inst->trading_symbol, "NIFTY") && 
                            !std::strstr(inst->trading_symbol, "BANKNIFTY") &&
                            !std::strstr(inst->trading_symbol, "FINNIFTY")) {
                            if (components.option_count < MAX_OPTIONS) {
                                components.option_tokens[components.option_count++] = static_cast<uint32_t>(std::strtoul(inst->instrument_token, nullptr, 10));
                            }
                            
                            // Limit to configured number of strikes
                            if (components.option_count >= 40) {  // 10 strikes * 2 (CE/PE) * 2 (nearby)
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        // Add major NIFTY 50 constituents (top 10 by weight)
        // These are approximate tokens - should be verified with actual data
        uint32_t nifty_constituents[] = {
            738561,   // RELIANCE
            340481,   // HDFC BANK
            341249,   // HDFC
            1510401,  // ICICI BANK
            492033,   // KOTAKBANK
            2953217,  // TCS
            408065,   // INFY
            315393,   // HINDUNILVR
            348929,   // ITC
            3861249   // SBIN
        };
        components.constituent_count = sizeof(nifty_constituents) / sizeof(nifty_constituents[0]);
        for (size_t i = 0; i < components.constituent_count && i < MAX_CONSTITUENTS; ++i) {
            components.constituent_tokens[i] = nifty_constituents[i];
        }
    }
    
    // BANK NIFTY specific resolution
    auto resolveBankNiftyComponents(IndexComponents& components) -> void {
        components.spot_token = 260105;  // NSE:NIFTY BANK
        
        if (instrument_fetcher_ && instrument_fetcher_->getInstrumentCount() > 0) {
            for (size_t i = 0; i < instrument_fetcher_->getInstrumentCount(); ++i) {
                const Instrument* inst = instrument_fetcher_->getInstrument(i);
                if (!inst) continue;
                
                if (std::strstr(inst->trading_symbol, "BANKNIFTY") != nullptr) {
                    if (inst->type == InstrumentType::FUTURE && components.futures_token == 0) {
                        components.futures_token = static_cast<uint32_t>(std::strtoul(inst->instrument_token, nullptr, 10));
                    } else if (inst->type == InstrumentType::OPTION_CALL || inst->type == InstrumentType::OPTION_PUT) {
                        if (components.option_count < MAX_OPTIONS) {
                            components.option_tokens[components.option_count++] = static_cast<uint32_t>(std::strtoul(inst->instrument_token, nullptr, 10));
                        }
                        if (components.option_count >= 40) break;
                    }
                }
            }
        }
        
        // Banking stocks
        uint32_t bank_constituents[] = {
            341249,   // HDFC BANK  
            1510401,  // ICICI BANK
            492033,   // KOTAKBANK
            3861249,  // SBIN
            1522689,  // AXISBANK
            3400961,  // INDUSINDBK
            5582849,  // BANDHANBNK
            579329,   // FEDERALBNK
            1270529,  // IDFCFIRSTB
            3539457   // PNB
        };
        components.constituent_count = sizeof(bank_constituents) / sizeof(bank_constituents[0]);
        for (size_t i = 0; i < components.constituent_count && i < MAX_CONSTITUENTS; ++i) {
            components.constituent_tokens[i] = bank_constituents[i];
        }
    }
    
    // FIN NIFTY specific resolution
    auto resolveFinniftyComponents(IndexComponents& components) -> void {
        components.spot_token = 257801;  // NSE:NIFTY FIN SERVICE
        
        if (instrument_fetcher_ && instrument_fetcher_->getInstrumentCount() > 0) {
            for (size_t i = 0; i < instrument_fetcher_->getInstrumentCount(); ++i) {
                const Instrument* inst = instrument_fetcher_->getInstrument(i);
                if (!inst) continue;
                
                if (std::strstr(inst->trading_symbol, "FINNIFTY") != nullptr) {
                    if (inst->type == InstrumentType::FUTURE && components.futures_token == 0) {
                        components.futures_token = static_cast<uint32_t>(std::strtoul(inst->instrument_token, nullptr, 10));
                    } else if (inst->type == InstrumentType::OPTION_CALL || inst->type == InstrumentType::OPTION_PUT) {
                        if (components.option_count < MAX_OPTIONS) {
                            components.option_tokens[components.option_count++] = static_cast<uint32_t>(std::strtoul(inst->instrument_token, nullptr, 10));
                        }
                        if (components.option_count >= 40) break;
                    }
                }
            }
        }
        
        // Financial services stocks
        uint32_t fin_constituents[] = {
            341249,   // HDFC BANK
            340481,   // HDFC
            1510401,  // ICICI BANK
            119553,   // BAJFINANCE
            81153,    // BAJAJFINSV
            3861249,  // SBIN
            4267265,  // SBILIFE
            5582849,  // HDFCLIFE
            2029825,  // ICICIPRULI
            492033    // KOTAKBANK
        };
        components.constituent_count = sizeof(fin_constituents) / sizeof(fin_constituents[0]);
        for (size_t i = 0; i < components.constituent_count && i < MAX_CONSTITUENTS; ++i) {
            components.constituent_tokens[i] = fin_constituents[i];
        }
    }
};

} // namespace Trading::MarketData::Zerodha