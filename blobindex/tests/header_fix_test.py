#!/usr/bin/env python3
import struct
import binascii
import blake3

def print_hex(data):
    """Print bytes in hex format"""
    print(binascii.hexlify(data).decode())

# Create MAGIC similar to the one in the codebase
MAGIC = blake3.blake3(b"blobindex").digest()
print("MAGIC: ", end="")
print_hex(MAGIC)

# Define test values (same as in C++ test)
version = 13
volume_prefix = 0x12345678
primary_offset = 4096
secondary_offset = 8192
root_offset = 12288
tail_pos = 16384

print("\n=== CURRENT IMPLEMENTATION (PYTHON STRUCT.PACK) ===")

# Construct the header using struct.pack as in blobindex4.py
header = struct.pack("32sIIQQQQ", 
                    MAGIC,
                    version, 
                    volume_prefix, 
                    primary_offset, 
                    secondary_offset, 
                    root_offset, 
                    tail_pos)

# Print header size
print(f"Header size: {len(header)} bytes")

# Compute checksum
checksum = blake3.blake3(header).digest()

# Print checksum
print("Header checksum: ", end="")
print_hex(checksum)

# Print individual components as packed
print("Individual components as packed:")
print("MAGIC (32 bytes): ", end="")
print_hex(MAGIC)

print("Version (4 bytes): ", end="")
version_bytes = struct.pack("I", version)
print_hex(version_bytes)

print("Volume prefix (4 bytes): ", end="")
prefix_bytes = struct.pack("I", volume_prefix)
print_hex(prefix_bytes)

print("Primary offset (8 bytes): ", end="")
primary_bytes = struct.pack("Q", primary_offset)
print_hex(primary_bytes)

print("Secondary offset (8 bytes): ", end="")
secondary_bytes = struct.pack("Q", secondary_offset)
print_hex(secondary_bytes)

print("Root offset (8 bytes): ", end="")
root_bytes = struct.pack("Q", root_offset)
print_hex(root_bytes)

print("Tail position (8 bytes): ", end="")
tail_bytes = struct.pack("Q", tail_pos)
print_hex(tail_bytes)

print("\n=== FIXED IMPLEMENTATION (COMPATIBLE WITH C++) ===")

# Use a format that matches the C++ implementation
# Note: Python's struct.pack uses native byte order by default
# We need to use '<' to specify little-endian byte order to match C++
header_fixed = struct.pack("<32sIIQQQQ", 
                          MAGIC,
                          version, 
                          volume_prefix, 
                          primary_offset, 
                          secondary_offset, 
                          root_offset, 
                          tail_pos)

# Print header size
print(f"Header size: {len(header_fixed)} bytes")

# Compute checksum
checksum_fixed = blake3.blake3(header_fixed).digest()

# Print checksum
print("Header checksum: ", end="")
print_hex(checksum_fixed)

# Print individual components as packed
print("Individual components as packed:")
print("MAGIC (32 bytes): ", end="")
print_hex(MAGIC)

print("Version (4 bytes): ", end="")
version_bytes = struct.pack("<I", version)
print_hex(version_bytes)

print("Volume prefix (4 bytes): ", end="")
prefix_bytes = struct.pack("<I", volume_prefix)
print_hex(prefix_bytes)

print("Primary offset (8 bytes): ", end="")
primary_bytes = struct.pack("<Q", primary_offset)
print_hex(primary_bytes)

print("Secondary offset (8 bytes): ", end="")
secondary_bytes = struct.pack("<Q", secondary_offset)
print_hex(secondary_bytes)

print("Root offset (8 bytes): ", end="")
root_bytes = struct.pack("<Q", root_offset)
print_hex(root_bytes)

print("Tail position (8 bytes): ", end="")
tail_bytes = struct.pack("<Q", tail_pos)
print_hex(tail_bytes)

# Compare with C++ checksum
print("\n=== COMPARISON ===")
print("Python original checksum: ", end="")
print_hex(checksum)
print("Python fixed checksum: ", end="")
print_hex(checksum_fixed)
print("Expected C++ checksum: 5549bdd001937892edb0d492e049dfa83cb9685f05842c1086c0dc1eb945a16b") 