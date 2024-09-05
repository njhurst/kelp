"""
Problem statement:
Over time blobs get deleted and new blobs get added. The goal is to compact the blobs to minimize the space used.
The blobs are stored in a file and each blob has a start and end position in the file.
The blobs are sorted by their start position.
The compaction process should move the blobs to the beginning of the file and fill the gaps left by the deleted blobs.
The compaction process should minimize the number of moves required to achieve the compacted state.
The operation should be performed in-place, i.e., no additional disk space should be used.
At every moment the file should be a valid state, i.e., no overlaps between blobs and the start position should be less than the end position.  This includes the intermediate states during the compaction process and even page tear.

It's okay to have some free space at the end of the file if it's not possible to move all the blobs to the beginning of the file.
"""

import numpy as np
from collections import deque

def quantize_size(size):
    if size == 0:
        return 0
    msb = int(np.floor(np.log2(size)))
    mantissa = (size >> (msb - 3)) & 0b111
    return (msb << 3) | mantissa

def compact_file(data_start, data_end, labels, debug=False):
    n = len(data_start)
    assert len(data_end) == n == len(labels)
    
    # Sort the input data
    sorted_indices = np.argsort(data_start)
    data_start = data_start[sorted_indices]
    data_end = data_end[sorted_indices]
    labels = labels[sorted_indices]
    
    # Initialize free space queues
    max_qsize = quantize_size(max(data_end) - min(data_start))
    print(f"Max qsize: {max_qsize}")
    free_spaces = [deque() for _ in range(max_qsize + 1)]
    
    new_start = []
    new_end = []
    new_labels = []
    current_position = 0

    total_moves = 0
    
    for i in range(n):
        # Add any new free space
        if data_start[i] > current_position:
            free_size = data_start[i] - current_position
            qsize = quantize_size(free_size)
            free_spaces[qsize].append((current_position, free_size))
            # print(f"Free spaces: {free_spaces}")
        
        # Try to move the current block
        block_size = data_end[i] - data_start[i]
        qsize = quantize_size(block_size)
        moved = False
        
        for j in range(qsize, max_qsize + 1):
            while free_spaces[j] and free_spaces[j][0][0] < data_start[i]:
                free_start, free_size = free_spaces[j].popleft()
                if free_size >= block_size:
                    new_start.append(free_start)
                    new_end.append(free_start + block_size)
                    new_labels.append(labels[i])
                    total_moves += 1
                    if debug:
                        print(f"Moving block {labels[i]} from {data_start[i]}-{data_end[i]} to {new_start[-1]}-{new_end[-1]}")
                    
                    # Add remaining free space back to the queue
                    if free_size > block_size:
                        remaining_size = free_size - block_size
                        remaining_qsize = quantize_size(remaining_size)
                        free_spaces[remaining_qsize].append((free_start + block_size, remaining_size))
                    
                    moved = True
                    break
                
            if moved:
                break
        
        if not moved:
            new_start.append(data_start[i])
            new_end.append(data_end[i])
            new_labels.append(labels[i])
            # if debug:
            #     print(f"Block {labels[i]} remains at {data_start[i]}-{data_end[i]}")
        
        current_position = max(current_position, data_end[i])
    
    return np.array(new_start), np.array(new_end), np.array(new_labels), total_moves

def generate_test_data(n=10):
    data_start = np.random.randint(0, 1000*n, n)
    # sort the data
    data_start = np.sort(data_start)
    # make the data end randomly between each data_start
    data_end = np.zeros_like(data_start)
    for i in range(n):
        space = data_start[i+1] - data_start[i] if i < n-1 else 1000
        data_end[i] = data_start[i] + np.random.randint(space // 2, space)

    labels = np.array([chr(ord('A') + i) for i in range(n)])
    return data_start, data_end, labels

def check_invariants(data_start, data_end, labels):
    n = len(data_start)
    assert len(data_end) == n == len(labels)
    assert np.all(data_start < data_end)
    assert np.all(data_start[1:] >= data_end[:-1])
    assert np.all(data_end - data_start > 0)

# Test function remains the same
def test_compact_file(debug=False):
    # data_start = np.array([0, 100, 200, 400])
    # data_end = np.array([50, 150, 300, 450])
    # labels = np.array(['A', 'B', 'C', 'D'])
    data_start, data_end, labels = generate_test_data(100)
    check_invariants(data_start, data_end, labels)
    
    print("Original data:")
    print("Start:", data_start)
    print("End:  ", data_end)
    print("Label:", labels)
    total_moves = 0

    for i in range(100):    
        print("\nCompaction process:")
        new_start, new_end, new_labels, round_moves = compact_file(data_start, data_end, labels, debug=debug)

        total_moves += round_moves

        perm = np.argsort(new_start)
        new_start = new_start[perm]
        new_end = new_end[perm]
        new_labels = new_labels[perm]
        
        print("\nCompacted data:")
        print("Start:", new_start)
        print("End:  ", new_end)
        print("Label:", new_labels)
        
        # Verify that all blocks are present and no overlaps
        assert len(new_start) == len(data_start)
        label_perm = np.argsort(labels)
        new_label_perm = np.argsort(new_labels)
        assert np.all((new_end - new_start)[new_label_perm] == (data_end - data_start)[label_perm])
        assert np.all(new_start[1:] >= new_end[:-1])

        # if unchanged then break
        if np.all(data_start == new_start) and np.all(data_end == new_end) and np.all(labels == new_labels):
            print(f"\nNo changes made, breaking the loop after {i} rounds and {total_moves} moves.")
            break

        # retry with the compacted data
        data_start = new_start
        data_end = new_end
        labels = new_labels
    
    print("\nTest passed successfully!")

# Run the test with debug mode on
test_compact_file(debug=True)