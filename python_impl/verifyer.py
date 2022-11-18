import json
import math
from AIEplacer import Design, Coord

def get_validity(JSON_input, JSON_output):
    node_names_input = []
    with open(JSON_input) as file:
        JSON = json.load(file)
        node_names_input = list(JSON["node_sizes"].keys())

    with open(JSON_output) as file:
        design, grid, orig_num_cols, _ = Design.readJSON(JSON_input)
        JSON = json.load(file)
        temp = list(JSON["partition"])
        node_names_output = []
        for node in temp:
            node_names_output.append(node[1])
        if len(set(node_names_output)) != len(node_names_output):
            return f"######## ERROR: Invalid placement - Duplicating Nodes. #######"
        for i in range(len(node_names_input)):
            if node_names_input[i] not in node_names_output:
                return f"######## ERROR: Invalid placement. Not all nodes in input file are in output file (missing {node_names_input[i]}). #######"
        nodes_by_timeslot = []*grid.num_timeslots
        for _ in range(grid.num_timeslots): nodes_by_timeslot.append([])

        nodes = JSON["partition"]
        dependencies_satisfied = []
        for node in nodes:
            slot = math.floor(node[2][1] / grid.timeslot_cols)
            index = 0
            if node[1] in design.node_names:
                index = design.node_names.index(node[1])
            if slot < len(nodes_by_timeslot):
                nodes_by_timeslot[slot].append(index)

            # Update coords in design (to compute wirelen of placed nodes)
            design.coords[index] = Coord(node[2][0], node[2][1])
            dependencies_satisfied.append(-1)
        max_dep = 0

        # Check to see that for each node, it's predecessors have been placed in the same or 
        # previous timeslots
        for i in range(len(design.node_names)):
            if design.dependencies[i] > max_dep:
                max_dep = design.dependencies[i]
            if not design.predecessors[i]:
                dependencies_satisfied[i] = math.floor(design.coords[i].col / grid.timeslot_cols)
        for i in range(1, max_dep + 1):
            for herd in range(len(dependencies_satisfied)):
                if design.dependencies[herd] == i:
                    curr_timeslot = math.floor(design.coords[herd].col / grid.timeslot_cols)
                    for j in design.predecessors[herd]:
                        if dependencies_satisfied[j] > curr_timeslot:
                            return f"######## ERROR: Invalid placement @ Node {design.node_names[herd]}, timeslot {curr_timeslot}. Dependency not satisfied: {design.node_names[j]}, timeslot {dependencies_satisfied[j]}. #######"
                    dependencies_satisfied[herd] = curr_timeslot
        return "###### Legal placement; all dependencies satisfied ######"