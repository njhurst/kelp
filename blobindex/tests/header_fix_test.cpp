#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <array>
#include <cstring>
#include <blake3.h>

// Function to compute BLAKE3 hash
std::array<uint8_t, 32> blake3_hash(const std::vector<uint8_t>& data) {
    std::array<uint8_t, 32> hash;
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data.data(), data.size());
    blake3_hasher_finalize(&hasher, hash.data(), hash.size());
    return hash;
}

// Function to print bytes in hex format
void print_hex(const std::vector<uint8_t>& bytes) {
    for (const auto& byte : bytes) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    std::cout << std::endl;
}

void print_hex(const std::array<uint8_t, 32>& bytes) {
    for (const auto& byte : bytes) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    std::cout << std::endl;
}

// Function to print a specific range of bytes in hex format
void print_hex_range(const std::vector<uint8_t>& bytes, size_t start, size_t length) {
    for (size_t i = start; i < start + length && i < bytes.size(); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]);
    }
    std::cout << std::endl;
}

// Struct that matches Python's struct.pack("32sIIQQQQ")
#pragma pack(push, 1)
struct HeaderStruct {
    uint8_t magic[32];
    uint32_t version;
    uint32_t volume_prefix;
    uint64_t primary_offset;
    uint64_t secondary_offset;
    uint64_t root_offset;
    uint64_t tail_pos;
};
#pragma pack(pop)

int main() {
    // Create a MAGIC array similar to the one in the codebase
    std::array<uint8_t, 32> MAGIC;
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, "blobindex", 9);
    blake3_hasher_finalize(&hasher, MAGIC.data(), MAGIC.size());
    
    // Print MAGIC
    std::cout << "MAGIC: ";
    print_hex(MAGIC);
    
    std::cout << "\n=== CURRENT IMPLEMENTATION (MANUAL BYTE PACKING) ===" << std::endl;
    
    // Create a header similar to save_index in blobvolume.cpp
    std::vector<uint8_t> header;
    
    // Magic (32 bytes)
    header.insert(header.end(), MAGIC.begin(), MAGIC.end());
    
    // Version (4 bytes) - use consistent byte order
    uint32_t version = 13;
    header.push_back(version & 0xFF);
    header.push_back((version >> 8) & 0xFF);
    header.push_back((version >> 16) & 0xFF);
    header.push_back((version >> 24) & 0xFF);
    
    // Volume prefix (4 bytes) - use consistent byte order
    uint32_t volume_prefix = 0x12345678;
    header.push_back(volume_prefix & 0xFF);
    header.push_back((volume_prefix >> 8) & 0xFF);
    header.push_back((volume_prefix >> 16) & 0xFF);
    header.push_back((volume_prefix >> 24) & 0xFF);
    
    // Primary index offset (8 bytes) - use consistent byte order
    uint64_t primary_offset = 4096;
    header.push_back(primary_offset & 0xFF);
    header.push_back((primary_offset >> 8) & 0xFF);
    header.push_back((primary_offset >> 16) & 0xFF);
    header.push_back((primary_offset >> 24) & 0xFF);
    header.push_back((primary_offset >> 32) & 0xFF);
    header.push_back((primary_offset >> 40) & 0xFF);
    header.push_back((primary_offset >> 48) & 0xFF);
    header.push_back((primary_offset >> 56) & 0xFF);
    
    // Secondary index offset (8 bytes) - use consistent byte order
    uint64_t secondary_offset = 8192;
    header.push_back(secondary_offset & 0xFF);
    header.push_back((secondary_offset >> 8) & 0xFF);
    header.push_back((secondary_offset >> 16) & 0xFF);
    header.push_back((secondary_offset >> 24) & 0xFF);
    header.push_back((secondary_offset >> 32) & 0xFF);
    header.push_back((secondary_offset >> 40) & 0xFF);
    header.push_back((secondary_offset >> 48) & 0xFF);
    header.push_back((secondary_offset >> 56) & 0xFF);
    
    // Root index offset (8 bytes) - use consistent byte order
    uint64_t root_offset = 12288;
    header.push_back(root_offset & 0xFF);
    header.push_back((root_offset >> 8) & 0xFF);
    header.push_back((root_offset >> 16) & 0xFF);
    header.push_back((root_offset >> 24) & 0xFF);
    header.push_back((root_offset >> 32) & 0xFF);
    header.push_back((root_offset >> 40) & 0xFF);
    header.push_back((root_offset >> 48) & 0xFF);
    header.push_back((root_offset >> 56) & 0xFF);
    
    // Tail position (8 bytes) - use consistent byte order
    uint64_t tail_pos = 16384;
    header.push_back(tail_pos & 0xFF);
    header.push_back((tail_pos >> 8) & 0xFF);
    header.push_back((tail_pos >> 16) & 0xFF);
    header.push_back((tail_pos >> 24) & 0xFF);
    header.push_back((tail_pos >> 32) & 0xFF);
    header.push_back((tail_pos >> 40) & 0xFF);
    header.push_back((tail_pos >> 48) & 0xFF);
    header.push_back((tail_pos >> 56) & 0xFF);
    
    // Print header size
    std::cout << "Header size: " << header.size() << " bytes" << std::endl;
    
    // Compute checksum
    std::array<uint8_t, 32> checksum = blake3_hash(header);
    
    // Print checksum
    std::cout << "Header checksum: ";
    print_hex(checksum);
    
    // Print individual components as packed
    std::cout << "Individual components as packed:" << std::endl;
    std::cout << "MAGIC (32 bytes): ";
    print_hex_range(header, 0, 32);
    std::cout << "Version (4 bytes): ";
    print_hex_range(header, 32, 4);
    std::cout << "Volume prefix (4 bytes): ";
    print_hex_range(header, 36, 4);
    std::cout << "Primary offset (8 bytes): ";
    print_hex_range(header, 40, 8);
    std::cout << "Secondary offset (8 bytes): ";
    print_hex_range(header, 48, 8);
    std::cout << "Root offset (8 bytes): ";
    print_hex_range(header, 56, 8);
    std::cout << "Tail position (8 bytes): ";
    print_hex_range(header, 64, 8);
    
    std::cout << "\n=== FIXED IMPLEMENTATION (STRUCT PACKING) ===" << std::endl;
    
    // Create a header using struct packing to match Python's struct.pack
    HeaderStruct header_struct;
    std::memcpy(header_struct.magic, MAGIC.data(), 32);
    header_struct.version = version;
    header_struct.volume_prefix = volume_prefix;
    header_struct.primary_offset = primary_offset;
    header_struct.secondary_offset = secondary_offset;
    header_struct.root_offset = root_offset;
    header_struct.tail_pos = tail_pos;
    
    // Convert struct to vector
    std::vector<uint8_t> header_fixed(sizeof(HeaderStruct));
    std::memcpy(header_fixed.data(), &header_struct, sizeof(HeaderStruct));
    
    // Print header size
    std::cout << "Header size: " << header_fixed.size() << " bytes" << std::endl;
    
    // Compute checksum
    std::array<uint8_t, 32> checksum_fixed = blake3_hash(header_fixed);
    
    // Print checksum
    std::cout << "Header checksum: ";
    print_hex(checksum_fixed);
    
    // Print individual components as packed
    std::cout << "Individual components as packed:" << std::endl;
    std::cout << "MAGIC (32 bytes): ";
    print_hex_range(header_fixed, 0, 32);
    std::cout << "Version (4 bytes): ";
    print_hex_range(header_fixed, 32, 4);
    std::cout << "Volume prefix (4 bytes): ";
    print_hex_range(header_fixed, 36, 4);
    std::cout << "Primary offset (8 bytes): ";
    print_hex_range(header_fixed, 40, 8);
    std::cout << "Secondary offset (8 bytes): ";
    print_hex_range(header_fixed, 48, 8);
    std::cout << "Root offset (8 bytes): ";
    print_hex_range(header_fixed, 56, 8);
    std::cout << "Tail position (8 bytes): ";
    print_hex_range(header_fixed, 64, 8);
    
    return 0;
} 