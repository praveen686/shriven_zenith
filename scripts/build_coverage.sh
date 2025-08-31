#!/bin/bash
# build_coverage.sh - Build with coverage instrumentation and generate reports

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}Building with Coverage Analysis (LLVM)${NC}"
echo -e "${GREEN}============================================${NC}"

# Check for Clang
if ! command -v clang++ &> /dev/null; then
    echo -e "${RED}Error: Clang++ not found. Coverage requires Clang compiler.${NC}"
    echo "Install with: sudo apt install clang llvm"
    exit 1
fi

# Check for llvm-cov and llvm-profdata
if ! command -v llvm-cov &> /dev/null || ! command -v llvm-profdata &> /dev/null; then
    echo -e "${RED}Error: LLVM coverage tools not found.${NC}"
    echo "Install with: sudo apt install llvm"
    exit 1
fi

# Get Clang version for tool compatibility
CLANG_VERSION=$(clang++ --version | grep -oP 'version \K[0-9]+' | head -1)
echo -e "${YELLOW}Using Clang version: ${CLANG_VERSION}${NC}"

# Set build directory
BUILD_DIR="cmake/build-coverage"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_ROOT"

# Clean previous coverage build
if [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Cleaning previous coverage build...${NC}"
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with Clang and coverage
# Note: Clang has issues with CMake's pthread detection, force it
echo -e "${GREEN}Configuring with coverage instrumentation...${NC}"
cmake ../.. \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=Coverage \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS} -pthread" \
    -DCMAKE_C_FLAGS="${CMAKE_C_FLAGS} -pthread" \
    -DCMAKE_EXE_LINKER_FLAGS="${CMAKE_EXE_LINKER_FLAGS} -pthread" \
    -DCMAKE_THREAD_PREFER_PTHREAD=ON \
    -DTHREADS_PREFER_PTHREAD_FLAG=ON \
    -G Ninja

# Build
echo -e "${GREEN}Building...${NC}"
ninja

# Create coverage directory
mkdir -p coverage

# Run tests to generate coverage data
echo -e "${GREEN}Running tests to generate coverage data...${NC}"
export LLVM_PROFILE_FILE="coverage/%p-%m.profraw"

# Run each test executable
for test in tests/test_*; do
    if [ -x "$test" ] && [[ ! "$test" == *.profraw ]]; then
        echo -e "${YELLOW}Running $(basename $test)...${NC}"
        $test || true  # Continue even if test fails
    fi
done

# Merge raw profiles
echo -e "${GREEN}Merging coverage profiles...${NC}"
llvm-profdata merge -sparse coverage/*.profraw -o coverage/coverage.profdata

# Generate summary report
echo -e "${GREEN}Generating coverage summary...${NC}"
echo ""

# Collect all test binaries for coverage report
TEST_BINARIES=""
for test in tests/test_*; do
    if [ -x "$test" ] && [[ ! "$test" == *.profraw ]]; then
        TEST_BINARIES="$TEST_BINARIES -object $test"
    fi
done

# Generate text report
llvm-cov report $TEST_BINARIES \
    -instr-profile=coverage/coverage.profdata \
    -ignore-filename-regex='.*test.*' \
    -ignore-filename-regex='.*/usr/.*' \
    -ignore-filename-regex='.*/examples/.*'

# Generate HTML report
echo -e "${GREEN}Generating HTML coverage report...${NC}"
llvm-cov show $TEST_BINARIES \
    -instr-profile=coverage/coverage.profdata \
    -format=html \
    -output-dir=coverage/html \
    -ignore-filename-regex='.*test.*' \
    -ignore-filename-regex='.*/usr/.*' \
    -ignore-filename-regex='.*/examples/.*' \
    -show-line-counts-or-regions \
    -show-instantiations \
    -show-expansions

# Generate LCOV for CI integration
echo -e "${GREEN}Generating LCOV report for CI...${NC}"
llvm-cov export $TEST_BINARIES \
    -instr-profile=coverage/coverage.profdata \
    -format=lcov \
    -ignore-filename-regex='.*test.*' \
    -ignore-filename-regex='.*/usr/.*' \
    -ignore-filename-regex='.*/examples/.*' \
    > coverage/coverage.lcov

# Generate JSON summary
llvm-cov export $TEST_BINARIES \
    -instr-profile=coverage/coverage.profdata \
    -format=text \
    -summary-only \
    > coverage/summary.json

# Report locations
echo ""
echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}Coverage Analysis Complete!${NC}"
echo -e "${GREEN}============================================${NC}"
echo ""
echo -e "${YELLOW}Reports generated:${NC}"
echo "  HTML Report: file://$PROJECT_ROOT/$BUILD_DIR/coverage/html/index.html"
echo "  LCOV Report: $PROJECT_ROOT/$BUILD_DIR/coverage/coverage.lcov"
echo "  JSON Summary: $PROJECT_ROOT/$BUILD_DIR/coverage/summary.json"
echo ""
echo -e "${YELLOW}To view HTML report:${NC}"
echo "  xdg-open $PROJECT_ROOT/$BUILD_DIR/coverage/html/index.html"
echo ""

# Show coverage summary
echo -e "${YELLOW}Coverage Summary:${NC}"
llvm-cov report $TEST_BINARIES \
    -instr-profile=coverage/coverage.profdata \
    -ignore-filename-regex='.*test.*' \
    -ignore-filename-regex='.*/usr/.*' \
    -ignore-filename-regex='.*/examples/.*' \
    | tail -1