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


if __name__ == "__main__":
    logging.root.name = 'AIEplace'
    logging.basicConfig(level=logging.INFO,
                        format='[%(levelname)-7s] %(name)s - %(message)s',
                        stream=sys.stdout)
    grid = Grid(50, 8)
    x_coords = [
                24,24,24,24,24,24,24,24,
                24,24,24,24,24,24,24,24,
                25,25,25,25,25,25,25,25,
                25,25,25,25,25,25,25,25
                ]
    y_coords = [
                4,4,4,4,4,4,4,4,
                3,3,3,3,3,3,3,3,
                3,3,3,3,3,3,3,3,
                4,4,4,4,4,4,4,4
                ]
    node_names = []
    for i in range(len(x_coords)):
        node_names.append("k"+str(i))
    nets = []
    for i in range(len(x_coords)):
        nets.append(makeRandomNet(len(x_coords)))
    design = Design(grid, x_coords, y_coords, node_names, nets)

    placer = AIEplacer(grid, design) 
    placer.run(2)
    #placer.legalize()

