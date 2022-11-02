# main.py
# Driver code for AIEplacer
from AIEplacer import *
import cProfile
import sys
import logging
import random
from naivePlacer import *

ExampleDict = {
    "a": [[], [2, 1]],
    "b": [[], [2, 2]],
    "c": [[], [1, 2]],
    "d": [["a", "b"], [2, 2]],
    "e": [["b", "c"], [2, 1]],
    "f": [[], [2, 2]],
    "g": [["d"], [2, 2]],
    "h": [["e"], [1, 1]],
    "i": [["e", "f"], [2, 1]],
    "j": [["g", "h"], [2, 1]],
    "k": [["i"], [2, 2]],
    "l": [[], [1, 2]],
    "m": [["l"], [1, 1]],
    "n": [["m", "k"], [2, 2]],
    "o": [[], [2, 2]],
    "p": [["o"], [1, 1]]
}
# ExampleDictNet = [["a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n"], ["o", "p"]]
ExampleDictNet = [["a", "d"], ["b", "d", "e"], ["c", "e"], ["d", "g"], ["e", "h", "i"], ["g", "j"], ["h", "j"], ["f", "i"], ["l", "m"], ["o", "p"], ["i", "k"], ["m", "n"], ["k", "n"]]
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


def createRandomDesign():
    num_rows = 4
    num_cols = 5
    grid = Grid(num_rows, num_cols)
    node_count = 16
    coords = Design.initializeCoords(grid, node_count)
    dependencies = []
    dep_dict = {}
    node_names = []
    node_sizes = []
    nets = []
    total_size = 0
    #if (make_random):
    #    dependencies = initializeDependencies(node_count)
    #    for i in range(len(coords)):
    #        node_names.append("k"+str(i))
    #        # node_sizes.append(Coord(.8, .8))
    #        num1 = random.random()
    #        num2 = random.random()
    #        row_size = 1
    #        col_size = 1
    #        if (num1 > .8):
    #            row_size = 2
    #        if (num2 > .8):
    #            col_size = 2
    #        node_sizes.append(Coord(row_size, col_size))
    #        total_size += col_size * row_size
    #    for i in range(int((2+random.random()) * len(coords))):
    #        nets.append(makeRandomNet(len(coords)))
    #else:
    dep_dict = ExampleDict
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
    design = Design(coords, node_names, node_sizes, dependencies, map_nets_to_list(ExampleDictNet, ExampleDict))
    return design, grid

def runAIEPlacer():
    make_random = False
    logging.root.name = 'AIEplace'
    logging.basicConfig(level=logging.INFO,
                        format='[%(levelname)-7s] %(name)s - %(message)s',
                        stream=sys.stdout)
    
    # Create a design environment and run AIEplacer
    #design, grid = createRandomDesign()
    filename = "simple"
    design, grid = Design.readJSON("./benchmarks/"+filename+".json")

    placer = AIEplacer(grid, design, grid.num_cols)
    placer.run(999)


if __name__ == "__main__":
    random.seed(1)
    #cProfile.run('runAIEPlacer()')
    runAIEPlacer()

