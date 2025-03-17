#include "blobindex.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

int main(int argc, char** argv) {
    // Check if a filename was provided
    std::string filename = "example_volume.dat";
    if (argc > 1) {
        filename = argv[1];
    }

    // Create a new volume
    try {
        std::cout << "Creating a new BlobVolume at " << filename << std::endl;
        BlobVolume volume(filename, true);

        // Add some blobs
        std::string text1 = "Hello, BlobIndex!";
        std::vector<uint8_t> data1(text1.begin(), text1.end());
        int blob_id1 = volume.add_blob(data1);
        std::cout << "Added blob 1 with ID " << blob_id1 << std::endl;

        std::string text2 = "This is a compressed blob.";
        std::vector<uint8_t> data2(text2.begin(), text2.end());
        int blob_id2 = volume.add_blob(data2, BlobVolume::FLAG_COMPRESSED);
        std::cout << "Added compressed blob 2 with ID " << blob_id2 << std::endl;

        std::string text3 = "This is a blob with a checksum.";
        std::vector<uint8_t> data3(text3.begin(), text3.end());
        int blob_id3 = volume.add_blob(data3, BlobVolume::FLAG_BLAKE3);
        std::cout << "Added checksummed blob 3 with ID " << blob_id3 << std::endl;

        std::string text4 = "This is a growable blob.";
        std::vector<uint8_t> data4(text4.begin(), text4.end());
        int blob_id4 = volume.add_blob(data4, BlobVolume::FLAG_GROWABLE);
        std::cout << "Added growable blob 4 with ID " << blob_id4 << std::endl;

        // Read the blobs back
        std::vector<uint8_t> read_data1 = volume.read_blob(blob_id1);
        std::string read_text1(read_data1.begin(), read_data1.end());
        std::cout << "Read blob 1: " << read_text1 << std::endl;

        std::vector<uint8_t> read_data2 = volume.read_blob(blob_id2);
        std::string read_text2(read_data2.begin(), read_data2.end());
        std::cout << "Read blob 2: " << read_text2 << std::endl;

        std::vector<uint8_t> read_data3 = volume.read_blob(blob_id3);
        std::string read_text3(read_data3.begin(), read_data3.end());
        std::cout << "Read blob 3: " << read_text3 << std::endl;

        std::vector<uint8_t> read_data4 = volume.read_blob(blob_id4);
        std::string read_text4(read_data4.begin(), read_data4.end());
        std::cout << "Read blob 4: " << read_text4 << std::endl;

        // Resize the growable blob
        std::string new_text4 = "This is a resized growable blob with more text.";
        std::vector<uint8_t> new_data4(new_text4.begin(), new_text4.end());
        volume.write_blob(blob_id4, new_data4);
        std::cout << "Resized blob 4" << std::endl;

        // Read the resized blob
        std::vector<uint8_t> read_new_data4 = volume.read_blob(blob_id4);
        std::string read_new_text4(read_new_data4.begin(), read_new_data4.end());
        std::cout << "Read resized blob 4: " << read_new_text4 << std::endl;

        // Delete a blob
        volume.delete_blob(blob_id3);
        std::cout << "Deleted blob 3" << std::endl;

        // Validate the volume
        std::cout << "Validating the volume..." << std::endl;
        volume.validate_all();
        std::cout << "Validation complete" << std::endl;

        // Close the volume
        volume.close();
        std::cout << "Closed the volume" << std::endl;

        // Reopen the volume
        std::cout << "Reopening the volume..." << std::endl;
        BlobVolume reopened_volume(filename);

        // Read a blob from the reopened volume
        std::vector<uint8_t> reopened_data1 = reopened_volume.read_blob(blob_id1);
        std::string reopened_text1(reopened_data1.begin(), reopened_data1.end());
        std::cout << "Read blob 1 from reopened volume: " << reopened_text1 << std::endl;

        // Try to read the deleted blob (should throw an exception)
        try {
            std::vector<uint8_t> deleted_data = reopened_volume.read_blob(blob_id3);
            std::cout << "ERROR: Was able to read deleted blob 3!" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Expected exception when reading deleted blob 3: " << e.what() << std::endl;
        }

        // Close the reopened volume
        reopened_volume.close();
        std::cout << "Closed the reopened volume" << std::endl;

        // Clean up
        std::filesystem::remove(filename);
        std::cout << "Removed the volume file" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
} 