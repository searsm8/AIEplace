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
import json, jsbeautifier
from naivePlacer import write_to_json
from copy import deepcopy
from statistics import mean

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

    def __repr__(self):
        return f"({self.row}, {self.col})"

class Grid:
    def __init__(self, num_rows: int, num_cols: int, num_timeslots=1) -> None:
        self.num_rows = num_rows
        self.num_cols = num_cols * num_timeslots # Total number of columns across all timeslots
        self.timeslot_cols = num_cols
        self.num_timeslots = num_timeslots 
        self.vals = []
        for _ in range(num_rows):
            self.vals.append([0]*num_cols)

    def getColsInTimeslot(self, timeslot_num):
        timeslot_start = (timeslot_num * self.num_cols/self.num_timeslots)
        return timeslot_start, timeslot_start + self.timeslot_cols

    def print_grid(self):
        temp_grid = np.asarray(self.vals)
        print(temp_grid)
        
    def get_dims(self):
        print("rows: " + str(self.num_rows) + ", cols: " + str(self.num_cols))

class Design:
    # primary lists which hold design information
    def __init__(self, coords, node_names, node_sizes, dependencies, predecessors, nets, times) -> None:
        self.coords = coords
        self.node_names = node_names
        self.node_sizes = node_sizes
        self.dependencies = dependencies
        self.predecessors = predecessors
        self.nets = nets # each net is a list of node indices
                    # e.g. [4,7,9] means nodes 4, 7, and 9 are connect by a net
        self.times = times

    @classmethod
    def initializeCoords(cls, num_rows, num_cols, node_count):
        #random initial position
        coords = []
        for i in range(node_count):
            # CENTER INITIALIZATION
            #coords.append( Coord( random.choice(range(round(grid.num_rows*.4), round(grid.num_rows*.6))) + random.random(), 
            #                      random.choice(range(round(grid.num_cols*.4), round(grid.num_cols*.6))) + random.random() ) )
            # Boundary INITIALIZATION
            coords.append( Coord( random.choice(range(math.floor(num_rows*.1), math.ceil(num_rows*.8))) + random.random(), 
                                  random.choice(range(math.floor(num_cols*.2), math.ceil(num_cols*.7))) + random.random() ) )
        return coords

    @classmethod
    def readJSON(cls, filepath):
        with open(filepath) as file:
            JSON = json.load(file)
            node_names = list(JSON["node_sizes"].keys())
            node_info = deepcopy(JSON["node_sizes"])
            node_sizes = [list(node_info.values())[i] for i in range(len(list(node_info)))]
            node_sizes = [Coord(node_sizes[i][0], node_sizes[i][1]) for i in range(len(list(node_info)))]
            net_names = list(JSON["nets"].values())
            nets, map_dict = map_nets_to_list(net_names, JSON["node_sizes"])
            dependencies, predecessors = extractDependenciesFromNetlist(nets, len(map_dict))

            node_runtimes = JSON["node_runtimes"]
            times = [list(node_runtimes.values())[i] for i in range(len(list(node_runtimes)))]
            
            num_rows = JSON["grid_info"]["num_rows"]
            num_cols = JSON["grid_info"]["num_cols"]
            coords = Design.initializeCoords(num_rows, num_cols, len(node_names))

            total_size = getTotalSize(node_sizes)
            num_timeslots = math.ceil(total_size / (num_rows*num_cols)) + 1
            grid = Grid(num_rows, num_cols, num_timeslots)
            design = Design(coords, node_names, node_sizes, dependencies, predecessors, nets, times)
        return design, grid, num_cols, map_dict
    
    @classmethod
    def partition_design(cls, partition_information, design, curr_part_herds):
        """Generates a subset of the design, containing only herds that will be placed
           in a given timeslot. These herds are mapped from their alphabetic values to
           values from 0 -> <# herds in timeslot - 1> to comply with the current input
           accepted by the force directed placement.

        Args:
            partition_information (dict): dict containing all of the herds (nodes) and 
                                          dependency sub-graphs (nets).
            design (Design): Design containing all of the herd information across all 
                             timeslots.
            curr_part_herds (list): herds to place in a timeslot

        Returns:
            new_design (Design): subset of new_design, to be used in a single timeslot
            herd_number_dict (Dict): mapping of herds from their "new_design" numbers
                                     to their original "design" numbers
        """
        new_coords = []
        new_node_names = []
        new_node_sizes = []
        new_dependencies = []
        new_nets = []
        new_times = []
        herd_number_dict = {}
        number = 0
        for i in curr_part_herds:
            new_coords.append(design.coords[i])
            new_times.append(design.times)
            herd_number_dict[i] = number
            new_node_names.append(number)
            number += 1
            new_node_sizes.append(design.node_sizes[i])
            # we don't want a right / left pulling dep. force
            new_dependencies.append(design.dependencies[i])
        net_keys = list(partition_information["nets"].keys())
        for net in range(len(net_keys)):
            unbroken_net = True
            for herd in partition_information["nets"][net]["nodes"]:
                if herd in curr_part_herds:
                    continue
                else:
                    unbroken_net = False
                    break
            if (unbroken_net):
                temp_net = partition_information["nets"][net]["nodes"]
                for node in range(len(temp_net)):
                    temp_net[node] = herd_number_dict[temp_net[node]]
                new_nets.append(temp_net)
        new_design = Design(new_coords, new_node_names, new_node_sizes, new_dependencies, new_nets, new_times)
        return new_design, herd_number_dict
    

def getTotalSize(node_sizes):
    total_size = 0
    for i in range(len(node_sizes)):
        total_size += node_sizes[i].row * node_sizes[i].col
    return total_size

def extractDependenciesFromNetlist(nets, num_nodes):
    '''Assumes that nets are sorted by dependency'''
    dependencies = [0]*num_nodes
    predecessors = {}
    for i in range(num_nodes): predecessors[i] = []

    for net in nets:
        for i in range(1, len(net)):
            dependencies[net[i]] = max(dependencies[net[i]], dependencies[net[0]]+1)
            predecessors[net[i]].append(net[0])
    return dependencies, predecessors


def convert_dep_to_force(dependency_list, node_names):
    """ converts the dependency list to a format that can be accepted by force directed placement

        Args:
            dependency_list (list<list<string>>): each sublist contains herds at the dependency for that index
                                            [['a', 'b'], ['c']]: a + b are dep. 0, c is dep 1
            node_names (list): names of all the nodes
                            ['a', 'b', 'c']

        Returns:
            list: dependency numbers lined up with the node_names list
                            [0, 0, 1]
        """
    new_dep_list = []

    new_dep_list = []
    for i in range(len(node_names)):
        for j in range(len(dependency_list)):
            if node_names[i] in dependency_list[j]:
                new_dep_list.append(j)
                break
    return new_dep_list

def map_nets_to_list(nets, nodes):
    """converts a list of nets that have string names to a list of nets that have integer names

    Args:
        nets (list<list<string>>): nets with associated nodes to each net
                                   [['a', 'c'], ['b', 'a']]
        nodes (dict): dict of nodes

    Returns:
        out_list: same size as nets, but with strings converted to integers
                  [[0, 2], [1, 3]]
        Dict (Dict): the mapping used to convert strings to ints
                  {'a': 0, 'b': 1, 'c': 2, 'd': 3}
    """
    Dict = {}
    out_list = []
    keys = list(nodes.keys())
    for i in range(len(keys)):
        Dict[keys[i]] = i
    for i in range(len(nets)):
        temp_list = []
        for net in range(len(nets[i])):
            temp_list.append(Dict[nets[i][net]])
        out_list.append(temp_list)
    return out_list, Dict

def get_dep_herds(dictionary):
    """Moves the dependencies from a dictionary to a list that has the length of the maximum
       dependency number + 1, with each sub list containing the herds at that dependency level.

    Args:
        dictionary (dict): dictionary of dependencies

    Returns:
        list: list containig herds at their dependency level
    """
    dependency_sorted_herds = []
    counter = 0
    while(dictionary):
        keys = list(dictionary.keys())
        same_dep_keys = []
        for i in range(len(keys)):
            has_dependency = False
            # check if dependencies have been placed
            for j in range(len(dictionary[keys[i]][0])):
                if dictionary[keys[i]][0][j] in dictionary: 
                    has_dependency = True
                    break
            if not has_dependency:
                same_dep_keys.append(keys[i])
        unplaced_herds = []
        for i in range(len(same_dep_keys)):
            unplaced_herds.append(same_dep_keys[i])
        while same_dep_keys:
            del dictionary[same_dep_keys.pop(0)]
        # unplaced_herds[i].sort(key=lambda x: x.size, reverse=True)
        # place herds
        dependency_sorted_herds.append(unplaced_herds)
        counter += 1
        if counter == 100:
            logging.warning("max iterations reached in getting dependencies")
            break
    return dependency_sorted_herds
        
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
        os.system(f"mkdir -p results/run_{self.design_run}/nodes")
    
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
        # Reset bin densities
        for i in range(self.bin_grid.num_rows):
            self.bin_grid.vals[i] = [0]*self.bin_grid.num_cols

        # iterate over all kernels, adding the overlap area to bin densities
        for i in range(len(self.design.coords)):
            node = self.design.coords[i]
            node_size = self.design.node_sizes[i]
            kernel_overlap = 0
            # iterate over all bins that this kernel could overlap and update bin values
            for bin_row in range(max(0, math.floor(node.row)-1), min(math.floor(node.row)+node_size.row+1, self.grid.num_rows-1)):
                for bin_col in range(max(0, math.floor(node.col)-1), min(math.floor(node.col+node_size.col+1), self.grid.num_cols-1)):
                    overlap = self.computeOverlap(i, bin_row, bin_col)
                    if overlap > 0:
                        self.bin_grid.vals[bin_row][bin_col] += overlap
                        kernel_overlap += overlap

        # Scale bin densities by the bin area
        bin_area = self.bin_width * self.bin_height
        if bin_area != 1:
            for bin_row in range(self.grid.num_rows):
                for bin_col in range(self.grid.num_cols):
                    self.bin_grid.vals[bin_row][bin_col] /= bin_area

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
        rows = []
        for i in range(len(net)):
            rows.append(self.design.coords[net[i]].row)
        rows = list(zip(rows, net))
        rows.sort(key=self.get0)
        return rows

    def getSortedCols(self, net):
        cols = []
        for i in range(len(net)):
            cols.append(self.design.coords[net[i]].col)
        cols = list(zip(cols, net))
        cols.sort(key=self.get0)
        return cols

    def run(self, iterations, method):
        ''' Runs the ePlace algorithm'''
        EXPORT_IMAGES = True
        EXPORT_NET_IMAGES = True
        
        # unplaced herds
        herds_to_return = []

        logging.info("Begin AIEplace")
        os.system("mkdir results")

        stagnant_iterations = 0 # number of iterations without seeing any improvement
        converged = False 
        
        # choose a random node and draw all nets for that node
        target_node=random.randint(0, len(self.design.coords)-1)

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

                # compute b+/- terms
                b_plus.append( Coord(AIEmath.computeTerm.b_term(a_plus[i].row), AIEmath.computeTerm.b_term(a_plus[i].col) ) )
                b_minus.append( Coord(AIEmath.computeTerm.b_term(a_minus[i].row), AIEmath.computeTerm.b_term(a_minus[i].col) ) )

                # compute c+/- terms
                c_plus.append( Coord(AIEmath.computeTerm.c_term(y_coords, a_plus[i].row), AIEmath.computeTerm.c_term(x_coords, a_plus[i].col) ) )
                c_minus.append( Coord(AIEmath.computeTerm.c_term(y_coords, a_minus[i].row), AIEmath.computeTerm.c_term(x_coords, a_minus[i].col) ) )

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
            density_penalty = 0.02*(iter) if iter < 50 else 0.05*50
            print(f"density_penalty: {density_penalty}")

            # Update node locations
            depend_weight = 1 if iter < 50 else 1.5#max(0.5, 0.02*(100-iter))
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
                
                dependency_force = 0
                if method == "Force":
                    dependency_force = depend_weight*(self.design.dependencies[i]-1)*(self.grid.num_cols-self.design.coords[i].col)/self.grid.num_cols
                # Subtract the electro force because we want to model REPULSIVE force
                self.design.coords[i].row -= step_len * (hpwl_gradient[i].row  - density_penalty * force.row)
                if self.design.coords[i].row < -1:
                   self.design.coords[i].row == -1
                if self.design.coords[i].row > self.grid.num_rows + 1:
                    self.design.coords[i].row = self.grid.num_rows + 1

                self.design.coords[i].col -= step_len * (hpwl_gradient[i].col - dependency_force - density_penalty * force.col)
                if self.design.coords[i].col < -1: self.design.coords[i].col = -1
                if self.design.coords[i].col > self.grid.num_cols + 1: self.design.coords[i] = self.grid.num_cols + 1

            # After the update, compute new metrics
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

            logging.info(f"\t\t{stagnant_iterations:.2f} stagnant_iterations\t{overflow:.2f} overflow\t {min_overflow:.2f} min_overflow")
            MAX_STAGNANT_ITERS = 20

            MIN_ITERS = 50

            if iter > MIN_ITERS and stagnant_iterations >= MAX_STAGNANT_ITERS:
                logging.info(f"No improvement in overflow for {MAX_STAGNANT_ITERS} iterations...Stopping.")
                converged = True

            if iter == iterations-1:
                converged = True

            nets_to_draw = []
            if converged:
                herds_to_return += self.legalize(list(range(len(self.design.node_sizes))))

            #for net in self.design.nets:
            #    if net.count(target_node):
            #        nets_to_draw.append(net)


            if EXPORT_IMAGES:
                self.computeBinDensities()
                net_bins = []
                for net_index in range(len(self.design.nets)):
                    net_bins.append(self.getNetBinDensities(net_index))
            
                hpwl_actual = self.computeActualHPWL() 
                total_hpwl = 0
                for i in range(len(self.design.nets)):
                    total_hpwl += hpwl_actual[i].row + hpwl_actual[i].col
                logging.info(f"Total HPWL actual: {total_hpwl:.2f}")
                logging.info(f"overflow: {overflow:.2f}")
                    
                #use PlaceDrawer to export an image
                if(iter%5 == 0) or converged:
                    filename=f"{time.strftime('%y%m%d_%H%M')}_iter_{iter:03}.png"
                    filename = os.path.join("results", f"run_{self.design_run}", filename)
                    ret = PlaceDrawer.PlaceDrawer.forward(
                            num_cols=self.num_cols_original,
                            pos= [self.design.coords[i].col for i in range(len(self.design.coords))]  +  \
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
                            node_names=self.design.node_names,
                            hwpl_force=[],
                            bin_force_x=electroForceX,
                            bin_force_y=electroForceY,
                            density_penalty=density_penalty,
                            iteration=iter,
                            dependencies=self.design.dependencies,
                            hpwl=total_hpwl,
                            overflow=overflow
                            )
            if converged and EXPORT_NET_IMAGES:
                # Export images showing nets for target nodes
                for target_node in range(len(self.design.coords)):
                    nets_to_draw = []
                    for net in self.design.nets:
                        if net.count(target_node):
                            nets_to_draw.append(net)
                            
                    filename = os.path.join("results", f"run_{self.design_run}", "nodes", f"{self.design.node_names[target_node]}.png")
                    ret = PlaceDrawer.PlaceDrawer.forward(
                            num_cols=self.num_cols_original,
                            pos= [self.design.coords[i].col for i in range(len(self.design.coords))]  +  \
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
                            node_names=self.design.node_names,
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
                # Export collected images as a .gif
                folder = os.path.join("results", f"run_{self.design_run}" )
                gif_name = f"_run_{self.design_run}.gif"
                PlaceDrawer.PlaceDrawer.export_gif(folder, gif_name)

                # Report metrics after finishing
                self.printMetrics()
                break
            #end iteration loop

        logging.root.name = 'AIEplace'
        logging.info("End AIEplace")

        return herds_to_return

    def printMetrics(self):
        # Print timeslot execution times
        print("\n###############")
        print("### METRICS ###")
        print("###############")
        print("Timeslot execution times:")
        nodes_by_timeslot = []
        for i in range(self.grid.num_timeslots): nodes_by_timeslot.append([])

        for i in range(len(self.design.coords)):
            node = self.design.coords[i]
            slot = math.floor(node.col / self.grid.timeslot_cols)
            nodes_by_timeslot[slot].append(i)
        
        # For each time slot, find the longest "chain" execution time.
        # If a node has a predecessor within the same timeslot, their 
        # execution times must be added together
        timeslot_area = self.grid.num_rows * self.grid.timeslot_cols
        execution_times = [0]*len(self.design.node_names)
        stats = []
        for slot in range(self.grid.num_timeslots):
            print(f"\n### Execution times for timeslot {slot}")
            for i in nodes_by_timeslot[slot]:
                execution_times[i] = (self.getExecutionTimeOfNode(i, nodes_by_timeslot[slot]))
                #print(f"{self.design.node_names[i]}: {execution_times[i]}")
                #print(f"Execution time of node {self.design.node_names[i]}: {t}")
            stats.append(self.computeStatsForSlot(nodes_by_timeslot[slot], execution_times))
            t_avg, t_max, area = stats[-1]
            print(f"### Timeslot execution time: {t_max}\tAvg execution time: {t_avg:.2f}\tTime Efficiency: {t_avg/t_max:.2f}")
            print(f"### Area: {area}\tTimeslot area: {timeslot_area}\tArea Utilization: {area/timeslot_area}")
            print(f"##################################################################")
            #print(f"Slot {slot}: {[self.design.node_names[i] for i in nodes_by_timeslot[slot]]}")
            #print(f"\tExecution times: {[self.design.times[i] for i in nodes_by_timeslot[slot]]}")
            #print(f"Worst execution time for slot {slot} is {worst_times[-1]}")
        #min_col, max_col = self.grid.getColsInTimeslot(timeslot_num)
        #for slot in range(self.grid.num_timeslots):
        #    print(f"Stats for Timeslot {slot}:")
        #    print(f"{stats[slot]}")
        overall_exec_eff  = mean([stats[i][0]/stats[i][1] for i in range(len(stats))])
        overall_area_util = mean([stats[i][2]/timeslot_area for i in range(len(stats))])
        print(f"\nOverall execution time efficiency: {overall_exec_eff*100:.1f}%")
        print(f"Overall Area utilization: {overall_area_util*100:.1f}%")
        print("")
        print(f"##################################################################")

    def computeStatsForSlot(self, nodes_in_slot, execution_times):
        sum = max = area = 0
        for i in nodes_in_slot:
            sum += execution_times[i]
            size = self.design.node_sizes[i]
            area += size.row * size.col
            if execution_times[i] > max:
                max = execution_times[i]
        avg = sum / len(nodes_in_slot)
        return avg, max, area


    def getExecutionTimeOfNode(self, index, nodes_in_timeslot):
        nodes = deepcopy(nodes_in_timeslot)
        t = self.design.times[index]
        #print(f"{self.design.node_names[index]}: t = {t}")
        if index in nodes:
            nodes.remove(index)
            longest_pred_time = 0
            for pred in self.design.predecessors[index]:
                if pred in nodes:
                    pred_time = self.getExecutionTimeOfNode(pred, nodes)
                    if pred_time > longest_pred_time:
                        longest_pred_time = pred_time
            t += longest_pred_time
        return t

    def printPredecessors(self, nodes):
        for node in nodes:
            print(f"node: {node}\tPreds of {self.design.node_names[node]}:")
            preds = self.design.predecessors[node]
            for pred in preds:
                print(f"\t{self.design.node_names[pred]}")




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
            temp = []
            coord_dict = {}
            for i in herds_of_interest:
                if self.design.dependencies[i] == j:
                    temp.append(i)
                    coord_dict[i] = self.design.coords[i].col
            sorted_dict = sorted(coord_dict.items(), key=lambda x: x[1])
            sorted_list = [item[0] for item in sorted_dict]
            dependency_ordered_array += sorted_list
            # dependency_ordered_array.append(temp)
            
        # print(self.design.dependencies)
        # print(dependency_ordered_array)
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
            curr_row = -1
            curr_col = -1

            # for col in range(0, coord_col):
            #     if (self.is_legal_placement([coord_row, col], size, lg)):
            #         curr_row = coord_row
            #         curr_col = col
            #         legalized = True
            #         break
            # if the top left corner is filled on the grid or if it's empty
            if lg.vals[coord_row][coord_col] == 0 and not legalized:
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
                    logging.info(f"Legalization FAILED: no legal placement found for node at {coord_row, coord_col}.")
                    # return unplaced herds
                    return dependency_ordered_array[(dependency_ordered_array.index(i)):]
                elif (dist > max(self.grid.num_rows, self.grid.num_cols)):
                    logging.info(f"Legalization FAILED: no legal placement found for node at {coord_row, coord_col}.")
                    # return unplaced herds
                    return dependency_ordered_array[(dependency_ordered_array.index(i)):]
                else: 
                    dist += 1
            if (legalized):
                coord_row = curr_row
                coord_col = curr_col
                self.design.coords[i].row = coord_row
                self.design.coords[i].col = coord_col
                herdList.append(Herd(self.grid.num_rows - curr_row - 1, curr_col, size.row, size.col, self.design.dependencies[i], self.design.node_names[i], self.design.dependencies[i]))
                number += 1
                for row in range(coord_row, coord_row + size.row):
                    for col in range(coord_col, coord_col + size.col):
                        lg.vals[row][col] = 1
                continue
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
        return []