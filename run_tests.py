#!/usr/bin/env python3
"""
Run comparison tests between Python and C++ implementations of BlobIndex.
"""

import os
import sys
import subprocess

# Add the blobindex directory to the Python path
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'blobindex'))

# Import the comparison test runner
from blobindex.compare_implementations import run_tests

if __name__ == "__main__":
    # Ensure the build directory exists
    build_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'build')
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)
        
    # Build the C++ code
    print("Building C++ code...")
    subprocess.run(["cmake", ".."], cwd=build_dir, check=True)
    subprocess.run(["make", "blobindex_compare"], cwd=build_dir, check=True)
    
    # Run the tests
    print("\nRunning comparison tests...")
    run_tests() 