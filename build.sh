#!/bin/bash

# Build script for lwsip
# This script ensures all build artifacts stay in the build directory

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

BUILD_DIR="build"
BUILD_TYPE="${1:-Release}"

echo -e "${GREEN}=== Building lwsip ===${NC}"
echo "Build type: $BUILD_TYPE"
echo "Build directory: $BUILD_DIR"

# Create build directory if it doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Creating build directory...${NC}"
    mkdir -p "$BUILD_DIR"
fi

# Navigate to build directory
cd "$BUILD_DIR"

# Run CMake
echo -e "${GREEN}Running CMake...${NC}"
cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" ..

# Build
echo -e "${GREEN}Building...${NC}"
cmake --build . -- -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo -e "${GREEN}=== Build complete ===${NC}"
echo -e "Executable: ${GREEN}$BUILD_DIR/bin/lwsip${NC}"
