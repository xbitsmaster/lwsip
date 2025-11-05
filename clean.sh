#!/bin/bash

# Clean script for lwsip
# Removes all build artifacts

BUILD_DIR="build"

# Clean build directory
if [ -d "$BUILD_DIR" ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    echo "Build directory cleaned."
else
    echo "Build directory does not exist."
fi

echo "Clean complete."
