cmake_minimum_required(VERSION 3.20)

# Trading library (main implementation)
add_library(Trading STATIC
    auth/zerodha/zerodha_auth.cpp
    auth/binance/binance_auth.cpp
    market_data/zerodha/zerodha_instrument_fetcher.cpp
    market_data/binance/binance_instrument_fetcher.cpp
    market_data/binance/binance_ws_client.cpp
)

target_include_directories(Trading PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(Trading PUBLIC
    CommonImpl
    config
    curl  # For HTTP requests in auth
    ssl   # For HMAC signatures in Binance
    crypto # For HMAC signatures in Binance
    websockets # For WebSocket connections
)

# Trading types interface (header-only)
add_library(TradingTypes INTERFACE)

target_include_directories(TradingTypes INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(TradingTypes INTERFACE
    CommonImpl
)

# Main trading executable
add_executable(trader_main
    trader_main.cpp
)

target_link_libraries(trader_main
    Trading
    CommonImpl
    config
    curl
    pthread
)