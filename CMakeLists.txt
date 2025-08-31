cmake_minimum_required(VERSION 3.20)

project(ShrvenZenith VERSION 1.0.0 LANGUAGES CXX)

# Set C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Maximum strictness compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -Wpedantic")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wcast-align -Wcast-qual -Wconversion")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wctor-dtor-privacy -Wdisabled-optimization")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wformat=2 -Winit-self -Wmissing-declarations")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wmissing-include-dirs -Wold-style-cast")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Woverloaded-virtual -Wredundant-decls")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wshadow -Wsign-conversion -Wsign-promo")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wstrict-overflow=5 -Wswitch-default")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wundef -Wno-unused -Wno-variadic-macros")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-parentheses -fdiagnostics-show-option")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunreachable-code -Wunused-parameter")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunused-function -Wunused-variable")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunused-value -Wwrite-strings")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-missing-field-initializers")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wpointer-arith -Wstack-protector")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-common -fstack-protector-strong")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wdouble-promotion -Wfloat-equal")

# GCC-specific warning flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wlogical-op -Wplacement-new=2")
endif()

# Clang-specific warning flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang doesn't have -Wlogical-op or -Wplacement-new=2
    # Suppress deprecation warnings for std::result_of (used by std::bind in C++17+)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")
endif()

# Compiler optimizations
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -mtune=native -flto")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -fsanitize=address")

# Include coverage configuration if available
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Coverage.cmake")
    include(cmake/Coverage.cmake)
endif()

# Coverage build type (for Clang with llvm-cov)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS_COVERAGE "-O0 -g -fprofile-instr-generate -fcoverage-mapping")
    set(CMAKE_EXE_LINKER_FLAGS_COVERAGE "-fprofile-instr-generate -fcoverage-mapping")
endif()

# Thread detection with robust fallback
# Prefer pthreads on POSIX systems
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(CMAKE_THREAD_PREFER_PTHREAD ON)

# Try standard detection first
find_package(Threads)

if(Threads_FOUND)
    message(STATUS "Found Threads: ${CMAKE_THREAD_LIBS_INIT}")
else()
    # Fallback for Clang/GCC when detection fails
    message(WARNING "CMake Threads detection failed; using -pthread fallback for ${CMAKE_CXX_COMPILER_ID}")
    
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        # Create a pseudo Threads::Threads target for consistency
        add_library(Threads::Threads INTERFACE IMPORTED)
        set_target_properties(Threads::Threads PROPERTIES
            INTERFACE_COMPILE_OPTIONS "-pthread"
            INTERFACE_LINK_OPTIONS "-pthread"
        )
        set(Threads_FOUND TRUE)
        set(CMAKE_THREAD_LIBS_INIT "-pthread")
        
        # For coverage builds, also set global flags
        if(CMAKE_BUILD_TYPE STREQUAL "Coverage")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread")
        endif()
    else()
        message(FATAL_ERROR "No thread library detected and no fallback available for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

# Check for libnuma
find_library(NUMA_LIBRARY numa)

# Add subdirectories
add_subdirectory(bldg_blocks)
add_subdirectory(examples)

# Add tests
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
