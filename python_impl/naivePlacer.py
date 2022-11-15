import numpy as np
import json
import jsbeautifier
from copy import deepcopy

class Grid_B:
    def __init__(self, num_rows, num_cols, number):
        self.num_rows = num_rows
        self.num_cols = num_cols
        self.number = number
        self.grid = np.full((num_rows, num_cols), -1)

    def print_dims(self):
        print("Number of Rows: " + str(self.num_rows) + "Number of columns: \
            " + str(self.num_cols))
        return

    def dims(self):
        return [self.num_rows, self.num_cols]
    
    def does_herd_fit(self, grid_coords, herd):
        """Checks if a herd fits in the grid at the specified grid tile. 
           Assumes the first tile in the grid will be the upper left hand
           tile of the herd.

        Args:
            coords (list): [row, col] to place herd
            herd (Herd): herd to be checked
        """
        for row in range(self.num_rows - grid_coords[0] - 1, self.num_rows - grid_coords[0] + herd.num_rows - 1):
            for col in range(grid_coords[1], grid_coords[1] + herd.num_cols):
                if (row < 0 or col < 0):
                    return "Does not fit"
                if (row >= self.num_rows or col >= self.num_cols):
                    return "Does not fit"
                if (row > self.num_rows - 1 or col > self.num_cols - 1):
                    return "Does not fit"
                if (self.grid[row][col] != -1):
                    return "Previously placed tile " + str(self.grid[row][col])
        # print("row: " + str(row) + ", col: " + str(col))
        # print("grid coords: " + str(grid_coords[0]) + ", " + str(grid_coords[1]))
        herd.set_position(grid_coords[0], grid_coords[1])
        return

    def place_herd(self, herd): 
        """Places a herd in the grid
        Example: 
            grid: [[-1,  1,  1],
                   [-1, -1, -1]
                   [-1, -1, -1]]
            Herd: [2, 2], Herd.upper_left_row = 1, Herd.upper_left_col = 1, \
                  Herd.name = 2 ->
            
            grid: [[-1,  1,  1],
                   [-1,  2,  2]
                   [-1,  2,  2]]

        Args:
            herd (Herd): herd to be placed

        """
        if not herd:
            print("No herd defined")
            return
        herd_coords = herd.get_position()
        if (herd_coords[0] == -1 or herd_coords[1] == -1):
            print("Herd has not yet been placed")
        for row in range(self.num_rows - herd_coords[0] - 1, self.num_rows - herd_coords[0] + herd.num_rows - 1):
            for col in range(herd_coords[1], herd_coords[1] + herd.num_cols):
                self.grid[row][col] = herd.number
        return
    
    def merge_grids(self, other_grid):
        """Merges 2 grids together into a super grid

        Args:
            other_grid (Grid): grid to be merged with self

        """
        new_grid_rows = self.num_rows
        new_grid_cols = self.num_cols + other_grid.num_cols
        new_grid = np.full((new_grid_rows, new_grid_cols), -1)
        for row in range(self.num_rows):
            for col in range(self.num_cols):
                new_grid[row][col] = self.grid[row][col]
        for row in range(other_grid.num_rows):
            for col in range(other_grid.num_cols):
                new_grid[row][col + self.num_cols] = other_grid.grid[row][col]
        # print(new_grid)
        return new_grid

class Herd_B:
    upper_left_row = -1
    upper_left_col = -1

    def __init__(self, num_rows, num_cols, number, name, dependency):
        self.num_rows = num_rows
        self.num_cols = num_cols
        self.number = number
        self.name = name
        self.dep = dependency
        self.size = num_cols * num_rows
    
    def print_self(self):
        print("Name: " + str(self.name) + ", num_rows: " + str(self.num_rows) + ", num_cols: " + str(self.num_cols) \
            + ", dependency: " + str(self.dep))
        return

    def print_dims(self):
        print("Number of Rows: " + str(self.num_rows) + " Number of columns: " + str(self.num_cols))
        return

    def print_positions(self):
        # print("Upper left row number: " + str(self.upper_left_row) + ", Upper left column number: " + str(self.upper_left_col))
        print("[" + str(self.upper_left_row) + ", " + str(self.upper_left_col) + "]")
        return

    def get_position(self):
        return [self.upper_left_row, self.upper_left_col]

    def set_position(self, row_pos, col_pos):
        """Sets the upper left row and column position

        Args:
            row_pos (int) \n
            col_pos (int)
        """
        self.upper_left_row = row_pos
        self.upper_left_col = col_pos
        return

    def convert_to_list(self):
        """Converts the herd locations and number to a format that will be 
           displayed correctly in the AIR partition grid

        Returns:
            list: [<number>, <name>, [<row #>, <col #>], ...]
        """
        locations = [self.number, self.name]
        for i in range(self.num_rows):
            for j in range(self.num_cols):
                # Right and down
                row_coord = self.upper_left_row - i
                col_coord = self.upper_left_col + j
                locations.append([row_coord, col_coord])
        return locations

def greedy_placement(herds, grid):
    """Organizes the herds by size before passing to the naive placer

    Args:
        herds (list): list of Herds to be placed
        grid (Grid): grid to place herds onto
    """
    # first, sort by level of dependency
    # Will need to add dimentionality to the herds
    for i in range(len(herds)):
        herds[i].sort(key=lambda x: x.size, reverse=True)
    naive_placement(herds, grid)
    return

def generate_part_dict(part_list, maxdims, number): 
    """Creates the dictionary necessary to generate JSON that can \
       be read by the partition viewer

    Args:
        part_list (list): list of herds [[<herd #>, [<row #>, <col #>], ...] ...]]
        maxdims (list): size of the grid [<max_row>, <max_col>]
        number: number of grids
    Returns:
        dict: {["switchbox": ...], ["partition": [[<herd #>, [<row #>, <col #>], ...] ...]]}
    """
    region = [maxdims[0], maxdims[1]]
    new_part_list = {
        "switchbox01": {
            "row": maxdims[0] - 1,
            "col": maxdims[1] * number - 1
        },
        "partition": part_list,
        "region": region
    }
    return new_part_list

def write_to_json(herds, grid_dims, output_file_name, number):
    """Writes the list of placed herds to a json file

    Args:
        herds (list): list containing all of the herds to be placed 
                      for each dependency [[dep 0 <Herd object>, ...], ...]
        grid_dims (list): list containing 
        output_file_name: string w/ output file name
        number: number of grids needed to realize placement (used w/ naive)

    Returns:
        None
    """
    herd_list = []
    for dep in range(len(herds)):
        for herd in range(len(herds[dep])): 
            herd_list.append(herds[dep][herd].convert_to_list())
    # print(grid.grid)
    partitions = generate_part_dict(herd_list, [grid_dims[0], grid_dims[1]], number)
    options = jsbeautifier.default_options()
    options.indent_size = 4
    json_object = jsbeautifier.beautify(json.dumps(partitions), options)
    with open(output_file_name, 'w', encoding='utf-8') as f:
        f.write(json_object)
    f.close()
    return

def write_to_json_super(part_dict, grid_dims, output_file_name, number):
    partitions = generate_part_dict(part_dict, [grid_dims[0], grid_dims[1]], number)
    options = jsbeautifier.default_options()
    options.indent_size = 4
    json_object = jsbeautifier.beautify(json.dumps(partitions), options)
    with open(output_file_name, 'w', encoding='utf-8') as f:
        f.write(json_object)
    f.close()
    return

def naive_placement(herds, grid):
    """Tries to place herds in a naive way, starting with the first herd in the
       list, trying to place it starting in the upper left hand corner and gradually
       working its way down to the bottom right. Updates the herd + grid objects.

    Args:
        herds (list): list of Herds to be placed
        grid (Grid): grid to place herds onto

    """
    unplaced_herds = deepcopy(herds)
    curr_grid = 0
    while unplaced_herds:
        unplaced_herds = naive_placement_helper(grid[curr_grid], herds, unplaced_herds)
        if unplaced_herds:
            curr_grid += 1
            new_grid = Grid_B(grid[0].num_rows, grid[0].num_cols, curr_grid)
            grid.append(new_grid)

    # for i in range(len(grid)):
    #     print(grid[i].grid)

    if (unplaced_herds):
        print("Placement failed. Unplaced herds: ")
        for dep in range(len(unplaced_herds)):
            for herd in range(len(unplaced_herds[dep])):
                unplaced_herds[dep][herd].print_self()
    return

def naive_placement_helper(grid, herds, unplaced_herds):
    curr_herd_dep = 0
    for row in reversed(range(grid.num_rows)):
        for col in range(grid.num_cols):
            if curr_herd_dep == len(unplaced_herds):
                break
            if(grid.grid[grid.num_rows - row - 1][col] == -1):
                for herd in range(len(unplaced_herds[curr_herd_dep])):
                    if not grid.does_herd_fit([row, col], unplaced_herds[curr_herd_dep][herd]):
                        grid.place_herd(unplaced_herds[curr_herd_dep][herd])
                        for i in range(len(herds)):
                            for j in range(len(herds[i])):
                                if unplaced_herds[curr_herd_dep][herd].number == herds[i][j].number:
                                    herds[i][j].set_position(row, col + (grid.number * grid.num_cols))
                        unplaced_herds[curr_herd_dep].remove(unplaced_herds[curr_herd_dep][herd])
                        if len(unplaced_herds[curr_herd_dep]) == 0:
                            curr_herd_dep += 1
                        break  
    dep = 0
    while(unplaced_herds):
        if dep == curr_herd_dep:
            break
        elif (not unplaced_herds[dep]):
           del unplaced_herds[dep]
        else: 
            break
    return unplaced_herds


def run_brandon_placement(num_rows, num_cols, node_sizes, node_names, dependency_list):
    """Performs a naive placement of the kernels.
       Starts by sorting all of the kernels based on their dependency level e.g., if a kernel 
       is on a "net" with dependency 3, it may only be placed after the previous dependencies
       (0, 1, 2) have been placed.

    Args:
        num_rows (int): num of rows in a partition
        num_cols (int): num of columns in a partition
        node_sizes (list<Coords>): size of herds
        node_names (list(<String>)): name of herds
        depedencies (list<Int>): dependency value of herds: dep. 1 has to come after 0,
                              2 after 1 and 0, etc.
    """
    grid = [Grid_B(num_rows, num_cols, 0)]
    herd_list = []
    dependency_list
    for _ in dependency_list:
        herd_list.append([])
    for i in range(len(node_sizes)): 
        herd = Herd_B(node_sizes[i].row, node_sizes[i].col, i, node_names[i], dependency_list[i])
        herd_list[dependency_list[i]].append(herd)
    naive_placement(herd_list, grid)
    for dep in range(len(herd_list)):
        for herd in range(len(herd_list[dep])):
            herd_list[dep][herd].number = herd_list[dep][herd].dep
    num_tiles_placed = 0
    for time_grid in grid:
        num_tiles_placed += np.count_nonzero(time_grid.grid > -1)
    print("Number of tiles placed: " + str(num_tiles_placed))
    write_to_json(herd_list, [grid[0].num_rows, grid[0].num_cols], "naive.json", len(grid))
    return