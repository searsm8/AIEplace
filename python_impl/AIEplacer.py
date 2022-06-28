# AIEplacer.py
# Implement the a rudimentary ePlace algorithm to place designs
# Intended only to run on small designs, such as a 50x8 AIE array

import AIEmath.computeTerm
import AIEmath.customDCT
import math
import numpy as np
import logging

class Grid:
    def __init__(self, num_rows: int, num_cols: int) -> None:
        self.num_rows = num_rows
        self.num_cols = num_cols
        self.vals = []
        for i in range(num_rows):
            self.vals.append([0]*num_cols)

class Design:
    # primary lists which hold design information
    def __init__(self, grid, x_coords, y_coords, node_names, nets) -> None:
        self.grid = grid
        self.x_coords = x_coords
        self.y_coords = y_coords
        self.node_names = node_names
        self.nets = nets # each net is a list of node indices
                    # e.g. [4,7,9] means nodes 4, 7, and 9 are connect by a net
        
class AIEplacer:
    def __init__(self, grid, design) -> None:
        self.grid = grid
        self.design = design
        self.bin_width= 2 # arbitrary
        self.bin_height= 2 # arbitrary
        self.bin_grid = Grid(int(grid.num_rows/self.bin_width), int(grid.num_cols/self.bin_height))
    
    def computeBinDensities(self):
        '''update bin_grid with density values based on where nodes are'''
        for i in range(self.bin_grid.num_rows):
            self.bin_grid.vals[i] = [0]*self.bin_grid.num_cols

        for i in range(len(self.design.x_coords)):
            x = self.design.x_coords[i]
            y = self.design.y_coords[i]
            self.bin_grid.vals[int(x/self.bin_width)-1][int(y/self.bin_height)-1] += \
                                    1 / (self.bin_width*self.bin_height)

    def run(self, iterations):
        ''' Runs the ePlace algorithm'''
        logging.info("Begin AIEplace")
        for iter in range(iterations):
            logging.info(f"***Begin Iteration {iter}***")
            # Compute gradients
            # compute a+/- terms

            # compute b+/- terms

            # compute c+/- terms

            # HPWL WA gradient

            
            # Density Gradients
            self.computeBinDensities()
            print(f"Densities:")
            print(self.bin_grid.vals)
            my_dct = AIEmath.customDCT.dct_2d(self.bin_grid.vals)
            # adjustment
            for i in range(len(my_dct)):
                my_dct[i][0] *= 0.5
            for i in range(len(my_dct[0])):
                my_dct[0][i] *= 0.5
            for i in range(len(my_dct)):
                for j in range(len(my_dct[0])):
                    my_dct[i][j] *= 4.0 / len(my_dct) / len(my_dct[0])

            electroPhi    = np.zeros((len(my_dct), len(my_dct[0])))
            electroForceX = np.zeros((len(my_dct), len(my_dct[0])))
            electroForceY = np.zeros((len(my_dct), len(my_dct[0])))
            # w_u and w_v factors
            for u in range(len(my_dct)):
                for v in range(len(my_dct[0])):
                    w_u = 1*math.pi*u / len(my_dct) # why not 2*pi?
                    w_u2 = w_u*w_u
                    w_v = 1*math.pi*v / len(my_dct[0])
                    w_v2 = w_v*w_v
                    if u == 0 and v == 0:
                        electroPhi[u][v] = 0
                    else:
                        electroPhi[u][v] = my_dct[u][v] / (w_u2 + w_v2)
                        electroForceX[u][v] = electroPhi[u][v] * w_u
                        electroForceY[u][v] = electroPhi[u][v] * w_v

            # Update node locations

        logging.info("End AIEplace")
    
    def legalize(self):
        ''' After running placement, legalize the design'''
        logging.info("Begin legalization")
        logging.info("End legalization")
        pass