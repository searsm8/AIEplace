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
    logging.root.name = 'AIEplace'
    logging.basicConfig(level=logging.INFO,
                        format='[%(levelname)-7s] %(name)s - %(message)s',
                        stream=sys.stdout)

    design, grid, orig_num_cols, _ = Design.readJSON("./benchmarks/"+filename+".json")

    placer = AIEplacer(grid, design, orig_num_cols)
    _ = placer.run(999, "Force")

def runNaivePlacer(filename):
    
    design, grid, orig_num_cols, _ = Design.readJSON("./benchmarks/" + filename + ".json")
    run_brandon_placement(grid.num_rows, orig_num_cols, design.node_sizes, design.node_names, design.dependencies)

def runPartitionAndForce(filename):
    design, grid, orig_num_cols, map_dict = Design.readJSON("./benchmarks/" + filename + ".json")
    partition_information = partition_initialization(design.nets, design.dependencies, design.node_sizes)
    target_part_size = grid.num_rows * orig_num_cols
    max_iters = 10
    curr_timeslot = 0
    while True:
        if max_iters == curr_timeslot:
            break
        curr_part_herds= partition(partition_information, target_part_size, max(design.dependencies))
        
        if (not curr_part_herds):
            break
        
        new_design, herd_number_dict = Design.partition_design(partition_information, design, curr_part_herds)
        placer = AIEplacer(Grid(grid.num_rows, orig_num_cols), new_design, orig_num_cols)
        unplaced_herds = placer.run(999, "Partition")
        
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
        

if __name__ == "__main__":
    random.seed(1)
    for i in range(1):
        filename = "synthetic/synthetic_" + str(i)
        #cProfile.run('runAIEPlacer()')
        runAIEPlacer(filename)
        print("==================")
    #runNaivePlacer(filename)
    #runPartitionAndForce(filename)


