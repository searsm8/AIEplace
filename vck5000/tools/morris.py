# morris.py
# ----------------------------------------------------------------------------
# Morris (Elementary Effects) SCREEN for AIEplace hyperparameters — sampler.
#
# Generates a Morris sample over the factors in morris_factors.py, maps each
# sampled row to a run_config override dict, and writes a run-set JSON that the
# existing dse.py runner consumes (dse.py --runset <path>).
# We reuse dse.py wholesale for config templating / execution / CSV collation —
# this script only does the SALib sampling + the X->config mapping.
#
# Flow:
#   1. python3 tools/morris.py --benchmark ispd2015/mgc_fft_a -r 10   (this file)
#        -> writes results/morris_<ts>/{problem.json, X.npy, runset.json, meta.json}
#      and prints the --runset command to launch the sweep.
#   2. python3 tools/dse.py --runset <runset.json>
#        -> runs every row, produces results/DSE_<ts>/results.csv
#   3. python3 tools/analyze_morris.py <DSE_dir> <morris_dir>
#        -> mu*/sigma ranking + plot, per objective.
# ----------------------------------------------------------------------------

import argparse
import datetime
import json
import os

import numpy as np
from SALib.sample import morris as morris_sample

import benchmarks
from morris_factors import FACTORS, FIXED_OVERRIDES, salib_problem, map_sample_row


def main():
    ap = argparse.ArgumentParser(description="Generate a Morris sample run-set for AIEplace DSE.")
    ap.add_argument("--benchmark", default="ispd2015/mgc_fft_a",
                    help="benchmark path under host/benchmarks/ (default: a fast one)")
    ap.add_argument("--grid", type=int, default=512, help="bins_per_row (default 512)")
    ap.add_argument("-r", "--trajectories", type=int, default=10,
                    help="Morris trajectories r; total runs = r*(k+1) (default 10)")
    ap.add_argument("--num-levels", type=int, default=4, help="Morris grid levels p (default 4)")
    ap.add_argument("--seed", type=int, default=1234, help="SALib sampling RNG seed (sampler, not placer)")
    ap.add_argument("--out", default=None, help="output dir (default results/morris_<ts>)")
    args = ap.parse_args()

    # Reject out-of-scope / mistyped designs at launch (master manifest).
    args.benchmark = benchmarks.resolve(args.benchmark)

    problem = salib_problem()
    k = problem["num_vars"]

    # optimal_trajectories improves EE coverage but is O(N^2); skip for speed.
    X = morris_sample.sample(problem, N=args.trajectories,
                             num_levels=args.num_levels, seed=args.seed)
    total = X.shape[0]
    assert total == args.trajectories * (k + 1), (total, args.trajectories, k)

    # Map each sampled row -> a dse.py explicit-run override dict.
    fixed = dict(FIXED_OVERRIDES)
    fixed.pop("benchmark", None)            # benchmark comes from --benchmark
    runset = []
    for i, row in enumerate(X):
        overrides = map_sample_row(row)
        run = {"label": f"m{i:04d}",
               "benchmark": args.benchmark,
               "bins_per_row": args.grid,
               **fixed,
               **overrides}
        runset.append(run)

    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    out = args.out or f"results/morris_{ts}"
    os.makedirs(out, exist_ok=True)
    np.save(os.path.join(out, "X.npy"), X)
    with open(os.path.join(out, "problem.json"), "w") as f:
        json.dump(problem, f, indent=2)
    with open(os.path.join(out, "runset.json"), "w") as f:
        json.dump(runset, f, indent=2)
    with open(os.path.join(out, "meta.json"), "w") as f:
        json.dump({"benchmark": args.benchmark, "grid": args.grid,
                   "trajectories": args.trajectories, "num_levels": args.num_levels,
                   "seed": args.seed, "num_vars": k, "total_runs": total,
                   "factor_names": problem["names"],
                   "factor_kinds": [fac["kind"] for fac in FACTORS]}, f, indent=2)

    runset_path = os.path.join(out, "runset.json")
    print(f"Morris sample: {k} factors x {args.trajectories} trajectories = {total} runs")
    print(f"Wrote: {out}/  (X.npy, problem.json, runset.json, meta.json)")
    print(f"\nLaunch the sweep:")
    print(f"  python3 tools/dse.py --runset {runset_path}\n")
    print(f"Then analyze:")
    print(f"  python3 tools/analyze_morris.py <results/DSE_...dir> {out}")


if __name__ == "__main__":
    main()
