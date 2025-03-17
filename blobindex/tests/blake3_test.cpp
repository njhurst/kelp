#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <blake3.h>

// Function to compute BLAKE3 hash
std::vector<uint8_t> blake3_hash(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hash(32);
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

int main() {
    // Test with a simple string
    std::string test_string = "blobindex";
    std::vector<uint8_t> test_data(test_string.begin(), test_string.end());
    
    std::cout << "Input: " << test_string << std::endl;
    
    // Compute hash
    std::vector<uint8_t> hash = blake3_hash(test_data);
    
    // Print hash
    std::cout << "BLAKE3 hash: ";
    print_hex(hash);
    
    // Test with header-like data
    std::vector<uint8_t> header_data(64, 0);  // 64 bytes of zeros
    std::vector<uint8_t> header_hash = blake3_hash(header_data);
    
    std::cout << "BLAKE3 hash of 64 zeros: ";
    print_hex(header_hash);
    
    return 0;
} 