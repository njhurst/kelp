#include "blobindex.hpp"
#include <algorithm>
#include <stdexcept>

BlobLocks::BlobLocks() : lock_id(0) {}

int BlobLocks::acquire_lock(int lock_type, int lock_owner, uint64_t start_offset, uint64_t end_offset) {
    std::lock_guard<std::mutex> guard(lock_lock);
    
    if (lock_type == 1) {
        // exclusive lock
        for (const auto& lock : locks) {
            if (std::get<2>(lock) == lock_owner && 
                std::get<4>(lock) <= start_offset && 
                std::get<5>(lock) >= end_offset) {
                throw std::runtime_error("Lock already held");
            }
        }
    } else if (lock_type == 0) {
        // shared lock
        for (const auto& lock : locks) {
            if (std::get<2>(lock) == lock_owner && 
                std::get<1>(lock) == 1 && 
                std::get<4>(lock) <= start_offset && 
                std::get<5>(lock) >= end_offset) {
                throw std::runtime_error("Exclusive lock already held");
            }
        }
    }
    // watch lock - add to the list of locks
    
    int current_lock_id = lock_id++;
    locks.emplace_back(current_lock_id, lock_type, lock_owner, 1, start_offset, end_offset);
    return current_lock_id;
}

void BlobLocks::release_lock(int lock_id) {
    for (size_t idx = 0; idx < locks.size(); ++idx) {
        if (std::get<0>(locks[idx]) == lock_id) {
            if (std::get<3>(locks[idx]) == 1) {
                locks.erase(locks.begin() + idx);
            } else {
                auto lock = locks[idx];
                locks[idx] = std::make_tuple(
                    std::get<0>(lock),
                    std::get<1>(lock),
                    std::get<2>(lock),
                    std::get<3>(lock) - 1,
                    std::get<4>(lock),
                    std::get<5>(lock)
                );
            }
            return;
        }
    }
    throw std::runtime_error("Lock not found");
}

bool BlobLocks::check_read_range(int lock_owner, uint64_t start_offset, uint64_t end_offset) {
    for (const auto& lock : locks) {
        if (std::get<2>(lock) == lock_owner && 
            std::get<1>(lock) == 1 && 
            std::get<4>(lock) <= start_offset && 
            std::get<5>(lock) >= end_offset) {
            return false;
        }
    }
    return true;
}

bool BlobLocks::check_write_range(int lock_owner, uint64_t start_offset, uint64_t end_offset) {
    for (const auto& lock : locks) {
        if (std::get<2>(lock) == lock_owner && 
            std::get<4>(lock) <= start_offset && 
            std::get<5>(lock) >= end_offset) {
            return false;
        }
    }
    return true;
}

void BlobLocks::release_all_locks(int lock_owner) {
    locks.erase(
        std::remove_if(locks.begin(), locks.end(),
            [lock_owner](const auto& lock) { return std::get<2>(lock) == lock_owner; }),
        locks.end()
    );
}

std::vector<std::tuple<int, int, int, int, uint64_t, uint64_t>> BlobLocks::get_locks(int lock_owner) {
    std::vector<std::tuple<int, int, int, int, uint64_t, uint64_t>> result;
    for (const auto& lock : locks) {
        if (std::get<2>(lock) == lock_owner) {
            result.push_back(lock);
        }
    }
    return result;
}

std::tuple<int, int, int, int, uint64_t, uint64_t> BlobLocks::get_lock(int lock_id) {
    for (const auto& lock : locks) {
        if (std::get<0>(lock) == lock_id) {
            return lock;
        }
    }
    throw std::runtime_error("Lock not found");
}

bool BlobLocks::has_lock(int lock_id) {
    for (const auto& lock : locks) {
        if (std::get<0>(lock) == lock_id) {
            return true;
        }
    }
    return false;
}

bool BlobLocks::has_locks(int lock_owner) {
    return !get_locks(lock_owner).empty();
}

bool BlobLocks::has_exclusive_locks(int lock_owner) {
    for (const auto& lock : get_locks(lock_owner)) {
        if (std::get<1>(lock) == 1) {
            return true;
        }
    }
    return false;
}

bool BlobLocks::has_shared_locks(int lock_owner) {
    for (const auto& lock : get_locks(lock_owner)) {
        if (std::get<1>(lock) == 0) {
            return true;
        }
    }
    return false;
}

bool BlobLocks::has_watch_locks(int lock_owner) {
    for (const auto& lock : get_locks(lock_owner)) {
        if (std::get<1>(lock) == 2) {
            return true;
        }
    }
    return false;
}

bool BlobLocks::has_locks_in_range(int lock_owner, uint64_t start_offset, uint64_t end_offset) {
    for (const auto& lock : get_locks(lock_owner)) {
        if (std::get<4>(lock) <= start_offset && std::get<5>(lock) >= end_offset) {
            return true;
        }
    }
    return false;
}

bool BlobLocks::has_exclusive_locks_in_range(int lock_owner, uint64_t start_offset, uint64_t end_offset) {
    for (const auto& lock : get_locks(lock_owner)) {
        if (std::get<1>(lock) == 1 && 
            std::get<4>(lock) <= start_offset && 
            std::get<5>(lock) >= end_offset) {
            return true;
        }
    }
    return false;
}

bool BlobLocks::has_shared_locks_in_range(int lock_owner, uint64_t start_offset, uint64_t end_offset) {
    for (const auto& lock : get_locks(lock_owner)) {
        if (std::get<1>(lock) == 0 && 
            std::get<4>(lock) <= start_offset && 
            std::get<5>(lock) >= end_offset) {
            return true;
        }
    }
    return false;
}

bool BlobLocks::has_watch_locks_in_range(int lock_owner, uint64_t start_offset, uint64_t end_offset) {
    for (const auto& lock : get_locks(lock_owner)) {
        if (std::get<1>(lock) == 2 && 
            std::get<4>(lock) <= start_offset && 
            std::get<5>(lock) >= end_offset) {
            return true;
        }
    }
    return false;
}

bool BlobLocks::has_locks_in_range_exclusive(int lock_owner, uint64_t start_offset, uint64_t end_offset) {
    for (const auto& lock : get_locks(lock_owner)) {
        if (std::get<1>(lock) == 1 && 
            std::get<4>(lock) <= start_offset && 
            std::get<5>(lock) >= end_offset) {
            return true;
        }
    }
    return false;
} 