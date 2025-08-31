cmake_minimum_required(VERSION 3.20)
project(BldgBlocks VERSION 1.0.0 LANGUAGES CXX)

# Set C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler optimizations for ultra-low latency
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -mtune=native -flto")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -fsanitize=address")

# Find required packages
find_package(Threads REQUIRED)

# Check for libnuma
find_library(NUMA_LIBRARY numa)
if(NUMA_LIBRARY)
    message(STATUS "Found libnuma: ${NUMA_LIBRARY}")
    set(HAVE_NUMA TRUE)
else()
    message(WARNING "libnuma not found - NUMA features disabled")
    set(HAVE_NUMA FALSE)
endif()

# Configure header with feature detection
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"
    @ONLY
)

# BldgBlocks header-only library
add_library(BldgBlocks INTERFACE)

target_include_directories(BldgBlocks INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(BldgBlocks INTERFACE 
    Threads::Threads
    $<$<BOOL:${HAVE_NUMA}>:${NUMA_LIBRARY}>
)

# Compile features and flags
target_compile_features(BldgBlocks INTERFACE cxx_std_23)
target_compile_options(BldgBlocks INTERFACE
    $<$<CONFIG:Release>:-O3 -DNDEBUG -march=native -mtune=native -flto>
    $<$<CONFIG:Debug>:-O0 -g3>
)

# Global logger source file
add_library(BldgBlocksImpl SHARED logging.cpp)
target_link_libraries(BldgBlocksImpl PUBLIC BldgBlocks)


# Installation
install(TARGETS BldgBlocks BldgBlocksImpl
    EXPORT BldgBlocksTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(FILES
    lf_queue.h
    mem_pool.h  
    thread_utils.h
    socket_utils.h
    logging.h
    time_utils.h
    macros.h
    types.h
    tcp_server.h
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
    DESTINATION include/BldgBlocks
)

install(EXPORT BldgBlocksTargets
    FILE BldgBlocksTargets.cmake
    NAMESPACE BldgBlocks::
    DESTINATION lib/cmake/BldgBlocks
)

# Generate config file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    BldgBlocksConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    BldgBlocksConfig.cmake.in
    BldgBlocksConfig.cmake
    INSTALL_DESTINATION lib/cmake/BldgBlocks
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/BldgBlocksConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/BldgBlocksConfigVersion.cmake
    DESTINATION lib/cmake/BldgBlocks
)