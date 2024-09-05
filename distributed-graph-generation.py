import matplotlib.pyplot as plt
import networkx as nx
import numpy as np

node_count = 20
radius = 250  # Radius for nodes layout in a circle
center_x = 0  # Center X for nodes
center_y = 0  # Center Y for nodes

def generate_zipf(s, N):
    # Calculate Zipfian constants for normalization
    c = sum(1.0 / (i ** s) for i in range(1, N + 1))
    c = 1 / c

    # Generate CDF (cumulative distribution function)
    cdf = [0]
    for i in range(1, N + 1):
        cdf.append(cdf[i - 1] + c / (i ** s))

    # Use random number to find corresponding value
    random = np.random.random()
    for i in range(1, N + 1):
        if random <= cdf[i]:
            return i - 1  # Adjust if you want 0 to N-1 range, otherwise it gives 1 to N
    return N - 1  # In case of rounding errors, return the last element

def generate_graph(p, degree, z_exp):
    nodes = []
    edges = nx.Graph()

    # Initialize nodes and place them in a circle
    for i in range(node_count):
        angle = (i / node_count) * 2 * np.pi
        x = center_x + radius * np.cos(angle)
        y = center_y + radius * np.sin(angle)
        nodes.append((x, y))
        edges.add_node(i, pos=(x, y))

    # Create a ring lattice with k neighbors
    k = degree  # Number of nearest neighbors (assumed even for simplicity)
    for i in range(node_count):
        for j in range(1, k + 1):
            neighbor = (i + j) % node_count
            edges.add_edge(i, neighbor)

    # Rewire edges with probability p
    for (u, v) in list(edges.edges()):
        if np.random.random() < p:
            edges.remove_edge(u, v)
            new_neighbor = generate_zipf(z_exp, node_count)
            while new_neighbor == u or edges.has_edge(u, new_neighbor):
                new_neighbor = generate_zipf(z_exp, node_count)
            edges.add_edge(u, new_neighbor)

    return edges, nodes

def draw_graph(graph, nodes):
    plt.figure(figsize=(8, 8))
    # pos = {i: nodes[i] for i in range(len(nodes))}
    pos = nx.spring_layout(graph)
    nx.draw(graph, pos, with_labels=True, node_size=500, node_color='skyblue', edge_color='gray', font_weight='bold')
    plt.show()

def update_graph(p, degree, z_exp):
    graph, nodes = generate_graph(p, degree, z_exp)
    draw_graph(graph, nodes)

# Example usage:
p = 0.1  # Rewiring probability
degree = 4  # Degree
z_exp = 1.0  # Zipf exponent
update_graph(p, degree, z_exp)
