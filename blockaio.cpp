#include "blockaio.hpp"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <immintrin.h>
#include <cassert>

int getKBlocksInStripe(const HeaderBlock& header) {
    int count = 8;
    // Count unique shard IDs
    while (count > 1 && (header.shard_ids[count - 2] == header.shard_ids[count - 1])) {
        count--;
    }
    return count;
}

int computeOffsetToBlock(const HeaderBlock& header, int stripe_number, int shard_id) {
    size_t offset = 0;
    // Calculate offset based on stripe number
    offset += 4096 * getKBlocksInStripe(header) * stripe_number;
    // Find the correct shard and calculate its offset
    for (int i = 0; i < 8; i++) {
        if (header.shard_ids[i] == shard_id) {
            return offset;
        }
        offset += 4096;
    }
    // If shard_id not found, this is an error condition
    abort();
    return offset;
}

uint32_t crc32c(const void* data, size_t length, uint32_t previousCrc32) {
    uint32_t crc = ~previousCrc32;  // Invert initial value
    const uint8_t* current = static_cast<const uint8_t*>(data);

    // Process individual bytes until we reach 8-byte alignment
    while (length && ((uintptr_t)current & 7)) {
        crc = _mm_crc32_u8(crc, *current++);
        length--;
    }

    // Process 8 bytes at a time
    while (length >= 8) {
        crc = _mm_crc32_u64(crc, *reinterpret_cast<const uint64_t*>(current));
        current += 8;
        length -= 8;
    }

    // Process any remaining bytes
    while (length--) {
        crc = _mm_crc32_u8(crc, *current++);
    }

    return ~crc;  // Invert final value
}

void spreadData(void* input, std::vector<void*>& output_blocks, size_t input_size, int k) {
    unsigned char* src = static_cast<unsigned char*>(input);
    std::vector<unsigned char*> dest(k);
    for (int i = 0; i < k; i++) {
        dest[i] = static_cast<unsigned char*>(output_blocks[i]);
    }
    std::vector<size_t> offset(k, 0);

    // Spread data across k blocks in round-robin fashion
    while (input_size >= 16 * k) {
        for (int i = 0; i < k; i++) {
            __m128i data = _mm_loadu_si128(reinterpret_cast<__m128i*>(src));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dest[i] + offset[i]), data);
            src += 16;
            offset[i] += 16;
        }
        input_size -= 16 * k;
    }
}

void unspreadData(std::vector<void*>& input_blocks, void* output, size_t output_size, int k) {
    std::vector<unsigned char*> src(k);
    for (int i = 0; i < k; i++) {
        src[i] = static_cast<unsigned char*>(input_blocks[i]);
    }
    unsigned char* dest = static_cast<unsigned char*>(output);
    std::vector<size_t> offset(k, 0);

    // Unspread data from k blocks in round-robin fashion
    while (output_size >= 16 * k) {
        for (int i = 0; i < k; i++) {
            __m128i data = _mm_loadu_si128(reinterpret_cast<__m128i*>(src[i] + offset[i]));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), data);
            dest += 16;
            offset[i] += 16;
        }
        output_size -= 16 * k;
    }
}

bool validateHeader(const HeaderBlock& header) {
    // Validate version number
    if (header.version_number != 1) {
        return false;
    }
    // Validate volume prefix id
    if (header.volume_prefix_id < (1 << 24)) {
        return false;
    }
    // Validate header checksum
    uint32_t computed_checksum = crc32c(&header, sizeof(HeaderBlock) - 4, 0);
    if (computed_checksum != header.header_crc32c) {
        return false;
    }
    return true;
}

bool validateBlock(const Block& block) {
    // Validate crc32c checksum
    uint32_t computed_checksum = crc32c(reinterpret_cast<const unsigned char*>(&block) + 4, sizeof(Block) - 4, 0);
    return computed_checksum == block.block_checksum;
}

void submitRead(io_context_t io_ctx, int fd, int start_page, int num_pages) {
    struct iocb cb;
    struct iocb* cbs[1];
    auto* ctx = new WriteContext;

    ctx->start_page = start_page;
    ctx->num_pages = num_pages;
    assert(posix_memalign(&ctx->buffer, PAGE_SIZE, PAGE_SIZE * num_pages) == 0);

    // Prepare asynchronous read operation
    io_prep_pread(&cb, fd, ctx->buffer, PAGE_SIZE * num_pages, start_page * PAGE_SIZE);
    cb.data = ctx;
    cbs[0] = &cb;

    // Submit the read operation
    if (io_submit(io_ctx, 1, cbs) != 1) {
        perror("io_submit");
        exit(1);
    }
}

int submitWrite(io_context_t io_ctx, int fd, int start_page, int num_pages) {
    struct iocb cb;
    struct iocb* cbs[1];
    auto* ctx = new WriteContext;

    ctx->start_page = start_page;
    ctx->num_pages = num_pages;
    if (posix_memalign(&ctx->buffer, PAGE_SIZE, PAGE_SIZE * num_pages) != 0) {
        perror("posix_memalign");
        delete ctx;
        return -1;
    }
    // Fill buffer with dummy data
    std::memset(ctx->buffer, 'A' + (start_page % 26), PAGE_SIZE * num_pages);

    // Prepare asynchronous write operation
    io_prep_pwrite(&cb, fd, ctx->buffer, PAGE_SIZE * num_pages, start_page * PAGE_SIZE);
    cb.data = ctx;
    cbs[0] = &cb;

    // Submit the write operation
    int ret = io_submit(io_ctx, 1, cbs);
    if (ret != 1) {
        std::cerr << "io_submit error: " << strerror(-ret) << " (errno: " << -ret << ")" << std::endl;
        std::cerr << "io_submit return value: " << ret << std::endl;
        std::cerr << "start_page: " << start_page << ", num_pages: " << num_pages << std::endl;
        free(ctx->buffer);
        delete ctx;
        return ret;
    }
    return 0;
}

int checkCompleted(io_context_t io_ctx) {
    struct io_event events[MAX_EVENTS];
    struct timespec timeout = {0, 0};  // Non-blocking
    
    // Get completed I/O events
    int completed = io_getevents(io_ctx, 0, MAX_EVENTS, events, &timeout);
    int total_written = 0;
    
    // Process completed events
    for (int i = 0; i < completed; i++) {
        auto* ctx = static_cast<WriteContext*>(events[i].data);
        total_written += ctx->num_pages;
        free(ctx->buffer);
        delete ctx;
    }
    return total_written;
}
