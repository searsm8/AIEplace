import pprint
import random
from copy import deepcopy
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
    
    # print(curr_part_herds)
    return partition_information

def time_part_structure_init(partition_information, longest_dep_list):
    """Creates structures + sets initial size & size needed for the time partitioner

    Args:
        partition_information (dict): information about the nodes and nets
        longest_dep_list (list(list(int))): each sublist contains herd @ each dependency level e.g.,
                                            sublist 0 contains herds at dep. 0, sub 1 @ dep 1, etc.
                                            for the longest chain (chain w/ highest dependency)

    Returns:
        dependencies (list(list(int))): each sublist contains herd @ each dependency level e.g.,
                                        sublist 0 contains herds at dep. 0, sub 1 @ dep 1, etc.
        curr_base_herd (int): herd to base all "acceptable" times off of
        initial_dep (int): dependency of curr_base_herd
        curr_max_time (int): time of curr_base_herd
        curr_size (int): size of curr_base_herd
        partition_herds (list(list(int))): each sublist contains herd @ each dependency level e.g.,
                                           sublist 0 contains herds at dep. 0, sub 1 @ dep 1, etc.
                                           for herds that have added to the partition
        chains (list(list(int))): 
    """
    curr_size = 0
    dependencies = []
    partition_herds = []
    chains = []
    for _ in range(len(longest_dep_list)):
        dependencies.append([])
        partition_herds.append([])
    for herd in range(len(partition_information["nodes"])):
        if partition_information["nodes"][herd]["placed"] != -1:
            continue
        dependencies[partition_information["nodes"][herd]["deps"]].append(herd)
    
    curr_base_herd = -1

    for dep in range(len(longest_dep_list)):
        for herd in longest_dep_list[dep]:
            if partition_information["nodes"][herd]["placed"] == -1:
                curr_base_herd = herd
                break
        # Check to make sure that we've placed all nodes at the lowest dependency
        # level before we move to the next level of dependency as our base.
        if curr_base_herd == -1:
            for herd in range(len(partition_information["nodes"])):
                if partition_information["nodes"][herd]["placed"] == -1 and \
                   partition_information["nodes"][herd]["deps"] == dep:
                   curr_base_herd = herd
                   break
        if curr_base_herd > -1:
            break
    if curr_base_herd == -1:
        curr_dep = -1
        for herd in partition_information["nodes"]:
            if partition_information["nodes"][herd]["deps"] > curr_dep and \
               partition_information["nodes"][herd]["placed"] == -1:
               curr_base_herd = herd
    if curr_base_herd == -1:
        return _, curr_base_herd, _, _, _, _, _
    # curr_base_herd = longest_dep_list[0][random.randint(0, len(longest_dep_list[0]) - 1)]
    initial_dep = partition_information["nodes"][curr_base_herd]["deps"]
    
    partition_herds[initial_dep].append(curr_base_herd)
    for _ in range(get_fanout(partition_information, curr_base_herd)):
        chains.append([curr_base_herd])
    curr_max_time = partition_information["nodes"][curr_base_herd]["time"]
    curr_size += partition_information["nodes"][curr_base_herd]["size"]

    return dependencies, curr_base_herd, initial_dep, curr_max_time, curr_size, partition_herds, chains

# def update_chain(partition_information, chains, herd):
#     """Adds a herd to the correct chains

#     Returns:
#         link_added (boolean): if a chain was updated or not
#     """
#     # print("fanout: "+ str(get_fanout(partition_information, herd)))
#     link_added = False
#     num_iters = len(chains)
#     for chain in range(num_iters):
#         if herd in chains[chain]:
#             continue
#         for net in range(len(partition_information["nets"])):
#             if chains[chain][-1] in partition_information["nets"][net]["nodes"] and \
#                 herd in partition_information["nets"][net]["nodes"]:

#                 # Makes sure that for fan out nodes, we don't add nodes to chains 
#                 # that aren't effected
#                 fanout = -1
#                 for node in reversed(chains[chain]):
#                     if partition_information["nodes"][node]["deps"] == partition_information["nodes"][herd]["deps"] - 1:
#                         fanout = get_fanout(partition_information, node)
#                 if fanout > 1:
#                     count = 0
#                     for i in range(len(partition_information["nets"][net]["nodes"])):
#                         if (partition_information["nets"][net]["nodes"][i] in chains[chain]):
#                             count += 1
#                     if count > 1:
#                         break

#                 # this is to deal with fanouts. We don't want to build multiple identical chains;
#                 # we want chains that feed into different output nodes.
#                 # print("this chain: " + str(chains[chain]))
#                 new_chain = deepcopy(chains[chain])
#                 new_chain.append(herd)
#                 if new_chain in chains:
#                     break
#                 chains[chain].append(herd)
#                 link_added = True
#                 break
#     return link_added


def get_fanout(partition_information, node):
    """Finds the max fanout of a node

    Args:
        partition_information (_type_): _description_
        node (_type_): _description_
    Returns:
        # of fanouts from node
    """
    for net in range(len(partition_information["nets"])):
        if partition_information["nets"][net]["nodes"][0] == node:
            return len(partition_information["nets"][net]["nodes"]) - 1
    return 1


def time_part_new(partition_information, target_part_size, longest_dep_list, tolerance):
    dependencies, curr_base_herd, initial_dep, curr_max_time, curr_size, partition_herds, chains = \
        time_part_structure_init(partition_information, longest_dep_list)
    if curr_base_herd == -1:
        return []
    condition = True
    partition_herds = [curr_base_herd]
    while condition:
        # print("current base herd: " + str(curr_base_herd))
        for herd in range(len(partition_information["nodes"])):
            if partition_information["nodes"][herd]["placed"] != -1 or \
                herd in partition_herds or \
                partition_information["nodes"][herd]["size"] + curr_size > target_part_size:
                continue
            chains = []
            new_chains = get_chains(partition_information, herd, chains, [])
            new_time = get_time(partition_information, partition_herds, new_chains)
            if new_time - tolerance < curr_max_time:
                partition_herds.append(herd)
                curr_size += partition_information["nodes"][herd]["size"] 
        if curr_size >= target_part_size * .8:
            condition = False
        elif all_placed(partition_information, partition_herds):
            condition = False
        else: 
            lowest_dep = 1000
            for dep in range(len(longest_dep_list)):
                for herd in longest_dep_list[dep]:
                    if partition_information["nodes"][herd]["placed"] != -1 or \
                       herd in partition_herds:
                       continue
                    lowest_dep = partition_information["nodes"][herd]["deps"]
                    curr_base_herd = herd
                    break
                if lowest_dep != -1:
                    break
            for herd in range(len(partition_information["nodes"])):
                if partition_information["nodes"][herd]["placed"] != -1 or \
                   herd in partition_herds or \
                   partition_information["nodes"][herd]["deps"] >= lowest_dep or \
                   partition_information["nodes"][herd]["size"] + curr_size > target_part_size:
                   
                   continue
                else:
                    curr_base_herd = herd
                    break
            curr_size += partition_information["nodes"][curr_base_herd]["size"]
            partition_herds.append(curr_base_herd)
            new_chains = get_chains(partition_information, curr_base_herd, chains, [])
            new_time = get_time(partition_information, partition_herds, new_chains)
            curr_max_time = new_time
    print("current max time: " + str(curr_max_time))
    return partition_herds

def all_placed(partition_information, partition_herds):
    count = 0
    for herd in range(len(partition_information["nodes"])):
        if partition_information["nodes"][herd]["placed"] != -1 or \
            herd in partition_herds:
            count += 1
    if count == len(partition_information["nodes"]):
        return True
    return False

# def time_partition(partition_information, target_part_size, longest_dep_list, tolerance):
#     """Does partitioning based on estimated completion time

#     """

#     dependencies, curr_base_herd, initial_dep, curr_max_time, curr_size, partition_herds, chains = \
#         time_part_structure_init(partition_information, longest_dep_list)
#     if curr_base_herd == -1:
#         return []
#     # This adds all of the herds that are the same dependency to the partition_herds list, as long
#     # as they execute in less time than the initial herd + some tolerance.
#     for dep in range(initial_dep, min(initial_dep + 1, len(dependencies))):
#         for herd in range(len(dependencies[dep])):
#             if curr_size + partition_information["nodes"][dependencies[dep][herd]]["size"] > target_part_size or \
#                partition_information["nodes"][dependencies[dep][herd]]["time"] > curr_max_time + tolerance or \
#                dependencies[dep][herd] == curr_base_herd:
#                continue
#             else:
#                 partition_herds[dep].append(dependencies[dep][herd])
#                 curr_size += partition_information["nodes"][dependencies[dep][herd]]["size"]
#                 chains.append([dependencies[dep][herd]])

#     breaking_condition = False
#     iteration = 0

#     while not breaking_condition:
#         if iteration == 1:
#             break
#         iteration += 1
#         # Loops over higher dependencies than we started with. Ensures that the new tiles will fit, 
#         # comply with program order (dependencies already placed, or already in placed_list), and
#         # that the execution time is within some max tolerance for the "chain" e.g., if A + B + C are
#         # in the same partition, and A depends on B but C is independent, ensure that the time for 
#         # A + B is approximately the same time as C (in order to reduce idle time).

#         for dep in range(initial_dep + 1, len(dependencies)):
#             for herd in dependencies[dep]:
#                 if partition_information["nodes"][herd]["size"] + curr_size > target_part_size or \
#                    node_in_partition(partition_herds, herd) or \
#                    partition_information["nodes"][herd]["placed"] != -1:
#                     continue
#                 legal_add = True
#                     # quit()
#                 for anet in partition_information["nodes"][herd]["anets"]:
#                     for node in partition_information["nets"][anet]["nodes"]:
#                         if node == herd:
#                             continue
#                         # We don't care if the higher dep. nodes are placed in a net
#                         elif partition_information["nodes"][node]["deps"] >= dep:
#                             continue
#                         # if we've already placed it in a partition / placement
#                         elif (partition_information["nodes"][node]["placed"] != -1 or \
#                            node_in_partition(partition_herds, node)): 
#                             # add the chain time, compare to the initial time
#                             # finding the relevant chain, seeing how long it takes:
#                             acceptable_times = 0
#                             node_occurances_in_chain = 0
#                             for i in range(len(chains)):
#                                 if node in chains[i]:
#                                     node_occurances_in_chain += 1
#                                     chain_time = 0
#                                     for j in range(len(chains[i])):
#                                         chain_time += partition_information["nodes"][chains[i][j]]["time"]
#                                     if chain_time + partition_information["nodes"][herd]["time"] < curr_max_time + tolerance:
#                                         acceptable_times += 1
#                                     else:
#                                         break
#                             if node_occurances_in_chain > 0 and acceptable_times == node_occurances_in_chain:
#                             # if acceptable_times == node_occurances_in_chain:
#                                 continue
#                             elif node_occurances_in_chain == 0:
#                                 continue
#                             else:
#                                 legal_add = False
#                                 break
#                         else:
#                             legal_add = False
#                             break
#                     if legal_add == False:
#                         break
#                 if legal_add == False:
#                     continue
#                 else:
#                     # if the herd we are trying to add and the previous herd in the chain are both
#                     # in any of the same nets, we know there is a dependence.
#                     link_added = update_chain(partition_information, chains, herd)
#                     # Not sure if this is in the right place or not to add a chain
#                     if not link_added:
#                         for _ in range(get_fanout(partition_information, herd)):
#                             chains.append([herd])
#                     partition_herds[dep].append(herd)
#                     curr_size += partition_information["nodes"][herd]["size"]
#         if curr_size >= target_part_size * .8:
#             break
#         else:
#             # Add another node from the longest dependency list
#             for dep in range(len(longest_dep_list)):
#                 for node in longest_dep_list[dep]:
#                     if partition_information["nodes"][node]["placed"] == -1 and \
#                     not node_in_partition(partition_herds, node):
#                         if curr_size + partition_information["nodes"][node]["size"] > target_part_size:
#                             return list(set([item for sublist in partition_herds for item in sublist]))
#                         else:
#                             partition_herds[partition_information["nodes"][node]["deps"]].append(node)
#                             curr_size += partition_information["nodes"][node]["size"]

#                             link_added = update_chain(partition_information, chains, node)
#                             if not link_added:
#                                 for _ in range(get_fanout(partition_information, node)):
#                                     chains.append([node])

#                             # Update new max time
#                             for chain in chains:
#                                 curr_chain_time = 0
#                                 for link in chain:
#                                     curr_chain_time += partition_information["nodes"][link]["time"]
#                                 if curr_chain_time > curr_max_time:
#                                     curr_max_time = curr_chain_time
                    

#     # shuffle herds
#     print(chains)
#     # print(dependencies)
#     print("curr_size" + str(curr_size))
    
#     # flatten list
#     return list(set([item for sublist in partition_herds for item in sublist]))

def node_in_partition(curr_part, node):
    for i in range(len(curr_part)):
        if node in curr_part[i]:
            return True
    return False

def get_chains(partition_information, base_node, chains, parent_chain):

    if partition_information["nodes"][base_node]["deps"] == 0:
        parent_chain.insert(0, base_node)
        chains.append(parent_chain)
        return
    for net in range(len(partition_information["nets"])):
        # if it's a sink and not a source
        if base_node in partition_information["nets"][net]["nodes"][1:]:
            parent_chain_new = deepcopy(parent_chain)
            parent_chain_new.insert(0, base_node)
            get_chains(partition_information, partition_information["nets"][net]["nodes"][0], chains, parent_chain_new)
    return chains

def get_time(partition_information, partition_herds, chains):
    max_time = 0
    if not chains:
        return max_time
    for chain in range(len(chains)):
        time = 0
        for herd in chains[chain]:
            if partition_information["nodes"][herd]["placed"] != -1:
                continue
            time += partition_information["nodes"][herd]["time"]
        if time > max_time:
            max_time = time
    return max_time

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