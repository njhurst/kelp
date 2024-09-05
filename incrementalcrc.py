import zlib

# CRC-64-ECMA polynomial and initial value
POLY = 0xC96C5795D7870F42
INIT = 0xFFFFFFFFFFFFFFFF

def crc64(data):
    """Calculate the CRC-64-ECMA of the given data."""
    crc = INIT
    for byte in data:
        crc ^= byte << 56
        for _ in range(8):
            crc = (crc << 1) ^ POLY if crc & (1 << 63) else crc << 1
    return crc ^ INIT

def generate_crc_table():
    """Generate a table for faster CRC calculation."""
    table = []
    for i in range(256):
        crc = i << 56
        for _ in range(8):
            crc = (crc << 1) ^ POLY if crc & (1 << 63) else crc << 1
        table.append(crc)
    return table

def generate_crc_table():
    """Generate a table for faster CRC calculation."""
    table = []
    for i in range(256):
        crc = i << 56
        for _ in range(8):
            crc = ((crc << 1) ^ POLY if crc & (1 << 63) else crc << 1) & 0xFFFFFFFFFFFFFFFF
        table.append(crc)
    return table

CRC_TABLE = generate_crc_table()

def fast_crc64(data):
    """Calculate CRC-64-ECMA using the precomputed table."""
    crc = INIT
    for byte in data:
        crc = (CRC_TABLE[(crc >> 56) ^ byte] ^ (crc << 8)) & 0xFFFFFFFFFFFFFFFF
    return crc ^ INIT

def update_crc64(original_crc, original_data, new_data, start_position):
    """Update CRC-64-ECMA for a modified block of data."""
    length = len(new_data)
    
    # Remove the contribution of the original data segment
    removed_crc = fast_crc64(original_data[start_position:start_position+length])
    temp_crc = remove_crc64(original_crc, removed_crc, start_position, length)
    
    # Add the contribution of the new data segment
    added_crc = fast_crc64(new_data)
    new_crc = add_crc64(temp_crc, added_crc, start_position, length)
    
    return new_crc

def remove_crc64(crc, segment_crc, position, length):
    """Remove the contribution of a segment from the CRC."""
    for i in range(length):
        crc = ((crc << 8) ^ CRC_TABLE[crc >> 56]) & 0xFFFFFFFFFFFFFFFF
    return crc ^ segment_crc

def add_crc64(crc, segment_crc, position, length):
    """Add the contribution of a segment to the CRC."""
    for i in range(length):
        crc = (crc >> 8) ^ CRC_TABLE[(crc ^ (segment_crc >> 56)) & 0xFF]
        segment_crc <<= 8
    return crc

# Example usage
if __name__ == "__main__":
    original_data = b"Hello, World! This is a test message for CRC64 calculation."
    print(f"Original data: {original_data}")
    
    original_crc = fast_crc64(original_data)
    print(f"Original CRC: {original_crc:016X}")
    
    # Modify a portion of the data
    start_position = 7
    new_data = b"Universe"
    modified_data = original_data[:start_position] + new_data + original_data[start_position+len(new_data):]
    print(f"Modified data: {modified_data}")
    
    # Calculate new CRC using the incremental update method
    new_crc = update_crc64(original_crc, original_data, new_data, start_position)
    print(f"New CRC (incremental): {new_crc:016X}")
    
    # Verify by calculating the CRC of the entire modified data
    verify_crc = fast_crc64(modified_data)
    print(f"New CRC (full recalculation): {verify_crc:016X}")
    
    print(f"CRCs match: {new_crc == verify_crc}")