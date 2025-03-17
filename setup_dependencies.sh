#!/bin/bash
set -e

# Create external directory if it doesn't exist
mkdir -p external

# Download and set up BLAKE3
if [ ! -d "external/blake3" ]; then
    echo "Downloading BLAKE3..."
    git clone https://github.com/BLAKE3-team/BLAKE3.git external/blake3_repo
    mkdir -p external/blake3
    cp external/blake3_repo/c/blake3*.h external/blake3/
    cp external/blake3_repo/c/blake3*.c external/blake3/
    rm -rf external/blake3_repo
    echo "BLAKE3 set up successfully."
else
    echo "BLAKE3 already exists, skipping."
fi

# Check if zstd is installed
if ! pkg-config --exists libzstd; then
    echo "Zstandard library not found. Please install it using your package manager."
    echo "For example:"
    echo "  Ubuntu/Debian: sudo apt-get install libzstd-dev"
    echo "  Fedora/RHEL: sudo dnf install libzstd-devel"
    echo "  macOS: brew install zstd"
    exit 1
else
    echo "Zstandard library found."
fi

# Check if Google Test is installed
if ! pkg-config --exists gtest; then
    echo "Google Test not found. Please install it using your package manager."
    echo "For example:"
    echo "  Ubuntu/Debian: sudo apt-get install libgtest-dev"
    echo "  Fedora/RHEL: sudo dnf install gtest-devel"
    echo "  macOS: brew install googletest"
    exit 1
else
    echo "Google Test found."
fi

echo "All dependencies set up successfully." 