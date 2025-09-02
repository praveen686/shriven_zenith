cmake_minimum_required(VERSION 3.20)

# Trading library
add_library(TradingTypes INTERFACE)

# Header-only library
target_include_directories(TradingTypes INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

# Link to Common library
target_link_libraries(TradingTypes INTERFACE
    CommonImpl
)

# Add a compilation test to verify headers compile
add_library(TradingTypesTest STATIC
    types/types.h
    types/constants.h
    types/order.h
    types/market_data.h
)

set_target_properties(TradingTypesTest PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(TradingTypesTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(TradingTypesTest PRIVATE
    CommonImpl
)