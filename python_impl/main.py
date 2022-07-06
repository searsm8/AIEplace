# main.py
# Driver code for AIEplacer
from AIEplacer import *
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
        while(net.count(new_index)): # don't allow duplicates on a net!
            new_index = random.choice(range(node_count))
        net.append(new_index)
    return net

def initializeCoords(grid, node_count):
    #random initial position
    coords = []
    for i in range(node_count):
        coords.append( Coord( random.choice(range(round(grid.num_rows*.4), round(grid.num_rows*.6))), 
                              random.choice(range(round(grid.num_cols*.4), round(grid.num_cols*.6)) ) ) )
    return coords

if __name__ == "__main__":
    logging.root.name = 'AIEplace'
    logging.basicConfig(level=logging.INFO,
                        format='[%(levelname)-7s] %(name)s - %(message)s',
                        stream=sys.stdout)
    grid = Grid(8, 8)
    node_count = 4 * 4 
    coords = initializeCoords(grid, node_count)
    node_names = []
    for i in range(len(coords)):
        node_names.append("k"+str(i))
    nets = []
    for i in range(int(2+random.random()) * len(coords)):
        nets.append(makeRandomNet(len(coords)))
    design = Design(grid, coords, node_names, nets)

    placer = AIEplacer(grid, design) 
    placer.run(100)
    #placer.legalize()

