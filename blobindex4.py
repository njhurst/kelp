""" BlobIndex - a class to manage the blob index file

    A blobindex is stored on disk only as the offsets and sizes, the blob id being implicit in the order of the blobs.

    array of u32 offset (in 8byte values) to the start of each blob in the blobdata file, u32 combined 24b length in bytes and 8b flags.

    The flags are:
    0x01: metadata - references to valid blobs are garbage collected, we require the blob to be a valid cbor2 object, and we can extract references from the blob using a custom tag (CUSTOM_REF_TAG)
    0x02: growable - the blob can be resized (in place if possible)
    0x04: compressed - the blob is compressed with zlib
    0x08: blake3 - the blob is hashed with blake3, the hash is stored at the end of the blob
    0x10: magiced - the blob begins with the magic number and a data length so it can be found by a exhaustive search.  Typically you'll want to add some tagging information to the blob so you can identify its purpose.
    ... unused ...
    0x80: deleted - the blob is deleted, but we never reuse indices

    special offsets:
    0x00: the offset of this index
    0x01: the offset of the secondary metadata index
    0x02: the offset of the list of root blobs (the blobs that are not referenced by any other blob for garbage collection)
    0x03: the offset of the source code for the blobindex

    when running the blobindex is loaded into memory, additional indices include:
    per blob trailing space (u32) to the start of the next blob
    list of free spaces (u32) in the blobindex file

    updates are alternating between two blobindex files, the current and the next, the next is written to disk and then the current is replaced with the next.  The whole process is done in a single transaction.  If there is space in the current blobindex file then the next is written to the end of the current, otherwise the current is copied to the next and the next is written to the end of the current.

    operations:
    add new blob with size and flags
    remove blob
    garbage collect metadata
    defragment range
    resize blob

    HEADER:

    4KB header
    Magic number 32 bytes (blake3 hash of "blobindex")
    Version number 4 bytes
    Volume prefix id (random number greater than 2^24, to make it unlikely to collide with a valid offset, and garbage collectable)
    Primary index offset 8 bytes
    Secondary index offset 8 bytes
    Root index offset 8 bytes
    Tail offset 8 bytes
    blake3 hash of the header 32 bytes
    
    struct string for header = "32sIIQQQQ"
"""


import struct
import os
import numpy as np
from typing import Tuple, Optional
import unittest
import tempfile
import blake3
import sys
import cbor2
import zstandard

MAGIC = blake3.blake3(b"blobindex").digest()
CUSTOM_REF_TAG = 1000  # The tag we're using for our 8-byte references
CUSTOM_WEAK_REF_TAG = 1001  # The tag we're using for our 8-byte weak references (not garbage collected)

def extract_tagged_references(obj, weak=False):
    """Extract the tagged references from a CBOR object. Optionally scan for weak references."""
    references = []
    weak_references = []

    def traverse(item):
        if isinstance(item, cbor2.CBORTag):
            if item.tag == CUSTOM_REF_TAG and isinstance(item.value, bytes) and len(item.value) == 8:
                references.append(item.value)
            elif weak and item.tag == CUSTOM_WEAK_REF_TAG and isinstance(item.value, bytes) and len(item.value) == 8:
                weak_references.append(item.value)
        elif isinstance(item, dict):
            for value in item.values():
                traverse(value)
        elif isinstance(item, (list, tuple)):
            for element in item:
                traverse(element)

    traverse(obj)
    return references, weak_references



class BlobStorage:
    HEADER_SIZE = 4096  # 4KB header

    def __init__(self, filename: str, create: bool = False):
        self.filename = filename
        self.fd = None
        self.file_size = 0
        
        if create:
            self._create_new_volume()
        else:
            self._open_existing_volume()

    def _create_new_volume(self):
        if os.path.exists(self.filename):
            raise FileExistsError(f"File {self.filename} already exists")
        
        self.fd = os.open(self.filename, os.O_RDWR | os.O_CREAT)
        os.write(self.fd, b'\0' * self.HEADER_SIZE)
        self.file_size = self.HEADER_SIZE

    def pread(self, size: int, offset: int) -> bytes:
        return os.pread(self.fd, size, offset)

    def pwrite(self, data: bytes, offset: int):
        os.pwrite(self.fd, data, offset)
        self.file_size = max(self.file_size, offset + len(data))

    def _open_existing_volume(self):
        if not os.path.exists(self.filename):
            raise FileNotFoundError(f"File {self.filename} not found")
        
        self.fd = os.open(self.filename, os.O_RDWR)
        self.file_size = os.path.getsize(self.filename)

    def write_blob(self, data: bytes, offset: int) -> int:
        os.pwrite(self.fd, data, offset)
        self.file_size = max(self.file_size, offset + len(data))
        return offset

    def read_blob(self, offset: int, size: int) -> bytes:
        return os.pread(self.fd, size, offset)

    def scan_for_pattern(self, pattern: bytes):
        """Read the whole storage and yield positions of pattern (e.g. MAGIC)."""
        chunk_size = 16 * 1024 * 1024  # 16MB chunks
        pattern_length = len(pattern)
        offset = 0
        
        while offset < self.file_size:
            chunk = os.pread(self.fd, chunk_size, offset)
            if not chunk or len(chunk) < pattern_length:
                break
            
            start = 0
            while True:
                idx = chunk.find(pattern, start)
                if idx == -1:
                    break
                yield offset + idx
                start = idx + 1
            
            offset += len(chunk) - pattern_length + 1

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None

class BlobLocks:
    """ 
    A simple lock manager for the BlobVolume

    The lock manager is a list of locks, each lock is a tuple of (lock_id, lock_type, lock_owner, lock_count, start_offset, end_offset)

    lock_id: a unique identifier for the lock
    lock_type: 0 for shared, 1 for exclusive, 2 for watch locks
    lock_owner: the owner of the lock
    lock_count: the number of times the lock has been acquired
    start_offset: the start of the locked range
    end_offset: the end of the locked range

    when a user wishes to read data they acquire a shared lock, when they wish to write data they acquire an exclusive lock, when they wish to watch for changes they acquire a watch lock.

    when a user acquires a lock they are given a lock_id, they must provide this lock_id when releasing the lock.

    It might be worth storing in a more efficient data structure, but for now we'll just use a list.
    """

    def __init__(self):
        self.locks = []
        self.lock_id = 0
        self.lock_lock = threading.Lock()
    
    def acquire_lock(self, lock_type: int, lock_owner: int, start_offset: int, end_offset: int) -> int:
        """ First check if the lock is already held, if not acquire the lock.
            Use the lock_lock, check for overlapping locks, and then add the lock if there are no overlaps.
            otherwise raise an exception.
        """
        with self.lock_lock:
            if lock_type == 1:
                # exclusive lock
                for lock in self.locks:
                    if lock[2] == lock_owner and lock[4] <= start_offset and lock[5] >= end_offset:
                        raise ValueError("Lock already held")
            elif lock_type == 0:
                # shared lock
                for lock in self.locks:
                    if lock[2] == lock_owner and lock[1] == 1 and lock[4] <= start_offset and lock[5] >= end_offset:
                        raise ValueError("Exclusive lock already held")
            else:
                # watch lock - add to the list of locks
                pass
            
            lock_id = self.lock_id
            self.lock_id += 1
            self.locks.append((lock_id, lock_type, lock_owner, 1, start_offset, end_offset))
            return lock_id
    
    def release_lock(self, lock_id: int):
        for idx, lock in enumerate(self.locks):
            if lock[0] == lock_id:
                if lock[3] == 1:
                    del self.locks[idx]
                else:
                    self.locks[idx] = (lock[0], lock[1], lock[2], lock[3] - 1, lock[4], lock[5])
                return
        raise ValueError("Lock not found")

    def check_read_range(self, lock_owner: int, start_offset: int, end_offset: int):
        for lock in self.locks:
            if lock[2] == lock_owner and lock[1] == 1 and lock[4] <= start_offset and lock[5] >= end_offset:
                return False
        return True
    
    def check_write_range(self, lock_owner: int, start_offset: int, end_offset: int):
        for lock in self.locks:
            if lock[2] == lock_owner and lock[4] <= start_offset and lock[5] >= end_offset:
                return False
        return True
    
    def release_all_locks(self, lock_owner: int):
        self.locks = [lock for lock in self.locks if lock[2] != lock_owner]
    
    def get_locks(self, lock_owner: int):
        return [lock for lock in self.locks if lock[2] == lock_owner]

    def get_lock(self, lock_id: int):
        for lock in self.locks:
            if lock[0] == lock_id:
                return lock
        return None
    
    def has_lock(self, lock_id: int):
        return self.get_lock(lock_id) is not None

    def has_locks(self, lock_owner: int):
        return len(self.get_locks(lock_owner)) > 0
    
    def has_exclusive_locks(self, lock_owner: int):
        return any(lock[1] == 1 for lock in self.get_locks(lock_owner))

    def has_shared_locks(self, lock_owner: int):
        return any(lock[1] == 0 for lock in self.get_locks(lock_owner))
    
    def has_watch_locks(self, lock_owner: int):
        return any(lock[1] == 2 for lock in self.get_locks(lock_owner))
    
    def has_locks_in_range(self, lock_owner: int, start_offset: int, end_offset: int):
        return any(lock[4] <= start_offset and lock[5] >= end_offset for lock in self.get_locks(lock_owner))
    
    def has_exclusive_locks_in_range(self, lock_owner: int, start_offset: int, end_offset: int):
        return any(lock[1] == 1 and lock[4] <= start_offset and lock[5] >= end_offset for lock in self.get_locks(lock_owner))
    
    def has_shared_locks_in_range(self, lock_owner: int, start_offset: int, end_offset: int):
        return any(lock[1] == 0 and lock[4] <= start_offset and lock[5] >= end_offset for lock in self.get_locks(lock_owner))
    
    def has_watch_locks_in_range(self, lock_owner: int, start_offset: int, end_offset: int):
        return any(lock[1] == 2 and lock[4] <= start_offset and lock[5] >= end_offset for lock in self.get_locks(lock_owner))

    def has_locks_in_range_exclusive(self, lock_owner: int, start_offset: int, end_offset: int):
        return any(lock[4] <= start_offset and lock[5] >= end_offset for lock in self.get_locks(lock_owner) if lock[1] == 1)

class BlobVolume:
    INDEX_ENTRY_DTYPE = np.dtype([
        ('offset', np.uint64),
        ('size', np.uint32),
        ('flags', np.uint32)
    ])
    SPECIAL_OFFSETS = {
        'index': 0,
        'metadata': 1,
        'root': 2
    }
    FLAGS = {
        'metadata': 0x01,
        'growable': 0x02,
        'compressed': 0x04,
        'blake3': 0x08,
        'magiced': 0x10,
        'deleted': 0x80,
    }
    HEADER_STRING = "32sIIQQQQ"

    def __init__(self, filename: str, create: bool = False):
        self.storage = BlobStorage(filename, create)
        self.entries_size = 3
        self.entries = np.zeros(shape=self.entries_size, dtype=self.INDEX_ENTRY_DTYPE)
        self.tail_pos = None
        self.growable = {}  # the growable blobs mapping to their allocated space
        if create:
            self._create_new_index()
        else:
            self._load_index()
    
    def _create_new_index(self):
        self.storage.pwrite(b'\0' * self.storage.HEADER_SIZE, 0)
        self.tail_pos = self.storage.HEADER_SIZE

        # write the special offsets - primary, secondary and root; all growable
        data_length = len(self.generate_index_blob())
        self.entries[0] = (self.tail_pos, data_length, self.FLAGS['growable'] | self.FLAGS['compressed'])
        self.tail_pos += data_length*2
        self.entries[1] = (self.tail_pos, data_length, self.FLAGS['growable'] | self.FLAGS['compressed'])
        self.tail_pos += data_length*2
        self.entries[2] = (self.tail_pos, 0, self.FLAGS['growable'] | self.FLAGS['metadata'] | self.FLAGS['compressed'])

        # include the source code for the blobindex in position 3
        with open(sys.argv[0], "rb") as f:
            source_code = f.read()
        self.add_blob(source_code, flags=self.FLAGS['compressed'] | self.FLAGS['magiced'])

        index_blob_data = self.generate_index_blob()
        for idx in [0, 1]:
            offset = self.storage.write_blob(index_blob_data, self.entries[idx]['offset'])

        self.magic = MAGIC
        self.version = 13
        self.volume_prefix = os.urandom(4)
        # convert to int
        self.volume_prefix = int.from_bytes(self.volume_prefix, byteorder='big')
        
        # save the index
        self.save_index()

    def _load_index(self):
        index_data = self.storage.pread(self.storage.HEADER_SIZE, 0)

        # Parse the header
        bare_header_size = struct.calcsize(self.HEADER_STRING)
        header_data = index_data[:bare_header_size]
        digest = blake3.blake3(header_data).digest()
        stored_digest = index_data[bare_header_size:bare_header_size+32]
        assert(digest == stored_digest)
        header = struct.unpack(self.HEADER_STRING, header_data)
        self.magic, self.version, self.volume_prefix, primary_index, secondary_index, root_index, self.tail_pos = header

        # Parse the index entries

        # first 3 entries are always there
        index_data = self.storage.pread(3 * self.INDEX_ENTRY_DTYPE.itemsize, primary_index)
        self.entries = np.frombuffer(index_data, dtype=self.INDEX_ENTRY_DTYPE).copy()

        # read all of the entries
        index_data = self.storage.pread(self.entries[0]['size'], self.entries[0]['offset'])
        self.entries = np.frombuffer(index_data, dtype=self.INDEX_ENTRY_DTYPE).copy()
        self.entries_size = len(self.entries)

        # find the growable blobs and their allocated space
        # disk_order = np.argsort(self.entries['offset'])
        # growable = np.where(self.entries['flags'] & self.FLAGS['growable'])
        # allocated = self.entries['offset'][disk_order+1] - self.entries['offset'][disk_order]
        # self.growable = dict(zip(growable, allocated))

    def add_blob_to_index(self, size: int, flags: int) -> int:
        offset = self.tail_pos
        alloc_size = size
        if flags & self.FLAGS['growable']:
            alloc_size = int(size * 1.25) + 8  # 25% extra space
        assert alloc_size >= size
        # TODO: how to leave space for growables at the end of the file?  adjust self.storage.file_size?
        self.tail_pos += alloc_size
        new_entry = np.array([(offset, size, flags)], dtype=self.INDEX_ENTRY_DTYPE)
        if self.entries_size >=  len(self.entries):
            new_size = int(self.entries_size * 4) + 1
            self.entries = np.resize(self.entries, new_size)
        
        idx = self.entries_size
        self.entries[idx] = new_entry
        self.entries_size += 1
        return idx, offset, alloc_size

    def delete_blob(self, blob_id: int):
        self.entries[blob_id] = (0, 0, 0)

    def get_blob_info(self, blob_id: int) -> Tuple[int, int, int]:
        entry = self.entries[blob_id]
        return int(entry['offset']), int(entry['size']), int(entry['flags'])

    def update_blob(self, blob_id: int, offset: int, size: int, flags: int):
        self.entries[blob_id] = (offset, size, flags)

    def save_index(self):
        index_blob_data = self.generate_index_blob()
        index_length = len(index_blob_data)
        self.resize_blob(0, index_length)
        self.resize_blob(1, index_length)
        index_blob_data = self.generate_index_blob()
        self.storage.write_blob(index_blob_data, self.entries[0]['offset'])
        self.storage.write_blob(index_blob_data, self.entries[1]['offset'])
        
        # construct the header
        header = struct.pack(self.HEADER_STRING, self.magic, self.version, self.volume_prefix, self.entries[0]['offset'], self.entries[1]['offset'], self.entries[2]['offset'], self.tail_pos)
        checksum = blake3.blake3(header).digest()
        header += checksum
        remaining = self.storage.HEADER_SIZE - len(header)

        self.storage.pwrite(header, 0)
        self.storage.pwrite(b'\0' * remaining, len(header))
    
    def get_sort_order(self):
        return np.argsort(self.entries[:self.entries_size]['offset'])
    
    def generate_index_blob(self):
        return self.entries[:self.entries_size].tobytes()

    def resize_blob(self, blob_id: int, new_size: int) -> bool:
        """Resize a blob"""
        offset, old_size, flags = self.get_blob_info(blob_id)
        
        if new_size <= old_size:
            # Shrinking is always possible
            self.update_blob(blob_id, offset, new_size, flags)
            return True
        
        # Check if we can grow in-place

        # optimisation for growable blobs
        if flags & self.FLAGS['growable']:
            if blob_id in self.growable:
                allocated = self.growable[blob_id]
                if new_size <= allocated:
                    self.update_blob(blob_id, offset, new_size, flags)
                    return True

        # Find the next blob
        # we might just move the blob to the end and let the compactor deal with it?
        candidates = self.entries[:self.entries_size]['offset'][self.entries[:self.entries_size]['offset'] > offset]
        next_offset = np.min(candidates) if len(candidates) > 0 else self.tail_pos
        
        if offset + new_size <= next_offset:
            # We can grow in-place
            data = self.storage.read_blob(offset, old_size)
            new_data = data + b'\0' * (new_size - old_size)
            self.storage.write_blob(new_data, offset)
            self.update_blob(blob_id, offset, new_size, flags)
            return True
        
        # We need to move the blob
        new_offset = self.tail_pos
        self.tail_pos += new_size
        
        data = self.storage.read_blob(offset, old_size)
        # pad with zeros
        data = data + b'\0' * (new_size - old_size)
        self.storage.write_blob(data, new_offset)
        self.update_blob(blob_id, new_offset, new_size, flags)
        return True

    def validate_index(self):
        disk_order = np.argsort(self.entries[:self.entries_size], order=['offset', 'size'])
        overlaps = self.entries[disk_order][1:]['offset'] < self.entries[disk_order][:-1]['offset'] + self.entries[disk_order][:-1]['size']
        print(f"Overlaps: {np.sum(overlaps)}")
        if np.sum(overlaps) > 0:
            print(f"Overlapping blobs: {np.where(overlaps)}")
            for i in np.where(overlaps)[0]:
                print(i, disk_order[i], self.entries[disk_order][i-1:])
        assert np.sum(overlaps) == 0

        gap_sizes = self.entries[disk_order][1:]['offset'] - (self.entries[disk_order][:-1]['offset'] + self.entries[disk_order][:-1]['size'])
        print(f"Max gap size: {np.max(gap_sizes)}")
        print(f"wasted space: {self.tail_pos - np.sum(self.entries[:self.entries_size]['size'])}")
        return overlaps, gap_sizes

    def validate_all_magic(self):
        # find all known magic blobs from index
        known_magic = []
        for idx,(offset, size, flags) in enumerate(self.entries[:self.entries_size]):
            if flags & self.FLAGS['magiced']:
                data = self.storage.read_blob(offset, size)
                assert data[:32] == MAGIC, f"Blob[{idx}] at {offset} does not have magic"
                print(f"Blob[{idx}] at {offset} has magic")
                known_magic.append(offset)
        print(f"Known magic blobs: {known_magic}")
        
        for i in self.storage.scan_for_pattern(MAGIC):
            if i == 0:
                # skip the header
                continue
            print(f"Magic at {i}")
            length_bytes = self.storage.read_blob(i+32, 4)
            if len(length_bytes) < 4:
                print(f"Invalid length at {i}")
                continue
            length_including_checksum = struct.unpack("I", length_bytes)[0]
            if length_including_checksum + i > self.storage.file_size:
                print(f"Invalid length {length_including_checksum} at {i}")
                continue
            length = length_including_checksum - 32
            # confirm MAGIC
            assert self.storage.read_blob(i, 32) == MAGIC, f"Invalid magic at {i}"
            data = self.storage.read_blob(i+36, length)
            computed = blake3.blake3(data[:]).digest()
            stored = self.storage.read_blob(i+36+length, 32)
            if stored != computed:
                print(f"Invalid checksum at {i}, skipping")
                continue
            # find corresponding blob
            for idx, (offset, size, flags) in enumerate(self.entries[:self.entries_size]):
                if offset == i:
                    print(f"Blob {idx} at {offset}")
                    # remove from known_magic
                    if offset in known_magic:
                        known_magic.remove(offset)
                    else:
                        print(f"Unknown magic blob at {offset}")
                    break
        if known_magic:
            print(f"Missing magic blobs: {known_magic}")
    
    def validate_all(self):
        self.validate_index()
        self.validate_all_magic()
    
    def yield_all_metadata(self):
        for idx in range(self.entries_size):
            offset, size, flags = self.get_blob_info(idx)
            if flags & self.FLAGS['metadata']:
                data = self.storage.read_blob(offset, size)
                yield idx, cbor2.loads(data)
    
    def scan_metadata_for_references(self):
        """Scan all metadata blobs for references"""
        references = []
        weak_references = []
        for idx, metadata in self.yield_all_metadata():
            refs, weak_refs = extract_tagged_references(metadata)
            references.extend((idx, refs))
            weak_references.extend((idx, weak_refs))
        return references, weak_references

    def add_blob(self, data: bytes, flags: int = 0) -> int:
        """Add a blob to the storage"""
        if flags & self.FLAGS['compressed']:
            data = zstandard.compress(data)
        if flags & self.FLAGS['blake3'] | flags & self.FLAGS['magiced']:
            data += blake3.blake3(data).digest()
        if flags & self.FLAGS['magiced']:
            data_length_as_4bytes = struct.pack("I", len(data))
            data = MAGIC + data_length_as_4bytes + data
        idx, offset, alloc_size = self.add_blob_to_index(len(data), flags)
        self.storage.write_blob(data, offset)
        return idx
    
    def read_blob(self, blob_id: int) -> bytes:
        """Read a blob from the storage"""
        offset, size, _ = self.get_blob_info(blob_id)
        flags = self.entries[blob_id]['flags']
        data = self.storage.read_blob(offset, size)
        data_start = 0
        # check for flags
        if flags & self.FLAGS['deleted']:
            raise ValueError("Blob has been deleted")
        if flags & self.FLAGS['magiced']:
            assert data[:32] == MAGIC
            # length = struct.unpack("I", data[32:36])[0]
            data = data[36:]
        if flags & self.FLAGS['blake3'] | flags & self.FLAGS['magiced']:
            data_end = -32
            data, hash = data[data_start:data_end], data[data_end:]
            computed = blake3.blake3(data).digest()
            assert hash == computed
        if flags & self.FLAGS['compressed']:
            data = zstandard.decompress(data)
        return data

    def write_blob(self, blob_id: int, data: bytes):
        """Write data to a blob, resizing if necessary"""
        offset, size, _ = self.get_blob_info(blob_id)
        self.resize_blob(blob_id, len(data))
        flags = self.entries[blob_id]['flags']
        if flags & self.FLAGS['compressed']:
            data = zstandard.compress(data)
        if flags & self.FLAGS['blake3'] | flags & self.FLAGS['magiced']:
            data += blake3.blake3(data).digest()
        if flags & self.FLAGS['magiced']:
            data_length_as_4bytes = struct.pack("I", len(data))
            data = MAGIC + data_length_as_4bytes + data
        self.storage.write_blob(data, offset)
        self.resize_blob(blob_id, len(data))

    def delete_blob(self, blob_id: int):
        """Delete a blob"""
        self.entries[blob_id]['flags'] |= self.FLAGS['deleted']
        # should we overwrite the data with zeros or something?
        # zero size
        self.entries[blob_id]['size'] = 0

    def get_blob_count(self):
        """Return the number of blobs in the volume"""
        return self.entries_size
    
    def close(self):
        self.save_index()
        self.storage.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()



class TestBlobVolume(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.volume_file = os.path.join(self.temp_dir, "test_volume.dat")

    def tearDown(self):
        os.remove(self.volume_file)
        os.rmdir(self.temp_dir)

    def test_create_new_volume(self):
        with BlobVolume(self.volume_file, create=True) as volume:
            self.assertTrue(os.path.exists(self.volume_file))
            volume.validate_all()

    def test_add_and_read_blob(self):
        with BlobVolume(self.volume_file, create=True) as volume:
            data = b"Hello, World!"
            blob_id = volume.add_blob(data)
            self.assertEqual(volume.read_blob(blob_id), data)
            volume.validate_all()

    def test_multiple_blobs(self):
        with BlobVolume(self.volume_file, create=True) as volume:
            data1 = b"First blob"
            data2 = b"Second blob"
            blob_id1 = volume.add_blob(data1)
            blob_id2 = volume.add_blob(data2)
            print(f"data size {len(data1)}, stored size {volume.get_blob_info(blob_id1)[1]}")
            self.assertEqual(volume.read_blob(blob_id1), data1)
            self.assertEqual(volume.read_blob(blob_id2), data2)
            volume.validate_all()

    def test_multiple_compressed_blobs(self):
        with BlobVolume(self.volume_file, create=True) as volume:
            data1 = b"First blob"
            data2 = b"Second blob"
            blob_id1 = volume.add_blob(data1, flags=volume.FLAGS['compressed'])
            blob_id2 = volume.add_blob(data2, flags=volume.FLAGS['compressed'])
            # print stored size vs data size
            print(f"data size {len(data1)}, stored size {volume.get_blob_info(blob_id1)[1]}")
            self.assertEqual(volume.read_blob(blob_id1), data1)
            self.assertEqual(volume.read_blob(blob_id2), data2)
            volume.validate_all()

    def test_reopen_volume(self):
        data = b"Test data"
        blob_id = None
        with BlobVolume(self.volume_file, create=True) as volume:
            blob_id = volume.add_blob(data)
            blob_id = volume.add_blob(data)
            blob_id = volume.add_blob(data)
            blob_id = volume.add_blob(data)
            blob_id = volume.add_blob(data)
            blob_id = volume.add_blob(data)
            blob_id = volume.add_blob(data)
            blob_id = volume.add_blob(data)
            volume.validate_all()

        with BlobVolume(self.volume_file) as volume:
            self.assertEqual(volume.read_blob(blob_id), data)
            volume.validate_all()
    
    def test_source_code_blob(self):
        """Position 0x03 in the index is the source code for the blobindex, confirm it can be read"""
        with BlobVolume(self.volume_file, create=True) as volume:
            source_code = open(sys.argv[0], "rb").read()
            pre_loaded = volume.read_blob(3)
            print(f"Source code size: {len(source_code)}, stored decompressed size: {len(pre_loaded)}, stored compressed size: {volume.get_blob_info(3)[1]}")
            self.assertEqual(source_code, pre_loaded)

    def test_large_blob(self):
        with BlobVolume(self.volume_file, create=True) as volume:
            data = b"Large blob" * 100000  # 10MB blob
            blob_id = volume.add_blob(data)
            self.assertEqual(volume.read_blob(blob_id), data)
            volume.validate_all()
    
    def test_magic_blob(self):
        with BlobVolume(self.volume_file, create=True) as volume:
            data = b"Magic blob"
            blob_id = volume.add_blob(data, flags=volume.FLAGS['magiced'])
            self.assertEqual(volume.read_blob(blob_id), data)
            data = MAGIC+b"Not-Magic blob but has magic at the start"
            blob_id = volume.add_blob(data)
            self.assertEqual(volume.read_blob(blob_id), data)
            data = b"Not-Magic blob with magic at the end"+MAGIC
            blob_id = volume.add_blob(data)
            self.assertEqual(volume.read_blob(blob_id), data)
            volume.validate_all()
    
    def test_checksum(self):
        with BlobVolume(self.volume_file, create=True) as volume:
            data = b"Checksummed blob"
            blob_id = volume.add_blob(data, flags=volume.FLAGS['blake3'])
            self.assertEqual(volume.read_blob(blob_id), data)
            volume.validate_all()

    def test_many_small_blobs(self):
        with BlobVolume(self.volume_file, create=True) as volume:
            blob_ids = []
            for i in range(100000):
                data = f"Blob {i}".encode()
                blob_ids.append(volume.add_blob(data))

            for i, blob_id in enumerate(blob_ids):
                self.assertEqual(volume.read_blob(blob_id), f"Blob {i}".encode())
            
            volume.validate_all()
            
            # delete all the blobs to free up space
            for blob_id in blob_ids:
                volume.delete_blob(blob_id)
            
            volume.validate_all()

    def test_resize_blob_grow(self):
        with BlobVolume(self.volume_file, create=True) as volume:
            data = b"Original data"
            blob_id = volume.add_blob(data)
            volume.resize_blob(blob_id, 20)
            resized_data = volume.read_blob(blob_id)
            self.assertEqual(resized_data[:len(data)], data)
            self.assertEqual(len(resized_data), 20)
            volume.validate_all()

    def test_resize_blob_shrink(self):
        with BlobVolume(self.volume_file, create=True) as volume:
            data = b"Original data"
            blob_id = volume.add_blob(data)
            volume.resize_blob(blob_id, 7)
            resized_data = volume.read_blob(blob_id)
            self.assertEqual(resized_data, b"Origina")
            self.assertEqual(len(resized_data), 7)
            volume.validate_all()

    def test_resize_blob_move(self):
        with BlobVolume(self.volume_file, create=True) as volume:
            data1 = b"First blob"
            data2 = b"Second blob"
            blob_id1 = volume.add_blob(data1)
            blob_id2 = volume.add_blob(data2)
            
            # Resize the first blob to force moving the second
            volume.resize_blob(blob_id1, 30)
            
            self.assertEqual(volume.read_blob(blob_id1)[:len(data1)], data1)
            self.assertEqual(volume.read_blob(blob_id2), data2)
            volume.validate_all()
    
    def test_growable_resize_blob_grow(self):
        with BlobVolume(self.volume_file, create=True) as volume:
            data = b"Original data"
            blob_id = volume.add_blob(data, flags=volume.FLAGS['growable'])
            volume.resize_blob(blob_id, 20)
            resized_data = volume.read_blob(blob_id)
            self.assertEqual(resized_data[:len(data)], data)
            self.assertEqual(len(resized_data), 20)
            volume.validate_all()

    def test_growable_resize_blob_move(self):
        with BlobVolume(self.volume_file, create=True) as volume:
            data1 = b"First blob"
            data2 = b"Second blob"
            blob_id1 = volume.add_blob(data1, flags=volume.FLAGS['growable'])
            blob_id2 = volume.add_blob(data2)
            
            # Resize the first blob but should be in-place
            volume.resize_blob(blob_id1, len(data1) + 2)
            
            # Resize the first blob to force moving the second
            volume.resize_blob(blob_id1, 30)
            
            self.assertEqual(volume.read_blob(blob_id1)[:len(data1)], data1)
            self.assertEqual(volume.read_blob(blob_id2), data2)
            volume.validate_all()
    

if __name__ == '__main__':
    unittest.main()