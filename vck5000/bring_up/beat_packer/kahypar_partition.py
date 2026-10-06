#!/usr/bin/env python3
"""#42 quality oracle: partition an hMETIS hypergraph with KaHyPar (direct k-way, km1 preset).

    kahypar_partition.py GRAPH.hgr K EPSILON OUT.part [--preset FILE] [--seed S]

GRAPH.hgr is what `partition_study --export-hgr` writes: line 1 "num_nets num_nodes 11"
(weighted nets and nodes), then one line per net "weight pin pin ...", 1-based, then one
weight per node. OUT.part gets one block id per node. Single-threaded (KaHyPar is sequential).
Reference only: it optimizes km1 under vertex-weight balance, not our external-slot capacity,
so partition_study scores its result exactly like every other method. Meow.
"""
import os, sys, time
import kahypar

PRESET = os.path.expanduser("~/aieplace_tmp/kahypar/km1_kKaHyPar_sea20.ini")


def read_hgr(path):
    with open(path) as f:
        num_nets, num_nodes, fmt = (int(x) for x in f.readline().split())
        assert fmt == 11, "expects weighted nets and nodes"
        net_index, pins, net_weights = [0], [], []
        for _ in range(num_nets):
            fields = [int(x) for x in f.readline().split()]
            net_weights.append(fields[0])
            pins.extend(p - 1 for p in fields[1:])
            net_index.append(len(pins))
        node_weights = [int(f.readline()) for _ in range(num_nodes)]
    return num_nodes, num_nets, net_index, pins, net_weights, node_weights


def main():
    args = sys.argv[1:]
    preset, seed = PRESET, 42
    if "--preset" in args:
        i = args.index("--preset"); preset = args[i + 1]; del args[i:i + 2]
    if "--seed" in args:
        i = args.index("--seed"); seed = int(args[i + 1]); del args[i:i + 2]
    graph, k, epsilon, out = args[0], int(args[1]), float(args[2]), args[3]

    start = time.time()
    num_nodes, num_nets, net_index, pins, net_weights, node_weights = read_hgr(graph)
    read_s = time.time() - start
    hypergraph = kahypar.Hypergraph(num_nodes, num_nets, net_index, pins, k, net_weights, node_weights)
    context = kahypar.Context()
    context.loadINIconfiguration(preset)
    context.setK(k)
    context.setEpsilon(epsilon)
    context.setSeed(seed)
    context.suppressOutput(True)
    start = time.time()
    kahypar.partition(hypergraph, context)
    partition_s = time.time() - start
    with open(out, "w") as f:
        f.write("\n".join(str(hypergraph.blockID(v)) for v in range(num_nodes)) + "\n")
    print(f"kahypar: {num_nodes} nodes, {num_nets} nets, k={k} eps={epsilon}: km1={kahypar.connectivityMinusOne(hypergraph)} "
          f"read {read_s:.1f}s partition {partition_s:.1f}s")


if __name__ == "__main__":
    main()
