# Kelp - A distributed storage system
## Block storage system

* everything is done on 16 byte boundaries.  This is convenient for the crc32c checksum, for the transpose operation (using sse), and AES encryption.  the fastest gf(2^8) implementation is done on 64 byte boundaries.

## aio (Asynchronous I/O) 
* AIO is a way to perform I/O operations without blocking the calling thread.
* reads and writes must be aligned to the block size of the device.
* we always read or write a whole 4k block.

## block format

(4096 - 16) * 8 = 32752 bytes of data per stripe, 32kB * 2^56 = 2^71 bytes of data = 2ZB

* 4k block size
* 16 byte header
** 8 byte stripe number, top 7bytes are the stripe number, the bottom byte is the block number
// ** 1 byte block number: there can be at most 256 blocks in a stripe due to using gf(2^8)
(these are effectively 64 bit id, 56 bits for the stripe number and 8 bits for the block number)
** 4 byte block sequence number
** 4 byte block checksum crc32c

## file format

    HEADER:  4KB header on all volumes:
    Magic number 32 bytes (blake3 hash of "blobindex")
    Version number 4 bytes
    Volume prefix id (random number greater than 2^24, to make it unlikely to collide with a valid offset, and garbage collectable)
    Primary index offset 8 bytes
    Secondary index offset 8 bytes
    Root index offset 8 bytes
    Tail offset 8 bytes
    Somethign which tells us which block numbers are stored in this volume, probably 256b = 32 bytes?
    blake3 hash of the header 32 bytes
    
    struct string for header = "32sIIQQQ32sQ"



we can store any number of block numbers in a volume, the file is stored in sorted order, and because the block numbers always the same, we can compute the offset of the block number in the file

## read

we need to read k blocks from the disk to reconstruct the original data.  We can read the blocks in parallel. Due to the interleaving of the blocks, we only need read the range of bytes

if [start, end] is the range of bytes we need to read, then s16, e16 = start & 0xf, (end + 0xf) & 0xf are the start and end positions we need to read from the disk.
because of interleaving, we need to read s16k, e16k = [floor(s16 / k), ceil(e16 / k)] range from each of the k blocks.

* read a block from the disk
* check if the block is valid by checking the crc32c checksum
* copy out [s16k, e16k] range from the block
* repeat for all k blocks
* reconstruct the original data from the k blocks


## write

modes: 
1) 'trim block' mode where there is no existing data in the block.  In this case, we can write the block in one go without saving the existing data.
2) 1-phase write, in this case, we need to read the existing block from the disk, modify the block and write it back to the disk.  We aren't concerned about errors during the write operation.
3) 2-phase write, in this case, we need to read the existing block from the disk, copy it to a backup on the disk, modify the block and write it back to the disk.  

These writes have to occur across the k disks in parallel, and we have to ensure that either a quorum of the writes succeed or we can recover from the failure.

If we had > 2k disks then we can have both the old state and the new state safely on the disk.

* Prepare
** read the current block from the disk
** check if the block is valid by checking the crc32c checksum
** copy the block to a temporary buffer, or to a backup block on the disk
** modify the block
** compute the crc32c checksum
* Commit (once all the blocks are prepared)
** overwrite the block on the disk
* Cleanup (once the commit is complete, potentially across multiple stripes)
** when the write is complete, we can delete the backup block

How do we track the trimed blocks?  We can have a bitmap of the blocks that are trimed.  This can be rebuilt from the data on the disk?  we can store it in a blob in the file system.

## managing volumes

## shard codes

We are going to fix the cost to be 8 systematic codes and 248 parity codes (cauchy matrix).  A single volume will only ever store 8 shards, because within block errors are much rarer than disk failures so parity on a same drive is low value compared to the cost.

To store this configuration we have 8 bytes, representing the 8 shards that are stored on the volume.  0-7 are systematic codes.
To store fewer than 8 shards, we repeat the last shard. 0505050505050505 means a volume with only shard 5. 7e23010101010101 means a volume with 3 shards, 0x7e, 0x23, 0x01.

To generate any new shards we need 8 distinct shards.

A set of volumes can be configured as a list of uint64s, where each uint64 represents the shards stored on the volume.

### moving data between volumes

These operations are done on the volume level, and not on the block level.  We can have the following

operations:
1. split a volume into multiple volumes by copying different shards to different volumes
2. merge multiple volumes into a single volume by copying the shards from the different volumes into a single volume
3. generate a new set of shards from the existing shards by reading the data from the existing shards, reed-solemon encoding the data, and writing the new shards to a new volume


As this operation will take a long time, we can do this in the background, and we can have a background process which does this.  This means we either need:
1. lock the volume while the operation is in progress
2. have a version number for the volume, and if the version number changes, then the operation is aborted.
3. A new kind of range lock that allows reading and writing, but notifying the lock holder if the range is modified.
4. ability to split a volume into a head and tail and access both volumes at the same time.

### random order volume

most of the blocks should be stored in their computed location, but it would be useful to also allow for a volume that is in appended order for handling the two phase commit process.