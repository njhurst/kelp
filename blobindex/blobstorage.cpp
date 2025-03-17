#include "blobindex.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <iostream>

BlobStorage::BlobStorage(const std::string& filename, bool create)
    : filename(filename), fd(-1), file_size(0) {
    if (create) {
        _create_new_volume();
    } else {
        _open_existing_volume();
    }
}

BlobStorage::~BlobStorage() {
    close();
}

void BlobStorage::_create_new_volume() {
    if (access(filename.c_str(), F_OK) == 0) {
        throw std::runtime_error("File " + filename + " already exists");
    }
    
    fd = ::open(filename.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        throw std::runtime_error("Failed to create file: " + std::string(strerror(errno)));
    }
    
    std::vector<uint8_t> zeros(HEADER_SIZE, 0);
    pwrite(zeros, 0);
    file_size = HEADER_SIZE;
}

void BlobStorage::_open_existing_volume() {
    if (access(filename.c_str(), F_OK) != 0) {
        throw std::runtime_error("File " + filename + " not found");
    }
    
    fd = ::open(filename.c_str(), O_RDWR);
    if (fd == -1) {
        throw std::runtime_error("Failed to open file: " + std::string(strerror(errno)));
    }
    
    struct stat st;
    if (fstat(fd, &st) == -1) {
        close();
        throw std::runtime_error("Failed to get file size: " + std::string(strerror(errno)));
    }
    file_size = st.st_size;
}

std::vector<uint8_t> BlobStorage::pread(size_t size, uint64_t offset) {
    std::vector<uint8_t> buffer(size);
    ssize_t bytes_read = ::pread(fd, buffer.data(), size, offset);
    
    if (bytes_read == -1) {
        throw std::runtime_error("Failed to read from file: " + std::string(strerror(errno)));
    }
    
    buffer.resize(bytes_read);
    return buffer;
}

void BlobStorage::pwrite(const std::vector<uint8_t>& data, uint64_t offset) {
    ssize_t bytes_written = ::pwrite(fd, data.data(), data.size(), offset);
    
    if (bytes_written == -1) {
        throw std::runtime_error("Failed to write to file: " + std::string(strerror(errno)));
    }
    
    if (static_cast<size_t>(bytes_written) != data.size()) {
        throw std::runtime_error("Failed to write all data to file");
    }
    
    file_size = std::max(file_size, offset + data.size());
}

uint64_t BlobStorage::write_blob(const std::vector<uint8_t>& data, uint64_t offset) {
    pwrite(data, offset);
    return offset;
}

std::vector<uint8_t> BlobStorage::read_blob(uint64_t offset, size_t size) {
    return pread(size, offset);
}

std::vector<uint64_t> BlobStorage::scan_for_pattern(const std::vector<uint8_t>& pattern) {
    std::vector<uint64_t> positions;
    const size_t chunk_size = 16 * 1024 * 1024;  // 16MB chunks
    const size_t pattern_length = pattern.size();
    uint64_t offset = 0;
    
    while (offset < file_size) {
        std::vector<uint8_t> chunk = pread(chunk_size, offset);
        if (chunk.empty() || chunk.size() < pattern_length) {
            break;
        }
        
        for (size_t i = 0; i <= chunk.size() - pattern_length; ++i) {
            if (std::equal(pattern.begin(), pattern.end(), chunk.begin() + i)) {
                positions.push_back(offset + i);
            }
        }
        
        offset += chunk.size() - pattern_length + 1;
    }
    
    return positions;
}

void BlobStorage::close() {
    if (fd != -1) {
        ::close(fd);
        fd = -1;
    }
}

bool BlobStorage::is_open() const {
    return fd != -1;
} 