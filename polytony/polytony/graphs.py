import sets

class Graph(object):

    def __init__(self, other=None):

        self.__map = {}
        if other is not None:
            for node in other:
                self.add_node(node)
            for parent, child in other.iteredges():
                self.add_edge(parent, child)


    def add_node(self, node):

        if node not in self.__map:
            self.__map[node] = sets.Set()


    def add_edge(self, parent, child):

        assert parent in self.__map
        assert child in self.__map
        self.__map[parent].add(child)


    def remove_edge(self, parent, child):

        assert parent in self.__map
        self.__map[parent].remove(child)


    def discard_edge(self, parent, child):

        if parent in self.__map:
            self.__map[parent].discard(child)


    def __delitem__(self, node):

        del self.__map[node]


    def __getitem__(self, node):

        return sets.ImmutableSet(self.__map[node])


    def __contains__(self, node):

        return node in self.__map
    

    def __iter__(self):
        """Iterates over nodes."""

        return iter(self.__map)


    def iteredges(self):
        """Iterate over '(parent, child)' edge pairs."""

        for parent, children in self.__map.iteritems():
            for child in children:
                yield parent, child


    def contains_edge(self, parent, child):

        try:
            return child in self.__map[parent]
        except KeyError:
            return 0


    def iterchildren(self):
        """Iterate over '(node, children)' pairs."""

        for node, children in self.__map.iteritems():
            yield (node, sets.ImmutableSet(children))


    def __len__(self):
        """Returns number of nodes."""

        return len(self.__map)



def reverse(graph):

    new_graph = Multimap()
    for source, target in graph.iterpairs():
        new_graph.add(target, source)
    return new_graph


### TOTALLY BROKEN
def collapse(graph):
    """Return a copy of the graph in which all nodes with indegree = outdegree
    = 1 have been removed."""

    backgraph = reverse(graph)
    new_graph = Graph(graph)

    for node in new_graph:
        try:
            from bibblebabble import hex_to_bibble as h2b
            print ""
            print "Considering removing node " + h2b(node)
            print "Parents: " + repr(map(h2b, backgraph[node]))
            print "Children: " + repr(map(h2b, new_graph[node]))
            if len(new_graph[node]) == 1 and len(backgraph[node]) == 1:
                print "Yep, doin' it."
                for next in new_graph[node]:
                    for prev in backgraph[node]:
                        new_graph.add(prev, next)
                        new_graph.remove(prev, node)
                        backgraph.add(next, prev)
                        backgraph.remove(next, node)
                del new_graph[node]
                del backgraph[node]
        except KeyError:
            # KeyError's mean the node wasn't in one graph of the other,
            # i.e. it's a root or leaf, so we skip it.
            pass

    return new_graph


def layer(graph):
    """Stratifies a graph.

    Returns a list of sets of nodes.  The children of the nodes in any given
    set are always listed in earlier sets."""

    layers = []

    graph_copy = Graph(graph)

    lower_layers = sets.Set()
    top_layer = sets.Set()
    while graph_copy:
        for node, children in graph_copy.iterchildren():
            if children <= lower_layers:
                top_layer.add(node)

        assert len(top_layer) != 0, "Loops in DAG"

        for node in top_layer:
            del graph_copy[node]

        lower_layers |= top_layer
        layers.append(top_layer)
        top_layer = sets.Set()

    return layers


