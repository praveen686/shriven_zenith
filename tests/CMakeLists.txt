set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/bldg_blocks)

# Libraries needed for tests
list(APPEND TEST_LIBS BldgBlocksImpl pthread numa atomic)

# Benchmarks
add_executable(benchmark_enhancements benchmarks/benchmark_enhancements.cpp)
target_link_libraries(benchmark_enhancements PUBLIC ${TEST_LIBS})

# Future unit tests can be added here
# add_executable(test_lf_queue unit_tests/test_lf_queue.cpp)
# target_link_libraries(test_lf_queue PUBLIC ${TEST_LIBS})