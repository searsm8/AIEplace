# AIEplacer.py
# Implement the a rudimentary ePlace algorithm to place designs
# Intended only to run on small designs, such as a 50x8 AIE array

import AIEmath.computeTerm
import AIEmath.customDCT
import math
import numpy as np
import logging

class Coord:
    def __init__(self, x, y):
        self.x = x
        self.y = y

class Grid:
    def __init__(self, num_rows: int, num_cols: int) -> None:
        self.num_rows = num_rows
        self.num_cols = num_cols
        self.vals = []
        for i in range(num_rows):
            self.vals.append([0]*num_cols)

class Design:
    # primary lists which hold design information
    def __init__(self, grid, coords, node_names, nets) -> None:
        self.coords = coords
        self.node_names = node_names
        self.nets = nets # each net is a list of node indices
                    # e.g. [4,7,9] means nodes 4, 7, and 9 are connect by a net
        
class AIEplacer:
    def __init__(self, grid, design) -> None:
        self.grid = grid
        self.design = design
        self.bin_width = 4 # arbitrary
        self.bin_height= 4 # arbitrary
        self.bin_grid = Grid(int(grid.num_rows/self.bin_height), int(grid.num_cols/self.bin_width))
    
    def computeActualHPWL(self):
        hpwls = []
        for net in self.design.nets:
            hpwl = 0
            min_x = self.grid.num_cols; min_y = self.grid.num_rows
            max_x = 0; max_y = 0
            for i in net:
                coord = self.design.coords[i]
                if coord.x < min_x:
                    min_x = coord.x
                if coord.y < min_y:
                    min_y = coord.y
                if coord.x > max_x:
                    max_x = coord.x
                if coord.y > max_y:
                    max_y = coord.y
            hpwls.append( (max_x - min_x, max_y - min_y) )
        return hpwls
    
    def computeBinDensities(self):
        '''update bin_grid with density values based on where nodes are'''
        for i in range(self.bin_grid.num_rows):
            self.bin_grid.vals[i] = [0]*self.bin_grid.num_cols

        for i in range(len(self.design.coords)):
            row = self.design.coords[i].x
            col = self.design.coords[i].y
            self.bin_grid.vals[int(row/self.bin_height)][int(col/self.bin_width)] += \
                                    1 / (self.bin_width*self.bin_height)

    def run(self, iterations):
        ''' Runs the ePlace algorithm'''
        logging.info("Begin AIEplace")
        for iter in range(iterations):
            logging.root.name = 'ITER ' + str(iter)
            logging.info(f"***Begin Iteration {iter}***")
            # Compute gradients
            # TODO: include the min/max terms to reduce computational burden
            # compute a+/- terms
            gamma = 4.
            a_plus  = []
            a_minus = []
            for coord in self.design.coords:
                a_plus.append( Coord(math.exp(coord.x/gamma), math.exp(coord.y/gamma)) )
                a_minus.append( Coord(math.exp(-coord.x/gamma), math.exp(-coord.y/gamma)) )

            # compute b+/- terms
            b_plus  = []
            b_minus = []
            for net in self.design.nets:
                b_plus.append( Coord(sum([a_plus[i].x for i in net]), sum([a_plus[i].x for i in net]) ) )
                b_minus.append( Coord(sum([a_minus[i].y for i in net]), sum([a_minus[i].y for i in net]) ) )

            # compute c+/- terms
            c_plus  = []
            c_minus = []
            for net in self.design.nets:
                c_plus.append( Coord(sum([self.design.coords[i].x*a_plus[i].x for i in net]), sum([self.design.coords[i].y*a_plus[i].x for i in net]) ) )
                c_minus.append( Coord(sum([self.design.coords[i].x*a_minus[i].y for i in net]), sum([self.design.coords[i].y*a_minus[i].y for i in net]) ) )

            # HPWL WA gradient
            hpwl_WA = []
            hpwl_actual = self.computeActualHPWL() 
            hpwl_gradient = []
            for i in range(len(self.design.coords)):
                hpwl_gradient.append(Coord(0,0))
            for i in range(len(self.design.nets)):
                hpwl_WA.append( Coord( AIEmath.computeTerm.WA_hpwl(b_plus[i].x, c_plus[i].x, b_minus[i].x, c_minus[i].x), \
                              AIEmath.computeTerm.WA_hpwl(b_plus[i].y, c_plus[i].y, b_minus[i].y, c_minus[i].y) ) )
                for j in range(len(self.design.nets[i])):
                    node_index = self.design.nets[i][j]
                    hpwl_gradient[node_index].x += AIEmath.computeTerm.WA_partial(self.design.coords[node_index].x, a_plus[node_index].x, b_plus[i].x, c_plus[i].x, a_minus[node_index].x, b_minus[i].x, c_minus[i].x, gamma) 
                    hpwl_gradient[node_index].y += AIEmath.computeTerm.WA_partial(self.design.coords[node_index].y, a_plus[node_index].y, b_plus[i].y, c_plus[i].y, a_minus[node_index].y, b_minus[i].y, c_minus[i].y, gamma) 
                    #hpwl_gradient[i].append( Coord( AIEmath.computeTerm.WA_partial(self.design.coords[i].x, a_plus[node_index].x, b_plus[i].x, c_plus[i].x, a_minus[node_index].x, b_minus[i].x, c_minus[i].x, gamma), \
                    #          AIEmath.computeTerm.WA_partial(self.design.coords[i].y, a_plus[node_index].y, b_plus[i].y, c_plus[i].y, a_minus[node_index].y, b_minus[i].y, c_minus[i].y, gamma) ) )
            for i in range(len(self.design.nets)):
                print(f"net {i}: {self.design.nets[i]}")
                for j in range(len(self.design.nets[i])):
                    print(f"({self.design.coords[self.design.nets[i][j]].x}, {self.design.coords[self.design.nets[i][j]].y})", end=", ")
                print()
                print()
            
            for i in range(len(self.design.nets)):
                print(f"NET[{i}]")
                print(f"HPWL estimate: ({hpwl_WA[i].x}, {hpwl_WA[i].y})")
                print(f"HPWL actual: {hpwl_actual[i]}")
                print()

            for i in range(len(self.design.coords)):
                logging.info(f"HPWL gradient[{i}] ({self.design.coords[i].x}, {self.design.coords[i].y}): ({hpwl_gradient[i].x}, {hpwl_gradient[i].y})")
            
            # Density Gradients
            self.computeBinDensities()
            print(f"Densities:")
            for i in range(len(self.bin_grid.vals)):
                print(self.bin_grid.vals[-i-1])
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

            print(f"electroForceX:")
            for i in range(len(electroForceX)):
                print(electroForceX[-i-1])

            # Update node locations
            print(f"Update coords:")
            density_penalty = round(iter/10) / 10
            for i in range(len(self.design.coords)):
                self.design.coords[i].x -= hpwl_gradient[i].x + density_penalty*0

        logging.info("End AIEplace")
    
    def legalize(self):
        ''' After running placement, legalize the design'''
        logging.info("Begin legalization")
        logging.info("End legalization")
        pass