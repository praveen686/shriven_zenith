# Config library
add_library(config STATIC
    config_manager.cpp
)

target_include_directories(config PUBLIC
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(config PUBLIC
    CommonImpl
)

# Set proper warning flags
target_compile_options(config PRIVATE
    -Wall -Wextra -Werror -Wpedantic
    -Wconversion -Wsign-conversion
    -Wold-style-cast -Wformat-security
)