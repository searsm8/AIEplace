# AIEplacer.py
# Implement the a rudimentary ePlace algorithm to place designs
# Intended only to run on small designs, such as a 5.row8 AIE array

import AIEmath.computeTerm
import AIEmath.customDCT
import sys, os
import prettyPrint
import math
import time
import numpy as np
import logging
import PlaceDrawer

class Coord:
    def __init__(self, row, col):
        self.row = row
        self.col = col

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
        self.bin_width = 1 # arbitrary
        self.bin_height= 1 # arbitrary
        self.bin_grid = Grid(int(grid.num_rows/self.bin_height), int(grid.num_cols/self.bin_width))
    
    def computeActualHPWL(self):
        hpwls = []
        for net in self.design.nets:
            hpwl = 0
            min_row = self.grid.num_rows; min_col = self.grid.num_cols
            max_row = 0; max_col = 0
            for i in net:
                coord = self.design.coords[i]
                if coord.row < min_row:
                    min_row = coord.row
                if coord.col < min_col:
                    min_col = coord.col
                if coord.row > max_row:
                    max_row = coord.row
                if coord.col > max_col:
                    max_col = coord.col
            hpwls.append( Coord(max_row - min_row, max_col - min_col) )
        return hpwls
    
    def computeBinDensities(self):
        '''update bin_grid with density values based on where nodes are'''
        for i in range(self.bin_grid.num_rows):
            self.bin_grid.vals[i] = [0]*self.bin_grid.num_cols

        for i in range(len(self.design.coords)):
            row = self.design.coords[i].row
            col = self.design.coords[i].col
            #print(f"({row}, {col})")
            #print(f"({int(row/self.bin_height)}, {int(col/self.bin_width)})")
            #print(f"bin_grid.vals[{self.bin_grid.num_rows}][{self.bin_grid.num_cols}]")
            self.bin_grid.vals[min(self.bin_grid.num_rows-1, int(row/self.bin_height))] \
                                    [min(self.bin_grid.num_cols-1,int(col/self.bin_width))] += \
                                    1 / (self.bin_width*self.bin_height)

    def getNetBinDensities(self, net_index):
        '''Returns a bin array populated with a single net'''
        net_bins = []
        for i in range(self.bin_grid.num_rows):
            net_bins.append([0]*self.bin_grid.num_cols)
        for node_index in self.design.nets[net_index]:
            row = self.design.coords[node_index].row
            col = self.design.coords[node_index].col
            net_bins[min(self.bin_grid.num_rows-1, int(row/self.bin_height))] \
                                    [min(self.bin_grid.num_cols-1,int(col/self.bin_width))] += \
                                    1 / (self.bin_width*self.bin_height)
        return net_bins


    def run(self, iterations):
        ''' Runs the ePlace algorithm'''
        logging.info("Begin AIEplace")
        os.system("mkdir images"); os.system("cd images")
        design_run = 0
        while(os.path.exists(f"images/run_{design_run}")):
            design_run += 1
        os.system(f"mkdir images/run_{design_run}"); os.system(f"cd images/run_{design_run}")
        
        for iter in range(iterations):
            logging.root.name = 'ITER ' + str(iter)
            logging.info(f"***Begin Iteration {iter}***")

            #use PlaceDrawer to export an image
            if(iter%10 == 0):
                filename=f"{time.strftime('%y%m%d_%H%M')}_iter_{iter}.png"
                filename = os.path.join("images", f"run_{design_run}", filename)
                pos= [self.design.coords[i].col for i in range(len(self.design.coords))]  + \
                    [self.design.coords[i].row for i in range(len(self.design.coords))] 
                print(pos)

                ret = PlaceDrawer.PlaceDrawer.forward(
                        pos= [10*self.design.coords[i].col for i in range(len(self.design.coords))]  + 
                             [10*self.design.coords[i].row for i in range(len(self.design.coords))], 
                        node_size_x=[8 for i in range(len(self.design.coords))], 
                        node_size_y=[8 for i in range(len(self.design.coords))], 
                        pin_offset_x=[0 for i in range(len(self.design.coords))],
                        pin_offset_y=[0 for i in range(len(self.design.coords))],
                        pin2node_map={}, 
                        xl=0, yl=0, xh=80, yh=80, 
                        site_width=1, row_height=1,
                        bin_size_x=20, bin_size_y=20, 
                        num_movable_nodes=0, num_filler_nodes=0, 
                        filename=filename
                        )

            
            # Compute gradients
            # TODO: include the min/m.row terms to reduce computational burden
            # compute a+/- terms
            gamma = 4.
            a_plus  = []
            a_minus = []
            for i in range(len(self.design.coords)):
                coord = self.design.coords[i]
                a_plus.append( Coord(math.exp(coord.row/gamma), math.exp(coord.col/gamma)) )
                a_minus.append( Coord(math.exp(-coord.row/gamma), math.exp(-coord.col/gamma)) )

            # compute b+/- terms
            b_plus  = []
            b_minus = []
            for net_index in range(len(self.design.nets)):
                net = self.design.nets[i]
                b_plus.append( Coord(sum([a_plus[i].row for i in net]), sum([a_plus[i].col for i in net]) ) ) #had row instead of col for b_plus.col
                b_minus.append( Coord(sum([a_minus[i].row for i in net]), sum([a_minus[i].col for i in net]) ) )#had col instead of row for b_minus.row

            # compute c+/- terms
            c_plus  = []
            c_minus = []
            for net_index in range(len(self.design.nets)):
                net = self.design.nets[i]
                c_plus.append ( Coord(sum([self.design.coords[i].row*a_plus[i].row for i in net]), sum([self.design.coords[i].col*a_plus[i].col for i in net]) ) )
                c_minus.append( Coord(sum([self.design.coords[i].row*a_minus[i].row for i in net]), sum([self.design.coords[i].col*a_minus[i].col for i in net]) ) )

            # HPWL WA gradient
            hpwl_WA = []
            hpwl_actual = self.computeActualHPWL() 
            hpwl_gradient = []
            for i in range(len(self.design.coords)):
                hpwl_gradient.append(Coord(0,0))

            #print a, b, c terms for debugging
            net_index = 0
            print(f"Net {net_index}: ", end=""); prettyPrint.net(self.design.nets[0], self.design)
            for node_index in self.design.nets[net_index]:
                prettyPrint.coord(self.design.coords[node_index])
            print()
            #    print("\ta+ ", end=''); prettyPrint.coord(a_plus[node_index]);
            #    print("\ta- ", end=''); prettyPrint.coord(a_minus[node_index]); print()
            #print("b+ ", end=''); prettyPrint.coord(b_plus[net_index]);
            #print("b- ", end=''); prettyPrint.coord(b_minus[net_index]); print()
            #print("c+ ", end=''); prettyPrint.coord(c_plus[net_index]);
            #print("c- ", end=''); prettyPrint.coord(c_minus[net_index]); print()

            for i in range(len(self.design.nets)):
                hpwl_WA.append( Coord( AIEmath.computeTerm.WA_hpwl(b_plus[i].row, c_plus[i].row, b_minus[i].row, c_minus[i].row), \
                                       AIEmath.computeTerm.WA_hpwl(b_plus[i].col, c_plus[i].col, b_minus[i].col, c_minus[i].col) ) )
                for j in range(len(self.design.nets[i])):
                    node_index = self.design.nets[i][j]
                    hpwl_gradient[node_index].row += AIEmath.computeTerm.WA_partial(self.design.coords[node_index].row, a_plus[node_index].row, b_plus[i].row, c_plus[i].row, a_minus[node_index].row, b_minus[i].row, c_minus[i].row, gamma) 
                    hpwl_gradient[node_index].col += AIEmath.computeTerm.WA_partial(self.design.coords[node_index].col, a_plus[node_index].col, b_plus[i].col, c_plus[i].col, a_minus[node_index].col, b_minus[i].col, c_minus[i].col, gamma) 
                    #if node_index == 0:
                    #    coord0 = self.design.coords[node_index]
                    #    print(f"ADDING TO hpwl_grad[0]: ({hpwl_gradient[0].row:.2f}, {hpwl_gradient[0].col:.2f}) updated for net {i}", end='')
                    #    prettyPrint.net(self.design.nets[i], self.design)
                    #hpwl_gradient[i].append( Coord( AIEmath.computeTerm.WA_partial(self.design.coords[i].row, a_plus[node_ind.row].row, b_plus[i].row, c_plus[i].row, a_minus[node_ind.row].row, b_minus[i].row, c_minus[i].row, gamma), \
                    #          AIEmath.computeTerm.WA_partial(self.design.coords[i].col, a_plus[node_ind.row].col, b_plus[i].col, c_plus[i].col, a_minus[node_ind.row].col, b_minus[i].col, c_minus[i].col, gamma) ) )
            for i in range(len(self.design.nets)):
                continue
                print(f"net {i}: {self.design.nets[i]}")
                for j in range(len(self.design.nets[i])):
                    print(f"({self.design.coords[self.design.nets[i][j]].row}, {self.design.coords[self.design.nets[i][j]].col})", end=", ")
                print()
                print()
            
            total_hpwl = 0
            for i in range(len(self.design.nets)):
                total_hpwl += hpwl_actual[i].row + hpwl_actual[i].col
                continue
                print(f"NET[{i}]")
                print(f"HPWL estimate: ({hpwl_WA[i].row}, {hpwl_WA[i].col})")
                print(f"HPWL actual: {hpwl_actual[i]}")
                print()
            print(f"Total HPWL actual: {total_hpwl}")

            # Density Gradients
            self.computeBinDensities()
            total_density = 0
            for i in range(len(self.bin_grid.vals)):
                for j in range(len(self.bin_grid.vals[i])):
                    total_density += self.bin_grid.vals[i][j]
            print(f"total_density: {total_density} ")

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
            
            # perform idct
            electroPhi    = AIEmath.customDCT.idct_2d(electroPhi)
            electroForceX = AIEmath.customDCT.idsct_2d(electroForceX)
            electroForceY = AIEmath.customDCT.idcst_2d(electroForceY)

            step_len = 0.05
            density_penalty = 0# 0.1 + (iter)*.01 #round(iter/10) * 10
            density_penalty = 1 + (iter)*.05 #round(iter/10) * 10
            print(f"density_penalty: {density_penalty}")
            overflow = 0

            for i in range(len(self.bin_grid.vals)):
                for j in range(len(self.bin_grid.vals[i])):
                    if self.bin_grid.vals[-i-1][j] > 1:
                        overflow += self.bin_grid.vals[-i-1][j] - 1
            print(f"overflow: {overflow}")


            my_node = 0
            print(f"node {my_node}: ({self.design.coords[my_node].row:.2f}, {self.design.coords[my_node].col:.2f})")
            print(f"hpwl_grad[0]: ({hpwl_gradient[0].row:.2f}, {hpwl_gradient[0].col:.2f}) ")
            print(f"density_grad[0] X: {density_penalty*electroForceX[int(self.design.coords[0].row/self.bin_height)][int(self.design.coords[0].col/self.bin_width)]:.2f}")
            print(f"density_grad[0] Y: {density_penalty*electroForceY[int(self.design.coords[0].row/self.bin_height)][int(self.design.coords[0].col/self.bin_width)]:.2f}")
            print(f"step[0].row: {step_len * (hpwl_gradient[0].row + density_penalty*electroForceX[int(self.design.coords[0].row/self.bin_height)][int(self.design.coords[0].col/self.bin_width)] ):.2f}")
            print(f"step[0].col: {step_len * (hpwl_gradient[0].col + density_penalty*electroForceY[int(self.design.coords[0].row/self.bin_height)][int(self.design.coords[0].col/self.bin_width)] ):.2f}")

            # Update node locations
            print(f"Update coords:")
            prettyPrint.coord(self.design.coords[my_node]); print(f" --> ", end="")
            for i in range(len(self.design.coords)):
                #print(f"({self.design.coords[i].col}, {self.design.coords[i].col})")
                #delta_x = 
                self.design.coords[i].row -= step_len * (hpwl_gradient[i].row 
                                            - density_penalty*electroForceX[int(self.design.coords[i].row/self.bin_height)][int(self.design.coords[i].col/self.bin_width)] )
                                            # WHY IS THIS MINUS SIGN NEEDED????
                if self.design.coords[i].row < 0: self.design.coords[i].row = 0 # this shouldn't be needed b/c of boundary condition?
                if self.design.coords[i].row >= self.grid.num_rows : self.design.coords[i].row = self.grid.num_rows-1

                self.design.coords[i].col -= step_len * (hpwl_gradient[i].col
                                            - density_penalty*electroForceY[int(self.design.coords[i].row/self.bin_height)][int(self.design.coords[i].col/self.bin_width)] )
                if self.design.coords[i].col < 0: self.design.coords[i].col = 0
                if self.design.coords[i].col >= self.grid.num_cols : self.design.coords[i].col = self.grid.num_cols-1

            prettyPrint.coord(self.design.coords[my_node])
            prettyPrint.matrix(self.bin_grid.vals, title="Densities:", tabs="\t"*8)
            prettyPrint.matrix(electroPhi, title="electroPhi:", tabs="\t"*8)
            prettyPrint.matrix(electroForceY, title="electroForceY(cols):", tabs="\t"*8)
            prettyPrint.matrix(electroForceX, title="electroForceX(rows):", tabs="\t"*8)


        net_bins = []
        for net_index in range(len(self.design.nets)):
            net_bins.append(self.getNetBinDensities(net_index))
        
        hpwl_actual = self.computeActualHPWL() 
        for my_net in range(len(self.design.nets)):
            continue
            if len(self.design.nets[my_net]) == 2:
                continue
            print(f"\nnets[{my_net}]: {self.design.nets[my_net]}")
            print(f"\tCoords: ", end="")
            for node in self.design.nets[my_net]:
                print(f"({self.design.coords[node].row}, {self.design.coords[node].col})", end=", ")
            print(f"\tHPWL: ({hpwl_actual[my_net].row}, {hpwl_actual[my_net].col})")
            print(f"net_bins[{my_net}]: ")
            for i in range(len(net_bins[my_net])):
                print(f"{net_bins[my_net][-i-1]}")

        prettyPrint.nets(self.design)

        logging.root.name = 'AIEplace'
        logging.info("End AIEplace")
        #for node in self.design.coords:
        #    print(f"({node.row}, {node.col})")
    
    def legalize(self):
        ''' After running placement, legalize the design'''
        logging.info("Begin legalization")
        logging.info("End legalization")
        pass