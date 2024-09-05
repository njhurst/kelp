"""
Simulate the ASGW-1 (Adaptive Scatter-Gather Write) algorithm with Thompson sampling for server selection and log-normal distribution for server response times. 
"""
import random
import math
import heapq
import threading
import time
import queue

R = random.Random(42)

class Server:
    def __init__(self, server_id, alpha=0.1):
        self.server_id = server_id
        self.busy = False
        self.latency = 10**(R.uniform(-8, 0))
        self.log_mean = -20
        self.log_var = 0.3
        self.count = 0
        self.alpha = alpha
        self.lock = threading.Lock()
        self.total_requests = 0

    def update_stats(self, wait_time):
        with self.lock:
            self.count += 1
            log_wait_time = math.log(wait_time)
            if self.count == 1:
                self.log_mean = log_wait_time
            else:
                delta = log_wait_time - self.log_mean
                self.log_mean += self.alpha * delta
                self.log_var = (1 - self.alpha) * (self.log_var + self.alpha * delta ** 2)

    def get_expected_wait_time(self, percentile=0.9):
        with self.lock:
            if self.count < 2:
                return None
            log_std = math.sqrt(self.log_var)
            z_score = 1.2816  # 90th percentile Z-score
            wait_time_90p = math.exp(self.log_mean + log_std * z_score)
            return wait_time_90p

    def write_data(self, data):
        # Simulating write operation with random wait time
        start_time = time.time()
        wait_time = random.lognormvariate(self.log_mean, math.sqrt(self.log_var))+self.latency
        self.busy = True
        time.sleep(wait_time)
        write_time = time.time() - start_time
        self.update_stats(write_time)
        # print(f"Data written to server {self.server_id}, expected wait time: {wait_time}, actual write time: {write_time}")
        self.busy = False
        self.total_requests += 1
        
    def sample_from_distribution(self):
        with self.lock:
            if self.count < 2:
                # If the server has processed fewer than 2 requests, use a default sample
                return random.lognormvariate(-5, 1)
            else:
                # Sample from the server's lognormal distribution
                return random.lognormvariate(self.log_mean, math.sqrt(self.log_var))
            
    def get_server_stats(self):
        return self.log_mean, math.sqrt(self.log_var)
    
    def __eq__(self, other):
        return self.server_id == other.server_id

    def __ne__(self, other):
        return self.server_id != other.server_id

    def __lt__(self, other):
        return self.server_id < other.server_id

    def __gt__(self, other):
        return self.server_id > other.server_id

    def __le__(self, other):
        return self.server_id <= other.server_id

    def __ge__(self, other):
        return self.server_id >= other.server_id

class Client:
    def __init__(self, servers):
        self.servers = servers
        self.request_id = 0
        self.lock = threading.Lock()

    def write_request_single(self, data):
        with self.lock:
            self.request_id += 1
            request_id = self.request_id

        # Create a priority queue of servers based on their expected wait times
        server_queue = [(server.sample_from_distribution(), server) for server in self.servers]
        heapq.heapify(server_queue)

        while server_queue:
            expected_wait_time, server = heapq.heappop(server_queue)

            if not server.busy:
                # Write data to the first available server
                server.write_data(data)
                break
            else:
                # If the server is busy, put it back in the queue with an updated expected wait time
                updated_wait_time = server.get_expected_wait_time() or float('inf')
                heapq.heappush(server_queue, (server.sample_from_distribution(), server))

            # If all servers are busy, wait for a short period and then retry
            if len(server_queue) == len(self.servers):
                time.sleep(0.01)  # Adjust the wait time as needed
    
    def write_request(self, data, k=2):
        with self.lock:
            self.request_id += 1
            request_id = self.request_id

        # Perform Thompson sampling to select k servers
        server_samples = [(server.sample_from_distribution(), server) for server in self.servers]
        selected_servers = sorted(server_samples, key=lambda x: x[0])[:k]

        # Calculate the timeout value based on the maximum expected latency among the selected servers
        timeout = max(expected_latency for expected_latency, _ in selected_servers) * 1.5

        # Send the request to the selected servers concurrently
        start_time = time.time()
        threads = []
        response_queue = queue.Queue()

        def send_request(server):
            server.write_data(data)
            if not server.busy:
                response_queue.put((server, time.time() - start_time))

        for _, server in selected_servers:
            thread = threading.Thread(target=send_request, args=(server,))
            thread.start()
            threads.append(thread)

        # Wait for the first response or timeout
        try:
            server, latency = response_queue.get(timeout=timeout)
            return server, latency
        except queue.Empty:
            # No response received within the timeout
            return None, None
        finally:
            # Wait for all threads to complete
            for thread in threads:
                thread.join()
    
if __name__ == "__main__":
    servers = [Server(i) for i in range(15)]
    clients = [Client(servers) for _ in range(5)]


    start_time = time.time()
    successful_requests = 0
    # Simulate write requests
    for client in clients:
        for i in range(200):
            data = f"Data {i}"
            client.write_request(data)
            successful_requests += 1

    print(f"Total time taken: {time.time() - start_time:.2f} seconds")
    print("Server stats:")
    for server in servers:
        # transform the log-normal distribution to a confidence interval
        mean, std = server.get_server_stats()
        print(f"Server {server.server_id}: Mean={server.log_mean}, Std={math.sqrt(server.log_var)} 90th percentile={math.exp(mean + 1.2816 * std)}")
        # server throughput
        print(f"Server {server.server_id}: Latency={server.latency} Throughput={server.total_requests / (time.time() - start_time)} requests/second, total requests={server.total_requests}")
    # print total throughput
    print(f"Total throughput: {successful_requests / (time.time() - start_time)} requests/second")
    # print maximimum throughput possible
    # print(f"Max throughput: {sum(1. / server.latency for server in servers)} requests/second")
