# main.py
# Driver code for AIEplacer
from AIEplacer import *
import cProfile
import sys
import logging
import random
from naivePlacer import *
from partitioner import *

import metrics
from jsonCombiner import *

def runAIEPlacer(filename):
    logging.root.name = 'AIEplace'
    logging.basicConfig(level=logging.INFO,
                        format='[%(levelname)-7s] %(name)s - %(message)s',
                        stream=sys.stdout)

    design, grid, orig_num_cols, _ = Design.readJSON("./benchmarks/"+filename+".json")

    placer = AIEplacer(grid, design, orig_num_cols)
    _ = placer.run(999, "Force", 0)
    metrics.printMetrics("./benchmarks/"+filename+".json", "forcePlacer.json", method_label="Force")

def runNaivePlacer(filename):
    
    design, grid, orig_num_cols, _ = Design.readJSON("./benchmarks/" + filename + ".json")
    run_brandon_placement(grid.num_rows, orig_num_cols, design.node_sizes, design.node_names, design.dependencies)
    metrics.printMetrics("./benchmarks/"+filename+".json", "naive.json", method_label="Naive")

def runPartitionAndForce(filename, method):
    os.system(f"mkdir -p partition")
    design, grid, orig_num_cols, map_dict = Design.readJSON("./benchmarks/" + filename + ".json")
    partition_information = partition_initialization(design.nets, design.dependencies, design.node_sizes, design.times)
    total_size = 0
    for i in range(len(design.node_sizes)):
        total_size += design.node_sizes[i].row * design.node_sizes[i].col
    print("total size of all herds: " + str(total_size))
    target_part_size = grid.num_rows * orig_num_cols
    max_iters = 10
    curr_timeslot = 0
    curr_part_herds = []
    tolerance = 50
    if (method == "time"):
        max_dep_key = get_max_dependency(partition_information)
        longest_dep_list = build_longest_dep_list(partition_information, max_dep_key)

    while True:

        if max_iters == curr_timeslot:
            break
        printing_curr_herds = []
        if (method == "time"):
            curr_part_herds = time_part_new(partition_information, target_part_size, longest_dep_list, tolerance)
            for herd in curr_part_herds:
                printing_curr_herds.append(design.node_names[herd])
            # curr_part_herds = time_partition(partition_information, target_part_size, longest_dep_list, tolerance)
        else:
            curr_part_herds = partition(partition_information, target_part_size, max(design.dependencies))

        if (not curr_part_herds):
            break
        
        new_design, herd_number_dict = Design.partition_design(partition_information, design, curr_part_herds)
        map_to_name = {}
        for herd in range(len(new_design.node_names)):
            map_to_name[herd] = printing_curr_herds[herd]
        new_design.map_dict = map_to_name
        placer = AIEplacer(Grid(grid.num_rows, orig_num_cols), new_design, orig_num_cols)
        unplaced_herds = placer.run(999, "time", curr_timeslot)
        
        for herd in range(len(unplaced_herds)):
            unplaced_herds[herd]
            for key, value in herd_number_dict.items():
                if value == unplaced_herds[herd]:
                    unplaced_herds[herd] = key
                    break

        curr_part_herds = list(set(curr_part_herds) - set(unplaced_herds))
        update_placed_status(partition_information, curr_part_herds, curr_timeslot)
        break_spanning_nets(partition_information, unplaced_herds)
        curr_timeslot += 1
    # END while True
    
    super_partition, num_rows, num_cols = create_super_list()
    # +1 because 1 is subtracted when creating the switchbox
    write_to_json_super(super_partition, [num_rows, orig_num_cols], "superForcePartPlacer.json", curr_timeslot)
    metrics.printMetrics("./benchmarks/"+filename+".json", "forcePlacer.json", method_label="Partition")


if __name__ == "__main__":
    random.seed(1)
    filename = "synthetic/synthetic_5"
    #cProfile.run('runAIEPlacer()')
    # runAIEPlacer(filename)
    print("==================")
    # runNaivePlacer(filename)

    runPartitionAndForce(filename, "time")



