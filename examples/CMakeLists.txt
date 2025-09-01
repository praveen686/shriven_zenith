# Examples are built as part of the parent project
# Include Common headers
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../common)

# Examples runner - single executable for all examples
add_executable(examples
    example_main.cpp
    mem_pool_example.cpp
    lf_queue_example.cpp
    thread_example.cpp
)

target_link_libraries(examples 
    CommonImpl
    Threads::Threads
    $<$<BOOL:${NUMA_LIBRARY}>:${NUMA_LIBRARY}>
)

target_compile_features(examples PRIVATE cxx_std_23)

