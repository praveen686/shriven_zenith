cmake_minimum_required(VERSION 3.20)

project(LowLatencyApp)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_FLAGS "-std=c++23 -Wall -Wextra -Werror -Wpedantic")
set(CMAKE_VERBOSE_MAKEFILE on)

add_subdirectory(common)

list(APPEND LIBS libcommon)
list(APPEND LIBS pthread)

include_directories(${PROJECT_SOURCE_DIR})
