#!/bin/bash

# Enhanced build script with logging
# Builds both Release and Debug versions with proper logging

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}Building Shriven Zenith${NC}"
echo -e "${GREEN}============================================${NC}"

# Find tools
CMAKE=$(which cmake)
NINJA=$(which ninja)

# Create logs directory if it doesn't exist
LOGS_DIR="logs"
mkdir -p "$LOGS_DIR"

# Generate timestamp for log files
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_PREFIX="${LOGS_DIR}/build_${TIMESTAMP}"

# Build directories in cmake folder
CMAKE_DIR="cmake"
mkdir -p "$CMAKE_DIR"

# Build Release
echo -e "\n${YELLOW}Building RELEASE version...${NC}"
RELEASE_LOG="${LOG_PREFIX}_release.log"
RELEASE_BUILD_DIR="${CMAKE_DIR}/build-release"
mkdir -p "$RELEASE_BUILD_DIR"

echo "Configuring Release build..." | tee "$RELEASE_LOG"
$CMAKE -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=$NINJA -G Ninja -S . -B "$RELEASE_BUILD_DIR" 2>&1 | tee -a "$RELEASE_LOG"

if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo -e "${RED}CMake configuration failed for Release!${NC}"
    echo -e "${YELLOW}Check $RELEASE_LOG for details${NC}"
    exit 1
fi

echo "Cleaning Release build..." | tee -a "$RELEASE_LOG"
$CMAKE --build "$RELEASE_BUILD_DIR" --target clean -j $(nproc) 2>&1 | tee -a "$RELEASE_LOG"

echo "Compiling Release..." | tee -a "$RELEASE_LOG"
$CMAKE --build "$RELEASE_BUILD_DIR" --target all -j $(nproc) 2>&1 | tee -a "$RELEASE_LOG"

if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo -e "${RED}Build failed for Release!${NC}"
    echo -e "${YELLOW}Check $RELEASE_LOG for details${NC}"
    exit 1
fi

echo -e "${GREEN}Release build completed successfully${NC}"

# Build Debug
echo -e "\n${YELLOW}Building DEBUG version...${NC}"
DEBUG_LOG="${LOG_PREFIX}_debug.log"
DEBUG_BUILD_DIR="${CMAKE_DIR}/build-debug"
mkdir -p "$DEBUG_BUILD_DIR"

echo "Configuring Debug build..." | tee "$DEBUG_LOG"
$CMAKE -DCMAKE_BUILD_TYPE=Debug -DCMAKE_MAKE_PROGRAM=$NINJA -G Ninja -S . -B "$DEBUG_BUILD_DIR" 2>&1 | tee -a "$DEBUG_LOG"

if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo -e "${RED}CMake configuration failed for Debug!${NC}"
    echo -e "${YELLOW}Check $DEBUG_LOG for details${NC}"
    exit 1
fi

echo "Cleaning Debug build..." | tee -a "$DEBUG_LOG"
$CMAKE --build "$DEBUG_BUILD_DIR" --target clean -j $(nproc) 2>&1 | tee -a "$DEBUG_LOG"

echo "Compiling Debug..." | tee -a "$DEBUG_LOG"
$CMAKE --build "$DEBUG_BUILD_DIR" --target all -j $(nproc) 2>&1 | tee -a "$DEBUG_LOG"

if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo -e "${RED}Build failed for Debug!${NC}"
    echo -e "${YELLOW}Check $DEBUG_LOG for details${NC}"
    exit 1
fi

echo -e "${GREEN}Debug build completed successfully${NC}"

# Summary
echo -e "\n${GREEN}============================================${NC}"
echo -e "${GREEN}BUILD COMPLETED SUCCESSFULLY!${NC}"
echo -e "${GREEN}============================================${NC}"
echo -e "Release build: ${RELEASE_BUILD_DIR}/"
echo -e "Debug build:   ${DEBUG_BUILD_DIR}/"
echo -e "Build logs:    ${LOG_PREFIX}_*.log"

# Show any warnings
RELEASE_WARNINGS=$(grep -c "warning:" "$RELEASE_LOG" 2>/dev/null || echo 0)
DEBUG_WARNINGS=$(grep -c "warning:" "$DEBUG_LOG" 2>/dev/null || echo 0)

if [ "$RELEASE_WARNINGS" -gt 0 ] || [ "$DEBUG_WARNINGS" -gt 0 ]; then
    echo -e "\n${YELLOW}Warnings found:${NC}"
    echo -e "  Release: $RELEASE_WARNINGS warnings"
    echo -e "  Debug:   $DEBUG_WARNINGS warnings"
    echo -e "  Check log files for details"
fi

echo -e "\n${GREEN}Ready to run examples:${NC}"
echo "  ./${RELEASE_BUILD_DIR}/examples/examples <example_name>"
echo "  ./${DEBUG_BUILD_DIR}/examples/examples <example_name>"