#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <libaio.h>

constexpr int PAGE_SIZE = 4096;
constexpr int MAX_EVENTS = 128;

struct WriteContext {
    int start_page;
    int num_pages;
    void* buffer;
};

/**
 * 4k block size
 * 16 byte header consisting of:
 * - 4 byte block checksum crc32c
 * - 4 byte block sequence number: used to detect out-of-order writes and failures
 * - 7 byte stripe number: (4096 - 16) * 8 = 32752 bytes of data per stripe, 32kB * 2^56 = 2^71 bytes of data = 2ZB
 * - 1 byte block number: there can be at most 256 blocks in a stripe due to using gf(2^8)
 * (these are effectively 64 bit id, 56 bits for the stripe number and 8 bits for the block number)
 * - 4096 - 16 = 4080 bytes of data
 */
struct Block {
    uint32_t block_checksum;
    uint32_t block_sequence_number;
    uint64_t stripe_number;    // including bottom uint8_t shard_id;  data on disk is stored in sorted order of stripe_number, except the tail which stores previous block backups
    std::array<unsigned char, 4080> data;
};

/**
 * HEADER:  4KB header on all volumes:
 * - Magic number 32 bytes
 * - Version number 4 bytes
 * - Volume prefix id (random number greater than 2^24, to make it unlikely to collide with a valid offset, and garbage collectable)
 * - Primary index offset 8 bytes
 * - Secondary index offset 8 bytes
 * - Tail offset 8 bytes
 * - 8x8b = at most 8 shards stored in this volume, for less than 8 shards, the last shard is repeated.  shards are sorted
 * - blake3 hash of the header 32 bytes
 */
struct HeaderBlock {
    std::array<unsigned char, 32> magic_number;
    uint32_t version_number;
    uint32_t volume_prefix_id;
    uint64_t primary_index_offset;
    uint64_t secondary_index_offset;
    uint64_t tail_offset;
    std::array<uint8_t, 8> shard_ids;
    uint32_t header_crc32c;
};

struct Volume {
    int fd;
    HeaderBlock header;
};


/**
 * Volume map structure
 */

struct VolumeMap {
    std::vector<Volume> volumes;
};

/**
 * Count shards in a stripe
 * @param header: reference to the header block
 * @return: number of shards in the stripe
 */
int getKBlocksInStripe(const HeaderBlock& header);

/**
 * Computes the offset to the block identified by the given header, stripe number, and shard ID.
 * @param header The reference to the header block.
 * @param stripe_number The stripe number.
 * @param shard_id The shard ID.
 * @return The offset to the block.
 */
int computeOffsetToBlock(const HeaderBlock& header, int stripe_number, int shard_id);

/**
 * CRC32C implementation
 * SSE4.2 hardware-accelerated implementation (CRC32C)
 * @param data: pointer to the data
 * @param length: length of the data
 * @param previousCrc32: previous crc32 value
 * @return: crc32c checksum
 */
uint32_t crc32c(const void* data, size_t length, uint32_t previousCrc32);

/**
 * Given k*16*x = input_size, spread the data into k blocks of size x
 * The input is a byte array of size input_size
 * The output is an array of k byte arrays of size x
 * The input is spread across the k blocks in a round-robin fashion
 * The input is assumed to be aligned to 16 bytes, and input_size is a multiple of 16*k
 */
void spreadData(void* input, std::vector<void*>& output_blocks, size_t input_size, int k);

/**
 * Unspread data from k blocks of size x into a single byte array of size k*x
 * The input is an array of k byte arrays of size x
 * The output is a byte array of size k*x
 * The input is unspread from the k blocks in a round-robin fashion
 * The output is assumed to be aligned to 16 bytes, and k*x is a multiple of 16
 */
void unspreadData(std::vector<void*>& input_blocks, void* output, size_t output_size, int k);

/**
 * Validates the header block.
 * @param header A reference to the header block structure.
 * @return Returns a boolean indicating the validation result.
 */
bool validateHeader(const HeaderBlock& header);

/**
 * Validates a block.
 * @param block The block to be validated.
 * @return Returns a boolean indicating the validation result.
 */
bool validateBlock(const Block& block);

/**
 * Submits an asynchronous read operation.
 * @param io_ctx The I/O context.
 * @param fd The file descriptor.
 * @param start_page The starting page number.
 * @param num_pages The number of pages to read.
 */
void submitRead(io_context_t io_ctx, int fd, int start_page, int num_pages);

/**
 * Submits an asynchronous write operation.
 * @param io_ctx The I/O context.
 * @param fd The file descriptor.
 * @param start_page The starting page number.
 * @param num_pages The number of pages to write.
 * @return Returns 0 on success, or a negative error code on failure.
 */
int submitWrite(io_context_t io_ctx, int fd, int start_page, int num_pages);

/**
 * Checks for completed I/O operations.
 * @param io_ctx The I/O context.
 * @return Returns the number of pages written/read in completed operations.
 */
int checkCompleted(io_context_t io_ctx);
