set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/bldg_blocks)

# Libraries needed for tests
list(APPEND TEST_LIBS BldgBlocksImpl pthread numa atomic)

# Google Test for unit tests
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTEST REQUIRED gtest)
list(APPEND TEST_LIBS ${GTEST_LIBRARIES})

# Benchmarks
add_executable(benchmark_enhancements benchmarks/benchmark_enhancements.cpp)
target_link_libraries(benchmark_enhancements PUBLIC ${TEST_LIBS})

# Unit Tests (to be created)
# add_executable(test_mem_pool unit/test_mem_pool.cpp)
# target_link_libraries(test_mem_pool PUBLIC ${TEST_LIBS})
# target_include_directories(test_mem_pool PRIVATE ${GTEST_INCLUDE_DIRS})

# add_executable(test_lf_queue unit/test_lf_queue.cpp)
# target_link_libraries(test_lf_queue PUBLIC ${TEST_LIBS})
# target_include_directories(test_lf_queue PRIVATE ${GTEST_INCLUDE_DIRS})

# add_executable(test_thread_utils unit/test_thread_utils.cpp)
# target_link_libraries(test_thread_utils PUBLIC ${TEST_LIBS})
# target_include_directories(test_thread_utils PRIVATE ${GTEST_INCLUDE_DIRS})