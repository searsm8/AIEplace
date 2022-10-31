# main.py
# Driver code for AIEplacer
from AIEplacer import *
import cProfile
import sys
import logging
import random
from naivePlacer import *

ExampleDict = {
    "k0": [[], [2, 2]],
    "k1": [[], [2, 2]],
    "k2": [[], [2, 2]],
    "k3": [["k0", "k1"], [2, 2]],
    "k4": [["k1", "k2"], [2, 2]],
    "k5": [[], [2, 2]],
    "k6": [["k3"], [2, 2]],
    "k7": [["k4"], [2, 2]],
    "k8": [["k4", "k5"], [2, 2]],
    "k9": [["k6", "k7"], [2, 2]],
    "k10": [["k8"], [2, 2]],
    "k11": [[], [2, 2]],
    "k12": [["k11"], [2, 2]],
    "k13": [["k12", "k10"], [2, 2]],
    "k14": [[], [2, 2]],
    "k15": [["k14"], [2, 2]]
}

AIE_dependency_dict = {
    "a+": [[], [1, 1]], # a+
    "a-": [[], [1, 1]], # a-
    "b+": [["a+"], [1, 1]], # b+
    "b-": [["a-"], [1, 1]], # b-
    "c+": [["a+"], [1, 1]], # c+
    "c-": [["a-"], [1, 1]], # c-
    "WL": [["b+", "c+", "b-", "c-"], [2, 2]], # WL 
    "dWL": [["a+", "b+", "c+", "b-", "c-", "a-"], [2, 2]], # dWL
    "p": [[], [1, 1]], # p
    "alpha": [["p"], [2, 2]], # alpha
    "psi": [["alpha"], [2, 2]], # psi
    "xi": [["alpha"], [2, 2]], # xi
    "D": [["psi"], [1, 1]], # D
    "dD": [["xi"], [1, 1]] # dD
}

def convert_dep_to_force(dependency_list, node_names):
    new_dep_list = []
    for i in range(len(node_names)):
        for j in range(len(dependency_list)):
            if node_names[i] in dependency_list[j]:
                new_dep_list.append(j)
                break
    return new_dep_list
        
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
        # CENTER INITIALIZATION
        #coords.append( Coord( random.choice(range(round(grid.num_rows*.4), round(grid.num_rows*.6))) + random.random(), 
        #                      random.choice(range(round(grid.num_cols*.4), round(grid.num_cols*.6))) + random.random() ) )
        # CENTER INITIALIZATION
        coords.append( Coord( random.choice(range(round(grid.num_rows*.3), round(grid.num_rows*.7))) + random.random(), 
                              random.choice(range(0, round(grid.num_cols*.3))) + random.random() ) )
    return coords

def initializeDependencies(node_count):
    dependencies = []
    for i in range(node_count):
        num = random.random()
        if(num < 0.5):
            dependencies.append(0)
        elif(num < 0.8):
            dependencies.append(1)
        elif(num < 0.9):
            dependencies.append(2)
        elif(num < 0.935):
            dependencies.append(3)
        elif(num < 0.96):
            dependencies.append(4)
        elif(num < 0.98):
            dependencies.append(5)
        elif(num < 0.99):
            dependencies.append(6)
        else: 
            dependencies.append(7)
    return dependencies

def get_dep_herds(dictionary):
    """Moves the dependencies from a dictionary to a list that has the length of the maximum
       dependency number - 1, with each sub list containing the herds at that dependency level.

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
        # print(same_dep_keys)
        while same_dep_keys:
            del dictionary[same_dep_keys.pop(0)]
        # unplaced_herds[i].sort(key=lambda x: x.size, reverse=True)
        # place herds
        dependency_sorted_herds.append(unplaced_herds)
        counter += 1
        if counter == 100:
            print("max iterations reached in getting dependencies")
            break
    return dependency_sorted_herds

def runAIEPlacer():
    make_random = False
    logging.root.name = 'AIEplace'
    logging.basicConfig(level=logging.INFO,
                        format='[%(levelname)-7s] %(name)s - %(message)s',
                        stream=sys.stdout)
    
    # Create a design environment and run AIEplacer
    num_rows = 4
    num_cols = 5
    grid = Grid(num_rows, num_cols)
    node_count = -1
    if make_random:
        node_count = 4
    else:
        node_count = 14
    coords = initializeCoords(grid, node_count)
    dependencies = []
    dep_dict = {}
    node_names = []
    node_sizes = []
    nets = []
    total_size = 0
    if (make_random):
        dependencies = initializeDependencies(node_count)
        for i in range(len(coords)):
            node_names.append("k"+str(i))
            # node_sizes.append(Coord(.8, .8))
            num1 = random.random()
            num2 = random.random()
            row_size = 1
            col_size = 1
            if (num1 > .8):
                row_size = 2
            if (num2 > .8):
                col_size = 2
            node_sizes.append(Coord(row_size, col_size))
            total_size += col_size * row_size
        for i in range(int((2+random.random()) * len(coords))):
            nets.append(makeRandomNet(len(coords)))
    else:
        dep_dict = AIE_dependency_dict
        node_names = list(dep_dict.keys())
        for i in range(len(node_names)):
            node_sizes.append(Coord(dep_dict[node_names[i]][1][0], dep_dict[node_names[i]][1][1]))
            total_size += node_sizes[i].row * node_sizes[i].col
        deps = get_dep_herds(dep_dict)
        dependencies = convert_dep_to_force(deps, node_names)
        run_brandon_placement(num_rows, num_cols, node_sizes, node_names, deps)

    print("Number of tiles to be placed: " + str(total_size))
    if (total_size > num_rows * num_cols):
        # + 5 is arbitrary - should be larger for larger problems.
        grid = Grid(num_rows, math.ceil(total_size / num_rows) + num_cols)

    design = Design(coords, node_names, node_sizes, dependencies, nets)
    print(dependencies)
    placer = AIEplacer(grid, design, num_cols)
    placer.run(999)

    print("Number of tiles to be placed: " + str(total_size))

if __name__ == "__main__":
    random.seed(1)
    #cProfile.run('runAIEPlacer()')
    runAIEPlacer()

