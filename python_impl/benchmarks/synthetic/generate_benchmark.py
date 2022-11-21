# generate_benchmark.py
# 
# Create a synthetic benchmark to be used with the AIEplacer.py algorithm
# Outputs a .json file which specifies a grid size, herd sizes, and dependencies

import random, math

node_types2runtimes = {
    "mm": 500,
    "gelu": 50,
    "a": 100,
    "b": 200,
    "c": 300,
    "n": 250,
    "k": 150,
    "x": 800,
    "y": 1000,
    "z": 2000
}
node_types2size = {
    "mm": "[2, 2]",
    "gelu": "[1, 1]",
    "a": "[1, 1]",
    "b": "[2, 1]",
    "c": "[1, 2]",
    "n": "[1, 2]",
    "k": "[2, 1]",
    "x": "[1, 1]",
    "y": "[2, 2]",
    "z": "[2, 2]"
}

grid_sizes = [(4,5), (4,10), (8,10), (8,20), (8,50)]
def generateAIEplaceBenchmark(bench_name, bench_num, grid_index):

    num_rows = grid_sizes[grid_index][0]
    num_cols = grid_sizes[grid_index][1]
    mult = bench_num%10 + 1 # size multiplier

    node_counts = {}
    node_counts["mm"]= random.choice(range(6*mult, 10*mult, 1))
    node_counts["gelu"] = round(node_counts["mm"] / 2)
    node_counts["a"] = random.choice(range(3*mult, 5*mult, 1))
    node_counts["b"] = random.choice(range(3*mult, 5*mult, 1))
    node_counts["c"] = random.choice(range(3*mult, 5*mult, 1))
    node_counts["n"] = random.choice(range(3*mult, 5*mult, 1))
    node_counts["k"] = random.choice(range(3*mult, 5*mult, 1))

    node_counts["x"] = random.choice(range(1*mult, 3*mult, 1))
    node_counts["y"] = random.choice(range(1*mult, 3*mult, 1))
    node_counts["z"] = random.choice(range(1*mult, 3*mult, 1))

    num_nodes= sum(node_counts.values())
    num_nets = random.choice(range(round(num_nodes*.75), num_nodes, 1))


    nodes = []
    sizes = []
    runtimes = []
    for key in node_counts.keys():
        for i in range(node_counts[key]):
            nodes.append(f"{key}{i}")
            sizes.append(node_types2size[key])
            runtimes.append(node_types2runtimes[key])

    with open(bench_name+"_" + str(bench_num) + ".json", "w") as bench:
        bench.write("{\n")
        bench.write('"grid_info": {\n')
        bench.write(f'\t"num_rows": {num_rows},\n')
        bench.write(f'\t"num_cols": {num_cols}\n')
        bench.write('},\n')

        bench.write('"node_sizes": {')
        for i in range(len(nodes)):
            bench.write(f'\n\t"{nodes[i]}": {sizes[i]}')
            if i < len(nodes)-1: bench.write(',')
        bench.write('\n},\n')

        bench.write('"node_runtimes": {')
        for i in range(len(nodes)):
            bench.write(f'\n\t"{nodes[i]}": {runtimes[i]}')
            if i < len(nodes)-1: bench.write(',')
        bench.write('\n},\n')
        
        bench.write('"nets": {\n')
        for i in range(num_nets):
            bench.write(f'\t"net{i}": ["{nodes[i]}"')
            prob = 1
            while(random.random() < prob):
                bench.write(f', "{nodes[random.choice(range(i+1, num_nodes))]}"')
                prob *= 0.2
            bench.write('],\n' if i < num_nets-1 else ']\n')
        bench.write('}\n')

        bench.write("}\n")

if __name__ == "__main__":
    random.seed(1)
    bench_name_root = "synthetic"
    NUM_TO_GENERATE = 100
    for i in range(NUM_TO_GENERATE):
        generateAIEplaceBenchmark(bench_name_root, i, math.floor(i / (NUM_TO_GENERATE / 5)))
