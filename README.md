# Kelp Distributed File System

A robust distributed file system designed as an alternative to SeaweedFS, combining ideas from Ceph and IPFS.

## Overview

Kelp is designed to be easy to use, deploy, and maintain. It is built for fault-tolerance, moderate security, and efficiency. The project was motivated by difficulties with a SeaweedFS deployment that led to data loss. Kelp is built from the ground up to provide:

- Highly reliable writes
- Erasure coding for data durability
- Simple, easy to understand design
- Only necessary components for clarity and maintainability

The system has three main levels:
1. **Blades** - The lowest level, responsible for storing data
2. **Thalus** - The middle level, responsible for storing metadata
3. **Kelp** - The highest level, managing the filesystem

The total address space is 64-bit indexing into 8-byte units, providing a total of 128EB of addressable storage.

## Project Components

### BlobIndex

BlobIndex is a core component of Kelp, providing blob storage with indexing, compression, and integrity verification. It serves as the foundation for the Blade storage layer.

#### Features

- Compression using Zstandard
- Integrity verification using BLAKE3 hashing
- Growable blobs that can be resized efficiently
- Magic numbers for blob identification
- Metadata support for garbage collection

## Project Structure

The project is organized as follows:

```
blobindex/               # Blob storage component
├── blobindex.hpp         # Main header file
├── blobstorage.cpp       # Storage implementation
├── blobvolume.cpp        # Volume management
├── blobutils.cpp         # Utility functions
├── bloblocks.cpp         # Locking mechanism
├── blobindex_test.cpp    # Unit tests
├── blobindex_example.cpp # Example usage
├── blobindex_compare.cpp # Comparison tool
├── blobindex4.py         # Python implementation
├── compare_implementations.py # Python/C++ comparison tool
└── tests/                # Additional tests
    ├── blake3_test.cpp   # BLAKE3 hash function test
    ├── blake3_test.py    # Python BLAKE3 test
    ├── header_fix_test.cpp # Header format compatibility test
    ├── header_fix_test.py  # Python header format compatibility test
    ├── header_test.cpp   # Basic header functionality test
    ├── header_test.py    # Python basic header functionality test
    ├── README.md         # Tests documentation
    └── run_all_tests.sh  # Script to run all tests

design/                  # Design documents
├── kelp-overview.md      # Overview of the Kelp system
├── blobvolume.md         # BlobVolume design
└── ...                   # Other design documents

external/                # External dependencies
├── blake3/               # BLAKE3 hashing library
└── zstd/                 # Zstandard compression library

build/                   # Build directory
```

## Requirements

- C++17 compatible compiler
- CMake 3.10 or higher
- Google Test (for running tests)
- Python 3.12 or higher
- Dependencies:
  - BLAKE3 library
  - Zstandard library
  - NumPy
  - CBOR2
  - nlohmann-json
  - psutil
  - scipy

## Building

```bash
# Clone the repository
git clone https://github.com/yourusername/kelp.git
cd kelp

# Create a build directory
mkdir build
cd build

# Configure and build
cmake ..
make

# Run tests
make test
```

### Running All Tests

To run all tests, including the BLAKE3 and header format tests, you can use the provided script:

```bash
# Run all tests
./blobindex/tests/run_all_tests.sh
```

This script will build and run all C++ and Python tests, including the main tests and comparison tests.

## BlobIndex Usage

Here's a simple example of how to use the BlobIndex library:

```cpp
#include "blobindex/blobindex.hpp"
#include <iostream>
#include <string>
#include <vector>

int main() {
    // Create a new volume
    BlobVolume volume("example.dat", true);
    
    // Add a blob
    std::string text = "Hello, BlobIndex!";
    std::vector<uint8_t> data(text.begin(), text.end());
    int blob_id = volume.add_blob(data);
    
    // Read the blob back
    std::vector<uint8_t> read_data = volume.read_blob(blob_id);
    std::string read_text(read_data.begin(), read_data.end());
    std::cout << "Read blob: " << read_text << std::endl;
    
    // Add a compressed blob
    std::string compressed_text = "This is a compressed blob.";
    std::vector<uint8_t> compressed_data(compressed_text.begin(), compressed_text.end());
    int compressed_blob_id = volume.add_blob(compressed_data, BlobVolume::FLAG_COMPRESSED);
    
    // Close the volume
    volume.close();
    
    return 0;
}
```

## Python Usage

The library also includes a Python implementation that is compatible with the C++ version:

```python
from blobindex.blobindex4 import BlobVolume

# Create a new volume
volume = BlobVolume("example.dat", create=True)

# Add a blob
data = b"Hello, BlobIndex!"
blob_id = volume.add_blob(data)

# Read the blob back
read_data = volume.read_blob(blob_id)
print(f"Read blob: {read_data.decode()}")

# Add a compressed blob
compressed_data = b"This is a compressed blob."
compressed_blob_id = volume.add_blob(compressed_data, flags=BlobVolume.FLAGS['compressed'])

# Close the volume
volume.close()
```

## BlobIndex API Reference

### BlobVolume

The main class for managing blob storage.

#### Constructor

```cpp
BlobVolume(const std::string& filename, bool create = false);
```

- `filename`: The path to the blob volume file
- `create`: Whether to create a new volume or open an existing one

#### Methods

```cpp
int add_blob(const std::vector<uint8_t>& data, uint32_t flags = 0);
std::vector<uint8_t> read_blob(int blob_id);
void write_blob(int blob_id, const std::vector<uint8_t>& data);
void delete_blob(int blob_id);
bool resize_blob(int blob_id, uint32_t new_size);
std::tuple<uint64_t, uint32_t, uint32_t> get_blob_info(int blob_id);
void validate_all();
void close();
```

#### Flags

```cpp
static constexpr uint32_t FLAG_METADATA = 0x01;   // Blob contains metadata
static constexpr uint32_t FLAG_GROWABLE = 0x02;   // Blob can be resized
static constexpr uint32_t FLAG_COMPRESSED = 0x04; // Blob is compressed
static constexpr uint32_t FLAG_BLAKE3 = 0x08;     // Blob has a BLAKE3 hash
static constexpr uint32_t FLAG_MAGICED = 0x10;    // Blob has a magic number
static constexpr uint32_t FLAG_DELETED = 0x80;    // Blob is deleted
```

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Acknowledgments

The BlobIndex component is a C++ port of the original Python BlobIndex implementation.