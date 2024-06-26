# main.py
# Driver code for AIEplacer
from AIEplacer import *
import cProfile
import sys
import logging
import random
from naivePlacer import *
from partitioner import *
#from longest_chain import get_min_runtime
from verifyer import get_validity

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
    placer.legalize_greedy("forcePlacer.json")
    copyJSON("forcePlacer.json")
    success, message = get_validity("benchmarks/" + filename + ".json", "forcePlacer.json")
    print(message)
    metrics.printMetrics("./benchmarks/"+filename+".json", "forcePlacer.json", success, method_label="Force",)

def runGreedyLegalizeOnly(filename):
    design, grid, orig_num_cols, _ = Design.readJSON("./benchmarks/"+filename+".json")

    placer = AIEplacer(grid, design, orig_num_cols)
    placer.legalize_greedy("greedyLegalizer.json")
    copyJSON("greedyLegalizer.json")
    success, message = get_validity("benchmarks/" + filename + ".json", "greedyLegalizer.json")
    print(message)
    metrics.printMetrics("./benchmarks/"+filename+".json", "greedyLegalizer.json", success, method_label="greedy")

def runNaivePlacer(filename):
    
    design, grid, orig_num_cols, _ = Design.readJSON("./benchmarks/" + filename + ".json")
    _ = run_brandon_placement(grid.num_rows, orig_num_cols, design, -1)
    copyJSON("naive.json")
    success, message = get_validity("benchmarks/" + filename + ".json", "naive.json")
    print(message)
    metrics.printMetrics("./benchmarks/"+filename+".json", "naive.json", success, method_label="Naive          ")

def runPartitionAndForce(filename, method):
    os.system(f"rm -rf partition")
    os.system(f"mkdir -p partition")
    design, grid, orig_num_cols, map_dict = Design.readJSON("./benchmarks/" + filename + ".json")
    partition_information = partition_initialization(design.nets, design.dependencies, design.node_sizes, design.times)
    total_size = 0
    for i in range(len(design.node_sizes)):
        total_size += design.node_sizes[i].row * design.node_sizes[i].col
    print("total size of all herds: " + str(total_size))
    target_part_size = grid.num_rows * orig_num_cols
    max_iters = 100
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
            for key, value in herd_number_dict.items():
                if value == unplaced_herds[herd]:
                    unplaced_herds[herd] = key
                    break

        curr_part_herds = list(set(curr_part_herds) - set(unplaced_herds))
        update_placed_status(partition_information, curr_part_herds, curr_timeslot)
        break_spanning_nets(partition_information, unplaced_herds)
        curr_timeslot += 1
    # END while True
    
    super_partition, num_rows, num_cols = create_super_list("partition")
    # +1 because 1 is subtracted when creating the switchbox
    write_to_json_super(super_partition, [num_rows, orig_num_cols], "superForcePartPlacer.json", curr_timeslot)
    copyJSON("superForcePartPlacer.json")
    success, message = get_validity("benchmarks/" + filename + ".json", "superForcePartPlacer.json")
    print(message)
    metrics.printMetrics("./benchmarks/"+filename+".json", "superForcePartPlacer.json", success, method_label="PartitionForce ")

def runPartitionAndGreedy(filename, method):
    os.system(f"rm -rf PartitionGreedy")
    os.system(f"mkdir -p PartitionGreedy")
    design, grid, orig_num_cols, orig_map_dict = Design.readJSON("./benchmarks/" + filename + ".json")
    total_size = 0
    for i in range(len(design.node_sizes)):
        total_size += design.node_sizes[i].row * design.node_sizes[i].col
    print("total size of all herds: " + str(total_size))
    partition_information = partition_initialization(design.nets, design.dependencies, design.node_sizes, design.times)
    target_part_size = grid.num_rows * orig_num_cols
    tolerance = 50
    max_dep_key = get_max_dependency(partition_information)
    longest_dep_list = build_longest_dep_list(partition_information, max_dep_key)
    curr_timeslot = 0
    while True:
        print(f"==================Current timeslot: {curr_timeslot}================")
        curr_part_herds = []
        if curr_timeslot > 100:
            break
        printing_curr_herds = []
        if (method == "partition"):
            curr_part_herds = time_part_new(partition_information, target_part_size, longest_dep_list, tolerance)
            for herd in curr_part_herds:
                printing_curr_herds.append(design.node_names[herd])
        if not curr_part_herds:
            break
        new_design, herd_number_dict = Design.partition_design(partition_information, design, curr_part_herds)
        new_number_to_old_number = {}
        for i in range(len(new_design.node_names)):
            new_number_to_old_number[i] = curr_part_herds[i]
        map_to_name = {}
        for herd in range(len(new_design.node_names)):
            map_to_name[herd] = printing_curr_herds[herd]
        new_design.map_dict = map_to_name

        unplaced_herds = run_brandon_placement(grid.num_rows, orig_num_cols, new_design, curr_timeslot)
        unplaced_herds = [item for sublist in unplaced_herds for item in sublist]
        temp = []

        for herd in range(len(unplaced_herds)):
            temp.append(new_number_to_old_number[unplaced_herds[herd].name])
        print("unplaced herds: " + str(temp))
        curr_part_herds = list(set(curr_part_herds) - set(temp))
       
        update_placed_status(partition_information, curr_part_herds, curr_timeslot)
       
        curr_timeslot += 1
    print("finished!")
    super_partition, num_rows, num_cols = create_super_list("PartitionGreedy")
    write_to_json_super(super_partition, [num_rows, orig_num_cols], "superGreedyPlacer.json", curr_timeslot)
    copyJSON("superGreedyPlacer.json")
    success, message = get_validity("benchmarks/" + filename + ".json", "superGreedyPlacer.json")
    print(message)
    metrics.printMetrics("./benchmarks/"+filename+".json", "superGreedyPlacer.json", success, method_label="GreedyPartition")

def copyJSON(outputJSONfile):
    '''Copies an output JSON file to appropriate folder. '''
    outputJSONname = outputJSONfile.split(".")[0]
    output_folder = os.path.join("results/JSON_outputs", outputJSONname)
    os.system(f"mkdir -p " + output_folder)
    num = 0
    output_file = f"{output_folder}/{outputJSONname}_{num}.json"
    while os.path.exists(output_file):
        num += 1
        output_file = f"{output_folder}/{outputJSONname}_{num}.json"
    os.system(f"cp {outputJSONfile} {output_file}")


if __name__ == "__main__":
    for i in range(0, 10):
        random.seed(1)
        filename = f"synthetic/synthetic_{i}"
        # filename = "simple"
        #cProfile.run('runAIEPlacer()')
        runNaivePlacer(filename)
        runPartitionAndGreedy(filename, "partition")
        runPartitionAndForce(filename, "time")
        runAIEPlacer(filename)
        runGreedyLegalizeOnly(filename)

