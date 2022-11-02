# main.py
# Driver code for AIEplacer
from AIEplacer import *
import cProfile
import sys
import logging
import random
from naivePlacer import *

def runAIEPlacer(filename):
    make_random = False
    logging.root.name = 'AIEplace'
    logging.basicConfig(level=logging.INFO,
                        format='[%(levelname)-7s] %(name)s - %(message)s',
                        stream=sys.stdout)
    
    # Create a design environment and run AIEplacer
    #design, grid = createRandomDesign()
    design, grid, orig_num_cols = Design.readJSON("./benchmarks/"+filename+".json")

    placer = AIEplacer(grid, design, orig_num_cols)
    placer.run(999)

def runNaivePlacer(filename):
    
    design, grid, orig_num_cols = Design.readJSON("./benchmarks/" + filename + ".json")
    run_brandon_placement(grid.num_rows, orig_num_cols, design.node_sizes, design.node_names, design.dependencies)

if __name__ == "__main__":
    random.seed(1)
    filename = "simple"
    #cProfile.run('runAIEPlacer()')
    runNaivePlacer(filename)
    runAIEPlacer(filename)

