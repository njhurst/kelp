
import ctypes
"""
This file contains the implementation of a benchmarking script for measuring the throughput of disk writes using asynchronous I/O (AIO) in Python.
The script utilizes a shared library, `libblockaio.so`, to perform AIO operations. It defines several structures and function prototypes required for interacting with the shared library.
The benchmarking script includes the following functions:
- `get_system_stats()`: Retrieves system statistics such as CPU usage, memory usage, and I/O counters.
- `run_detailed_benchmark()`: Runs a detailed benchmark by performing multiple iterations of write operations with varying total pages.
- `run_benchmark()`: Runs a benchmark by performing a fixed number of iterations of write operations with a fixed number of total pages.
- `get_system_info()`: Retrieves system information such as kernel version, total memory, disk information, and file system information.
- `get_aio_stats()`: Retrieves AIO statistics such as the number of AIO requests and the maximum number of AIO requests.
- `run_benchmark_on_drive(drive_path)`: Runs a benchmark on a specific drive by performing write operations and measuring the throughput.
- `run_multi_drive_benchmark()`: Runs a benchmark on multiple drives concurrently and prints the results.
The benchmarking script measures the throughput of disk writes by submitting write requests to the AIO context and checking for completed operations. It provides detailed information about system statistics, AIO statistics, and drive-specific results.
Note: This script requires the `libblockaio.so` shared library and appropriate permissions to access the drives for benchmarking.
"""
from ctypes import Structure, c_uint32, c_uint64, c_uint8, c_char, POINTER, c_void_p, c_int, c_size_t, c_ubyte, c_uint
import os
import time
import math
import psutil
import subprocess
from concurrent.futures import ThreadPoolExecutor
import statistics
import concurrent.futures
import blockaio

def get_system_stats():
    process = psutil.Process()
    return {
        "cpu_percent": process.cpu_percent(),
        "memory_percent": process.memory_percent(),
        "num_threads": process.num_threads(),
        "num_fds": process.num_fds(),
        "io_counters": process.io_counters(),
    }

def get_system_info():
    info = {}
    info['kernel_version'] = subprocess.getoutput('uname -r')
    info['total_memory'] = subprocess.getoutput('free -h')
    info['disk_info'] = subprocess.getoutput('lsblk')
    info['file_system'] = subprocess.getoutput('df -T .')
    return info

def get_aio_stats():
    try:
        with open('/proc/sys/fs/aio-nr', 'r') as f:
            aio_nr = int(f.read().strip())
        with open('/proc/sys/fs/aio-max-nr', 'r') as f:
            aio_max_nr = int(f.read().strip())
        return aio_nr, aio_max_nr
    except:
        return None, None

def submit_write(ctx, fd, start_page, num_pages, in_flight):
    ret = blockaio.submit_write(ctx, fd, start_page, num_pages)
    if ret != 0:
        error_code = ctypes.get_errno()
        error_message = os.strerror(error_code)
        aio_nr, aio_max_nr = get_aio_stats()
        print(f"Write submission failed:")
        print(f"  start_page: {start_page}")
        print(f"  num_pages: {num_pages}")
        print(f"  in_flight: {in_flight}")
        print(f"  error_code: {error_code}")
        print(f"  error_message: {error_message}")
        print(f"  return_value: {ret}")
        print(f"  file_offset: {start_page * blockaio.PAGE_SIZE}")
        print(f"  request_size: {num_pages * blockaio.PAGE_SIZE}")
        print(f"  total_requested_pages: {start_page + num_pages}")
        print(f"  available_memory: {psutil.virtual_memory().available / (1024*1024):.2f} MB")
        print(f"  cpu_percent: {psutil.cpu_percent(interval=0.1)}")
        print(f"  disk_usage: {psutil.disk_usage('/').percent}%")
        print(f"  aio_nr: {aio_nr}")
        print(f"  aio_max_nr: {aio_max_nr}")
        print(f"  open_files: {len(psutil.Process().open_files())}")
    return ret

def run_detailed_benchmark():
    system_info = get_system_info()
    print("System Information:")
    for key, value in system_info.items():
        print(f"{key}:\n{value}\n")

    fd = os.open("test_file", os.O_WRONLY | os.O_CREAT | os.O_DIRECT)
    io_ctx = blockaio.io_setup(blockaio.MAX_EVENTS)

    p2w = 30
    start_total_pages = 10000
    end_total_pages = 10002
    iterations_per_page = 5

    for total_pages in range(start_total_pages, end_total_pages + 1):
        successes = 0
        failures = 0
        for i in range(iterations_per_page):
            print(f"\nTesting with total_pages = {total_pages}, iteration {i+1}/{iterations_per_page}")
            submitted = 0
            in_flight = 0
            success = True

            while submitted < total_pages:
                if in_flight < blockaio.MAX_EVENTS:
                    pages_to_write = min(p2w, total_pages - submitted)
                    ret = submit_write(io_ctx, fd, submitted, pages_to_write, in_flight)
                    if ret == 0:
                        submitted += pages_to_write
                        in_flight += 1
                    else:
                        print(f"Failed at total_pages = {total_pages}, submitted = {submitted}")
                        success = False
                        break
                
                written = blockaio.check_completed(io_ctx)
                in_flight -= written

            if success:
                successes += 1
                print(f"Successfully completed total_pages = {total_pages}")
            else:
                failures += 1

            # Clear the AIO context and reopen the file for each iteration
            blockaio.io_destroy(io_ctx)
            os.close(fd)
            fd = os.open("test_file", os.O_WRONLY | os.O_CREAT | os.O_DIRECT)
            io_ctx = blockaio.io_setup(blockaio.MAX_EVENTS)

        print(f"\nResults for total_pages = {total_pages}:")
        print(f"Successes: {successes}/{iterations_per_page}")
        print(f"Failures: {failures}/{iterations_per_page}")

    blockaio.io_destroy(io_ctx)
    os.close(fd)

# Benchmark function
def run_benchmark():
    fd = os.open("test_file", os.O_WRONLY | os.O_CREAT | os.O_DIRECT)
    io_ctx = blockaio.io_setup(blockaio.MAX_EVENTS)

    iterations = 10
    cumulative_written = 0
    sum_throughput = 0
    sum_throughput_squared = 0

    for i in range(iterations):
        total_pages = 10000
        pages_per_write = 100
        submitted = 0
        in_flight = 0

        start_time = time.monotonic()
        while submitted < total_pages:
            if in_flight < blockaio.MAX_EVENTS:
                pages_to_write = min(pages_per_write, total_pages - submitted)
                ret = submit_write(io_ctx, fd, submitted, pages_to_write, in_flight)
                if ret == 0:
                    submitted += pages_to_write
                    in_flight += 1
                else:
                    print(f"Failed at total_pages = {total_pages}, submitted = {submitted}")
                    success = False
                    break
            
            written = blockaio.check_completed(io_ctx)
            in_flight -= written

        cumulative_written += total_pages
        end_time = time.monotonic()
        duration = end_time - start_time
        throughput = total_pages * blockaio.PAGE_SIZE / duration / 1e6
        sum_throughput += throughput
        sum_throughput_squared += throughput * throughput

    print(f"Total written: {cumulative_written} pages = {cumulative_written * (blockaio.PAGE_SIZE / 1e9):.2f} GB")
    print(f"Throughput: {sum_throughput / iterations:.2f} MB/s")
    throughput_std_dev = math.sqrt(sum_throughput_squared / iterations - (sum_throughput / iterations) ** 2)
    print(f"Throughput std dev: {throughput_std_dev:.2f} MB/s")

    blockaio.io_destroy(io_ctx)
    os.close(fd)

def get_system_info():
    info = {}
    info['kernel_version'] = subprocess.getoutput('uname -r')
    info['total_memory'] = subprocess.getoutput('free -h')
    info['disk_info'] = subprocess.getoutput('lsblk')
    info['file_system'] = subprocess.getoutput('df -T .')
    return info

def get_aio_stats():
    try:
        with open('/proc/sys/fs/aio-nr', 'r') as f:
            aio_nr = int(f.read().strip())
        with open('/proc/sys/fs/aio-max-nr', 'r') as f:
            aio_max_nr = int(f.read().strip())
        return aio_nr, aio_max_nr
    except:
        return None, None

def run_benchmark_on_drive(drive_path):
    file_path = os.path.join(drive_path, "test_file")
    fd = os.open(file_path, os.O_WRONLY | os.O_CREAT | os.O_DIRECT)
    io_ctx = blockaio.io_setup(blockaio.MAX_EVENTS)

    iterations = 10
    total_pages = 10000
    pages_per_write = 100
    throughputs = []

    for _ in range(iterations):
        submitted = 0
        in_flight = 0

        start_time = time.monotonic()
        while submitted < total_pages:
            if in_flight < blockaio.MAX_EVENTS:
                pages_to_write = min(pages_per_write, total_pages - submitted)
                ret = submit_write(io_ctx, fd, submitted, pages_to_write, in_flight)
                if ret == 0:
                    submitted += pages_to_write
                    in_flight += 1
                else:
                    print(f"Failed at total_pages = {total_pages}, submitted = {submitted}")
                    break
            
            written = blockaio.check_completed(io_ctx)
            in_flight -= written

        end_time = time.monotonic()
        duration = end_time - start_time
        throughput = total_pages * blockaio.PAGE_SIZE / duration / 1e6
        throughputs.append(throughput)

    blockaio.io_destroy(io_ctx)
    os.close(fd)

    return {
        'drive': drive_path,
        'throughputs': throughputs,
        'avg_throughput': statistics.mean(throughputs),
        'std_dev': statistics.stdev(throughputs) if len(throughputs) > 1 else 0,
        'min_throughput': min(throughputs),
        'max_throughput': max(throughputs),
    }

def run_multi_drive_benchmark():
    drives = [f"/data{i}/njh" for i in range(1, 25)]
    # remove drives that do not exist
    drives = [drive for drive in drives if os.path.exists(drive)]
    if not drives:
        print("No valid drives found for benchmarking.")
        return
    
    print("System Information:")
    system_info = get_system_info()
    for key, value in system_info.items():
        print(f"{key}:\n{value}\n")

    results = []
    with ThreadPoolExecutor(max_workers=len(drives)) as executor:
        future_to_drive = {executor.submit(run_benchmark_on_drive, drive): drive for drive in drives}
        for future in concurrent.futures.as_completed(future_to_drive):
            drive = future_to_drive[future]
            try:
                result = future.result()
                results.append(result)
            except Exception as exc:
                print(f'{drive} generated an exception: {exc}')

    # Print individual drive results
    for result in results:
        print(f"\nResults for {result['drive']}:")
        print(f"Average Throughput: {result['avg_throughput']:.2f} MB/s")
        print(f"Throughput Std Dev: {result['std_dev']:.2f} MB/s")
        print(f"Min Throughput: {result['min_throughput']:.2f} MB/s")
        print(f"Max Throughput: {result['max_throughput']:.2f} MB/s")

    # Calculate and print total results
    total_avg_throughput = sum(result['avg_throughput'] for result in results)
    total_min_throughput = sum(result['min_throughput'] for result in results)
    total_max_throughput = sum(result['max_throughput'] for result in results)

    print("\nTotal Results:")
    print(f"Total Average Throughput: {total_avg_throughput:.2f} MB/s")
    print(f"Total Min Throughput: {total_min_throughput:.2f} MB/s")
    print(f"Total Max Throughput: {total_max_throughput:.2f} MB/s")

if __name__ == "__main__":
    run_multi_drive_benchmark()