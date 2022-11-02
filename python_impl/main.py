# main.py
# Driver code for AIEplacer
from AIEplacer import *
import cProfile
import sys
import logging
import random
from naivePlacer import *

def runAIEPlacer():
    make_random = False
    logging.root.name = 'AIEplace'
    logging.basicConfig(level=logging.INFO,
                        format='[%(levelname)-7s] %(name)s - %(message)s',
                        stream=sys.stdout)
    
    # Create a design environment and run AIEplacer
    #design, grid = createRandomDesign()
    filename = "simple"
    design, grid = Design.readJSON("./benchmarks/"+filename+".json")

    placer = AIEplacer(grid, design, grid.num_cols)
    placer.run(999)


if __name__ == "__main__":
    random.seed(1)
    #cProfile.run('runAIEPlacer()')
    runAIEPlacer()

