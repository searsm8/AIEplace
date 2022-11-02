# main.py
# Driver code for AIEplacer
from AIEplacer import *
import cProfile
import sys
import logging
import random
from naivePlacer import *
from partitioner import partition

def runAIEPlacer(filename):
    make_random = False
    logging.root.name = 'AIEplace'
    logging.basicConfig(level=logging.INFO,
                        format='[%(levelname)-7s] %(name)s - %(message)s',
                        stream=sys.stdout)
    
    # Create a design environment and run AIEplacer
    #design, grid = createRandomDesign()
    design, grid, orig_num_cols, _ = Design.readJSON("./benchmarks/"+filename+".json")
    print(design.dependencies)
    placer = AIEplacer(grid, design, orig_num_cols)
    placer.run(999)

def runNaivePlacer(filename):
    
    design, grid, orig_num_cols, _ = Design.readJSON("./benchmarks/" + filename + ".json")
    run_brandon_placement(grid.num_rows, orig_num_cols, design.node_sizes, design.node_names, design.dependencies)

def runPartitionAndForce(filename):
    design, grid, orig_num_cols, map_dict = Design.readJSON("./benchmarks/" + filename + ".json")
    # print(design.node_names)
    # print(design.dependencies)
    # print(design.nets)
    # print(map_dict)
    # for i in range(len(design.node_sizes)):
    #     print("[" + str(design.node_sizes[i].row) + ", " + str(design.node_sizes[i].col) + "]")
    # coords
    # node_names
    # node_sizes
    # dependencies
    # nets
    partition(design.nets, design.dependencies, map_dict, design.node_sizes, Grid(grid.num_rows, orig_num_cols))

if __name__ == "__main__":
    random.seed(1)
    filename = "simple"
    #cProfile.run('runAIEPlacer()')
    runAIEPlacer(filename)
    runNaivePlacer(filename)
    # runPartitionAndForce(filename)


