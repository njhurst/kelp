import numpy as np
from scipy import sparse
import queue
import random

class Machine:
    def __init__(self, machine_id, machine_offset, vertices, csr_matrix):
        self.id = machine_id
        self.offset = machine_offset
        self.vertices = set(vertices)
        self.graph = csr_matrix
        self.visited = {v: False for v in vertices}
        self.distance = {v: float('inf') for v in vertices}
        self.queue = queue.Queue()
        self.message_queue = queue.Queue()
        self.is_idle = True

    def process_local_queue(self):
        while not self.queue.empty():
            v = self.queue.get()
            neighbors = self.graph.getrow(v - self.offset).indices
            for u in neighbors:
                if u in self.vertices and not self.visited[u]:
                    self.visited[u] = True
                    self.distance[u] = self.distance[v] + 1
                    self.queue.put(u)
                elif u not in self.vertices:
                    yield u, self.distance[v] + 1

    def process_messages(self):
        while not self.message_queue.empty():
            u, dist = self.message_queue.get()
            if not self.visited[u]:
                self.visited[u] = True
                self.distance[u] = dist
                self.queue.put(u)

    def step(self):
        self.is_idle = self.queue.empty() and self.message_queue.empty()
        if not self.is_idle:
            outgoing_messages = list(self.process_local_queue())
            self.process_messages()
            return outgoing_messages
        return []
    
def create_random_csr_matrix(n, density=0.1):
    return sparse.random(n, n, density=density, format='csr', dtype=np.int8)


def distribute_graph(total_vertices, num_machines):
    vertices_per_machine = total_vertices // num_machines
    machines = []
    global_graph = create_random_csr_matrix(total_vertices)
    
    for i in range(num_machines):
        start = i * vertices_per_machine
        end = start + vertices_per_machine if i < num_machines - 1 else total_vertices
        vertices = range(start, end)
        local_graph = global_graph[start:end, :]
        machines.append(Machine(i, start, vertices, local_graph))
    
    return machines

def distributed_bfs(machines, start_vertex):
    start_machine = next(m for m in machines if start_vertex in m.vertices)
    start_machine.visited[start_vertex] = True
    start_machine.distance[start_vertex] = 0
    start_machine.queue.put(start_vertex)
    start_machine.is_idle = False

    while any(not m.is_idle for m in machines):
        for machine in machines:
            outgoing_messages = machine.step()
            for dest_vertex, dist in outgoing_messages:
                dest_machine = next(m for m in machines if dest_vertex in m.vertices)
                dest_machine.message_queue.put((dest_vertex, dist))
                dest_machine.is_idle = False

    return machines

# Simulation parameters
total_vertices = 1000
num_machines = 4
start_vertex = 0

# Run the simulation
machines = distribute_graph(total_vertices, num_machines)
result_machines = distributed_bfs(machines, start_vertex)

# Print results
for machine in result_machines:
    reachable_vertices = [v for v in machine.vertices if machine.visited[v]]
    print(f"Machine {machine.id}: {len(reachable_vertices)} reachable vertices")
    print(f"Sample distances: {dict(list(machine.distance.items())[:5])}")
    print()