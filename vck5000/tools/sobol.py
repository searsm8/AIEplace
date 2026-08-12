# sobol.py
# ----------------------------------------------------------------------------
# Sobol (variance-based) sensitivity analysis — sampler. The second, rigorous
# stage after the Morris screen: quantifies how much of the output variance each
# factor explains on its own (first-order S1) and including all its interactions
# (total-order ST). ST - S1 isolates the interaction contribution that Morris's
# sigma only flagged.
#
# Mirrors morris.py: reuses the SAME factor spec (morris_factors.py) and the SAME
# dse.py runner (--runset <runset.json>; the loader is format-agnostic). Only the
# sampler differs (Saltelli sequence, not Morris trajectories).
#
#   python3 tools/sobol.py --benchmark ispd2015/mgc_fft_a -N 128
#     -> results/sobol_<ts>/{problem.json, X.npy, runset.json, meta.json}
#   python3 tools/dse.py --runset <runset.json>
#   python3 tools/analyze_sobol.py <DSE_dir> <sobol_dir>
#
# Cost = N*(k+2) with calc_second_order=False, or N*(2k+2) with second-order.
# N should be a power of 2 for the Sobol sequence's convergence properties.
# ----------------------------------------------------------------------------

import argparse
import datetime
import json
import os

import numpy as np
try:
    from SALib.sample import sobol as sobol_sample          # SALib >= 1.4.5
    _SAMPLE = lambda problem, N, so: sobol_sample.sample(problem, N, calc_second_order=so)
except ImportError:
    from SALib.sample import saltelli                        # older name
    _SAMPLE = lambda problem, N, so: saltelli.sample(problem, N, calc_second_order=so)

import benchmarks
from morris_factors import FIXED_OVERRIDES, salib_problem, map_sample_row


def main():
    ap = argparse.ArgumentParser(description="Generate a Sobol/Saltelli sample run-set for AIEplace DSE.")
    ap.add_argument("--benchmark", default="ispd2015/mgc_fft_a")
    ap.add_argument("--grid", type=int, default=512)
    ap.add_argument("-N", "--base-samples", type=int, default=128,
                    help="Saltelli base sample size N (power of 2); runs = N*(k+2) or N*(2k+2)")
    ap.add_argument("--second-order", action="store_true",
                    help="also estimate pairwise S2 (doubles cost to N*(2k+2))")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    args.benchmark = benchmarks.resolve(args.benchmark)
    problem = salib_problem()
    k = problem["num_vars"]

    X = _SAMPLE(problem, args.base_samples, args.second_order)
    total = X.shape[0]

    fixed = dict(FIXED_OVERRIDES)
    fixed.pop("benchmark", None)
    runset = []
    for i, row in enumerate(X):
        runset.append({"label": f"m{i:05d}",
                       "benchmark": args.benchmark,
                       "bins_per_row": args.grid,
                       **fixed,
                       **map_sample_row(row)})

    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    out = args.out or f"results/sobol_{ts}"
    os.makedirs(out, exist_ok=True)
    np.save(os.path.join(out, "X.npy"), X)
    json.dump(problem, open(os.path.join(out, "problem.json"), "w"), indent=2)
    json.dump(runset, open(os.path.join(out, "runset.json"), "w"), indent=2)
    json.dump({"method": "sobol", "benchmark": args.benchmark, "grid": args.grid,
               "base_samples": args.base_samples, "calc_second_order": bool(args.second_order),
               "num_vars": k, "total_runs": total, "factor_names": problem["names"]},
              open(os.path.join(out, "meta.json"), "w"), indent=2)

    rs = os.path.join(out, "runset.json")
    print(f"Sobol sample: N={args.base_samples}, {k} factors, second_order={args.second_order} "
          f"-> {total} runs")
    print(f"Wrote: {out}/")
    print(f"\nLaunch:\n  python3 tools/dse.py --runset {rs}")
    print(f"Analyze:\n  python3 tools/analyze_sobol.py <results/DSE_...dir> {out}")


if __name__ == "__main__":
    main()
