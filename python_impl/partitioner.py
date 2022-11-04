import pprint
import math
import random

def partition_initialization(graph, dependencies, node_sizes):
    partition_information = {}
    partition_information["nodes"] = {}
    partition_information["nets"] = {}
    for i in range(len(graph)):
        partition_information["nets"][i] = {"broken": 1, "nodes": []}
        for j in range(len(graph[i])):
            partition_information["nets"][i]["nodes"].append(graph[i][j])
    for i in range(len(dependencies)):
        partition_information["nodes"][i] = {"deps": dependencies[i], "size": node_sizes[i].row * node_sizes[i].col, "anets": [], "placed": -1}
        for j in range(len(graph)):
            for k in range(len(graph[j])):
                if i == graph[j][k]:
                    partition_information["nodes"][i]["anets"].append(j)

    
    # curr_part_herds, curr_part_size, curr_max_dep = partition(partition_information, target_part_size, max(dependencies))
    # random.shuffle(curr_part_herds)
    # print(curr_part_herds)

    # update_net_cuts(partition_information, curr_part_herds)
    # other_max_dep_herds = []
    # for i in range(len(partition_information["nodes"])):
    #     if partition_information["nodes"][i]["deps"] == curr_max_dep and \
    #        partition_information["nodes"][i]["placed"] == 0 and \
    #        i not in curr_part_herds:
           
    #        other_max_dep_herds.append(i)

    # ###TODO: Create swapping method

    return partition_information

def partition(partition_information, target_part_size, max_dependency):
    curr_size = 0
    curr_dep = 0
    target_partition_herd_names = []
    for dep in range(0, max_dependency + 1):
        # print(dep)
        # e.g., we don't want to add all dep 0, then no dep 1, then some dep 2's.
        keys = list(partition_information["nodes"].keys())
        dep_herds = []
        for key in keys:
            if partition_information["nodes"][key]["deps"] == dep:
                dep_herds.append(key)
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
    return target_partition_herd_names, curr_dep

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