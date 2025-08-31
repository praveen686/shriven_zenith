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
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wlogical-op -Wplacement-new=2")

# Compiler optimizations
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -mtune=native -flto")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -fsanitize=address")

# Find required packages
find_package(Threads REQUIRED)

# Check for libnuma
find_library(NUMA_LIBRARY numa)

# Add subdirectories
add_subdirectory(bldg_blocks)
add_subdirectory(examples)

# Add tests
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
