#!/bin/bash
# build_coverage_gcc.sh - Build with GCC coverage instrumentation (gcov/lcov)

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}Building with Coverage Analysis (GCC/LCOV)${NC}"
echo -e "${GREEN}============================================${NC}"

# Check for required tools
if ! command -v gcov &> /dev/null; then
    echo -e "${RED}Error: gcov not found.${NC}"
    exit 1
fi

if ! command -v lcov &> /dev/null; then
    echo -e "${YELLOW}Warning: lcov not found. Installing...${NC}"
    sudo apt-get update && sudo apt-get install -y lcov
fi

if ! command -v genhtml &> /dev/null; then
    echo -e "${YELLOW}Warning: genhtml not found (part of lcov).${NC}"
    exit 1
fi

# Set build directory
BUILD_DIR="cmake/build-coverage-gcc"
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

# Configure with GCC and coverage flags
echo -e "${GREEN}Configuring with coverage instrumentation...${NC}"
cmake ../.. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-g -O0 --coverage -fprofile-arcs -ftest-coverage" \
    -DCMAKE_C_FLAGS="-g -O0 --coverage -fprofile-arcs -ftest-coverage" \
    -DCMAKE_EXE_LINKER_FLAGS="--coverage" \
    -DCMAKE_SHARED_LINKER_FLAGS="--coverage" \
    -G Ninja

# Build
echo -e "${GREEN}Building...${NC}"
ninja

# Initialize coverage
echo -e "${GREEN}Initializing coverage baseline...${NC}"
lcov --capture --initial --directory . --output-file coverage_base.info --no-external

# Run tests
echo -e "${GREEN}Running tests to generate coverage data...${NC}"
for test in tests/test_*; do
    if [ -x "$test" ] && [[ "$test" != *.gcda ]] && [[ "$test" != *.gcno ]]; then
        echo -e "${YELLOW}Running $(basename $test)...${NC}"
        $test || true  # Continue even if test fails
    fi
done

# Capture coverage data
echo -e "${GREEN}Capturing coverage data...${NC}"
lcov --capture --directory . --output-file coverage_test.info --no-external

# Combine baseline and test coverage
echo -e "${GREEN}Combining coverage data...${NC}"
lcov --add-tracefile coverage_base.info --add-tracefile coverage_test.info --output-file coverage_total.info

# Remove unwanted files from coverage
echo -e "${GREEN}Filtering coverage data...${NC}"
lcov --remove coverage_total.info \
    '*/test/*' \
    '*/tests/*' \
    '*/examples/*' \
    '/usr/*' \
    '*/gtest/*' \
    --output-file coverage_filtered.info

# Generate HTML report
echo -e "${GREEN}Generating HTML coverage report...${NC}"
genhtml coverage_filtered.info \
    --output-directory coverage_html \
    --demangle-cpp \
    --num-spaces 4 \
    --sort \
    --title "Shriven Zenith Coverage Report" \
    --function-coverage \
    --branch-coverage \
    --legend

# Generate text summary
echo -e "${GREEN}Generating coverage summary...${NC}"
lcov --summary coverage_filtered.info

# Report locations
echo ""
echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}Coverage Analysis Complete!${NC}"
echo -e "${GREEN}============================================${NC}"
echo ""
echo -e "${YELLOW}Reports generated:${NC}"
echo "  HTML Report: file://$PROJECT_ROOT/$BUILD_DIR/coverage_html/index.html"
echo "  LCOV Data: $PROJECT_ROOT/$BUILD_DIR/coverage_filtered.info"
echo ""
echo -e "${YELLOW}To view HTML report:${NC}"
echo "  xdg-open $PROJECT_ROOT/$BUILD_DIR/coverage_html/index.html"
echo ""

# Show coverage percentages
echo -e "${YELLOW}Coverage Summary:${NC}"
lcov --summary coverage_filtered.info 2>&1 | grep -E "lines|functions|branches"