#!/bin/bash

# Ultra-strict build script for production-grade code quality
# This script builds with maximum compiler warnings and treats them as errors

echo "============================================"
echo "Building with MAXIMUM STRICTNESS"
echo "All warnings are errors!"
echo "============================================"

# Find tools
CMAKE=$(which cmake)
NINJA=$(which ninja)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Create logs directory if it doesn't exist
LOGS_DIR="logs"
mkdir -p "$LOGS_DIR"

# Generate timestamp for log files
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_PREFIX="${LOGS_DIR}/build_strict_${TIMESTAMP}"

# Maximum strictness flags
STRICT_FLAGS="-Wall -Wextra -Werror -Wpedantic"
STRICT_FLAGS="$STRICT_FLAGS -Wcast-align -Wcast-qual -Wconversion"
STRICT_FLAGS="$STRICT_FLAGS -Wctor-dtor-privacy -Wdisabled-optimization"
STRICT_FLAGS="$STRICT_FLAGS -Wformat=2 -Winit-self -Wmissing-declarations"
STRICT_FLAGS="$STRICT_FLAGS -Wmissing-include-dirs -Wold-style-cast"
STRICT_FLAGS="$STRICT_FLAGS -Woverloaded-virtual -Wredundant-decls"
STRICT_FLAGS="$STRICT_FLAGS -Wshadow -Wsign-conversion -Wsign-promo"
STRICT_FLAGS="$STRICT_FLAGS -Wstrict-overflow=5 -Wswitch-default"
STRICT_FLAGS="$STRICT_FLAGS -Wundef -Wno-unused -Wno-variadic-macros"
STRICT_FLAGS="$STRICT_FLAGS -Wno-parentheses -fdiagnostics-show-option"
STRICT_FLAGS="$STRICT_FLAGS -Wunreachable-code -Wunused-parameter"
STRICT_FLAGS="$STRICT_FLAGS -Wunused-function -Wunused-variable"
STRICT_FLAGS="$STRICT_FLAGS -Wunused-value -Wwrite-strings"
STRICT_FLAGS="$STRICT_FLAGS -Wno-missing-field-initializers"
STRICT_FLAGS="$STRICT_FLAGS -Wpointer-arith -Wstack-protector"
STRICT_FLAGS="$STRICT_FLAGS -fno-common -fstack-protector-strong"
STRICT_FLAGS="$STRICT_FLAGS -Wdouble-promotion -Wfloat-equal"
STRICT_FLAGS="$STRICT_FLAGS -Wlogical-op -Wplacement-new=2"

# Additional C++23 specific warnings
STRICT_FLAGS="$STRICT_FLAGS -Wzero-as-null-pointer-constant"
STRICT_FLAGS="$STRICT_FLAGS -Wnon-virtual-dtor -Weffc++"
STRICT_FLAGS="$STRICT_FLAGS -Wstrict-null-sentinel"

# Security flags
STRICT_FLAGS="$STRICT_FLAGS -D_FORTIFY_SOURCE=2"
# Note: -fPIE -pie are for executables only, not shared libraries
# Using -fPIC instead which works for both
STRICT_FLAGS="$STRICT_FLAGS -fPIC"
STRICT_FLAGS="$STRICT_FLAGS -Wformat-security"

echo -e "${YELLOW}Compiler flags:${NC}"
echo "$STRICT_FLAGS" | tr ' ' '\n' | sort | uniq

# Build directories in cmake folder
CMAKE_DIR="cmake"
mkdir -p "$CMAKE_DIR"

# Clean previous builds
echo -e "\n${YELLOW}Cleaning previous builds...${NC}"
rm -rf "${CMAKE_DIR}/build-strict-release" "${CMAKE_DIR}/build-strict-debug"

# Build Release with strict flags
echo -e "\n${GREEN}Building STRICT RELEASE version...${NC}"
STRICT_RELEASE_BUILD_DIR="${CMAKE_DIR}/build-strict-release"
mkdir -p "$STRICT_RELEASE_BUILD_DIR"
$CMAKE -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_CXX_FLAGS="$STRICT_FLAGS" \
       -DCMAKE_MAKE_PROGRAM=$NINJA \
       -G Ninja \
       -S . \
       -B "$STRICT_RELEASE_BUILD_DIR"

if [ $? -ne 0 ]; then
    echo -e "${RED}CMake configuration failed for Release!${NC}"
    exit 1
fi

echo -e "${YELLOW}Compiling Release...${NC}"
RELEASE_LOG="${LOG_PREFIX}_release.log"
$CMAKE --build "$STRICT_RELEASE_BUILD_DIR" --target all -j $(nproc) 2>&1 | tee "$RELEASE_LOG"

RELEASE_BUILD_STATUS=${PIPESTATUS[0]}
if [ $RELEASE_BUILD_STATUS -ne 0 ]; then
    echo -e "${RED}Build failed for Release!${NC}"
    echo -e "${YELLOW}Check $RELEASE_LOG for details${NC}"
    
    # Show error summary
    echo -e "\n${RED}=== ERROR SUMMARY ===${NC}"
    grep -E "error:|warning:" "$RELEASE_LOG" | head -20
    exit 1
fi

# Build Debug with strict flags
echo -e "\n${GREEN}Building STRICT DEBUG version...${NC}"
STRICT_DEBUG_BUILD_DIR="${CMAKE_DIR}/build-strict-debug"
mkdir -p "$STRICT_DEBUG_BUILD_DIR"
$CMAKE -DCMAKE_BUILD_TYPE=Debug \
       -DCMAKE_CXX_FLAGS="$STRICT_FLAGS -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer" \
       -DCMAKE_MAKE_PROGRAM=$NINJA \
       -G Ninja \
       -S . \
       -B "$STRICT_DEBUG_BUILD_DIR"

if [ $? -ne 0 ]; then
    echo -e "${RED}CMake configuration failed for Debug!${NC}"
    exit 1
fi

echo -e "${YELLOW}Compiling Debug...${NC}"
DEBUG_LOG="${LOG_PREFIX}_debug.log"
$CMAKE --build "$STRICT_DEBUG_BUILD_DIR" --target all -j $(nproc) 2>&1 | tee "$DEBUG_LOG"

DEBUG_BUILD_STATUS=${PIPESTATUS[0]}
if [ $DEBUG_BUILD_STATUS -ne 0 ]; then
    echo -e "${RED}Build failed for Debug!${NC}"
    echo -e "${YELLOW}Check $DEBUG_LOG for details${NC}"
    
    # Show error summary
    echo -e "\n${RED}=== ERROR SUMMARY ===${NC}"
    grep -E "error:|warning:" "$DEBUG_LOG" | head -20
    exit 1
fi

# Summary
echo -e "\n${GREEN}============================================${NC}"
echo -e "${GREEN}STRICT BUILD COMPLETED SUCCESSFULLY!${NC}"
echo -e "${GREEN}============================================${NC}"
echo -e "Release build: ${STRICT_RELEASE_BUILD_DIR}/"
echo -e "Debug build:   ${STRICT_DEBUG_BUILD_DIR}/"
echo -e "Build logs:    ${LOG_PREFIX}_*.log"

# Count warnings if any slipped through
RELEASE_WARNINGS=$(grep -c "warning:" "$RELEASE_LOG" 2>/dev/null || echo 0)
DEBUG_WARNINGS=$(grep -c "warning:" "$DEBUG_LOG" 2>/dev/null || echo 0)

if [ "$RELEASE_WARNINGS" -gt 0 ] || [ "$DEBUG_WARNINGS" -gt 0 ]; then
    echo -e "\n${YELLOW}Warnings found:${NC}"
    echo -e "  Release: $RELEASE_WARNINGS warnings"
    echo -e "  Debug:   $DEBUG_WARNINGS warnings"
fi

echo -e "\n${GREEN}Ready to run examples:${NC}"
echo "  ./${STRICT_RELEASE_BUILD_DIR}/examples/examples <example_name>"
echo "  ./${STRICT_DEBUG_BUILD_DIR}/examples/examples <example_name>"