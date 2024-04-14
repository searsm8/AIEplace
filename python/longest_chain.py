import json

def extractDependenciesFromNetlist_b(nets, num_nodes):
    '''Assumes that nets are sorted by dependency'''
    dependencies = [0]*num_nodes
    predecessors = {}
    for i in range(num_nodes): predecessors[i] = []

    for net in nets:
        for i in range(1, len(net)):
            dependencies[net[i]] = max(dependencies[net[i]], dependencies[net[0]]+1)
            predecessors[net[i]].append(net[0])
    return dependencies, predecessors

def map_nets_to_list(nets, nodes):
    """converts a list of nets that have string names to a list of nets that have integer names

    Args:
        nets (list<list<string>>): nets with associated nodes to each net
                                   [['a', 'c'], ['b', 'a']]
        nodes (dict): dict of nodes

    Returns:
        out_list: same size as nets, but with strings converted to integers
                  [[0, 2], [1, 3]]
        Dict (Dict): the mapping used to convert strings to ints
                  {'a': 0, 'b': 1, 'c': 2, 'd': 3}
    """
    Dict = {}
    out_list = []
    keys = list(nodes.keys())
    for i in range(len(keys)):
        Dict[keys[i]] = i
    for i in range(len(nets)):
        temp_list = []
        for net in range(len(nets[i])):
            temp_list.append(Dict[nets[i][net]])
        out_list.append(temp_list)
    return out_list, Dict

def get_longest_chain(filename):
    with open(filename) as file:
        JSON = json.load(file)
        node_names = list(JSON["node_sizes"].keys())
        net_names = list(JSON["nets"].values())
        node_times = list(JSON["node_runtimes"].values())
        nets, map_dict = map_nets_to_list(net_names, JSON["node_sizes"])
        dependencies, predecessors = extractDependenciesFromNetlist_b(nets, len(map_dict))
        # print(predecessors)
        # dependency, herd time, total time including predecessors
        herd_info = {key: [-1, -1, -1] for key in range(len(node_names))}
        keys = list(herd_info.keys())
        for i in range(len(dependencies)):
            herd_info[keys[i]][0] = dependencies[i]
            herd_info[keys[i]][1] = node_times[i]
        return nets, predecessors, herd_info        

def get_total_node_time(herd_info, nets, predecessors):
    keys = list(herd_info.keys())
    max_dep = 0
    for key in keys:
        if herd_info[key][0] == 0:
            herd_info[key][2] = herd_info[key][1]
        if herd_info[key][0] > max_dep:
            max_dep = herd_info[key][0]
    for dep in range(1, max_dep + 1):
        for key in keys:
            if herd_info[key][0] == dep:
                times = []
                for node in range(len(predecessors[key])):
                    times.append(herd_info[predecessors[key][node]][2])
                herd_info[key][2] = max(times) + herd_info[key][1]
    max_time = 0
    max_key = -1
    for key in keys:
        if herd_info[key][2] > max_time:
            max_time = herd_info[key][2]
            max_key = key
    return max_time

def get_min_runtime(filename):
    nets, predecessors, herd_info = get_longest_chain("benchmarks/" + str(filename))
    return get_total_node_time(herd_info, nets, predecessors)