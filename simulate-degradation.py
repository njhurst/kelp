"""
This is a simulation of a JABOD (Just a Bunch of Disks) storage system.

The goal is to fuzz test that the storage protocol is failsafe and that the storage system is reliable.

We have a set of disks stored as arrays of 'blocks'"""

import numpy as np
import zfec


print("Blobcat v0.1")

# set output width to 100
np.set_printoptions(linewidth=200)

enc = zfec.Encoder(8, 16)
dec = zfec.Decoder(8, 16)

# lightweight int32 hash function
def hash32(x):
    return (x * 2654435761 + 0xdeadbeef) & 0xFFFFFFFF

class Disk:
    def __init__(self, size):
        self.size = size
        self.leading = 0 # the number of blocks that have been written to the disk
        self.trailing = 0 # the number of blocks that have been verified on the disk
        self.blocks = np.zeros((size, 2), dtype=np.int32) # 64-bit block numbers which are fake blocks with the property that their value is their index + 1.  This is to simulate the fact that the blocks are not actually stored in memory, but on disk.
    def verify_block(self, block):
        return block == self.blocks[block-1]
    def verify_disk(self):
        return np.all(self.blocks[:self.leading, 0] - np.arange(self.leading, dtype=np.int32) + 1 == 0)
    
class Volume:
    def __init__(self, volume_blocks):
        self.volume_blocks = volume_blocks
        self.disks = np.zeros((16, self.volume_blocks, 2), dtype=np.int32) # always 16 volume files
        self.leading = 0 # the number of blocks that have been written to the storage system
        self.trailing = 0 # the number of blocks that have been verified on the storage system
    def verify_stripe(self, stripe):
        """A stripe is a list of 16 blocks, the first 8 are data blocks, the last 8 are parity blocks.  The parity blocks are computed using reed-solomon erasure coding.  A stripe is verified if at least 8 of the 16 blocks are correct."""
        valid_blocks = 0
        for i in range(16):
            if self.disks[i].verify_block(stripe):
                valid_blocks += 1
        return valid_blocks >= 8
    def direct_index_block(self, block_index):
        return self.disks[block_index % 16, block_index // 16]
    def write_file(self, data_blocks):
        """Write a file to the storage system.  The file is written as a series of blocks.  The blocks are written to the disks in a round-robin fashion.
        
        Clever future improvements:
        1. write the blocks in parallel
        2. write the parity blocks in parallel
        3. only use the mirroring approach when the final stripe is incomplete
        
        """
        stripe_block_index = self.leading % 16
        current_disk = self.leading % 16
        block_count = 0
        n_blocks = len(data_blocks)
        while block_count < n_blocks or stripe_block_index >= 8:
            stripe = self.disks[:, self.leading // 16, :]
            b = stripe[self.leading % 16]
            if stripe_block_index < 8:
                d = data_blocks[block_count]
                b[0] = hash32(d)
                b[1] = d
                stripe[self.leading % 16 + 8] = b
                block_count += 1
                self.leading += 1
                stripe_block_index = (stripe_block_index + 1) % 16
            else:
                encoded_stripe = encode_stripe(stripe)
                self.disks[:, self.leading // 16, :] = encoded_stripe
                self.leading += 8
                self.trailing += 16
                stripe_block_index = (stripe_block_index + 8) % 16
    def extract_stripe(self, stripe_index):
        """Extract a stripe from the storage system."""
        return self.disks[:, stripe_index, :]
    def update_stripe(self, stripe_index, stripe):
        """Update a stripe in the storage system."""
        self.disks[:, stripe_index, :] = stripe
        
    def display_volume(self):
        print(f"Leading: {self.leading}, Trailing: {self.trailing}")
        for i in range((self.leading + 15) // 16):
            print(' '.join([f"{'*' if x[0].view(np.uint32) == hash32(x[1]) else ' '}{x[0]&0xffff:4x}{x[1]:5}|" for x in self.disks[:, i, :]]))

    def verify_volume(self):
        for disk in self.disks:
            for block in disk.blocks:
                if not disk.verify_block(block):
                    print(f"Disk {disk} block {block} failed verification")
    def ensure_parity(self):
        for stripe_index in range(self.trailing // 16, self.leading // 16):
            stripe = self.extract_stripe(stripe_index)
            encoded_stripe = encode_stripe(stripe)
            self.update_stripe(stripe_index, encoded_stripe)
            self.trailing += 16


class StorageSystem:
    def __init__(self, disks):
        self.disks = disks
        self.leading = 0 # the number of blocks that have been written to the storage system
        self.trailing = 0 # the number of blocks that have been verified on the storage system
    def verify_stripe(self, stripe):
        """A stripe is a list of 16 blocks, the first 8 are data blocks, the last 8 are parity blocks.  The parity blocks are computed using reed-solomon erasure coding.  A stripe is verified if at least 8 of the 16 blocks are correct."""
        valid_blocks = 0
        for i in range(16):
            if self.disks[i].verify_block(stripe):
                valid_blocks += 1
        return valid_blocks >= 8
    def direct_index_block(self, block_index):
        return self.disks[block_index % len(self.disks)].blocks[block_index // len(self.disks)]
    def write_file(self, n_blocks):
        """Write a file to the storage system.  The file is written as a series of blocks.  The blocks are written to the disks in a round-robin fashion.
        """
        stripe_block_index = self.leading % 16
        current_disk = self.leading % len(self.disks)
        block_count = 0
        while block_count < n_blocks or stripe_block_index >= 8:
            d = self.disks[current_disk]
            if stripe_block_index < 8:
                block_count += 1
                d.blocks[d.leading,0] = self.leading + 1
                d.blocks[d.leading,1] = block_count
            else:
                d.blocks[d.leading, 0] = 0#(self.leading + 1)
                d.blocks[d.leading, 1] = 0

            d.leading += 1
            self.leading += 1
            current_disk = (current_disk + 1) % len(self.disks)
            stripe_block_index = (stripe_block_index + 1) % 16
    def extract_stripe(self, stripe_index):
        """Extract a stripe from the storage system."""
        stripe = np.zeros((16, 2), dtype=np.int32)
        start = stripe_index * 16
        print(f"Extracting stripe {stripe_index} from block {start} to block {start+15}")
        start_disk = start % len(self.disks)
        for i in range(16):
            stripe[i] = self.direct_index_block(start + i)
        return stripe
    def update_stripe(self, stripe_index, stripe):
        """Update a stripe in the storage system."""
        start = stripe_index * 16
        print(f"Updating stripe {stripe_index} from block {start} to block {start+15}")
        start_disk = start % len(self.disks)
        for i in range(16):
            self.disks[(start + i) % len(self.disks)].blocks[(start + i) // len(self.disks)] = stripe[i]
        
    def display_disks(self):
        print(f"Leading: {self.leading}, Trailing: {self.trailing}")
        for i, disk in enumerate(self.disks):
            print(f"Disk {i}: {disk.size} blocks: {disk.leading} leading, {disk.trailing} trailing")
            print(disk.blocks[:disk.leading].flatten())

    def verify_disks(self):
        for disk in self.disks:
            for block in disk.blocks:
                if not disk.verify_block(block):
                    print(f"Disk {disk} block {block} failed verification")


def encode_stripe(stripe):
    """Encode a stripe using reed-solomon erasure coding."""
    data = stripe[:8]
    parity = stripe[8:]
    e = enc.encode(data[:,1].flatten())
    e = np.array([np.array([e[i]], dtype=np.int32) if i < 8 else np.frombuffer(e[i], dtype=np.int32) for i in range(16)]).flatten()
    # print(f"Encoded: {e}, e.shape: {e.shape}")
    stripe[:, 1] = e
    for i in range(16):
        stripe[i,0] = hash32(stripe[i,1])
    return stripe

v = Volume(10)

v.display_volume()

for l in [13, 5, 1, 2, 1, 1, 1]:
    v.write_file(np.arange(l, dtype=np.int32))
    v.display_volume()


# ss = StorageSystem([Disk(sz*1000) for sz in [4, 4, 4, 6, 6, 8, 12, 12, 14, 18, 18, 18]])

# ss.display_disks()

# ss.write_file(100)

# ss.display_disks()

# if ss.trailing + 8 <= ss.leading:
#     stripe = ss.extract_stripe(ss.trailing // 16)
#     stripe = encode_stripe(stripe)
#     print(stripe)
#     # ss.update_stripe(ss.trailing // 16, stripe)
#     ss.trailing += 16
# print(ss.extract_stripe(10))
# st = ss.extract_stripe(10)
# print(encode_stripe(st))
# ss.update_stripe(10, st)

# ss.display_disks()

"""Failure simulation
The disks are going to fail.  

block failure: 1 in 1000 blocks fails
We will simulate this by randomly selecting a disk and then randomly selecting a block on that disk to fail.  
When a disk fails, it is removed from the list of available disks.  When a block fails, it is removed from the list of available blocks on 
that disk.  We will also simulate the repair of the disk, which will be done by replacing the disk with a new one.  We will also simulate 
the repair of a block, which will be done by replacing the block with a new one.  We will also simulate the addition of a new disk, which 
will be done by adding a new disk to the list of available disks.  We will also simulate the addition of a new block, which will be done by 
adding a new block to the list of available blocks on a disk.

disk failure: 1 in 100 disks fails
We will simulate this by randomly selecting a disk to fail.  When a disk fails, it is removed from the list of available disks.  We will also
simulate the repair of the disk, which will be done by replacing the disk with a new one.  We will also simulate the addition of a new disk,
which will be done by adding a new disk to the list of available disks.

disk addition: 1 in 100 disks is added
We will simulate this by adding a new disk to the list of available disks.

resilvering: 1 in 1000 blocks is resilvered
a system-wide resilvering operation is initiated, which reads all the blocks and rewrites broken blocks.  This is done by reading all the blocks
sequentially and computing the md5sum of the block, if the md5sum does not match the surety block, the block is rewritten.  This is done by
using any other 8 blocks in the RS stripe to compute the missing block.
"""

def fail_block(R):
    # disk = R.choice(available_disks)
    # block = R.choice(disk.blocks)
    pass
