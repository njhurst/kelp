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

# Create a header similar to save_index in blobindex4.py
# Using the same values as in the C++ test for comparison
version = 13
volume_prefix = 0x12345678
primary_offset = 4096
secondary_offset = 8192
root_offset = 12288
tail_pos = 16384

# Construct the header using struct.pack
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

# Print header content
print("Header content: ", end="")
print_hex(header[:16])
print("...")

# Compute checksum
checksum = blake3.blake3(header).digest()

# Print checksum
print("Header checksum: ", end="")
print_hex(checksum)

# Create a complete header with checksum
complete_header = header + checksum

# Print complete header size
print(f"Complete header size: {len(complete_header)} bytes")

# For debugging, let's also print the individual components as they're packed
print("\nIndividual components as packed:")
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