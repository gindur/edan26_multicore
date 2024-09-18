from graphviz import Digraph

# Function to read edges from a file
def read_edges_from_file(filename):
    edges = []
    with open(filename, 'r') as file:
        for line in file:
            # Split the line by spaces
            parts = line.split()

            if len(parts) == 3:
                # Convert parts to integers and append as a tuple
                source, target, weight = map(int, parts)
                edges.append((source, target, weight))
    return edges

# Create a directed graph (or use 'Graph' for an undirected graph)
dot = Digraph()

# Read edges from file
filename = './data/big/001.in'  # Replace with your actual filename
edges = read_edges_from_file(filename)

# Add edges to the graph
for src, dst, weight in edges:
    dot.edge(str(src), str(dst), label=str(weight))

# Save and render the graph
dot.render('weighted_graph', view=True)
