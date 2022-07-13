# main.py
# Driver code for AIEplacer
from AIEplacer import *
import cProfile
import sys
import logging
import random

def makeRandomNet(node_count):
    netsize = 2
    num = random.random()
    if(num < 0.8):
        netsize = 2
    elif(num < 0.86):
        netsize = 3
    elif(num < 0.9):
        netsize = 4
    elif(num < 0.935):
        netsize = 5
    elif(num < 0.96):
        netsize = 6
    elif(num < 0.98):
        netsize = 7
    elif(num < 0.99):
        netsize = 8
    else: netsize = 9

    net = []
    for i in range(netsize):
        new_index = random.choice(range(node_count))
        attempts = 0
        while(net.count(new_index) and attempts < 10): # don't allow duplicates on a net!
            new_index = random.choice(range(node_count))
            attempts += 1
        net.append(new_index)
    return net

def initializeCoords(grid, node_count):
    #random initial position
    coords = []
    for i in range(node_count):
        coords.append( Coord( random.choice(range(round(grid.num_rows*.4), round(grid.num_rows*.6))) + random.random(), 
                              random.choice(range(round(grid.num_cols*.4), round(grid.num_cols*.6))) + random.random() ) )
    return coords

def runAIEPlacer():
    logging.root.name = 'AIEplace'
    logging.basicConfig(level=logging.INFO,
                        format='[%(levelname)-7s] %(name)s - %(message)s',
                        stream=sys.stdout)
    
    # Create a design environment and run AIEplacer
    num_rows = 8*1
    num_cols = 8*5
    grid = Grid(num_rows, num_cols)
    node_count = int(num_rows * num_cols * 0.5)
    coords = initializeCoords(grid, node_count)
    node_names = []
    node_sizes = []
    for i in range(len(coords)):
        node_names.append("k"+str(i))
        node_sizes.append(Coord(.8, .8))
    nets = []
    for i in range(int((2+random.random()) * len(coords))):
    #for i in range(2):
        nets.append(makeRandomNet(len(coords)))
    design = Design(coords, node_names, node_sizes, nets)

    placer = AIEplacer(grid, design) 
    placer.run(1000)

if __name__ == "__main__":
    cProfile.run('runAIEPlacer()')