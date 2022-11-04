# generate_benchmark.py
# 
# Create a synthetic benchmark to be used with the AIEplacer.py algorithm
# Outputs a .json file which specifies a grid size, herd sizes, and dependencies

import random

def generateAIEplaceBenchmark(bench_name):
    num_rows = 8 #random.choice(range(8, 9, 4))
    num_cols = random.choice(range(5, 21, 5))
    area = num_rows*num_cols
    num_nodes= random.choice(range(round(area*1.5), round(area*2.5), 1))
    num_nets = random.choice(range(round(num_nodes*.75), num_nodes, 1))

    with open(bench_name+".json", "w") as bench:
        bench.write("{\n")
        bench.write('"grid_info": {\n')
        bench.write(f'\t"num_rows": {num_rows},\n')
        bench.write(f'\t"num_cols": {num_cols}\n')
        bench.write(f'}},\n')
        bench.write(f'')

        bench.write('"node_sizes": {\n')
        for i in range(num_nodes):
            bench.write(f'\t"n{i}": [{random.choice([1,2])}, {random.choice([1,2])}')
            bench.write('],\n' if i < num_nodes-1 else ']\n')
        bench.write(f'}},\n')

        bench.write('"nets": {\n')
        for i in range(num_nets):
            bench.write(f'\t"net{i}": ["n{i}"')
            prob = 1
            while(random.random() < prob):
                bench.write(f', "n{random.choice(range(i+1, num_nodes))}"')
                prob *= 0.2
            bench.write('],\n' if i < num_nets-1 else ']\n')
        bench.write(f'}}\n')
        bench.write("}")
if __name__ == "__main__":
    bench_name_root = "synthetic"
    NUM_TO_GENERATE = 10
    for i in range(NUM_TO_GENERATE):
        generateAIEplaceBenchmark(bench_name_root + "_" + str(i))
