#include <gtest/gtest.h>
#include "blockaio.hpp"
#include <cstring>

TEST(BlockAIOTest, GetKBlocksInStripe) {
    HeaderBlock header;
    std::fill(header.shard_ids.begin(), header.shard_ids.end(), 0);
    header.shard_ids[0] = 1;
    header.shard_ids[1] = 2;
    header.shard_ids[2] = 3;
    
    EXPECT_EQ(getKBlocksInStripe(header), 3);
    
    header.shard_ids[3] = 4;
    EXPECT_EQ(getKBlocksInStripe(header), 4);
}

TEST(BlockAIOTest, ComputeOffsetToBlock) {
    HeaderBlock header;
    std::fill(header.shard_ids.begin(), header.shard_ids.end(), 0);
    header.shard_ids[0] = 1;
    header.shard_ids[1] = 2;
    header.shard_ids[2] = 3;
    
    EXPECT_EQ(computeOffsetToBlock(header, 0, 1), 0);
    EXPECT_EQ(computeOffsetToBlock(header, 0, 2), 4096);
    EXPECT_EQ(computeOffsetToBlock(header, 1, 1), 3 * 4096);
}

TEST(BlockAIOTest, CRC32C) {
    const char* data = "Hello, World!";
    uint32_t crc = crc32c(data, strlen(data), 0);
    EXPECT_NE(crc, 0);
    
    uint32_t crc2 = crc32c(data, strlen(data), 0);
    EXPECT_EQ(crc, crc2);
}

TEST(BlockAIOTest, SpreadAndUnspreadData) {
    const int k = 3;
    const size_t input_size = 16 * k * 2;  // 2 rounds of spreading
    std::vector<char> input(input_size);
    for (size_t i = 0; i < input_size; ++i) {
        input[i] = static_cast<char>(i);
    }
    
    std::vector<std::vector<char>> output_blocks(k, std::vector<char>(input_size / k));
    std::vector<void*> output_ptrs(k);
    for (int i = 0; i < k; ++i) {
        output_ptrs[i] = output_blocks[i].data();
    }
    
    spreadData(input.data(), output_ptrs, input_size, k);
    
    std::vector<char> result(input_size);
    unspreadData(output_ptrs, result.data(), input_size, k);
    
    EXPECT_EQ(input, result);
}

TEST(BlockAIOTest, ValidateHeader) {
    HeaderBlock header;
    std::memset(&header, 0, sizeof(HeaderBlock));
    header.version_number = 1;
    header.volume_prefix_id = 1 << 24;
    header.header_crc32c = crc32c(&header, sizeof(HeaderBlock) - 4, 0);
    
    EXPECT_TRUE(validateHeader(header));
    
    header.version_number = 2;
    EXPECT_FALSE(validateHeader(header));
}

TEST(BlockAIOTest, ValidateBlock) {
    Block block;
    std::memset(&block, 0, sizeof(Block));
    block.block_checksum = crc32c(reinterpret_cast<const unsigned char*>(&block) + 4, sizeof(Block) - 4, 0);
    
    EXPECT_TRUE(validateBlock(block));
    
    block.block_sequence_number = 1;
    EXPECT_FALSE(validateBlock(block));
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
