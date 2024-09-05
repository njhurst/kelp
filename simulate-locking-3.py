import random
import time
import threading

dump_lock = threading.Lock()

class Server:
    def __init__(self, size=10):
        self.data = [{'value': 0, 'seq': 0} for _ in range(size)]
        self.prepared_data = None

    def read(self, start, end):
        time.sleep(random.uniform(0.01, 0.05))
        return [d['value'] for d in self.data[start:end]]

    def prepare(self, start, end, values):
        self.prepared_data = {
            'start': start,
            'end': end,
            'values': [{'value': v, 'seq': self.data[i]['seq']+1} for i,v in enumerate(values)]
        }

    def commit(self):
        if self.prepared_data:
            start, end = self.prepared_data['start'], self.prepared_data['end']
            self.data[start:end] = self.prepared_data['values']
            self.prepared_data = None
        else:
            raise Exception("No prepared data to commit")
    def dump(self):
        with dump_lock:
            for i, d in enumerate(self.data):
                print(f"{d['value']}({d['seq']})", end=' ')
            print()

class Coordinator:
    def __init__(self, servers):
        self.servers = servers
        self.range_locks = {}
        self.locks_lock = threading.Lock()

    def acquire_range_lock(self, start, end):
        with self.locks_lock:
            for (locked_start, locked_end), lock in list(self.range_locks.items()):
                if start < locked_end and end > locked_start:
                    if not lock.acquire(blocking=False):
                        return False
            new_lock = threading.Lock()
            new_lock.acquire()
            self.range_locks[(start, end)] = new_lock
            return True

    def release_range_lock(self, start, end):
        with self.locks_lock:
            if (start, end) in self.range_locks:
                self.range_locks[(start, end)].release()
                del self.range_locks[(start, end)]

    def two_phase_commit(self, start, end, values, max_retries=15):
        attempts = 0
        while attempts < max_retries:
            if self.acquire_range_lock(start, end):
                try:
                    for server in self.servers:
                        server.prepare(start, end, values)
                    for server in self.servers:
                        server.commit()
                    return  # Successful write, exit function
                finally:
                    self.release_range_lock(start, end)
            else:
                time.sleep(0.1 * 2 ** attempts + random.uniform(0, 0.1))
                attempts += 1
        raise Exception("Failed to acquire necessary range locks after several retries")

# Usage of these functions remains similar, where you would use the `two_phase_commit` with the added `seq_num` parameter.

def client_task(coordinator, start, end, values=None):
    if values:
        coordinator.two_phase_commit(start, end, values)
    else:
        results = []
        for server in coordinator.servers:
            results.append(server.read(start, end))
        assert all(r == results[0] for r in results)
        # print(f"Read values: {results}")
    coordinator.servers[0].dump()

def simulate_clients(coordinator):
    # clients = [
    #     threading.Thread(target=client_task, args=(coordinator, 0, 3, [1]*3)),
    #     threading.Thread(target=client_task, args=(coordinator, 2, 5, [2]*3)),  # Overlapping range
    #     threading.Thread(target=client_task, args=(coordinator, 5, 10, [3]*5)),  # Non-overlapping range
    # ]
    clients = [
        threading.Thread(target=client_task, args=(coordinator, 0, 3, [1]*3)),
        threading.Thread(target=client_task, args=(coordinator, 5, 8, [2]*3)),
        threading.Thread(target=client_task, args=(coordinator, 0, 10)),
        threading.Thread(target=client_task, args=(coordinator, 0, 5, [3]*5)),
        threading.Thread(target=client_task, args=(coordinator, 3, 8, [4]*5)),
        threading.Thread(target=client_task, args=(coordinator, 0, 10)),
        threading.Thread(target=client_task, args=(coordinator, 0, 3, [5]*3)),
        threading.Thread(target=client_task, args=(coordinator, 5, 8, [6]*3)),
        threading.Thread(target=client_task, args=(coordinator, 0, 10)),
        threading.Thread(target=client_task, args=(coordinator, 0, 5, [7]*5)),
        threading.Thread(target=client_task, args=(coordinator, 3, 8, [8]*5)),
        threading.Thread(target=client_task, args=(coordinator, 0, 10)),
    ]
    for client in clients:
        client.start()
    for client in clients:
        client.join()
    
    client_task(coordinator, 0, 10)  # Read all values

# Example Usage
servers = [Server() for _ in range(12)]
coordinator = Coordinator(servers)
simulate_clients(coordinator)
