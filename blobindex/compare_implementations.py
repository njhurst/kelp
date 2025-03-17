#!/usr/bin/env python3
import os
import sys
import tempfile
import subprocess
import json
import numpy as np
from typing import Dict, List, Tuple, Any
import shutil

# Import the Python implementation
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
from blobindex4 import BlobVolume as PyBlobVolume

class ComparisonTest:
    def __init__(self, test_name: str):
        self.test_name = test_name
        self.temp_dir = tempfile.mkdtemp()
        self.py_volume_file = os.path.join(self.temp_dir, f"py_{test_name}.dat")
        self.cpp_volume_file = os.path.join(self.temp_dir, f"cpp_{test_name}.dat")
        self.py_results = {}
        self.cpp_results = {}
        
    def cleanup(self):
        shutil.rmtree(self.temp_dir)
    
    def run_python_test(self, operations: List[Dict[str, Any]]) -> Dict[str, Any]:
        """Run operations on the Python implementation and return results"""
        results = {}
        
        # Create the volume
        py_volume = PyBlobVolume(self.py_volume_file, create=True)
        
        for op in operations:
            op_type = op["type"]
            
            if op_type == "add_blob":
                data = bytes(op["data"])
                flags = op.get("flags", 0)
                blob_id = py_volume.add_blob(data, flags)
                results[f"blob_{blob_id}"] = {"id": blob_id}
            
            elif op_type == "read_blob":
                blob_id = op["blob_id"]
                data = py_volume.read_blob(blob_id)
                results[f"read_{blob_id}"] = {"data": list(data)}
            
            elif op_type == "resize_blob":
                blob_id = op["blob_id"]
                new_size = op["new_size"]
                success = py_volume.resize_blob(blob_id, new_size)
                results[f"resize_{blob_id}_{new_size}"] = {"success": success}
            
            elif op_type == "get_blob_info":
                blob_id = op["blob_id"]
                offset, size, flags = py_volume.get_blob_info(blob_id)
                results[f"info_{blob_id}"] = {"offset": offset, "size": size, "flags": flags}
            
            elif op_type == "validate":
                py_volume.validate_all()
                results["validate"] = {"success": True}
        
        # Close the volume
        py_volume.close()
        
        # If reopen is requested, reopen and perform additional operations
        if any(op["type"] == "reopen" for op in operations):
            py_volume = PyBlobVolume(self.py_volume_file, create=False)
            
            for op in operations:
                if op["type"] == "reopen":
                    for reopen_op in op["operations"]:
                        if reopen_op["type"] == "read_blob":
                            blob_id = reopen_op["blob_id"]
                            data = py_volume.read_blob(blob_id)
                            results[f"reopen_read_{blob_id}"] = {"data": list(data)}
            
            py_volume.close()
        
        return results
    
    def run_cpp_test(self, operations: List[Dict[str, Any]]) -> Dict[str, Any]:
        """Run operations on the C++ implementation and return results"""
        results = {}
        
        # Create a temporary JSON file with the operations
        ops_file = os.path.join(self.temp_dir, f"ops_{self.test_name}.json")
        with open(ops_file, 'w') as f:
            json.dump({
                "volume_file": self.cpp_volume_file,
                "operations": operations
            }, f)
        
        # Run the C++ test program
        cpp_test_program = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build/blobindex_compare")
        result = subprocess.run([cpp_test_program, ops_file], 
                               stdout=subprocess.PIPE, 
                               stderr=subprocess.PIPE,
                               text=True)
        
        if result.returncode != 0:
            print(f"C++ test failed with error: {result.stderr}")
            return {"error": result.stderr}
        
        # Extract only the JSON part from the output
        output = result.stdout
        try:
            # Find the first '{' and the last '}'
            start_idx = output.find('{')
            end_idx = output.rfind('}')
            
            if start_idx >= 0 and end_idx > start_idx:
                json_str = output[start_idx:end_idx+1]
                results = json.loads(json_str)
            else:
                print(f"Failed to extract JSON from output: {output}")
                return {"error": "Failed to extract JSON from output"}
        except json.JSONDecodeError as e:
            print(f"Failed to parse C++ output: {output}")
            print(f"JSON error: {e}")
            return {"error": "Failed to parse output"}
        
        return results
    
    def compare_results(self) -> Tuple[bool, List[str]]:
        """Compare Python and C++ results and return differences"""
        differences = []
        
        # Check if all keys in Python results are in C++ results
        for key in self.py_results:
            if key not in self.cpp_results:
                differences.append(f"Key '{key}' missing in C++ results")
                continue
            
            py_value = self.py_results[key]
            cpp_value = self.cpp_results[key]
            
            # Compare dictionaries
            if isinstance(py_value, dict) and isinstance(cpp_value, dict):
                for k in py_value:
                    if k not in cpp_value:
                        differences.append(f"Subkey '{k}' of '{key}' missing in C++ results")
                    elif k == 'offset':
                        # Skip comparing offset values as they can differ between implementations
                        continue
                    elif py_value[k] != cpp_value[k]:
                        differences.append(f"Value mismatch for '{key}.{k}': Python={py_value[k]}, C++={cpp_value[k]}")
        
        # Check if all keys in C++ results are in Python results
        for key in self.cpp_results:
            if key not in self.py_results:
                differences.append(f"Key '{key}' missing in Python results")
        
        return len(differences) == 0, differences
    
    def run_test(self, operations: List[Dict[str, Any]]) -> Tuple[bool, List[str]]:
        """Run the test on both implementations and compare results"""
        try:
            self.py_results = self.run_python_test(operations)
            self.cpp_results = self.run_cpp_test(operations)
            return self.compare_results()
        finally:
            self.cleanup()

def create_cpp_comparison_program():
    """Create a C++ program that runs operations from a JSON file"""
    cpp_code = """
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
"""
    
    # Create the C++ file
    cpp_file = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "blobindex/blobindex_compare.cpp")
    with open(cpp_file, 'w') as f:
        f.write(cpp_code)
    
    # Add to CMakeLists.txt
    cmake_file = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "CMakeLists.txt")
    with open(cmake_file, 'r') as f:
        cmake_content = f.read()
    
    if "blobindex_compare" not in cmake_content:
        with open(cmake_file, 'a') as f:
            f.write("\n# Comparison test executable\n")
            f.write("add_executable(blobindex_compare blobindex_compare.cpp)\n")
            f.write("target_link_libraries(blobindex_compare PRIVATE blobindex)\n")
            f.write("target_include_directories(blobindex_compare PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})\n")
    
    # Build the program
    build_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build")
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)
    
    subprocess.run(["cmake", ".."], cwd=build_dir, check=True)
    subprocess.run(["make", "blobindex_compare"], cwd=build_dir, check=True)

def run_tests():
    """Run a series of comparison tests"""
    # Create the C++ comparison program
    create_cpp_comparison_program()
    
    # Define test cases
    test_cases = [
        {
            "name": "simple_add_read",
            "operations": [
                {
                    "type": "add_blob",
                    "data": [72, 101, 108, 108, 111, 44, 32, 87, 111, 114, 108, 100, 33]  # "Hello, World!"
                },
                {
                    "type": "read_blob",
                    "blob_id": 4  # First user blob is at index 4
                },
                {
                    "type": "validate"
                }
            ]
        },
        {
            "name": "multiple_blobs",
            "operations": [
                {
                    "type": "add_blob",
                    "data": [70, 105, 114, 115, 116, 32, 98, 108, 111, 98]  # "First blob"
                },
                {
                    "type": "add_blob",
                    "data": [83, 101, 99, 111, 110, 100, 32, 98, 108, 111, 98]  # "Second blob"
                },
                {
                    "type": "read_blob",
                    "blob_id": 4
                },
                {
                    "type": "read_blob",
                    "blob_id": 5
                },
                {
                    "type": "get_blob_info",
                    "blob_id": 4
                },
                {
                    "type": "validate"
                }
            ]
        },
        {
            "name": "compressed_blobs",
            "operations": [
                {
                    "type": "add_blob",
                    "data": [70, 105, 114, 115, 116, 32, 98, 108, 111, 98],  # "First blob"
                    "flags": 4  # FLAG_COMPRESSED
                },
                {
                    "type": "read_blob",
                    "blob_id": 4
                },
                {
                    "type": "validate"
                }
            ]
        },
        {
            "name": "reopen_volume",
            "operations": [
                {
                    "type": "add_blob",
                    "data": [84, 101, 115, 116, 32, 100, 97, 116, 97]  # "Test data"
                },
                {
                    "type": "reopen",
                    "operations": [
                        {
                            "type": "read_blob",
                            "blob_id": 4
                        }
                    ]
                },
                {
                    "type": "validate"
                }
            ]
        },
        {
            "name": "resize_blob",
            "operations": [
                {
                    "type": "add_blob",
                    "data": [79, 114, 105, 103, 105, 110, 97, 108, 32, 100, 97, 116, 97]  # "Original data"
                },
                {
                    "type": "resize_blob",
                    "blob_id": 4,
                    "new_size": 20
                },
                {
                    "type": "read_blob",
                    "blob_id": 4
                },
                {
                    "type": "validate"
                }
            ]
        }
    ]
    
    # Run the tests
    for test_case in test_cases:
        print(f"Running test: {test_case['name']}")
        test = ComparisonTest(test_case["name"])
        success, differences = test.run_test(test_case["operations"])
        
        if success:
            print(f"✅ Test {test_case['name']} passed!")
        else:
            print(f"❌ Test {test_case['name']} failed!")
            for diff in differences:
                print(f"  - {diff}")
        print()

if __name__ == "__main__":
    run_tests() 