# BlobIndex Tests

This directory contains various test files for the BlobIndex library.

## Test Files

### BLAKE3 Tests

- `blake3_test.cpp` - C++ test for BLAKE3 hash function
- `blake3_test.py` - Python test for BLAKE3 hash function

These tests verify that the BLAKE3 hash function works correctly in both C++ and Python implementations. They compute hashes of simple strings and compare the results.

### Header Format Tests

- `header_fix_test.cpp` - C++ test for header format compatibility
- `header_fix_test.py` - Python test for header format compatibility
- `header_test.cpp` - C++ test for basic header functionality
- `header_test.py` - Python test for basic header functionality

These tests verify that the header format is compatible between the C++ and Python implementations. They construct headers using different methods and compare the resulting checksums to ensure they match.

The `header_test` files focus on basic header functionality, while the `header_fix_test` files specifically address the byte order and struct packing issues that were identified during development.

## Running the Tests

### C++ Tests

```bash
# Build the tests
cd build
cmake ..
make blake3_test header_fix_test header_test

# Run the tests
./blake3_test
./header_fix_test
./header_test
```

### Python Tests

```bash
# Run the Python tests
cd blobindex/tests
python3 blake3_test.py
python3 header_fix_test.py
python3 header_test.py
```

## Purpose

These tests were created to diagnose and fix issues with the BLAKE3 header checksum mismatch between the C++ and Python implementations. They helped identify that the issue was related to byte order and struct packing differences between the two implementations.

The solution was to use explicit little-endian format in Python's struct.pack (`"<32sIIQQQQ"`) and a matching packed struct in C++ to ensure both implementations use the exact same memory layout for the header. 