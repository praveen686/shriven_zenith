cmake_minimum_required(VERSION 3.20)

# Test executables
add_executable(test_zerodha_auth test_zerodha_auth.cpp)

target_link_libraries(test_zerodha_auth
    Trading
    CommonImpl
    curl
    Threads::Threads
)

target_include_directories(test_zerodha_auth PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Instrument fetcher test
add_executable(test_instrument_fetcher test_instrument_fetcher.cpp)

target_link_libraries(test_instrument_fetcher
    Trading
    CommonImpl
    curl
    Threads::Threads
)

target_include_directories(test_instrument_fetcher PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Binance test
add_executable(test_binance test_binance.cpp)

target_link_libraries(test_binance
    Trading
    CommonImpl
    config
    curl
    ssl
    crypto
    Threads::Threads
)

target_include_directories(test_binance PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Binance WebSocket test
# add_executable(test_binance_ws test_binance_ws.cpp)  # Disabled - use trader_main only

# target_link_libraries(test_binance_ws
#     Trading
#     CommonImpl
#     config
#     curl
#     ssl
#     crypto
#     websockets
#     Threads::Threads
# )
# 
# target_include_directories(test_binance_ws PRIVATE
#     ${CMAKE_SOURCE_DIR}
# )

# Kite WebSocket test
add_executable(test_kite_ws test_kite_ws.cpp)

target_link_libraries(test_kite_ws
    Trading
    CommonImpl
    config
    curl
    ssl
    crypto
    websockets
    Threads::Threads
)

target_include_directories(test_kite_ws PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Add more tests as they are created
# add_executable(test_trade_engine test_trade_engine.cpp)
# target_link_libraries(test_trade_engine Trading CommonImpl Threads::Threads)