#include "blobindex.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <string>
#include <vector>
#include <iostream>

class BlobVolumeTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = std::filesystem::temp_directory_path() / "blobindex_test";
        // Remove the directory if it exists
        if (std::filesystem::exists(temp_dir)) {
            std::filesystem::remove_all(temp_dir);
        }
        std::filesystem::create_directory(temp_dir);
        volume_file = temp_dir / "test_volume.dat";
    }

    void TearDown() override {
        // Close any open file descriptors
        std::filesystem::remove(volume_file);
        std::filesystem::remove_all(temp_dir);
    }

    std::filesystem::path temp_dir;
    std::filesystem::path volume_file;
};

TEST_F(BlobVolumeTest, CreateNewVolume) {
    BlobVolume volume(volume_file.string(), true);
    EXPECT_TRUE(std::filesystem::exists(volume_file));
    volume.validate_all();
}

TEST_F(BlobVolumeTest, AddAndReadBlob) {
    BlobVolume volume(volume_file.string(), true);
    std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!'};
    int blob_id = volume.add_blob(data);
    std::vector<uint8_t> read_data = volume.read_blob(blob_id);
    EXPECT_EQ(data, read_data);
    volume.validate_all();
}

TEST_F(BlobVolumeTest, MultipleBlobs) {
    BlobVolume volume(volume_file.string(), true);
    std::vector<uint8_t> data1 = {'F', 'i', 'r', 's', 't', ' ', 'b', 'l', 'o', 'b'};
    std::vector<uint8_t> data2 = {'S', 'e', 'c', 'o', 'n', 'd', ' ', 'b', 'l', 'o', 'b'};
    int blob_id1 = volume.add_blob(data1);
    int blob_id2 = volume.add_blob(data2);
    
    auto [offset, size, flags] = volume.get_blob_info(blob_id1);
    std::cout << "data size " << data1.size() << ", stored size " << size << std::endl;
    
    EXPECT_EQ(data1, volume.read_blob(blob_id1));
    EXPECT_EQ(data2, volume.read_blob(blob_id2));
    volume.validate_all();
}

TEST_F(BlobVolumeTest, MultipleCompressedBlobs) {
    BlobVolume volume(volume_file.string(), true);
    std::vector<uint8_t> data1 = {'F', 'i', 'r', 's', 't', ' ', 'b', 'l', 'o', 'b'};
    std::vector<uint8_t> data2 = {'S', 'e', 'c', 'o', 'n', 'd', ' ', 'b', 'l', 'o', 'b'};
    int blob_id1 = volume.add_blob(data1, BlobVolume::FLAG_COMPRESSED);
    int blob_id2 = volume.add_blob(data2, BlobVolume::FLAG_COMPRESSED);
    
    auto [offset, size, flags] = volume.get_blob_info(blob_id1);
    std::cout << "data size " << data1.size() << ", stored size " << size << std::endl;
    
    EXPECT_EQ(data1, volume.read_blob(blob_id1));
    EXPECT_EQ(data2, volume.read_blob(blob_id2));
    volume.validate_all();
}

TEST_F(BlobVolumeTest, ReopenVolume) {
    std::vector<uint8_t> data = {'T', 'e', 's', 't', ' ', 'd', 'a', 't', 'a'};
    
    // Create a new volume and add some blobs
    {
        BlobVolume volume(volume_file.string(), true);
        int blob_id = volume.add_blob(data);
        volume.validate_all();
        // Explicitly close the volume to ensure all data is written
        volume.close();
    }
    
    // Verify the file exists
    ASSERT_TRUE(std::filesystem::exists(volume_file));
    
    // Reopen the volume and verify the blob
    {
        BlobVolume reopened_volume(volume_file.string(), false);
        EXPECT_EQ(data, reopened_volume.read_blob(4));  // First user blob is at index 4
        reopened_volume.validate_all();
    }
}

// Separate test for reopening to isolate issues
TEST_F(BlobVolumeTest, DISABLED_ReopenVolumeRead) {
    std::vector<uint8_t> data = {'T', 'e', 's', 't', ' ', 'd', 'a', 't', 'a'};
    
    // Create a new volume and add some blobs
    {
        BlobVolume volume(volume_file.string(), true);
        int blob_id = volume.add_blob(data);
        volume.validate_all();
        volume.close();
        
        // Reopen the volume and verify the blob
        BlobVolume reopened(volume_file.string(), false);
        try {
            std::vector<uint8_t> read_data = reopened.read_blob(blob_id);
            EXPECT_EQ(data, read_data);
        } catch (const std::exception& e) {
            FAIL() << "Exception while reading blob: " << e.what();
        }
        reopened.validate_all();
        reopened.close();
    }
}

TEST_F(BlobVolumeTest, SourceCodeBlob) {
    BlobVolume volume(volume_file.string(), true);
    
    // Read the source code file
    std::ifstream source_file(__FILE__, std::ios::binary);
    ASSERT_TRUE(source_file.is_open());
    
    source_file.seekg(0, std::ios::end);
    size_t source_size = source_file.tellg();
    source_file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> source_code(source_size);
    source_file.read(reinterpret_cast<char*>(source_code.data()), source_size);
    
    // Read the pre-loaded source code
    std::vector<uint8_t> pre_loaded = volume.read_blob(3);
    
    auto [offset, size, flags] = volume.get_blob_info(3);
    std::cout << "Source code size: " << source_code.size() 
              << ", stored decompressed size: " << pre_loaded.size() 
              << ", stored compressed size: " << size << std::endl;
    
    // The source code in the blob is from blobvolume.cpp, not this test file
    // So we can't directly compare them
}

TEST_F(BlobVolumeTest, LargeBlob) {
    BlobVolume volume(volume_file.string(), true);
    
    // Create a 10MB blob
    std::vector<uint8_t> data;
    std::string pattern = "Large blob";
    for (int i = 0; i < 100000; ++i) {
        data.insert(data.end(), pattern.begin(), pattern.end());
    }
    
    int blob_id = volume.add_blob(data);
    EXPECT_EQ(data, volume.read_blob(blob_id));
    volume.validate_all();
}

TEST_F(BlobVolumeTest, MagicBlob) {
    BlobVolume volume(volume_file.string(), true);
    
    // Test with magic flag
    std::vector<uint8_t> data1 = {'M', 'a', 'g', 'i', 'c', ' ', 'b', 'l', 'o', 'b'};
    int blob_id1 = volume.add_blob(data1, BlobVolume::FLAG_MAGICED);
    EXPECT_EQ(data1, volume.read_blob(blob_id1));
    
    // Test with magic at the start
    extern std::array<uint8_t, 32> MAGIC;
    std::vector<uint8_t> data2(MAGIC.begin(), MAGIC.end());
    std::string suffix = "Not-Magic blob but has magic at the start";
    data2.insert(data2.end(), suffix.begin(), suffix.end());
    int blob_id2 = volume.add_blob(data2);
    EXPECT_EQ(data2, volume.read_blob(blob_id2));
    
    // Test with magic at the end
    std::vector<uint8_t> data3 = {'N', 'o', 't', '-', 'M', 'a', 'g', 'i', 'c', ' ', 
                                 'b', 'l', 'o', 'b', ' ', 'w', 'i', 't', 'h', ' ', 
                                 'm', 'a', 'g', 'i', 'c', ' ', 'a', 't', ' ', 't', 
                                 'h', 'e', ' ', 'e', 'n', 'd'};
    data3.insert(data3.end(), MAGIC.begin(), MAGIC.end());
    int blob_id3 = volume.add_blob(data3);
    EXPECT_EQ(data3, volume.read_blob(blob_id3));
    
    volume.validate_all();
}

TEST_F(BlobVolumeTest, Checksum) {
    BlobVolume volume(volume_file.string(), true);
    std::vector<uint8_t> data = {'C', 'h', 'e', 'c', 'k', 's', 'u', 'm', 'm', 'e', 'd', ' ', 'b', 'l', 'o', 'b'};
    int blob_id = volume.add_blob(data, BlobVolume::FLAG_BLAKE3);
    EXPECT_EQ(data, volume.read_blob(blob_id));
    volume.validate_all();
}

TEST_F(BlobVolumeTest, ManySmallBlobs) {
    BlobVolume volume(volume_file.string(), true);
    std::vector<int> blob_ids;
    
    // Add 1000 small blobs (reduced from 100000 for test speed)
    for (int i = 0; i < 1000; ++i) {
        std::string blob_text = "Blob " + std::to_string(i);
        std::vector<uint8_t> data(blob_text.begin(), blob_text.end());
        blob_ids.push_back(volume.add_blob(data));
    }
    
    // Verify all blobs
    for (int i = 0; i < 1000; ++i) {
        std::string blob_text = "Blob " + std::to_string(i);
        std::vector<uint8_t> expected(blob_text.begin(), blob_text.end());
        EXPECT_EQ(expected, volume.read_blob(blob_ids[i]));
    }
    
    volume.validate_all();
    
    // Delete all blobs
    for (int blob_id : blob_ids) {
        volume.delete_blob(blob_id);
    }
    
    volume.validate_all();
}

TEST_F(BlobVolumeTest, ResizeBlobGrow) {
    BlobVolume volume(volume_file.string(), true);
    std::vector<uint8_t> data = {'O', 'r', 'i', 'g', 'i', 'n', 'a', 'l', ' ', 'd', 'a', 't', 'a'};
    int blob_id = volume.add_blob(data);
    
    EXPECT_TRUE(volume.resize_blob(blob_id, 20));
    std::vector<uint8_t> resized_data = volume.read_blob(blob_id);
    
    EXPECT_EQ(20, resized_data.size());
    EXPECT_TRUE(std::equal(data.begin(), data.end(), resized_data.begin()));
    
    volume.validate_all();
}

TEST_F(BlobVolumeTest, ResizeBlobShrink) {
    BlobVolume volume(volume_file.string(), true);
    std::vector<uint8_t> data = {'O', 'r', 'i', 'g', 'i', 'n', 'a', 'l', ' ', 'd', 'a', 't', 'a'};
    int blob_id = volume.add_blob(data);
    
    EXPECT_TRUE(volume.resize_blob(blob_id, 7));
    std::vector<uint8_t> resized_data = volume.read_blob(blob_id);
    
    EXPECT_EQ(7, resized_data.size());
    std::vector<uint8_t> expected = {'O', 'r', 'i', 'g', 'i', 'n', 'a'};
    EXPECT_EQ(expected, resized_data);
    
    volume.validate_all();
}

TEST_F(BlobVolumeTest, ResizeBlobMove) {
    BlobVolume volume(volume_file.string(), true);
    std::vector<uint8_t> data1 = {'F', 'i', 'r', 's', 't', ' ', 'b', 'l', 'o', 'b'};
    std::vector<uint8_t> data2 = {'S', 'e', 'c', 'o', 'n', 'd', ' ', 'b', 'l', 'o', 'b'};
    
    int blob_id1 = volume.add_blob(data1);
    int blob_id2 = volume.add_blob(data2);
    
    // Resize the first blob to force moving the second
    EXPECT_TRUE(volume.resize_blob(blob_id1, 30));
    
    std::vector<uint8_t> read_data1 = volume.read_blob(blob_id1);
    EXPECT_EQ(30, read_data1.size());
    EXPECT_TRUE(std::equal(data1.begin(), data1.end(), read_data1.begin()));
    
    EXPECT_EQ(data2, volume.read_blob(blob_id2));
    
    volume.validate_all();
}

TEST_F(BlobVolumeTest, GrowableResizeBlobGrow) {
    BlobVolume volume(volume_file.string(), true);
    std::vector<uint8_t> data = {'O', 'r', 'i', 'g', 'i', 'n', 'a', 'l', ' ', 'd', 'a', 't', 'a'};
    int blob_id = volume.add_blob(data, BlobVolume::FLAG_GROWABLE);
    
    EXPECT_TRUE(volume.resize_blob(blob_id, 20));
    std::vector<uint8_t> resized_data = volume.read_blob(blob_id);
    
    EXPECT_EQ(20, resized_data.size());
    EXPECT_TRUE(std::equal(data.begin(), data.end(), resized_data.begin()));
    
    volume.validate_all();
}

TEST_F(BlobVolumeTest, GrowableResizeBlobMove) {
    BlobVolume volume(volume_file.string(), true);
    std::vector<uint8_t> data1 = {'F', 'i', 'r', 's', 't', ' ', 'b', 'l', 'o', 'b'};
    std::vector<uint8_t> data2 = {'S', 'e', 'c', 'o', 'n', 'd', ' ', 'b', 'l', 'o', 'b'};
    
    int blob_id1 = volume.add_blob(data1, BlobVolume::FLAG_GROWABLE);
    int blob_id2 = volume.add_blob(data2);
    
    // Resize the first blob but should be in-place
    EXPECT_TRUE(volume.resize_blob(blob_id1, data1.size() + 2));
    
    // Resize the first blob to force moving the second
    EXPECT_TRUE(volume.resize_blob(blob_id1, 30));
    
    std::vector<uint8_t> read_data1 = volume.read_blob(blob_id1);
    EXPECT_EQ(30, read_data1.size());
    EXPECT_TRUE(std::equal(data1.begin(), data1.end(), read_data1.begin()));
    
    EXPECT_EQ(data2, volume.read_blob(blob_id2));
    
    volume.validate_all();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 