#!/usr/bin/env python3
"""Configs for a full-suite sw_only run, any of the three benchmark tiers.

Supersedes gen_phase2_suite_configs.py, which was MMS-only and carried its own copy of the
per-design grid/target_density table. This one reads tools/benchmarks.py -- the master manifest,
which holds XPlace's own tuned values from utils/setup_dataset.py -- so there is one table, not
two that can drift.

Why grid and target_density must be set explicitly (memory `mms-needs-explicit-target-density`):
Bookshelf suites (ispd2005, mms) ship no `placement.constraints`, so a template-derived config
silently sits at target_density = 1.0. ISPD2015 does ship one and DataBase overrides the config
value from it, so setting it there is a harmless no-op that keeps the configs uniform.

Usage:
  python3 tools/gen_suite_configs.py --suites ispd2005 ispd2015 \
      --outdir /tmp/full44/configs --results-root /tmp/full44/results
"""
import argparse
import os
import sys

import tomlkit

REPO = "/home/msears/phd/AIEplace/vck5000"
TEMPLATE = os.path.join(REPO, "host/src/sw_only/default_config.toml")
sys.path.insert(0, os.path.join(REPO, "tools"))
import benchmarks as B  # noqa: E402


def write_config(path_key, outdir, results_root, seed, max_iters):
    meta = B.BENCHMARKS[path_key]
    design, suite = meta["name"], meta["suite"]

    with open(TEMPLATE) as f:
        cfg = tomlkit.parse(f.read())

    cfg["input"]["benchmark"] = f"host/benchmarks/{suite}/{design}"

    params = cfg["params"]
    params["bins_per_row"] = meta["grid"]
    params["maximum_utilization"] = meta["target_density"]
    params["random_seed"] = seed
    params["deterministic"] = True
    if max_iters:
        params["convergence_max_iterations"] = max_iters

    out = cfg["output"]
    out["quiet"] = False
    out["interactive"] = False
    out["visualize"] = False
    out["dump_positions"] = False          # ~100-500 MB/run; nothing here renders it
    # Run dirs are keyed by SUITE/DESIGN: adaptec1 exists in both ispd2005 and mms, and a bare
    # design name would collide and silently interleave two different designs' runs.
    out["results_dir"] = os.path.join(results_root, suite, design)

    fn = f"{suite}_{design}.toml"
    with open(os.path.join(outdir, fn), "w") as f:
        f.write(tomlkit.dumps(cfg))
    return fn


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--suites", nargs="+", default=["ispd2005", "ispd2015", "mms"])
    ap.add_argument("--outdir", default="/tmp/full44/configs")
    ap.add_argument("--results-root", default="/tmp/full44/results")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--max-iters", type=int, default=0, help="0 = keep the template's value")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    keys = [p for p, m in B.BENCHMARKS.items() if m["suite"] in args.suites]
    keys.sort(key=lambda p: (B.BENCHMARKS[p]["suite"], B.BENCHMARKS[p]["name"]))
    for k in keys:
        write_config(k, args.outdir, args.results_root, args.seed, args.max_iters)
    print(f"wrote {len(keys)} configs to {args.outdir}")
    for s in args.suites:
        n = sum(1 for k in keys if B.BENCHMARKS[k]["suite"] == s)
        print(f"  {s:10} {n:3}")


if __name__ == "__main__":
    main()
