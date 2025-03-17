
#include "blobindex.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <operations_file.json>" << std::endl;
        return 1;
    }
    
    // Read the operations file
    std::ifstream file(argv[1]);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << argv[1] << std::endl;
        return 1;
    }
    
    json input;
    file >> input;
    file.close();
    
    std::string volume_file = input["volume_file"];
    json operations = input["operations"];
    
    // Results to be returned
    json results;
    
    // Create the volume
    BlobVolume volume(volume_file, true);
    
    for (const auto& op : operations) {
        std::string op_type = op["type"];
        
        if (op_type == "add_blob") {
            std::vector<uint8_t> data;
            for (auto val : op["data"]) {
                data.push_back(static_cast<uint8_t>(val));
            }
            
            uint32_t flags = 0;
            if (op.contains("flags")) {
                flags = op["flags"];
            }
            
            int blob_id = volume.add_blob(data, flags);
            results["blob_" + std::to_string(blob_id)] = {{"id", blob_id}};
        }
        else if (op_type == "read_blob") {
            int blob_id = op["blob_id"];
            std::vector<uint8_t> data = volume.read_blob(blob_id);
            
            std::vector<int> data_as_int;
            for (auto val : data) {
                data_as_int.push_back(static_cast<int>(val));
            }
            
            results["read_" + std::to_string(blob_id)] = {{"data", data_as_int}};
        }
        else if (op_type == "resize_blob") {
            int blob_id = op["blob_id"];
            uint32_t new_size = op["new_size"];
            bool success = volume.resize_blob(blob_id, new_size);
            
            results["resize_" + std::to_string(blob_id) + "_" + std::to_string(new_size)] = 
                {{"success", success}};
        }
        else if (op_type == "get_blob_info") {
            int blob_id = op["blob_id"];
            auto [offset, size, flags] = volume.get_blob_info(blob_id);
            
            results["info_" + std::to_string(blob_id)] = {
                {"offset", offset},
                {"size", size},
                {"flags", flags}
            };
        }
        else if (op_type == "validate") {
            volume.validate_all();
            results["validate"] = {{"success", true}};
        }
    }
    
    // Close the volume
    volume.close();
    
    // If reopen is requested, reopen and perform additional operations
    for (const auto& op : operations) {
        if (op["type"] == "reopen") {
            BlobVolume reopened_volume(volume_file, false);
            
            for (const auto& reopen_op : op["operations"]) {
                if (reopen_op["type"] == "read_blob") {
                    int blob_id = reopen_op["blob_id"];
                    std::vector<uint8_t> data = reopened_volume.read_blob(blob_id);
                    
                    std::vector<int> data_as_int;
                    for (auto val : data) {
                        data_as_int.push_back(static_cast<int>(val));
                    }
                    
                    results["reopen_read_" + std::to_string(blob_id)] = {{"data", data_as_int}};
                }
            }
            
            reopened_volume.close();
        }
    }
    
    // Output the results as JSON
    std::cout << results.dump(4) << std::endl;
    
    return 0;
}
