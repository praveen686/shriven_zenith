set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/common)

# Libraries needed for tests
list(APPEND TEST_LIBS CommonImpl numa atomic)

# Add Threads using the imported target if available
if(TARGET Threads::Threads)
    list(APPEND TEST_LIBS Threads::Threads)
else()
    list(APPEND TEST_LIBS pthread)
endif()

# Google Test for unit tests
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTEST REQUIRED gtest)
list(APPEND TEST_LIBS ${GTEST_LIBRARIES})

# Benchmarks
add_executable(benchmark_common benchmarks/benchmark_common.cpp)
target_link_libraries(benchmark_common PUBLIC ${TEST_LIBS})

# Unit Tests
add_executable(test_mem_pool unit/test_mem_pool.cpp)
target_link_libraries(test_mem_pool PUBLIC ${TEST_LIBS})
target_include_directories(test_mem_pool PRIVATE ${GTEST_INCLUDE_DIRS})

add_executable(test_lf_queue unit/test_lf_queue.cpp)
target_link_libraries(test_lf_queue PUBLIC ${TEST_LIBS})
target_include_directories(test_lf_queue PRIVATE ${GTEST_INCLUDE_DIRS})

add_executable(test_logger unit/test_logger.cpp)
target_link_libraries(test_logger PUBLIC ${TEST_LIBS})
target_include_directories(test_logger PRIVATE ${GTEST_INCLUDE_DIRS})

add_executable(test_thread_utils unit/test_thread_utils.cpp)
target_link_libraries(test_thread_utils PUBLIC ${TEST_LIBS})
target_include_directories(test_thread_utils PRIVATE ${GTEST_INCLUDE_DIRS})

add_executable(test_zero_policy unit/test_zero_policy.cpp)
target_link_libraries(test_zero_policy PUBLIC ${TEST_LIBS})
target_include_directories(test_zero_policy PRIVATE ${GTEST_INCLUDE_DIRS})
target_compile_definitions(test_zero_policy PRIVATE TESTING)


# Bullet-proof GCC 13 + LTO strict-overflow workaround for std::sort false positive
# Apply to both compile AND link phases (LTO warnings can reappear at link time)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Use -Wno-error=strict-overflow to keep visibility but not fail builds
    target_compile_options(test_mem_pool PRIVATE -Wno-error=strict-overflow)
    target_link_options(test_mem_pool PRIVATE -Wno-error=strict-overflow)
    
    target_compile_options(test_lf_queue PRIVATE -Wno-error=strict-overflow)
    target_link_options(test_lf_queue PRIVATE -Wno-error=strict-overflow)
    
    target_compile_options(test_logger PRIVATE -Wno-error=strict-overflow)
    target_link_options(test_logger PRIVATE -Wno-error=strict-overflow)
    
    target_compile_options(test_thread_utils PRIVATE -Wno-error=strict-overflow)
    target_link_options(test_thread_utils PRIVATE -Wno-error=strict-overflow)
    
    target_compile_options(test_zero_policy PRIVATE -Wno-error=strict-overflow)
    target_link_options(test_zero_policy PRIVATE -Wno-error=strict-overflow)
endif()

# add_executable(test_thread_utils unit/test_thread_utils.cpp)
# target_link_libraries(test_thread_utils PUBLIC ${TEST_LIBS})
# target_include_directories(test_thread_utils PRIVATE ${GTEST_INCLUDE_DIRS})