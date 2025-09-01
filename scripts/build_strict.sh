#!/bin/bash
# build_strict.sh - MANDATORY build script per CLAUDE.md requirements

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_ROOT}/cmake/build-strict-debug"

echo "============================================"
echo "Building with MAXIMUM STRICTNESS"
echo "All warnings are errors!"
echo "============================================"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "Configuring CMake..."
cmake ../.. -DCMAKE_BUILD_TYPE=Debug -G Ninja

echo "Building with Ninja..."
ninja

echo "============================================"
echo "Build SUCCESSFUL with MAXIMUM STRICTNESS!"
echo "============================================"
