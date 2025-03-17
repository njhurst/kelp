#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>
#include <array>
#include <optional>

// Forward declarations
namespace blake3 {
    class Hasher;
}

// Struct that matches Python's struct.pack("<32sIIQQQQ")
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

class BlobStorage {
public:
    static constexpr size_t HEADER_SIZE = 4096;  // 4KB header

    BlobStorage(const std::string& filename, bool create = false);
    ~BlobStorage();

    std::vector<uint8_t> pread(size_t size, uint64_t offset);
    void pwrite(const std::vector<uint8_t>& data, uint64_t offset);
    uint64_t write_blob(const std::vector<uint8_t>& data, uint64_t offset);
    std::vector<uint8_t> read_blob(uint64_t offset, size_t size);
    std::vector<uint64_t> scan_for_pattern(const std::vector<uint8_t>& pattern);
    void close();
    bool is_open() const;

    uint64_t file_size;

private:
    void _create_new_volume();
    void _open_existing_volume();

    std::string filename;
    int fd;
};

class BlobLocks {
public:
    BlobLocks();

    int acquire_lock(int lock_type, int lock_owner, uint64_t start_offset, uint64_t end_offset);
    void release_lock(int lock_id);
    bool check_read_range(int lock_owner, uint64_t start_offset, uint64_t end_offset);
    bool check_write_range(int lock_owner, uint64_t start_offset, uint64_t end_offset);
    void release_all_locks(int lock_owner);
    std::vector<std::tuple<int, int, int, int, uint64_t, uint64_t>> get_locks(int lock_owner);
    std::tuple<int, int, int, int, uint64_t, uint64_t> get_lock(int lock_id);
    bool has_lock(int lock_id);
    bool has_locks(int lock_owner);
    bool has_exclusive_locks(int lock_owner);
    bool has_shared_locks(int lock_owner);
    bool has_watch_locks(int lock_owner);
    bool has_locks_in_range(int lock_owner, uint64_t start_offset, uint64_t end_offset);
    bool has_exclusive_locks_in_range(int lock_owner, uint64_t start_offset, uint64_t end_offset);
    bool has_shared_locks_in_range(int lock_owner, uint64_t start_offset, uint64_t end_offset);
    bool has_watch_locks_in_range(int lock_owner, uint64_t start_offset, uint64_t end_offset);
    bool has_locks_in_range_exclusive(int lock_owner, uint64_t start_offset, uint64_t end_offset);

private:
    std::vector<std::tuple<int, int, int, int, uint64_t, uint64_t>> locks;
    int lock_id;
    std::mutex lock_lock;
};

struct IndexEntry {
    uint64_t offset;
    uint32_t size;
    uint32_t flags;
};

class BlobVolume {
public:
    // Constants
    static constexpr uint32_t FLAG_METADATA = 0x01;
    static constexpr uint32_t FLAG_GROWABLE = 0x02;
    static constexpr uint32_t FLAG_COMPRESSED = 0x04;
    static constexpr uint32_t FLAG_BLAKE3 = 0x08;
    static constexpr uint32_t FLAG_MAGICED = 0x10;
    static constexpr uint32_t FLAG_DELETED = 0x80;

    BlobVolume(const std::string& filename, bool create = false);
    ~BlobVolume();

    int add_blob(const std::vector<uint8_t>& data, uint32_t flags = 0);
    std::vector<uint8_t> read_blob(int blob_id);
    void write_blob(int blob_id, const std::vector<uint8_t>& data);
    void delete_blob(int blob_id);
    bool resize_blob(int blob_id, uint32_t new_size);
    std::tuple<uint64_t, uint32_t, uint32_t> get_blob_info(int blob_id);
    void update_blob(int blob_id, uint64_t offset, uint32_t size, uint32_t flags);
    void save_index();
    void validate_index();
    void validate_all_magic();
    void validate_all();
    int get_blob_count();
    void close();

private:
    void _create_new_index();
    void _load_index();
    std::tuple<int, uint64_t, uint32_t> add_blob_to_index(uint32_t size, uint32_t flags);
    std::vector<uint8_t> generate_index_blob();
    std::vector<int> get_sort_order();

    BlobStorage storage;
    std::vector<IndexEntry> entries;
    size_t entries_size;
    uint64_t tail_pos;
    std::unordered_map<int, uint32_t> growable;
    std::array<uint8_t, 32> magic;
    uint32_t version;
    uint32_t volume_prefix;
};

// Utility functions
std::vector<uint8_t> zstandard_compress(const std::vector<uint8_t>& data);
std::vector<uint8_t> zstandard_decompress(const std::vector<uint8_t>& data);
std::array<uint8_t, 32> blake3_hash(const std::vector<uint8_t>& data);
std::vector<uint8_t> int_to_bytes(uint32_t value);
uint32_t bytes_to_int(const std::vector<uint8_t>& bytes); 