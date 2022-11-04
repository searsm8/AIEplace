# main.py
# Driver code for AIEplacer
from AIEplacer import *
import cProfile
import sys
import logging
import random
from naivePlacer import *
from partitioner import *

def runAIEPlacer(filename):
    make_random = False
    logging.root.name = 'AIEplace'
    logging.basicConfig(level=logging.INFO,
                        format='[%(levelname)-7s] %(name)s - %(message)s',
                        stream=sys.stdout)

    design, grid, orig_num_cols, _ = Design.readJSON("./benchmarks/"+filename+".json")
    placer = AIEplacer(grid, design, orig_num_cols)
    _ = placer.run(999)

def runNaivePlacer(filename):
    
    design, grid, orig_num_cols, _ = Design.readJSON("./benchmarks/" + filename + ".json")
    run_brandon_placement(grid.num_rows, orig_num_cols, design.node_sizes, design.node_names, design.dependencies)

def runPartitionAndForce(filename):
    design, grid, orig_num_cols, map_dict = Design.readJSON("./benchmarks/" + filename + ".json")
    partition_information = partition_initialization(design.nets, design.dependencies, design.node_sizes)
    target_part_size = grid.num_rows * orig_num_cols
    curr_max_dep = 0
    for t in range(3):
        curr_part_herds, curr_max_dep = partition(partition_information, target_part_size, max(design.dependencies))
        print(curr_part_herds)

        new_coords = []
        new_node_names = []
        new_node_sizes = []
        new_dependencies = []
        new_nets = []
        herd_number_dict = {}
        number = 0
        for i in curr_part_herds:
            new_coords.append(design.coords[i])
            herd_number_dict[i] = number
            new_node_names.append(number)
            number += 1
            new_node_sizes.append(design.node_sizes[i])
            # we don't want a right / left pulling dep. force
            new_dependencies.append(design.dependencies[i])
        net_keys = list(partition_information["nets"].keys())
        for net in range(len(net_keys)):
            unbroken_net = True
            for herd in partition_information["nets"][net]["nodes"]:
                if herd in curr_part_herds:
                    continue
                else:
                    unbroken_net = False
                    break
            if (unbroken_net):
                temp_net = partition_information["nets"][net]["nodes"]
                for node in range(len(temp_net)):
                    temp_net[node] = herd_number_dict[temp_net[node]]
                new_nets.append(temp_net)
        new_design = Design(new_coords, new_node_names, new_node_sizes, new_dependencies, new_nets)
        # print(new_node_names)
        # for i in range(len(new_design.coords)):
        #     print("coords: " + str(new_design.coords[i].row) + ", " + str(new_design.coords[i].col))
        # for i in range(len(new_design.coords)):
        #     print("size: " + str(new_design.node_sizes[i].row) + ", " + str(new_design.node_sizes[i].col))
        # print(new_design.nets)
        placer = AIEplacer(Grid(grid.num_rows, orig_num_cols), new_design, orig_num_cols)
        unplaced_herds = placer.run(999)
        
        
        # print(herd_number_dict)
        # print(unplaced_herds)
        for herd in range(len(unplaced_herds)):
            unplaced_herds[herd]
            for key, value in herd_number_dict.items():
                if value == unplaced_herds[herd]:
                    unplaced_herds[herd] = key
                    break
        # print(curr_part_herds)
        # print(unplaced_herds)
        curr_part_herds = list(set(curr_part_herds) - set(unplaced_herds))
        update_placed_status(partition_information, curr_part_herds, t)
        break_spanning_nets(partition_information, unplaced_herds)
        pprint.pprint(partition_information["nodes"])
        print(map_dict)
        

if __name__ == "__main__":
    random.seed(1)
    filename = "simple"
    #cProfile.run('runAIEPlacer()')
    # runAIEPlacer(filename)
    print("==================")
    # runNaivePlacer(filename)
    runPartitionAndForce(filename)


