import polytony.graphs

def layout(graph, node_width, node_height, margin):

    layers = polytony.graphs.layer(graph)
    
    width_in_nodes = max(map(len, layers))
    width_in_units = node_width * width_in_nodes \
                     + margin * (width_in_nodes - 1)
    center_in_units = width_in_units / 2

    positions = {}
    layers.reverse()
    y = margin + node_height / 2
    for layer in layers:
        x = margin * 2 + node_width / 2
        for node in layer:
            positions[node] = (x, y)
            x += node_width + margin
        y += node_height + margin
    return positions
