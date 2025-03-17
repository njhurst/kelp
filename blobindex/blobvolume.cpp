#include "blobindex.hpp"
#include <algorithm>
#include <numeric>
#include <fstream>
#include <iostream>
#include <random>
#include <cstring>
#include <cstdlib>

// External constants defined in blobutils.cpp
extern std::array<uint8_t, 32> MAGIC;

BlobVolume::BlobVolume(const std::string& filename, bool create)
    : storage(filename, create), entries_size(3), tail_pos(0) {
    
    // Initialize entries with space for at least 3 entries
    entries.resize(3);
    
    if (create) {
        _create_new_index();
    } else {
        _load_index();
    }
}

BlobVolume::~BlobVolume() {
    close();
}

void BlobVolume::_create_new_index() {
    storage.pwrite(std::vector<uint8_t>(storage.HEADER_SIZE, 0), 0);
    tail_pos = storage.HEADER_SIZE;

    // Write the special offsets - primary, secondary and root; all growable
    std::vector<uint8_t> index_blob = generate_index_blob();
    uint32_t data_length = index_blob.size();
    
    entries[0] = {tail_pos, data_length, FLAG_GROWABLE | FLAG_COMPRESSED};
    tail_pos += data_length * 2;
    
    entries[1] = {tail_pos, data_length, FLAG_GROWABLE | FLAG_COMPRESSED};
    tail_pos += data_length * 2;
    
    entries[2] = {tail_pos, 0, FLAG_GROWABLE | FLAG_METADATA | FLAG_COMPRESSED};

    // Include the source code for the blobindex in position 3
    std::ifstream source_file(__FILE__, std::ios::binary);
    if (!source_file) {
        throw std::runtime_error("Failed to open source file");
    }
    
    source_file.seekg(0, std::ios::end);
    size_t source_size = source_file.tellg();
    source_file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> source_code(source_size);
    source_file.read(reinterpret_cast<char*>(source_code.data()), source_size);
    
    add_blob(source_code, FLAG_COMPRESSED | FLAG_MAGICED);

    // Write the index blob to both primary and secondary locations
    index_blob = generate_index_blob();
    
    // Compress the index blob if needed
    if (entries[0].flags & FLAG_COMPRESSED) {
        index_blob = zstandard_compress(index_blob);
    }
    
    storage.write_blob(index_blob, entries[0].offset);
    storage.write_blob(index_blob, entries[1].offset);

    // Set up the magic and version
    magic = MAGIC;
    version = 13;
    
    // Generate a random volume prefix
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0x1000000, 0xFFFFFFFF);  // Ensure it's greater than 2^24
    volume_prefix = dis(gen);
    
    // Save the index
    save_index();
}

void BlobVolume::_load_index() {
    std::vector<uint8_t> index_data = storage.pread(storage.HEADER_SIZE, 0);

    // Parse the header using the HeaderStruct
    const size_t HEADER_STRUCT_SIZE = sizeof(HeaderStruct);
    
    // Verify the header checksum
    std::array<uint8_t, 32> digest = blake3_hash(std::vector<uint8_t>(index_data.begin(), index_data.begin() + HEADER_STRUCT_SIZE));
    std::vector<uint8_t> stored_digest(index_data.begin() + HEADER_STRUCT_SIZE, 
                                      index_data.begin() + HEADER_STRUCT_SIZE + 32);
    
    // Verify checksum
    bool checksum_match = true;
    for (size_t i = 0; i < 32; ++i) {
        if (digest[i] != stored_digest[i]) {
            checksum_match = false;
            break;
        }
    }
    
    if (!checksum_match) {
        // Print debug info
        std::cerr << "Header checksum mismatch:" << std::endl;
        std::cerr << "Header data size: " << HEADER_STRUCT_SIZE << std::endl;
        std::cerr << "Computed digest: ";
        for (size_t i = 0; i < 8; ++i) {
            std::cerr << std::hex << static_cast<int>(digest[i]) << " ";
        }
        std::cerr << "..." << std::endl;
        
        std::cerr << "Stored digest: ";
        for (size_t i = 0; i < 8; ++i) {
            std::cerr << std::hex << static_cast<int>(stored_digest[i]) << " ";
        }
        std::cerr << "..." << std::endl;
        
        // If checksum doesn't match, use default values
        std::cerr << "Checksum mismatch, using default values" << std::endl;
        entries_size = 3;
        tail_pos = storage.HEADER_SIZE;
        return;
    }

    // Extract header fields using the HeaderStruct
    HeaderStruct header_struct;
    std::memcpy(&header_struct, index_data.data(), HEADER_STRUCT_SIZE);
    
    // Copy values from the struct
    std::memcpy(magic.data(), header_struct.magic, 32);
    version = header_struct.version;
    volume_prefix = header_struct.volume_prefix;
    uint64_t primary_index = header_struct.primary_offset;
    uint64_t secondary_index = header_struct.secondary_offset;
    uint64_t root_index = header_struct.root_offset;
    tail_pos = header_struct.tail_pos;
    
    // Debug output for loaded header values
    std::cout << "Loaded header values:" << std::endl;
    std::cout << "Primary index: " << primary_index << std::endl;
    std::cout << "Secondary index: " << secondary_index << std::endl;
    std::cout << "Root index: " << root_index << std::endl;
    std::cout << "Tail position: " << tail_pos << std::endl;

    // Parse the index entries
    try {
        // First 3 entries are always there
        std::vector<uint8_t> entries_data = storage.pread(3 * sizeof(IndexEntry), primary_index);
        entries.resize(3);
        std::memcpy(entries.data(), entries_data.data(), entries_data.size());
        
        // Read all of the entries
        entries_data = storage.pread(entries[0].size, entries[0].offset);
        entries.resize(entries_data.size() / sizeof(IndexEntry));
        std::memcpy(entries.data(), entries_data.data(), entries_data.size());
        entries_size = entries.size();
    } catch (const std::exception& e) {
        std::cerr << "Error reading index entries: " << e.what() << std::endl;
        std::cerr << "Defaulting to 3 entries" << std::endl;
        entries_size = 3;
    }
}

std::tuple<int, uint64_t, uint32_t> BlobVolume::add_blob_to_index(uint32_t size, uint32_t flags) {
    uint64_t offset = tail_pos;
    uint32_t alloc_size = size;
    
    if (flags & FLAG_GROWABLE) {
        alloc_size = static_cast<uint32_t>(size * 1.25) + 8;  // 25% extra space
    }
    
    if (alloc_size < size) {
        throw std::runtime_error("Allocation size overflow");
    }
    
    tail_pos += alloc_size;
    
    if (entries_size >= entries.size()) {
        size_t new_size = entries_size * 4 + 1;
        entries.resize(new_size);
    }
    
    int idx = entries_size;
    entries[idx] = {offset, size, flags};
    entries_size++;
    
    return {idx, offset, alloc_size};
}

void BlobVolume::delete_blob(int blob_id) {
    entries[blob_id] = {0, 0, 0};
}

std::tuple<uint64_t, uint32_t, uint32_t> BlobVolume::get_blob_info(int blob_id) {
    const IndexEntry& entry = entries[blob_id];
    return {entry.offset, entry.size, entry.flags};
}

void BlobVolume::update_blob(int blob_id, uint64_t offset, uint32_t size, uint32_t flags) {
    entries[blob_id] = {offset, size, flags};
}

void BlobVolume::save_index() {
    std::vector<uint8_t> index_blob = generate_index_blob();
    uint32_t index_length = index_blob.size();
    resize_blob(0, index_length);
    resize_blob(1, index_length);
    
    index_blob = generate_index_blob();
    storage.write_blob(index_blob, entries[0].offset);
    storage.write_blob(index_blob, entries[1].offset);
    
    // Construct the header using a struct to match Python's struct.pack("<32sIIQQQQ")
    HeaderStruct header_struct;
    std::memcpy(header_struct.magic, magic.data(), 32);
    header_struct.version = version;
    header_struct.volume_prefix = volume_prefix;
    header_struct.primary_offset = entries[0].offset;
    header_struct.secondary_offset = entries[1].offset;
    header_struct.root_offset = entries[2].offset;
    header_struct.tail_pos = tail_pos;
    
    // Convert struct to vector
    std::vector<uint8_t> header(sizeof(HeaderStruct));
    std::memcpy(header.data(), &header_struct, sizeof(HeaderStruct));
    
    // Compute checksum
    std::array<uint8_t, 32> checksum = blake3_hash(header);
    header.insert(header.end(), checksum.begin(), checksum.end());
    
    // Pad to header size
    size_t remaining = storage.HEADER_SIZE - header.size();
    header.insert(header.end(), remaining, 0);
    
    storage.pwrite(header, 0);
}

std::vector<int> BlobVolume::get_sort_order() {
    std::vector<int> indices(entries_size);
    std::iota(indices.begin(), indices.end(), 0);
    
    std::sort(indices.begin(), indices.end(), [this](int a, int b) {
        return entries[a].offset < entries[b].offset;
    });
    
    return indices;
}

std::vector<uint8_t> BlobVolume::generate_index_blob() {
    std::vector<uint8_t> blob(entries_size * sizeof(IndexEntry));
    std::memcpy(blob.data(), entries.data(), blob.size());
    return blob;
}

bool BlobVolume::resize_blob(int blob_id, uint32_t new_size) {
    auto [offset, old_size, flags] = get_blob_info(blob_id);
    
    if (new_size <= old_size) {
        // Shrinking is always possible
        update_blob(blob_id, offset, new_size, flags);
        return true;
    }
    
    // Check if we can grow in-place
    if (flags & FLAG_GROWABLE) {
        auto it = growable.find(blob_id);
        if (it != growable.end()) {
            uint32_t allocated = it->second;
            if (new_size <= allocated) {
                update_blob(blob_id, offset, new_size, flags);
                return true;
            }
        }
    }
    
    // Find the next blob
    uint64_t next_offset = tail_pos;
    for (size_t i = 0; i < entries_size; ++i) {
        if (entries[i].offset > offset) {
            next_offset = std::min(next_offset, entries[i].offset);
        }
    }
    
    if (offset + new_size <= next_offset) {
        // We can grow in-place
        std::vector<uint8_t> data = storage.read_blob(offset, old_size);
        data.resize(new_size, 0);  // Pad with zeros
        storage.write_blob(data, offset);
        update_blob(blob_id, offset, new_size, flags);
        return true;
    }
    
    // We need to move the blob
    uint64_t new_offset = tail_pos;
    tail_pos += new_size;
    
    std::vector<uint8_t> data = storage.read_blob(offset, old_size);
    data.resize(new_size, 0);  // Pad with zeros
    storage.write_blob(data, new_offset);
    update_blob(blob_id, new_offset, new_size, flags);
    return true;
}

void BlobVolume::validate_index() {
    std::vector<int> disk_order = get_sort_order();
    int overlaps = 0;
    
    for (size_t i = 1; i < disk_order.size(); ++i) {
        int prev_idx = disk_order[i-1];
        int curr_idx = disk_order[i];
        
        if (entries[prev_idx].offset + entries[prev_idx].size > entries[curr_idx].offset) {
            overlaps++;
            std::cout << "Overlap at index " << i << ": " 
                      << prev_idx << " and " << curr_idx << std::endl;
        }
    }
    
    std::cout << "Overlaps: " << overlaps << std::endl;
    if (overlaps > 0) {
        for (size_t i = 1; i < disk_order.size(); ++i) {
            int prev_idx = disk_order[i-1];
            int curr_idx = disk_order[i];
            
            if (entries[prev_idx].offset + entries[prev_idx].size > entries[curr_idx].offset) {
                std::cout << i << " " << disk_order[i] << " " 
                          << entries[prev_idx].offset << " " << entries[prev_idx].size << " "
                          << entries[curr_idx].offset << std::endl;
            }
        }
    }
    
    // Calculate gap sizes
    std::vector<uint64_t> gap_sizes;
    for (size_t i = 1; i < disk_order.size(); ++i) {
        int prev_idx = disk_order[i-1];
        int curr_idx = disk_order[i];
        
        uint64_t gap = entries[curr_idx].offset - (entries[prev_idx].offset + entries[prev_idx].size);
        gap_sizes.push_back(gap);
    }
    
    uint64_t max_gap = 0;
    if (!gap_sizes.empty()) {
        max_gap = *std::max_element(gap_sizes.begin(), gap_sizes.end());
    }
    
    std::cout << "Max gap size: " << max_gap << std::endl;
    
    // Calculate wasted space
    uint64_t total_size = 0;
    for (size_t i = 0; i < entries_size; ++i) {
        total_size += entries[i].size;
    }
    
    std::cout << "Wasted space: " << (tail_pos - total_size) << std::endl;
}

void BlobVolume::validate_all_magic() {
    // Find all known magic blobs from index
    std::vector<uint64_t> known_magic;
    
    for (size_t idx = 0; idx < entries_size; ++idx) {
        const auto& entry = entries[idx];
        if (entry.flags & FLAG_MAGICED) {
            std::vector<uint8_t> data = storage.read_blob(entry.offset, entry.size);
            
            bool has_magic = true;
            for (size_t i = 0; i < 32; ++i) {
                if (data[i] != MAGIC[i]) {
                    has_magic = false;
                    break;
                }
            }
            
            if (!has_magic) {
                std::cout << "Blob[" << idx << "] at " << entry.offset << " does not have magic" << std::endl;
            } else {
                std::cout << "Blob[" << idx << "] at " << entry.offset << " has magic" << std::endl;
                known_magic.push_back(entry.offset);
            }
        }
    }
    
    std::cout << "Known magic blobs: ";
    for (uint64_t offset : known_magic) {
        std::cout << offset << " ";
    }
    std::cout << std::endl;
    
    // Scan for magic patterns
    std::vector<uint8_t> magic_vec(MAGIC.begin(), MAGIC.end());
    for (uint64_t i : storage.scan_for_pattern(magic_vec)) {
        if (i == 0) {
            // Skip the header
            continue;
        }
        
        std::cout << "Magic at " << i << std::endl;
        
        std::vector<uint8_t> length_bytes = storage.read_blob(i + 32, 4);
        if (length_bytes.size() < 4) {
            std::cout << "Invalid length at " << i << std::endl;
            continue;
        }
        
        uint32_t length_including_checksum = bytes_to_int(length_bytes);
        if (length_including_checksum + i > storage.file_size) {
            std::cout << "Invalid length " << length_including_checksum << " at " << i << std::endl;
            continue;
        }
        
        uint32_t length = length_including_checksum - 32;
        
        // Confirm MAGIC
        std::vector<uint8_t> magic_check = storage.read_blob(i, 32);
        bool magic_match = true;
        for (size_t j = 0; j < 32; ++j) {
            if (magic_check[j] != MAGIC[j]) {
                magic_match = false;
                break;
            }
        }
        
        if (!magic_match) {
            std::cout << "Invalid magic at " << i << std::endl;
            continue;
        }
        
        std::vector<uint8_t> data = storage.read_blob(i + 36, length);
        std::array<uint8_t, 32> computed = blake3_hash(data);
        std::vector<uint8_t> stored = storage.read_blob(i + 36 + length, 32);
        
        bool checksum_match = true;
        for (size_t j = 0; j < 32; ++j) {
            if (stored[j] != computed[j]) {
                checksum_match = false;
                break;
            }
        }
        
        if (!checksum_match) {
            std::cout << "Invalid checksum at " << i << ", skipping" << std::endl;
            continue;
        }
        
        // Find corresponding blob
        bool found = false;
        for (size_t idx = 0; idx < entries_size; ++idx) {
            if (entries[idx].offset == i) {
                std::cout << "Blob " << idx << " at " << entries[idx].offset << std::endl;
                
                // Remove from known_magic
                auto it = std::find(known_magic.begin(), known_magic.end(), i);
                if (it != known_magic.end()) {
                    known_magic.erase(it);
                } else {
                    std::cout << "Unknown magic blob at " << i << std::endl;
                }
                
                found = true;
                break;
            }
        }
        
        if (!found) {
            std::cout << "No blob found for magic at " << i << std::endl;
        }
    }
    
    if (!known_magic.empty()) {
        std::cout << "Missing magic blobs: ";
        for (uint64_t offset : known_magic) {
            std::cout << offset << " ";
        }
        std::cout << std::endl;
    }
}

void BlobVolume::validate_all() {
    validate_index();
    validate_all_magic();
}

int BlobVolume::add_blob(const std::vector<uint8_t>& data, uint32_t flags) {
    std::vector<uint8_t> processed_data = data;
    
    if (flags & FLAG_COMPRESSED) {
        processed_data = zstandard_compress(processed_data);
    }
    
    if (flags & FLAG_BLAKE3 || flags & FLAG_MAGICED) {
        std::array<uint8_t, 32> hash = blake3_hash(processed_data);
        processed_data.insert(processed_data.end(), hash.begin(), hash.end());
    }
    
    if (flags & FLAG_MAGICED) {
        std::vector<uint8_t> data_length = int_to_bytes(processed_data.size());
        
        std::vector<uint8_t> prefix;
        prefix.insert(prefix.end(), MAGIC.begin(), MAGIC.end());
        prefix.insert(prefix.end(), data_length.begin(), data_length.end());
        
        processed_data.insert(processed_data.begin(), prefix.begin(), prefix.end());
    }
    
    auto [idx, offset, alloc_size] = add_blob_to_index(processed_data.size(), flags);
    storage.write_blob(processed_data, offset);
    
    return idx;
}

std::vector<uint8_t> BlobVolume::read_blob(int blob_id) {
    auto [offset, size, flags] = get_blob_info(blob_id);
    
    if (flags & FLAG_DELETED) {
        throw std::runtime_error("Blob has been deleted");
    }
    
    std::vector<uint8_t> data = storage.read_blob(offset, size);
    size_t data_start = 0;
    
    if (flags & FLAG_MAGICED) {
        // Check magic
        std::vector<uint8_t> magic_check(data.begin(), data.begin() + 32);
        bool magic_match = true;
        for (size_t i = 0; i < 32; ++i) {
            if (magic_check[i] != MAGIC[i]) {
                magic_match = false;
                break;
            }
        }
        
        if (!magic_match) {
            throw std::runtime_error("Invalid magic in blob");
        }
        
        data = std::vector<uint8_t>(data.begin() + 36, data.end());
    }
    
    if (flags & FLAG_BLAKE3 || flags & FLAG_MAGICED) {
        size_t data_end = data.size() - 32;
        std::vector<uint8_t> content(data.begin() + data_start, data.begin() + data_end);
        std::vector<uint8_t> hash(data.begin() + data_end, data.end());
        
        std::array<uint8_t, 32> computed = blake3_hash(content);
        
        bool hash_match = true;
        for (size_t i = 0; i < 32; ++i) {
            if (hash[i] != computed[i]) {
                hash_match = false;
                break;
            }
        }
        
        if (!hash_match) {
            throw std::runtime_error("Invalid hash in blob");
        }
        
        data = content;
    }
    
    if (flags & FLAG_COMPRESSED) {
        data = zstandard_decompress(data);
    }
    
    return data;
}

void BlobVolume::write_blob(int blob_id, const std::vector<uint8_t>& data) {
    auto [offset, size, flags] = get_blob_info(blob_id);
    
    resize_blob(blob_id, data.size());
    
    std::vector<uint8_t> processed_data = data;
    
    if (flags & FLAG_COMPRESSED) {
        processed_data = zstandard_compress(processed_data);
    }
    
    if (flags & FLAG_BLAKE3 || flags & FLAG_MAGICED) {
        std::array<uint8_t, 32> hash = blake3_hash(processed_data);
        processed_data.insert(processed_data.end(), hash.begin(), hash.end());
    }
    
    if (flags & FLAG_MAGICED) {
        std::vector<uint8_t> data_length = int_to_bytes(processed_data.size());
        
        std::vector<uint8_t> prefix;
        prefix.insert(prefix.end(), MAGIC.begin(), MAGIC.end());
        prefix.insert(prefix.end(), data_length.begin(), data_length.end());
        
        processed_data.insert(processed_data.begin(), prefix.begin(), prefix.end());
    }
    
    storage.write_blob(processed_data, entries[blob_id].offset);
    resize_blob(blob_id, processed_data.size());
}

int BlobVolume::get_blob_count() {
    return entries_size;
}

void BlobVolume::close() {
    if (storage.is_open()) {
        save_index();
        storage.close();
    }
} 