cmake_minimum_required(VERSION 3.10)
project(blobindex)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include FetchContent for downloading dependencies
include(FetchContent)

# Add nlohmann-json
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json)

# Find required packages
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

# Detect architecture and platform for BLAKE3
include(CheckCCompilerFlag)
check_c_compiler_flag(-mavx2 HAVE_AVX2)
check_c_compiler_flag(-msse4.1 HAVE_SSE41)
check_c_compiler_flag(-msse2 HAVE_SSE2)

# Add blake3 library
add_library(blake3 STATIC
    external/blake3/blake3.c
    external/blake3/blake3_dispatch.c
    external/blake3/blake3_portable.c
)

# Add architecture-specific files based on available compiler flags
if(HAVE_SSE2)
    target_sources(blake3 PRIVATE external/blake3/blake3_sse2.c)
    set_source_files_properties(external/blake3/blake3_sse2.c PROPERTIES COMPILE_FLAGS -msse2)
endif()

if(HAVE_SSE41)
    target_sources(blake3 PRIVATE external/blake3/blake3_sse41.c)
    set_source_files_properties(external/blake3/blake3_sse41.c PROPERTIES COMPILE_FLAGS -msse4.1)
endif()

if(HAVE_AVX2)
    target_sources(blake3 PRIVATE external/blake3/blake3_avx2.c)
    set_source_files_properties(external/blake3/blake3_avx2.c PROPERTIES COMPILE_FLAGS -mavx2)
endif()

# Check if we're on x86_64 architecture for AVX-512
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64" AND HAVE_AVX2)
    check_c_compiler_flag(-mavx512f HAVE_AVX512)
    if(HAVE_AVX512)
        target_sources(blake3 PRIVATE external/blake3/blake3_avx512.c)
        set_source_files_properties(external/blake3/blake3_avx512.c PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512vl")
    endif()
endif()

# ARM NEON support
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
    check_c_compiler_flag(-mfpu=neon HAVE_NEON)
    if(HAVE_NEON)
        target_sources(blake3 PRIVATE external/blake3/blake3_neon.c)
        set_source_files_properties(external/blake3/blake3_neon.c PROPERTIES COMPILE_FLAGS -mfpu=neon)
    endif()
endif()

# Add definitions to ensure consistent behavior
target_compile_definitions(blake3 PRIVATE BLAKE3_USE_NEON=0)

target_include_directories(blake3 PUBLIC external/blake3)

# Add zstd library
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "Build programs" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "Build tests" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "Build shared library" FORCE)
add_subdirectory(external/zstd/build/cmake)

# Add the BlobIndex library
add_library(blobindex
    blobindex/blobindex.hpp
    blobindex/blobstorage.cpp
    blobindex/blobvolume.cpp
    blobindex/blobutils.cpp
    blobindex/bloblocks.cpp
)
target_link_libraries(blobindex PRIVATE blake3 libzstd_static)
target_include_directories(blobindex PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/blobindex)

# Add the test executable
add_executable(blobindex_test blobindex/blobindex_test.cpp)
target_link_libraries(blobindex_test PRIVATE blobindex gtest gtest_main pthread)

# Enable testing
enable_testing()
add_test(NAME BlobIndexTests COMMAND blobindex_test)

# Add a simple example executable
add_executable(blobindex_example blobindex/blobindex_example.cpp)
target_link_libraries(blobindex_example PRIVATE blobindex)

# Comparison test executable
add_executable(blobindex_compare blobindex/blobindex_compare.cpp)
target_link_libraries(blobindex_compare PRIVATE blobindex nlohmann_json::nlohmann_json)
target_include_directories(blobindex_compare PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/blobindex)

# Additional test executables
add_executable(blake3_test blobindex/tests/blake3_test.cpp)
target_link_libraries(blake3_test PRIVATE blake3)

add_executable(header_fix_test blobindex/tests/header_fix_test.cpp)
target_link_libraries(header_fix_test PRIVATE blake3)

add_executable(header_test blobindex/tests/header_test.cpp)
target_link_libraries(header_test PRIVATE blake3) 