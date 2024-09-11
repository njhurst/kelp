#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <libaio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include <assert.h>
#include <immintrin.h>

#define PAGE_SIZE 4096
#define MAX_EVENTS 128

struct write_context {
    int start_page;
    int num_pages;
    void *buffer;
};

/*** 4k block size
* 16 byte header consisting of:
** 4 byte block checksum crc32c
** 4 byte block sequence number: this is used to detect out-of-order writes and failures
** 7 byte stripe number: (4096 - 16) * 8 = 32752 bytes of data per stripe, 32kB * 2^56 = 2^71 bytes of data = 2ZB
** 1 byte block number: there can be at most 256 blocks in a stripe due to using gf(2^8)
(these are effectively 64 bit id, 56 bits for the stripe number and 8 bits for the block number)
** 4096 - 16 = 4080 bytes of data
 */
struct block {
    uint32_t block_checksum;
    uint32_t block_sequence_number;
    uint64_t stripe_number;    // including bottom uint8_t shard_id;  data on disk is stored in sorted order of stripe_number, except the tail which stores previous block backups
    unsigned char data[4080];
};

/**     HEADER:  4KB header on all volumes:
    Magic number 32 bytes
    Version number 4 bytes
    Volume prefix id (random number greater than 2^24, to make it unlikely to collide with a valid offset, and garbage collectable)
    Primary index offset 8 bytes
    Secondary index offset 8 bytes
    Tail offset 8 bytes
    8x8b = at most 8 shards stored in this volume, for less than 8 shards, the last shard is repeated.  shards are sorted
    blake3 hash of the header 32 bytes
 */

struct header_block {
    unsigned char magic_number[32];
    uint32_t version_number;
    uint32_t volume_prefix_id;
    uint64_t primary_index_offset;
    uint64_t secondary_index_offset;
    uint64_t tail_offset;
    uint8_t shard_ids[8];
    uint32_t header_crc32c;
};

/** count shards in a stripe
 *  returns the number of shards in a stripe
 * 
 * @param header: pointer to the header block
 * @return: number of shards in the stripe
 */
int get_k_blocks_in_stripe(struct header_block *header) {
    int count = 8;
    while (count > 1 && (header->shard_ids[count - 2] == header->shard_ids[count - 1])) {
        count--;
    }
    return count;
}

/**
 * Computes the offset to the block identified by the given header, stripe number, and shard ID.
 *
 * @param header The pointer to the header block.
 * @param stripe_number The stripe number.
 * @param shard_id The shard ID.
 * @return The offset to the block.
 */
int compute_offset_to_block(struct header_block *header, int stripe_number, int shard_id) {
    size_t offset = 0;
    offset += 4096*get_k_blocks_in_stripe(header)*stripe_number;
    for (int i = 0; i < 8; i++) {
        if (header->shard_ids[i] == shard_id) {
            return offset;
        }
        offset += 4096;
    }
    abort();
    return offset;
}

/** crc32c implementation
 * SSE4.2 hardware-accelerated implementation (CRC32C)
 * 
 * @param data: pointer to the data
 * @param length: length of the data
 * @param previousCrc32: previous crc32 value
 * @return: crc32c checksum
 */
uint32_t crc32c_sse42(const void *data, size_t length, uint32_t previousCrc32) {
    uint32_t crc = ~previousCrc32;  // Invert initial value
    const uint8_t *current = (const uint8_t *)data;

    // Process individual bytes until we reach 8-byte alignment
    while (length && ((uintptr_t)current & 7)) {
        crc = _mm_crc32_u8(crc, *current++);
        length--;
    }

    // Process 8 bytes at a time
    while (length >= 8) {
        crc = _mm_crc32_u64(crc, *(uint64_t*)current);
        current += 8;
        length -= 8;
    }

    // Process any remaining bytes
    while (length--) {
        crc = _mm_crc32_u8(crc, *current++);
    }

    return ~crc;  // Invert final value
}

#define crc32c crc32c_sse42


/** Given k*16*x = input_size, spread the data into k blocks of size x
 *  The input is a byte array of size input_size
 *  The output is an array of k byte arrays of size x
 * 
 * The input is spread across the k blocks in a round-robin fashion
 * 
 * The input is assumed to be aligned to 16 bytes, and input_size is a multiple of 16*k
 */
void spread_data(void *input, void **output_blocks, size_t input_size, int k) {
    unsigned char *src = (unsigned char *)input;
    unsigned char **dest = (unsigned char **)output_blocks;
    size_t offset[k];
    
    for (int i = 0; i < k; i++) {
        offset[i] = 0;
    }

    while (input_size >= 16 * k) {
        for (int i = 0; i < k; i++) {
            __m128i data = _mm_loadu_si128((__m128i*)src);
            _mm_storeu_si128((__m128i*)(dest[i] + offset[i]), data);
            src += 16;
            offset[i] += 16;
        }
        input_size -= 16 * k;
    }
}

/** Unspread data from k blocks of size x into a single byte array of size k*x
 *  The input is an array of k byte arrays of size x
 *  The output is a byte array of size k*x
 * 
 * The input is unspread from the k blocks in a round-robin fashion
 * 
 * The output is assumed to be aligned to 16 bytes, and k*x is a multiple of 16
 */
void unspread_data(void **input_blocks, void *output, size_t output_size, int k) {
    unsigned char **src = (unsigned char **)input_blocks;
    unsigned char *dest = (unsigned char *)output;
    size_t offset[k];
    
    for (int i = 0; i < k; i++) {
        offset[i] = 0;
    }

    while (output_size >= 16 * k) {
        for (int i = 0; i < k; i++) {
            __m128i data = _mm_loadu_si128((__m128i*)(src[i] + offset[i]));
            _mm_storeu_si128((__m128i*)dest, data);
            dest += 16;
            offset[i] += 16;
        }
        output_size -= 16 * k;
    }
}


/**
 * Validates the header block.
 *
 * @param header A pointer to the header block structure.
 * @return Returns an integer indicating the validation result.
 */
int validate_header(struct header_block *header) {
    // validate magic number
    // if (memcmp(header->magic_number, "blobindex", 32) != 0) {
    //     return 0;
    // }
    // validate version number
    if (header->version_number != 1) {
        return 0;
    }
    // validate volume prefix id
    if (header->volume_prefix_id < (1 << 24)) {
        return 0;
    }
    // validate header checksum
    uint32_t computed_checksum = crc32c((const unsigned char *)header, sizeof(struct header_block) - 4, 0);
    if (computed_checksum != header->header_crc32c) {
        return 0;
    }
    return 1;
}


/**
 * Validates a block.
 *
 * @param block The block to be validated.
 * @return Returns an integer indicating the validation result.
 */
int validate_block(struct block *block) {
    // validate crc32c checksum
    uint32_t computed_checksum = crc32c((const unsigned char *)block + 4, 4096 - 4, 0);
    if (computed_checksum != block->block_checksum) {
        return 0;
    }
    return 1;
}

void submit_read(io_context_t io_ctx, int fd, int start_page, int num_pages) {
    struct iocb cb;
    struct iocb *cbs[1];
    struct write_context *ctx = malloc(sizeof(struct write_context));

    ctx->start_page = start_page;
    ctx->num_pages = num_pages;
    assert(posix_memalign(&ctx->buffer, PAGE_SIZE, PAGE_SIZE * num_pages) == 0);

    io_prep_pread(&cb, fd, ctx->buffer, PAGE_SIZE * num_pages, start_page * PAGE_SIZE);
    cb.data = ctx;
    cbs[0] = &cb;

    if (io_submit(io_ctx, 1, cbs) != 1) {
        perror("io_submit");
        exit(1);
    }
}

int submit_write(io_context_t io_ctx, int fd, int start_page, int num_pages) {
    struct iocb cb;
    struct iocb *cbs[1];
    struct write_context *ctx = malloc(sizeof(struct write_context));

    if (!ctx) {
        perror("malloc");
        return -1;
    }

    ctx->start_page = start_page;
    ctx->num_pages = num_pages;
    if (posix_memalign(&ctx->buffer, PAGE_SIZE, PAGE_SIZE * num_pages) != 0) {
        perror("posix_memalign");
        free(ctx);
        return -1;
    }
    memset(ctx->buffer, 'A' + (start_page % 26), PAGE_SIZE * num_pages);

    io_prep_pwrite(&cb, fd, ctx->buffer, PAGE_SIZE * num_pages, start_page * PAGE_SIZE);
    cb.data = ctx;
    cbs[0] = &cb;

    int ret = io_submit(io_ctx, 1, cbs);
    if (ret != 1) {
        fprintf(stderr, "io_submit error: %s (errno: %d)\n", strerror(-ret), -ret);
        fprintf(stderr, "io_submit return value: %d\n", ret);
        fprintf(stderr, "start_page: %d, num_pages: %d\n", start_page, num_pages);
        free(ctx->buffer);
        free(ctx);
        return ret;
    }
    return 0;
}

/** Write blocks from buffer
 * 
 * Given a buffer and an offset into the logical volume, read and update the blocks
 */

int check_completed(io_context_t io_ctx) {
    struct io_event events[MAX_EVENTS];
    struct timespec timeout = {0, 0};  // Non-blocking
    
    int completed = io_getevents(io_ctx, 0, MAX_EVENTS, events, &timeout);
    int total_written = 0;
    
    for (int i = 0; i < completed; i++) {
        struct write_context *ctx = (struct write_context *)events[i].data;
        // printf("Pages %d to %d have been written or read\n", ctx->start_page, ctx->start_page + ctx->num_pages - 1);
        total_written += ctx->num_pages;
        free(ctx->buffer);
        free(ctx);
        }
    return total_written;
}