#!/usr/bin/env python3
import blake3
import binascii

def print_hex(data):
    """Print bytes in hex format"""
    print(binascii.hexlify(data).decode())

# Test with a simple string
test_string = b"blobindex"
print(f"Input: {test_string.decode()}")

# Compute hash
hash_value = blake3.blake3(test_string).digest()
print("BLAKE3 hash: ", end="")
print_hex(hash_value)

# Test with header-like data
header_data = b"\x00" * 64  # 64 bytes of zeros
header_hash = blake3.blake3(header_data).digest()
print("BLAKE3 hash of 64 zeros: ", end="")
print_hex(header_hash) 