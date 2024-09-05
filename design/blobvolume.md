# BlobVolume

A class for managing a blob storage system with an indexed file structure.

## Key Features

- Efficient storage and retrieval of binary data (blobs)
- Support for compressed, growable, and metadata blobs
- Built-in data integrity checks using Blake3 hashing
- Ability to resize and delete blobs
- Index-based blob management with special offsets for system data

## Main Methods

- `__init__(filename: str, create: bool = False)`: Initialize or load a blob volume
- `add_blob(data: bytes, flags: int = 0) -> int`: Add a new blob and return its ID
- `read_blob(blob_id: int) -> bytes`: Retrieve a blob's data by its ID
- `write_blob(blob_id: int, data: bytes)`: Overwrite a blob's data
- `delete_blob(blob_id: int)`: Mark a blob as deleted
- `resize_blob(blob_id: int, new_size: int) -> bool`: Change the size of a blob
- `save_index()`: Write the current index to storage
- `close()`: Save changes and close the volume

## Blob Flags

- `metadata`: 0x01 - Blob contains metadata (subject to garbage collection)
- `growable`: 0x02 - Blob can be resized
- `compressed`: 0x04 - Blob is compressed using zlib
- `blake3`: 0x08 - Blob includes a Blake3 hash for integrity checking
- `magiced`: 0x10 - Blob starts with a magic number for identification
- `deleted`: 0x80 - Blob has been marked as deleted

## Special Offsets

- 0: Primary index
- 1: Secondary index
- 2: Root index
- 3: Source code for the blobindex

## Usage

```python
with BlobVolume("my_volume.dat", create=True) as volume:
    blob_id = volume.add_blob(b"Hello, World!")
    data = volume.read_blob(blob_id)
    volume.resize_blob(blob_id, 20)
    volume.delete_blob(blob_id)