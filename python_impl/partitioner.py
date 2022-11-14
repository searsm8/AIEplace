import pprint
import random
def partition_initialization(graph, dependencies, node_sizes, times):
    """Creates the partitioning structure that holds all of the nodes (herds) and nets
       (sub-graphs of herds)

    Args:
        graph (list<list<int>>): list containing sublists with herd numbers, with each
                                 sublist representing a net.
        dependencies (list): list of dependencies for each herd
        node_sizes (list<coords>): list of node sizes, with each node size represented
                                   by a coord 

    Returns:
        partition_information (dict): information about the nodes and nets

    """
    partition_information = {}
    partition_information["nodes"] = {}
    partition_information["nets"] = {}
    for i in range(len(graph)):
        partition_information["nets"][i] = {"broken": 1, "nodes": []}
        for j in range(len(graph[i])):
            partition_information["nets"][i]["nodes"].append(graph[i][j])
    for i in range(len(dependencies)):
        # Time is in units of micro seconds
        partition_information["nodes"][i] = {"time": times[i], "deps": dependencies[i], "size": node_sizes[i].row * node_sizes[i].col, "anets": [], "placed": -1}
        for j in range(len(graph)):
            for k in range(len(graph[j])):
                if i == graph[j][k]:
                    partition_information["nodes"][i]["anets"].append(j)

    ###TODO: Create swapping method
    # Ensure that we have a valid configuration while allowing for dependencies above the current dependency 
    # level to be placed at the same time as lower dependencies (will also need to update legalizer)
    # We should try first to create the target number of AIE tiles, but after that focus more so on 
    # Optimizing for execution time.

    # Within a dependency level, it should be random which nodes we take. At the same time, we should
    # still be targeting a specific size.

    # Should also have some way of swapping (randomly) for higher dependencies. 

    # At the same time, we want to be considering time.

    # How do we balance all of those?
    max_dep_key = get_max_dependency(partition_information)
    longest_dep_list = build_longest_dep_list(partition_information, max_dep_key)
    curr_part_herds = time_partition(partition_information, 20, longest_dep_list, 50)
    print(curr_part_herds)
    return partition_information

def time_partition(partition_information, target_part_size, longest_dep_list, tolerance):
    """Does partitioning based on completion time

    """
    curr_size = 0
    dependencies = []
    partition_herds = []
    for _ in range(len(longest_dep_list)):
        dependencies.append([])
        partition_herds.append([])
    for herd in range(len(partition_information["nodes"])):
        if partition_information["nodes"][herd]["placed"] != -1:
            continue
        dependencies[partition_information["nodes"][herd]["deps"]].append(herd)
    
    initial_herd = longest_dep_list[0][random.randint(0, len(longest_dep_list[0]) - 1)]
    partition_herds[0].append(initial_herd)
    print(partition_herds)
    curr_max_time = partition_information["nodes"][initial_herd]["time"]
    curr_size += partition_information["nodes"][initial_herd]["size"]

    for dep in range(len(dependencies)):
        for herd in range(len(dependencies[dep])):
            if curr_size + partition_information["nodes"][dependencies[dep][herd]]["size"] > target_part_size or \
               partition_information["nodes"][dependencies[dep][herd]]["time"] > curr_max_time + tolerance:
               continue
            else:
                partition_herds[dep].append(dependencies[dep][herd])
                curr_size += partition_information["nodes"][dependencies[dep][herd]]["size"]

    # shuffle herds
    return partition_herds


def build_longest_dep_list(partition_information, longest_dep_key):
    longest_dep = partition_information["nodes"][longest_dep_key]["deps"]
    dep_list = []
    for _ in range(longest_dep + 1):
        dep_list.append([])
    dep_list = get_dep_nets(partition_information, longest_dep_key, dep_list, longest_dep + 1)

    return dep_list

def get_dep_nets(partition_information, root_herd, dep_list, prev_dep):
    
    # visited
    for i in range(len(dep_list)):
        if root_herd in dep_list[i]:
            return
    curr_dep = partition_information["nodes"][root_herd]["deps"]

    # only interested in lower dependencies
    if curr_dep != prev_dep - 1:
        return
    dep_list[partition_information["nodes"][root_herd]["deps"]].append(root_herd)

    for net in partition_information["nodes"][root_herd]["anets"]:
        for herd in partition_information["nets"][net]["nodes"]:
            get_dep_nets(partition_information, herd, dep_list, curr_dep)
    
    return dep_list

def get_max_dependency(partition_information):
    max_dependency = -1
    max_key = -1
    for i in range(len(partition_information["nodes"])):
        if partition_information["nodes"][i]["deps"] > max_dependency:
            max_dependency = partition_information["nodes"][i]["deps"]
            max_key = i
    return max_key


def partition(partition_information, target_part_size, max_dependency):
    """Partitions herds into partition at or less than the target size. Groups
       all of the lowest dependency herds that are still unplaced first. Will
       not increase the current dependency level until all nodes in a dependency
       level have been placed, or are in the target_partition_herd_names
       list.

    Args:
        partition_information (dict): information about the nodes and nets
        target_part_size (int): size of target partitions
        max_dependency (int): max dependency level in the entire graph (across all
                              timeslots)

    Returns:
        list: herd names in the target partition
    """
    curr_size = 0
    target_partition_herd_names = []
    for dep in range(0, max_dependency + 1):
        # e.g., we don't want to add all dep 0, then no dep 1, then some dep 2's.
        keys = list(partition_information["nodes"].keys())
        dep_herds = []
        for key in keys:
            if partition_information["nodes"][key]["deps"] == dep:
                dep_herds.append(key)
        # Checks to make sure that all herds in a dependency level have been placed,
        # or are in the target_partition_herd_names list
        accounted_herds = 0
        for key in dep_herds:
            if partition_information["nodes"][key]["placed"] == -1:
                if partition_information["nodes"][key]["size"] + curr_size <= target_part_size:
                    target_partition_herd_names.append(key)
                    curr_size += partition_information["nodes"][key]["size"]
                    accounted_herds += 1
            else:
                accounted_herds += 1
        if accounted_herds != len(dep_herds):
            break

    update_net_cuts(partition_information, target_partition_herd_names)
    return target_partition_herd_names

def update_net_cuts(partition_information, herds_of_interest):
    """Updates the "broken" field in the partition information dictionary if there are
       nets in the different partitions. The herds checked are those that are in the
       anets field for each node in the herds of interest.

    Args:
        partition_information (dict): information about the nodes and nets
        herds_of_interest (list): herds to check to see if their nets are in different
                                  partitions
    """
    net_keys = []
    for i in range(len(partition_information["nodes"])):
        net_keys += partition_information["nodes"][i]["anets"]
    net_keys = list(set(net_keys))

    for i in range(len(net_keys)):
        part_herds_in_net = 0
        for herd in range(len(herds_of_interest)):
            if herds_of_interest[herd] in partition_information["nets"][i]["nodes"]:
                part_herds_in_net += 1
        if part_herds_in_net != len(partition_information["nets"][i]["nodes"]):
            partition_information["nets"][i]["broken"] = 1
        else:
            partition_information["nets"][i]["broken"] = 0
    return

def break_spanning_nets(partition_information, unplaced_herds):
    """Break nets if they span multiple timeslots

    Args:
        partition_information (_type_): _description_
        unplaced_herds (_type_): _description_
    """
    nets = []
    for herd in unplaced_herds:
        nets += partition_information["nodes"][herd]["anets"]
    for net in nets:
        partition_information["nets"][net]["broken"] = 1
    return

def swap_herds(partition_information, unplaced_herds, new_herd, old_herd):
    if old_herd not in unplaced_herds:
        return "old_herd not found in unplaced_herds list."
    else: 
        unplaced_herds.remove(old_herd)
        unplaced_herds.append(new_herd)
        update_net_cuts(partition_information, [old_herd, new_herd])
        return

def update_placed_status(partition_information, herds, timeslot):
    for herd in herds:
        partition_information["nodes"][herd]["placed"] = timeslot
    return

def get_total_size(partition_information):
    keys = partition_information["nodes"].keys()
    total_size = 0
    for key in range(len(keys)):
        total_size += partition_information["nodes"][key]["size"]
    return total_size