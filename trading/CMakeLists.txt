cmake_minimum_required(VERSION 3.20)

# Trading library (main implementation)
add_library(Trading STATIC
    config_manager.cpp
    auth/zerodha/zerodha_auth.cpp
    market_data/zerodha/zerodha_instrument_fetcher.cpp
)

target_include_directories(Trading PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(Trading PUBLIC
    CommonImpl
    curl  # For HTTP requests in auth
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
    curl
    pthread
)