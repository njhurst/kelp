# Kelp distributed file system  (a robust seaweedfs alternative)

Kelp takes some nice ideas from seaweedfs, ceph, and ipfs, and combines them into a new distributed file system.  It is designed to be easy to use, easy to deploy, and easy to maintain.  It is designed to be fault-tolerant, and moderately secure.  It is designed to be efficient.

It was motivated by a seaweedfs deployment that was difficult to understand, and led to a large data loss.  As a result kelp is built from the ground up to have highly reliable writes, erasure coding for data durability, and a simple, easy to understand design.  Nothing that is not strictly necessary is included in the design.  It is designed to be easy to understand and easy to maintain.

There are three levels to the design: Blades, Thalus, and Kelp.  Blades are the lowest level, and are responsible for storing data.  Thalus are the middle level, and are responsible for storing metadata.  Kelp is the highest level, and is responsible for managing the filesystem.

The total address space is 64b indexing into 8byte units, this is a total of 128EB, we use 8byte alignment because the cost of padding was small and the increase in Blade file size was worth it.

Creating a naming system for a data storage system inspired by the concept of kelp offers a unique opportunity to use terms related to its biology and ecology, arranged from the smallest components to the largest.

    Cell - Representing the smallest unit of data, akin to a byte or a bit. this would be 4kB blocks written to disk across multiple disks.

    Stipe - a collection of cells that are written to disk in a single operation from the same RS encoding group.
    Blade - A contiguous collection of stipes in a single address block.  Addresses are 64b, and the blade is from the top 32b.
    
    Frond - a fixed length block of data (a blob).  These are referenced by their address in the system, with the address being a 64b number pointing at 8byte offsets.  The frond is the basic unit of storage in the thalus.
    Thalus - A collection of blades, similar to a file in a storage system.  This is the appended tar-like layer made on top of the blades.
    Holdfast - Representing the base or root directory that anchors and organizes all higher levels, like the root in a filesystem.
    Canopy - This could represent the top level or the user interface that oversees and accesses the entire data forest.
    Forest - Representing the entire storage ecosystem, encompassing all stipes and their subordinate structures.

This naming system mirrors the biological structure of kelp, from the microscopic level (cells) to the ecological scale (forest), and could provide an intuitive way to visualize and manage data storage systems.

## Define Done

What are the core features that constitute a complete project?

1. A distributed file system that can store and retrieve data across multiple servers, and reliably backup between data centers.
2. A robust write system that can handle failures and recover from them.
3. A simple, easy to understand design that is easy to maintain.

## Blades

Blades spread data over a pool of drives, the assumption being that the only real source of failure is the drive.

The blades have 4kB Cells which are written atomically, and include both their data and metadata.  The metadata includes a checksum, an id, and a sequence count.   The checksum is used to determine when a cell has been corrupted, and is used to determine when a cell can be garbage collected.  The id is globally unique, and is used to determine when a cell has been moved.  The sequence count is used to determine when a cell has been overwritten.

The Cells are grouped into stipes, a stipe is a group of cells that are written together.  The stipe is the unit of erasure coding, and is used to determine when a cell has been lost.  The stipe is the unit of replication, and is used to determine when a cell has been corrupted.  We chose RS(8, 8+), which is a Reed-Solomon code with 8 data cells and 8 parity cells.  This means that we can lose up to 8 cells, and still recover the data.  This has the same storage cost as a 2x replication, but is more efficient.  Reads are done with hedging where we read more than the minimum number of cells, and use the first 8 cells that agree to reconstruct the data.  

A blade is either 'data', or 'metadata' and only the metadata blades are used for garbage collection.  Any reference to a valid Frond makes that frond live.  References are only considered from blades marked as metadata.

### Holdfast

The disk that stores shard id 0 is called the holdfast for that blade, and is responsible for coordinating locking.

Writes rely on coordination between the blades, and are done with a two-phase commit.  The first phase is a prepare phase, where the blade writes the data to the cells, and the second phase is a commit phase, where the blade writes the metadata to the cells.  The prepare phase is done with a write quorum, and the commit phase is done with a read quorum.  The write quorum is a majority of the blades, and the read quorum is a majority of the blades.  The write quorum is used to ensure that the data is written to a majority of the blades, and the read quorum is used to ensure that the data is read from a majority of the blades.

The holdfast phase and just write the data and metadata in one go.  The holdfast is responsible for coordinating the writes, and is responsible for ensuring that the data is written to a majority of the blades.  

One challenge for a distributed RS fs is that updates are not atomic, and so we need to be able to handle partial writes.  Rather than being clever, we simply require the holdfast to handle updates, and to ensure that the updates are atomic.  This is a simple, easy to understand, and easy to implement solution.  If a Stipe is 32kB and a typical file chunk is 2MB then about 64 cells new cells are written for each file chunk, and the coordinator can handle the updates for the single stipe at the beginning.

Because the holdfast is handling actual data and computing RS codes, it may become the bottleneck.  We solve this by having multiple holdfasts, and by having the holdfasts be distributed.  This means that we can spread the load out over multiple holdfasts, and that we can ensure that the holdfasts are fault-tolerant.  We can also use the holdfasts to ensure that the data is written to a majority of the blades, and that the data is read from a majority of the blades.  Because there are multiple blades, and because the blades are distributed, we can spread writes out over multiple blades; indeed we assume that every disk is the holdfast for at least one blade and thus writes can spread out over all disks and servers.

A single server will be responsible for all the disks on that machine.  Both Lizardfs and seaweedfs conflate the server and the disk which leads to confusion as well as more complex configuration.  Instead we let the server work out where it can store the data and also the individual disk's performance and capacity.


Although I do not have a direct use for updating existing data, I felt it was worth the effort to provide that functionality.  This means that any client can update any data through the coordinator, and we rely on LWW CRDTs to ensure that the updates are atomic.  There is a little dance where old data is first moved to a scratch file, and then the new data is written to the stipe, and then the old data is garbage collected.  This dance is necessary to ensure that the updates are atomic, and to ensure that the updates are durable.  Because we have a single source for the sequence number, we know that we can always copy the highest sequence number to the correct location and that the data is correct.


## Thalus

Thalus are designed to store metadata, and are responsible for storing the filesystem. 

A thalus is a variable length data structure that stores data (fronds) in a kelp filesystem.   A thalus is a sequence of fronds, each with a length, data, and a checksum.  The length in bytes is a u32, the data is a [u8; length + padding], and the checksum is a Blake3 hash of the length and data.  The checksum is Blake3 and 32 bytes long.  The address is 32b of Blade id and 32b offset within the blade, pointing at 8byte aligned data.

Fancy file systems with directories and permissions and users and groups can be built on top of this, and we can use the thalus to store the metadata for backup.  The most recent metadata is stored in some database.

(length u32, data : [u8; length + padding to 8byte boundary], checksum : Blake3 (32bytes))

64bits can index how many bytes? 2^64 = 18,446,744,073,709,551,616 bytes

besides length, data, and checksum, what other metadata do we need to store?
- creation time
- last modified time
- last accessed time
- owner
- group
- permissions
- xattrs
- extended metadata
- file type
- file name
- file path
- file id
- parent directory id
- parent directory path
- md5 hash

None of these are strictly necessary, but they are useful for a filesystem. Does it make sense to store them in the 
thalus itself? Or should they be stored in a separate metadata store?  seems like a separate metadata store would be
more flexible and efficient.  We might store a backup of the metadata in the thalus itself, but we would not use it for
normal operations.


say I have a pool of servers that can store data.  They can be different amounts of busy, and on a write request can either return an accept, a busy, or a wait-in-line with an expected time of accept.  I don't care which particular server stores the data, only that the data is stored; so I cunningly ask multiple servers at the same time, and only write to one.  Can you propose a protocol that uses this to efficiently ensure maximum throughput?


# Blade

## Cells:
Cell is 4kB with an index (64b), a sequence number (24b), shardid(8b), crc64 (64b), and data (4kB - 32b - 24b - 8b - 24b - 32b = 4kB - 128b = 4kB - 16B = 4080B).  The cell is self-verifying and self-naming.  The cell is also self-repairing, in that if the cell is damaged, it can be repaired by reading the cell and recomputing the crc32 of the cell.  If the crc32 does not match the crc32 of the data, then the cell is damaged, and can be repaired by using the reed-solomon erasure coding to compute the missing cell.  This is done by using any other 8 cells in the RS stipe to compute the missing cell.

The blocks are RS(8, 8+) erasure-coded, so that the loss of any 8 disks does not result in the loss of any data; but the write is considered successful when 8+k disks have been written to disk.  Larger k is more reliable, but slower, for now let's use k=4.  There's no reason not to create more than 8 parity blocks, we can store these on slower or less reliable disks to save money.  We could store extra parity shards on tape, for example.

32b volume id, 24b stipe id, 8b shardid, 8bit version, 24b sequence number, 32b crc32, 4kB - 16B data

An address is a 64b number pointing at 8byte offsets, rather than bytes.

address spaces are the top 32b of the 64b address, they could be either allocated randomly, or from an allocation server.  It seems like it should be easy to ask for the next address space rather than having them spread out randomly.  Should we allow remapping of address spaces?  This would be very cheap, but would also require a consistent view, perhaps raft or similar would be a good option.

## Servers:

A server:
    can have multiple disks
    can see all the other servers
    has a scratch space for writing orphan blocks
    uploads health information to the holdfast
    uses smartctl to monitor disk health
    tracks lognormal distribution of disk latency

### Holdfast:

Every server has a mixture of shardids, aiming to spread the load evenly across all servers.  The server with shard 0 is the holdfast for that address space, if address 0 is missing, it can be recreated on a different server by reconsrtucting blocks in the stipe and computing the missing block.  The holdfast server is responsible for reading the stipe and computing the missing block.  The holdfast server is responsible for writing the missing block to the disk.  The holdfast server is responsible for updating the sequence number.  The holdfast server is responsible for updating the index.  The holdfast server is responsible for updating the crc64.

The holdfast knows where the other shards are, and can:
    tell other servers where the other shards are.
    read them to compute missing blocks.  
    write the missing block to the disk.  
    return the next sequence number for a stipe.

 Any server can:
    read a block from the disk.
    write a block to the disk.
    request blocks for a stipe from other servers.
    verify the crc64 of a block.
    request the current sequence number or next for a stipe by asking the holdfast.

when a two-phase commit is occuring for a write all reads need to either block, or get the old values.  This means that the servers need to track in progress writes, and probably a write lock needs to be over a range of stipes (covering the edit)

overhead = 16B/4kB = 0.4%

Is the non-power of two block size a problem?  The block size is 4080bytes, and the stipe is 8 blocks, so the stipe size is 8*4080 = 32640B of payload.  The RS(8, 8) code adds 8 blocks, so the stipe size is 16*4080 = 65280B needs to be written to the disk.

The writer asks the holdfast to allocate a given number of bytes.  The holdfast returns the next offset and locks the range.  Any stipe that already exists must be written through the holdfast, empty stipes (0 sequence number) can be written directly from the writer.  Writes to existing stipes must occur through the holdfast because otherwise there could be a race from concurrent writes.  Alternatively we need a locking mechanism to ensure that the stipe is written atomically.

We can write just a quorum of the stipe, and then write the rest of the stipe later either by recovering the missing blocks from the RS(8, 8) code, or by writing the missing blocks to other disks.  This allows us to write the stipe incrementally, and to recover from disk failures.

updating a stipe is a two-step process.  First, the stipe is read, and the missing blocks are recovered.  Then the stipe is updated, and the missing blocks are written to the disk.  The sequence number is incremented.  A two-phase commit protocol is used to ensure that the stipe is updated atomically.

1. read the stipe
2. recover the missing blocks
3. update the stipe
4. prepare the stipe on each disk
    a) copy the old block into scratch space
    b) ack the prepare
5. commit the stipe on each disk
    a) write the new block to the disk
    b) ack the commit
    c) delete the old block from the scratch space

Scratch space is a reserved area on the disk that is used to store the old block while the new block is being written.  This allows the old block to be recovered if the write fails.  It can also accept writes from other disks, so that the stipe can be updated in parallel.  The scratch space is append, collecting blocks.  Periodically and new scratch space is allocated, and the old scratch space is sequentially handled.

1. blocks that belong to local volumes are compared to existing blocks on the disk, and if they are the same or the sequence number is less than the existing block, then the block is not written to the disk. If the block is different and the sequence number is greater than the existing block, then the block is written to the disk.
2. blocks that belong to remote volumes sent to the server owning that disk, and the server updates the block to the disk.  Once the server ACKs the block, the block is assumed to be written to the disk.

 For an incomplete stipe we have a number of options to maintain redundancy:

1. leave the parity blocks empty
2. fill the parity blocks with zeros
3. replicate the data blocks into the parity blocks
how can we tell if a stipe is complete?  we can use a surety block, which is the md5sum of the stipe.  If the surety block is correct, then the stipe is complete.  If the surety block is incorrect, then the stipe is incomplete.
but we won't know if it is a replication or a parity block.
4. use a smaller RS code, such as RS(8, 12) or RS(8, 8)
This is the most robust solution, but it is the most complex. it would require rewrite the entire stipe when a block is added or removed.
are there any other options?  we could use a smaller stipe size, such as 8 blocks, but this would reduce the robustness of the system.
is there an incremental Reed-Solomon erasure coding algorithm?  I don't think so.  I think the entire stipe has to be read and rewritten when a block is added or removed.



The holdfast needs to know the most recent sequence number for each stipe, so that it can update the sequence number.  This means storing 3bytes for at most every 8*4kB = 32kB of data, or 3B/32kB = 0.009% overhead.  This is a small overhead, and it is necessary to ensure that the system is robust.  Could we run out of sequence numbers?  No, because the sequence number is 24b, so there are 2^24 = 16M sequence numbers, which is enough for 16M*32kB = 512TB of data written to a single stipe.  We only need to store the non-zero sequence numbers, so we can use a sparse representation to store the sequence numbers.

If we got more than halfway through the sequence numbers, we could wrap around and start over.  This would require a two-phase commit protocol to ensure that the sequence number is updated atomically.

Almost all the sequence numbers are 0 (somewhere around 31/32 if we have 2MB typical data size), so we could use a sparse representation to store the sequence numbers.  hash table would work.

def compute_padding(length):
    """compute the smallest multiple of 8 larger than length."""
    return (length + 7) & ~7

Locking algorithm:
    1. lock the stipe range
    For each stipe that is being updated rather than created:
        a) read the stipe
        b) recover the missing blocks
        c) update the stipe
        d) prepare the stipe on each disk
        e) commit the stipe on each disk
    For each stipe that is being created rather than updated:
        a) create the stipe
        b) write the stipe on each disk
        - We can skip the prepare step because the stipe is new and there are no old blocks to recover; the range is locked so no other server can write to the stipe.
    7. unlock the stipe range

Preventing locks from blocking other writers:

When a process wants to write a new frond it has to pick a volume - it can ask multiple holdfasts if they are able to allocate a length.  Each server responds with an estimate of the time it will take to write the frond.  The process then picks the server with the shortest time.  

General idea for multiserver latency - track the lognormal wait times for each server, and use the server with the shortest wait time.  This is a simple way to balance the load across servers.  If a write takes more than mean + a*stddev, then the server is considered to be slow, and the process should pick a different server.  The process should also track the number of slow servers, and if the number of slow servers is greater than some threshold, then the process should pick a different server.  This is a simple way to balance the load across servers.  Dropping requests that take too long.


## File layout:

/data[0-9]+/kelp/blades/
/data[0-9]+/kelp/blades/README.md # human readable description of the data and how to access it, includes link to github and version number
/data[0-9]+/kelp/blades/00/ # 256 directories

/home/kelp/
/home/kelp/kelp/ # github repo
/home/kelp/config/ # configuration files
/home/kelp/state.db # sqlite3 database of the state of the system
/home/kelp/wal/ # write ahead log for unwritten blocks
/home/kelp/wal/wal-<date>.log # write ahead log for unwritten blocks for a given date
/home/kelp/locks/ # locks for stipes


We use an adaptive radix tree, the maximum number of files in a dir is configured; when adding a file to a dir exceeds this limits we split the files by the next byte in the address.  For example:

/data0/kelp/blades
/data0/kelp/blades/00
/data0/kelp/blades/01
...
/data0/kelp/blades/ff

/data0/kelp/blades/00/00

Users are allowed to move blade files around, the system will scan at startup.


## Disk monitoring:

We use smartctl to monitor disk health.  We track the lognormal distribution of disk latency.  We track the number of slow disks, and if the number of slow disks is greater than some threshold, then the disk is considered to be slow, and the disk is marked as slow.  The disk is then removed from the list of disks that are used for writing.  The disk is then replaced with a new disk.  The disk is then formatted and kelp will recopy and reconstruct data onto the disk.



## compact index

One problem we would like to solve is to provide a mapping for each address space to a sequential id.  This is useful for indexing into a thalus, and for indexing into a blade.  

it turns into storing a monotonic sequence of offsets.  We can use Elias-Fano encoding to store this sequence.