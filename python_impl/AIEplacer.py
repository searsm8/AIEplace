# AIEplacer.py
# Implement the a rudimentary ePlace algorithm to place designs
# Intended only to run on small designs, such as a 5.row8 AIE array

import AIEmath.computeTerm
import AIEmath.customDCT
import sys, os
import prettyPrint
import random
import math
import time
import numpy as np
import logging
import PlaceDrawer
import jsbeautifier
from naivePlacer import write_to_json

class Herd:
    def __init__(self, row_loc, col_loc, row_size, col_size, number, name, dependency):
        self.row_loc = row_loc
        self.col_loc = col_loc
        self.row_size = row_size
        self.col_size = col_size
        self.number = number
        self.name = name
        self.dep = dependency
    
    def convert_to_list(self):
        """Converts the herd locations and number to a format that will be 
           displayed correctly in the AIR partition grid

        Returns:
            list: [<number>, <name>, [<row #>, <col #>], ...]
        """
        locations = [self.number, self.name]
        for i in range(self.row_size):
            for j in range(self.col_size):
                # Right and down
                row_coord = self.row_loc - i
                col_coord = self.col_loc + j
                locations.append([row_coord, col_coord])
        return locations

    def print_info(self):
        print("name: " + str(self.name) + ", [" + str(self.col_loc) + ", " + str(self.row_loc) + "]")


class Coord:
    def __init__(self, row, col):
        self.row = row
        self.col = col

    def __add__(self, oc):
        return Coord(self.row + oc.row, self.col + oc.col)
    
    def __str__(self):
        return f"({self.row}, {self.col})"

class Grid:
    def __init__(self, num_rows: int, num_cols: int) -> None:
        self.num_rows = num_rows
        self.num_cols = num_cols
        self.vals = []
        for i in range(num_rows):
            self.vals.append([0]*num_cols)
    def print_grid(self):
        temp_grid = np.asarray(self.vals)
        print(temp_grid)
    def get_dims(self):
        print("rows: " + str(self.num_rows) + ", cols: " + str(self.num_cols))

class Design:
    # primary lists which hold design information
    def __init__(self, coords, node_names, node_sizes, dependencies, nets) -> None:
        self.coords = coords
        self.node_names = node_names
        self.node_sizes = node_sizes
        self.dependencies = dependencies
        self.nets = nets # each net is a list of node indices
                    # e.g. [4,7,9] means nodes 4, 7, and 9 are connect by a net
        
class AIEplacer:
    def __init__(self, grid, design, num_cols) -> None:
        self.grid = grid
        self.design = design
        self.num_cols_original = num_cols
        self.bin_width = 1 # arbitrary
        self.bin_height= 1 # arbitrary
        self.bin_grid = Grid(int(grid.num_rows/self.bin_height), int(grid.num_cols/self.bin_width))
        self.design_run = 0
        while(os.path.exists(f"results/run_{self.design_run}")):
            self.design_run += 1
        os.system(f"mkdir results/run_{self.design_run}")
    
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

    def computeOverlap(self, node_index, u, v):
        node = self.design.coords[node_index]
        node_size = self.design.node_sizes[node_index]
        low_row = max(u*self.bin_height, node.row)
        low_col = max(v*self.bin_width, node.col)
        high_row= min((u+1)*self.bin_height, node.row + node_size.row) 
        high_col= min((v+1)*self.bin_height, node.col + node_size.col) 
        if low_row >= high_row or low_col >= high_col:
            return 0 
        else:
            return (high_row - low_row) * (high_col - low_col)

    def get0(self, a):
        return a[0]

    def getSortedRows(self, net):
        rows = [self.design.coords[i].row for i in net]
        rows = list(zip(rows, net))
        rows.sort(key=self.get0)
        return rows

    def getSortedCols(self, net):
        cols = [self.design.coords[i].col for i in net]
        cols = list(zip(cols, net))
        cols.sort(key=self.get0)
        return cols

    def run(self, iterations):
        ''' Runs the ePlace algorithm'''
        export_images = True

        logging.info("Begin AIEplace")
        os.system("mkdir results")

        stagnant_iterations = 0 # number of iterations without seeing any improvement
        converged = False 
        
        for iter in range(iterations):
            logging.root.name = 'ITER ' + str(iter)
            logging.info(f"***Begin Iteration {iter}***")

                
            # Compute gradients
            # TODO: include the min/m.row terms to reduce computational burden
            # compute a+/- terms
            gamma = 4.
            a_plus  = []; a_minus = []
            b_plus  = []; b_minus = []
            c_plus  = []; c_minus = []
            # HPWL WA gradient
            hpwl_WA = []
            hpwl_gradient = []
            for i in range(len(self.design.coords)):
                hpwl_gradient.append(Coord(0,0))

            for i in range(len(self.design.nets)):
                net = self.design.nets[i]
                zipped_y_coords = self.getSortedRows(net)
                zipped_x_coords = self.getSortedCols(net)
                y_coords = [zipped_y_coords[i][0] for i in range(len(zipped_y_coords))]
                x_coords = [zipped_x_coords[i][0] for i in range(len(zipped_x_coords))]
                a_plus.append( Coord( AIEmath.computeTerm.a_plus(y_coords, gamma), AIEmath.computeTerm.a_plus(x_coords, gamma) ) )
                a_minus.append( Coord( AIEmath.computeTerm.a_minus(y_coords, gamma), AIEmath.computeTerm.a_minus(x_coords, gamma) ) )
            #for i in range(len(self.design.coords)):
            #    coord = self.design.coords[i]
            #    a_plus.append( Coord(math.exp(coord.row/gamma), math.exp(coord.col/gamma)) )
            #    a_minus.append( Coord(math.exp(-coord.row/gamma), math.exp(-coord.col/gamma)) )

                # compute b+/- terms
                b_plus.append( Coord(AIEmath.computeTerm.b_term(a_plus[i].row), AIEmath.computeTerm.b_term(a_plus[i].col) ) )
                b_minus.append( Coord(AIEmath.computeTerm.b_term(a_minus[i].row), AIEmath.computeTerm.b_term(a_minus[i].col) ) )

                # compute c+/- terms
                c_plus.append( Coord(AIEmath.computeTerm.c_term(y_coords, a_plus[i].row), AIEmath.computeTerm.c_term(x_coords, a_plus[i].col) ) )
                c_minus.append( Coord(AIEmath.computeTerm.c_term(y_coords, a_minus[i].row), AIEmath.computeTerm.c_term(x_coords, a_minus[i].col) ) )
                #c_plus.append ( Coord(sum([self.design.coords[i].row*a_plus[i].row for i in net]), sum([self.design.coords[i].col*a_plus[i].col for i in net]) ) )
                #c_minus.append( Coord(sum([self.design.coords[i].row*a_minus[i].row for i in net]), sum([self.design.coords[i].col*a_minus[i].col for i in net]) ) )

                # Compute HPWL gradients
                hpwl_WA.append( Coord( AIEmath.computeTerm.WA_hpwl(b_plus[i].row, c_plus[i].row, b_minus[i].row, c_minus[i].row), \
                                       AIEmath.computeTerm.WA_hpwl(b_plus[i].col, c_plus[i].col, b_minus[i].col, c_minus[i].col) ) )
                for j in range(len(zipped_y_coords)):
                    node_index = zipped_y_coords[j][1]
                    hpwl_gradient[node_index].row += AIEmath.computeTerm.WA_partial(self.design.coords[node_index].row, a_plus[i].row[j], b_plus[i].row, c_plus[i].row, a_minus[i].row[j], b_minus[i].row, c_minus[i].row, gamma) 
                    node_index = zipped_x_coords[j][1]
                    hpwl_gradient[node_index].col += AIEmath.computeTerm.WA_partial(self.design.coords[node_index].col, a_plus[i].row[j], b_plus[i].col, c_plus[i].col, a_minus[i].row[j], b_minus[i].col, c_minus[i].col, gamma) 


            # Density Gradients
            self.computeBinDensities()

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

            step_len = 0.09
            density_penalty = 0.02*(iter) if iter < 50 else 0.05*iter
            print(f"density_penalty: {density_penalty}")

            # Update node locations
            depend_weight = 3 if iter < 50 else 1.5#max(0.5, 0.02*(100-iter))
            for i in range(len(self.design.coords)):
                # Compute the electro force for each node, accounting for bin overlaps
                force = Coord(0, 0)
                node = self.design.coords[i]
                for bin_row in range(max(0, math.floor(node.row)-1), min(math.floor(node.row)+2, self.grid.num_rows-1)):
                    for bin_col in range(max(0, math.floor(node.col)-1), min(math.floor(node.col+2), self.grid.num_cols-1)):
                        overlap = self.computeOverlap(i, bin_row, bin_col)
                        if overlap > 0:
                            force.row += overlap * electroForceX[bin_row][bin_col]
                            force.col += overlap * electroForceY[bin_row][bin_col]

                dependency_force = depend_weight*(self.design.dependencies[i]-0.5)*(self.grid.num_cols-self.design.coords[i].col)/self.grid.num_cols
                # Subtract the electro force because we want to model REPULSIVE force
                self.design.coords[i].row -= step_len * (hpwl_gradient[i].row  - density_penalty * force.row)
                if self.design.coords[i].row < -1:
                    self.design.coords[i].row == -1

                #self.design.coords[i].row -= step_len * (0 - density_penalty * force.row)
                                            #- density_penalty*electroForceX[int(self.design.coords[i].row/self.bin_height)][int(self.design.coords[i].col/self.bin_width)] )
                                            # WHY IS THIS MINUS SIGN NEEDED????
                #if self.design.coords[i].row < 0: self.design.coords[i].row = 0 # this shouldn't be needed b/c of boundary condition?
                #if self.design.coords[i].row + self.design.node_sizes[i].row >= self.grid.num_rows : self.design.coords[i].row = self.grid.num_rows-1

                self.design.coords[i].col -= step_len * (hpwl_gradient[i].col - dependency_force - density_penalty * force.col)
                #self.design.coords[i].col -= step_len * (0 - density_penalty * force.col)
                                            #- density_penalty*electroForceY[int(self.design.coords[i].row/self.bin_height)][int(self.design.coords[i].col/self.bin_width)] )
                if self.design.coords[i].col < -1: self.design.coords[i].col = -1
                if self.design.coords[i].col > self.grid.num_cols + 1: self.design.coords[i] = self.grid.num_cols + 1
                #if self.design.coords[i].col + self.design.node_sizes[i].col  >= self.grid.num_cols : self.design.coords[i].col = self.grid.num_cols-1

            #prettyPrint.coord(self.design.coords[my_node])
            #prettyPrint.matrix(self.bin_grid.vals, title="Densities:", tabs="\t"*8)
            #prettyPrint.matrix(electroPhi, title="electroPhi:", tabs="\t"*8)
            #prettyPrint.matrix(electroForceY, title="electroForceY(cols):", tabs="\t"*8)
            #prettyPrint.matrix(electroForceX, title="electroForceX(rows):", tabs="\t"*8)
            #os.system("clear")


            # after the update, compute new metrics
            overflow = 0
            for i in range(len(self.bin_grid.vals)):
                for j in range(len(self.bin_grid.vals[i])):
                    if self.bin_grid.vals[-i-1][j] > 1:
                        overflow += self.bin_grid.vals[-i-1][j] - 1

            if iter == 0:
                min_overflow = overflow
            if overflow >= min_overflow:
                stagnant_iterations += 1
            else: stagnant_iterations = 0
            if overflow < min_overflow:
                min_overflow = overflow

            logging.info(f"\t\t{stagnant_iterations} stagnant_iterations\t{overflow} overflow\t {min_overflow} min_overflow")
            convergence_iterations = 20
            if iter > 200 and stagnant_iterations >= convergence_iterations:
                logging.info(f"No improvement in overflow for {convergence_iterations} iterations...Stopping.")
                converged = True

            if iter == iterations-1:
                converged = True

            nets_to_draw = []
            if converged:
                self.legalize(list(range(len(self.design.node_sizes))))
                # choose a random node and draw all nets for that node
                for i in range(1):
                    while True:
                        target_node=random.randint(0, len(self.design.coords)-1)
                        if self.design.dependencies[target_node] >= 1:
                            break
                    for net in self.design.nets:
                        if net.count(target_node):
                            nets_to_draw.append(net)

            if export_images:
                self.computeBinDensities()
                net_bins = []
                for net_index in range(len(self.design.nets)):
                    net_bins.append(self.getNetBinDensities(net_index))
            
                hpwl_actual = self.computeActualHPWL() 
                total_hpwl = 0
                for i in range(len(self.design.nets)):
                    total_hpwl += hpwl_actual[i].row + hpwl_actual[i].col
                print(f"Total HPWL actual: {total_hpwl}")
                print(f"overflow: {overflow}")
                    
                #use PlaceDrawer to export an image
                if(iter%5 == 0) or converged:
                    filename=f"{time.strftime('%y%m%d_%H%M')}_iter_{iter}.png"
                    filename = os.path.join("results", f"run_{self.design_run}", filename)
                            
                    ret = PlaceDrawer.PlaceDrawer.forward(
                            self.num_cols_original,
                            pos= [self.design.coords[i].col for i in range(len(self.design.coords))]  + 
                                [self.design.coords[i].row for i in range(len(self.design.coords))], 
                            node_size_x=[self.design.node_sizes[i].col for i in range(len(self.design.coords))], 
                            node_size_y=[self.design.node_sizes[i].row for i in range(len(self.design.coords))], 
                            pin_offset_x=[0 for i in range(len(self.design.coords))],
                            pin_offset_y=[0 for i in range(len(self.design.coords))],
                            pin2node_map={}, 
                            xl=0, yl=0, xh=self.grid.num_cols, yh=self.grid.num_rows, 
                            site_width=1, row_height=1,
                            bin_size_x=self.bin_width, bin_size_y=self.bin_height, 
                            num_movable_nodes=0, num_filler_nodes=0, 
                            filename=filename,
                            nets=nets_to_draw,
                            hwpl_force=[],
                            bin_force_x=electroForceX,
                            bin_force_y=electroForceY,
                            density_penalty=density_penalty,
                            iteration=iter,
                            dependencies=self.design.dependencies,
                            hpwl=total_hpwl,
                            overflow=overflow
                            )
            if converged:
                break
            #end iteration loop

        logging.root.name = 'AIEplace'
        logging.info("End AIEplace")

    def is_legal_placement(self, grid_coords, herd, lg):
                """Checks if a herd fits in the grid at the specified grid tile. 
                    Assumes the first tile in the grid will be the upper left hand
                    tile of the herd.
                Args:
                    coords (list): [row, col] to place herd
                    herd (Herd): herd to be checked
                    lg (Grid): Grid containing current partition
                """
                original_timeslot = math.floor(grid_coords[1] / self.num_cols_original)
                for row in range(grid_coords[0], grid_coords[0] + herd.row):
                    for col in range(grid_coords[1], grid_coords[1] + herd.col):
                        current_timeslot = math.floor(col / self.num_cols_original)
                        # crossing time boundaries
                        if current_timeslot != original_timeslot:
                            return False
                        if (row < 0 or col < 0):
                            return False
                        if (row >= self.grid.num_rows or col >= self.grid.num_cols):
                            return False
                        if (lg.vals[row][col] != 0):
                            return False
                return True
                
    def legalize(self, herds_of_interest):
        """After some level of placement, perform a legalization

        Args:
            herds_of_interest (list): herds numbers to be placed at the current time
        """

        logging.info("Begin legalization")
        lg = Grid (self.grid.num_rows, self.grid.num_cols) # legalization grid
        herdList = []
        number = 0
        dependency_ordered_array = []
        for j in range(max(self.design.dependencies) + 1):
            for i in herds_of_interest:
                if self.design.dependencies[i] == j:
                    dependency_ordered_array.append(i)
        print(self.design.dependencies)
        print(dependency_ordered_array)
        # place all dependency 0 herds first, then dep 1, etc.
        for i in dependency_ordered_array:
        # for coord in self.design.coords:
            legalized = False
            coord_row = self.design.coords[i].row
            coord_col = self.design.coords[i].col
            coord_row = round(coord_row)
            coord_col = round(coord_col)
            size = self.design.node_sizes[i]
            # don't know how to place things smaller than tiles, so just rounding up
            size.row = int(math.ceil(size.row))
            size.col = int(math.ceil(size.col))
            if coord_row < 0: coord_row = 0
            if coord_col < 0: coord_col = 0
            if coord_row >= self.grid.num_rows: coord_row = self.grid.num_rows-1
            if coord_col >= self.grid.num_cols: coord_col = self.grid.num_cols-1

            # if the top left corner is filled on the grid or if it's empty
            if lg.vals[coord_row][coord_col] == 0:
                # Do not go out of the top or the left of the grid

                # Because we're looking @ the top left corner, move the herd upwards until all
                # rows have been checked.
                for row in range(coord_row, coord_row - size.row, -1):
                    # Start at "placed" location, Move the herd to the left, closer to t=0
                    for col in range(coord_col, coord_col - size.col, -1):
                        if (self.is_legal_placement([row, col], size, lg)):
                            curr_row = row
                            curr_col = col
                            legalized = True
                            break
                    if legalized:
                        break
            dist = 0

            while not legalized:
                attempted_placement = False
                curr_row = min(max(coord_row - dist, 0), self.grid.num_rows)
                curr_col = min(max(coord_col + dist, 0), self.grid.num_cols)

                # starting right of the target tile, moving down
                while (not legalized and curr_row < coord_row + size.row + dist):
                    attempted_placement = True
                    if (self.is_legal_placement([curr_row, curr_col], size, lg)):
                        legalized = 1
                        break
                    elif (curr_row == self.grid.num_rows - 1):
                        break
                    else:
                        curr_row += 1

                # moving left, beneath the target tile, until the herd
                # is clear of the target tile (column wise), and won't 
                # overlap with the target tile until it wants to move up
                # Note: -1 is a magic number, not exactly sure on why it works, 
                # but if written out, it makes sense.
                while (not legalized and coord_col - curr_col - dist - size.col <= -1):
                    attempted_placement = True
                    if (self.is_legal_placement([curr_row, curr_col], size, lg)):
                        legalized = 1
                        break
                    elif (curr_col == 0):
                        break
                    else:
                        curr_col -= 1

                # moving up, to the left of the target tile, until the 
                # bottom of the current herd clears the top of the target
                # tile.
                while (not legalized and coord_row - curr_row - dist - size.row <= -1):
                    attempted_placement = True
                    if (self.is_legal_placement([curr_row, curr_col], size, lg)):
                        legalized = 1
                        break
                    elif (curr_row == 0):
                        break
                    else:
                        curr_row -= 1

                # moving right, until the herd is clear of the right hand 
                # side of the target tile.
                while (not legalized and curr_col < coord_col + dist + size.col):
                    attempted_placement = True
                    if (self.is_legal_placement([curr_row, curr_col], size, lg)):
                        legalized = 1
                        break
                    elif (curr_col == self.grid.num_cols - 1):
                        break
                    else:
                        curr_col += 1
                if (not attempted_placement):
                    print("failed")
                    logging.info(f"Legalization FAILED: no legal placement found for node at {coord_row, coord_col}.")
                    return
                elif (dist > max(self.grid.num_rows, self.grid.num_cols)):
                    logging.info(f"Legalization FAILED: no legal placement found for node at {coord_row, coord_col}.")
                    return
                else: 
                    dist += 1
            if (legalized):
                coord_row = curr_row
                coord_col = curr_col
                self.design.coords[i].row = coord_row
                self.design.coords[i].col = coord_col
                herdList.append(Herd(self.grid.num_rows - curr_row - 1, curr_col, size.row, size.col, self.design.dependencies[i], self.design.node_names[i], self.design.dependencies[i]))
                number += 1
                # is this right? for the row
                for row in range(coord_row, coord_row + size.row):
                    for col in range(coord_col, coord_col + size.col):
                        lg.vals[row][col] = 1
                continue
        print(lg.print_grid())
        dep_herd_list = []
        for dep in range(max(self.design.dependencies) + 1):
            temp = []
            for herd in range(len(herdList)):
                if herdList[herd].dep == dep:
                    temp.append(herdList[herd])
            dep_herd_list.append(temp)
        total_tiles_placed = np.sum(np.asarray(lg.vals))
        write_to_json(dep_herd_list, [lg.num_rows, self.num_cols_original], "forcePlacer.json",  math.ceil(total_tiles_placed / (self.grid.num_rows + self.num_cols_original)))
        print("===============")
        print("Total tiles placed: " + str(total_tiles_placed))

        logging.info("End legalization")