import random
import numpy as np
import threading
import matplotlib.pyplot as plt

class StorageNode:
    def __init__(self, node_id, latency_params, failure_probability):
        self.node_id = node_id
        self.latency_params = latency_params
        self.failure_probability = failure_probability
    
    def store_data(self, data_block):
        if random.random() < self.failure_probability:
            raise Exception(f"Node {self.node_id} failed to store data.")
        
        latency = np.random.lognormal(np.log(self.latency_params['mean']), np.log(1 + (self.latency_params['stddev'] / self.latency_params['mean'])))
        return latency
    
    def retrieve_data(self):
        if random.random() < self.failure_probability:
            raise Exception(f"Node {self.node_id} failed to retrieve data.")
        
        latency = np.random.lognormal(np.log(self.latency_params['mean']), np.log(1 + (self.latency_params['stddev'] / self.latency_params['mean'])))
        return latency

class StorageNetwork:
    def __init__(self, num_nodes, latency_params, failure_probability):
        self.nodes = [StorageNode(i, latency_params, failure_probability) for i in range(num_nodes)]
    
    def store_data(self, data, n=8, k=8):
        data_blocks = [data[i:i+len(data)//n] for i in range(0, len(data), len(data)//n)]
        encoded_blocks = self.rs_encode(data_blocks, n, k)
        
        latencies = []
        for i, node in enumerate(self.nodes):
            try:
                latency = node.store_data(encoded_blocks[i])
                latencies.append(latency)
            except Exception as e:
                pass #print(e)
        return latencies
    
    def retrieve_data(self, n=8, k=8, dk=1):
        latencies = []
        retrieved_blocks = []
        random_nodes = random.sample(self.nodes, n+8)
        wait_lat = 0
        already = 0
        requested_blocks = set()
        while len(retrieved_blocks) < n:
            for node in random_nodes[already:n+k]:
                if node.node_id in requested_blocks:
                    continue
                requested_blocks.add(node.node_id)
                try:
                    latency = node.retrieve_data()+wait_lat
                    wait_lat += 0.02 # network time
                    latencies.append(latency)
                    retrieved_blocks.append(node.node_id)
                    if len(retrieved_blocks) == n:
                        break
                except Exception as e:
                    pass #print(e)
            
            if len(retrieved_blocks) < n:
                pass#print("Failed to retrieve enough data blocks.")
                wait_lat = max(latencies)
                already = len(requested_blocks)
                k += dk
                # print(wait_lat, already, k)
            if n+k > len(self.nodes):
                raise Exception("Failed to retrieve enough data blocks.")
            if already == n+k:
                break
        
        if len(retrieved_blocks) < n:
            #print("Failed to retrieve enough data blocks.")
            raise Exception("Failed to retrieve enough data blocks.")
        
        return latencies
    
    @staticmethod
    def rs_encode(data_blocks, n, k):
        # Placeholder for Reed-Solomon encoding
        encoded_blocks = data_blocks + [f"Parity{i}" for i in range(k)]
        return encoded_blocks

# Example usage
num_nodes = 16

latency_params = {'mean': 0.01, 'stddev': 0.05}  # Latency parameters in seconds
failure_probability = 0.1  # Probability of node failure
num_experiments = 1000

retrieve_p50s = {}
retrieve_p95s = {}
retrieve_p99s = {}

def simulation_run(num_nodes, latency_params, failure_probability, num_experiments, k, dk):
    store_latencies = []
    retrieve_latencies = []
    retrieve_failures = 0

    for i in range(num_experiments):
        network = StorageNetwork(num_nodes, latency_params, failure_probability)
        
        data = f"Experiment {i+1}: This is the data to be stored across the network."
        store_latencies.extend(network.store_data(data))
        try:
            retrieve_latencies.extend(network.retrieve_data(k=k, dk=dk))
        except Exception as e:
            retrieve_failures += 1

    store_p50 = np.percentile(store_latencies, 50)
    store_p95 = np.percentile(store_latencies, 95)
    retrieve_p50 = np.percentile(retrieve_latencies, 50)
    retrieve_p95 = np.percentile(retrieve_latencies, 95)
    retrieve_p99 = np.percentile(retrieve_latencies, 99)

    return store_p50, store_p95, retrieve_p50, retrieve_p95, retrieve_p99, retrieve_failures


store_latencies = []
retrieve_latencies = []
retrieve_failures = 0

def run_simulation(num_nodes, latency_params, failure_probability, num_experiments, k, dk, retrieve_p50s, retrieve_p95s, retrieve_p99s, run):
    store_p50, store_p95, retrieve_p50, retrieve_p95, retrieve_p99, retrieve_failures = simulation_run(num_nodes, latency_params, failure_probability, num_experiments, k, dk)
    retrieve_p50s[(k, dk, run)] = retrieve_p50
    retrieve_p95s[(k, dk, run)] = retrieve_p95
    retrieve_p99s[(k, dk, run)] = retrieve_p99

retrieve_p50s = {}
retrieve_p95s = {}
retrieve_p99s = {}

threads = []
for run in range(10):
    for dk in range(1, 3):
        for k in range(9):
            t = threading.Thread(target=run_simulation, args=(num_nodes, latency_params, failure_probability, num_experiments, k, dk, retrieve_p50s, retrieve_p95s, retrieve_p99s, run))
            threads.append(t)
            t.start()

for t in threads:
    t.join()

# Rest of the code...

# print()
# print(f"Latency Stats: k={k}")
# print(f"Retrieve Failures: {retrieve_failures}")
# print(f"Store P50: {store_p50:.4f}s")
# print(f"Store P95: {store_p95:.4f}s")
# print(f"Retrieve P50: {retrieve_p50:.4f}s")
# print(f"Retrieve P95: {retrieve_p95:.4f}s")
# print(f"Retrieve P99: {retrieve_p99:.4f}s")
# print(f"Retrieve P100: {max(retrieve_latencies):.4f}s")

# Plotting retrieve_p*
plt.figure()

# Create subplots
fig, axs = plt.subplots(2)

for dk in range(1, 3):
    for run in range(10):
        axs[dk-1].plot(range(9), [retrieve_p50s[(k, dk, run)] for k in range(9)])
        axs[dk-1].plot(range(9), [retrieve_p95s[(k, dk, run)] for k in range(9)])
        axs[dk-1].plot(range(9), [retrieve_p99s[(k, dk, run)] for k in range(9)])

# Set labels and title for subplots
axs[0].set_xlabel('k')
axs[0].set_ylabel('Latency (s)')
axs[0].set_title(f'Retrieve Latency Statistics {dk}')
axs[1].set_xlabel('k')
axs[1].set_ylabel('Latency (s)')
axs[1].set_title(f'Retrieve Latency Statistics {dk}')

# Display the plot
plt.show()
