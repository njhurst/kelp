#!/bin/bash
# Script to run all BlobIndex tests

set -e  # Exit on error

# Directory of this script
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/../.." && pwd )"

# Build directory
BUILD_DIR="$PROJECT_ROOT/build"

# Ensure build directory exists
mkdir -p "$BUILD_DIR"

# Build the tests
echo "Building tests..."
cd "$BUILD_DIR"
cmake ..
make blake3_test header_fix_test header_test

# Run C++ tests
echo -e "\nRunning C++ tests..."
echo -e "\n=== BLAKE3 Test ==="
./blake3_test

echo -e "\n=== Header Fix Test ==="
./header_fix_test

echo -e "\n=== Header Test ==="
./header_test

# Run Python tests
echo -e "\nRunning Python tests..."
cd "$SCRIPT_DIR"

echo -e "\n=== Python BLAKE3 Test ==="
python3 blake3_test.py

echo -e "\n=== Python Header Fix Test ==="
python3 header_fix_test.py

echo -e "\n=== Python Header Test ==="
python3 header_test.py

# Run the main tests
echo -e "\nRunning main tests..."
cd "$BUILD_DIR"
./blobindex_test

# Run comparison tests
echo -e "\nRunning comparison tests..."
cd "$PROJECT_ROOT"
python3 run_tests.py

echo -e "\nAll tests completed successfully!" 