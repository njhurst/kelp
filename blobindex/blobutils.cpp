#include "blobindex.hpp"
#include <blake3.h>
#include <zstd.h>
#include <stdexcept>
#include <cstring>

// Global constants
std::array<uint8_t, 32> MAGIC = []() {
    std::array<uint8_t, 32> magic;
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, "blobindex", 9);
    blake3_hasher_finalize(&hasher, magic.data(), magic.size());
    return magic;
}();

constexpr int CUSTOM_REF_TAG = 1000;  // The tag we're using for our 8-byte references
constexpr int CUSTOM_WEAK_REF_TAG = 1001;  // The tag we're using for our 8-byte weak references

std::vector<uint8_t> zstandard_compress(const std::vector<uint8_t>& data) {
    size_t const max_dst_size = ZSTD_compressBound(data.size());
    std::vector<uint8_t> compressed(max_dst_size);
    
    size_t const compressed_size = ZSTD_compress(
        compressed.data(), compressed.size(),
        data.data(), data.size(),
        1  // Compression level
    );
    
    if (ZSTD_isError(compressed_size)) {
        throw std::runtime_error(std::string("Zstandard compression error: ") + ZSTD_getErrorName(compressed_size));
    }
    
    compressed.resize(compressed_size);
    return compressed;
}

std::vector<uint8_t> zstandard_decompress(const std::vector<uint8_t>& data) {
    unsigned long long const decompressed_size = ZSTD_getFrameContentSize(data.data(), data.size());
    
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR) {
        throw std::runtime_error("Zstandard: not a valid compressed frame");
    }
    
    if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        throw std::runtime_error("Zstandard: original size unknown");
    }
    
    std::vector<uint8_t> decompressed(decompressed_size);
    
    size_t const result = ZSTD_decompress(
        decompressed.data(), decompressed.size(),
        data.data(), data.size()
    );
    
    if (ZSTD_isError(result)) {
        throw std::runtime_error(std::string("Zstandard decompression error: ") + ZSTD_getErrorName(result));
    }
    
    if (result != decompressed_size) {
        throw std::runtime_error("Zstandard: decompressed size mismatch");
    }
    
    return decompressed;
}

std::array<uint8_t, 32> blake3_hash(const std::vector<uint8_t>& data) {
    std::array<uint8_t, 32> hash;
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data.data(), data.size());
    blake3_hasher_finalize(&hasher, hash.data(), hash.size());
    return hash;
}

std::vector<uint8_t> int_to_bytes(uint32_t value) {
    std::vector<uint8_t> bytes(4);
    bytes[0] = (value >> 0) & 0xFF;
    bytes[1] = (value >> 8) & 0xFF;
    bytes[2] = (value >> 16) & 0xFF;
    bytes[3] = (value >> 24) & 0xFF;
    return bytes;
}

uint32_t bytes_to_int(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 4) {
        throw std::runtime_error("Not enough bytes to convert to int");
    }
    return (bytes[0] << 0) | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
} 