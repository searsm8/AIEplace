#!/usr/bin/env python3
"""Run a few larger benchmarks with visualization ON so the exe auto-builds a placement
GIF for each (Output.cpp calls tools/gif_builder.py on the dumped per-iteration PNGs).

Runs sequentially — per-frame cairo rendering of a large design is heavy and there is no
benefit to overlapping it. Reuses sw_only's base config and the same seed/grid/threshold as
the DSE full suite so the animated trajectory matches the swept result. Must be run from
vck5000/ (paths are relative to it), same as dse.py.
"""
import copy
import datetime
import json
import os
import subprocess

from dse import load_config, EXE_PATH, CONFIG_PATH

# Larger benchmarks, each at its XPlace grid (matches _full_suite in dse.py).
VIZ_RUNS = [
    {"label": "adaptec3", "benchmark": "ispd2005/adaptec3", "bins_per_row": 1024},
    {"label": "adaptec4", "benchmark": "ispd2005/adaptec4", "bins_per_row": 1024},
    {"label": "bigblue2", "benchmark": "ispd2005/bigblue2", "bins_per_row": 1024},
]


def main():
    base = load_config(CONFIG_PATH)
    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    out_root = f"results/GIFS_{timestamp}"
    os.makedirs(os.path.join(out_root, "configs"), exist_ok=True)

    for i, run in enumerate(VIZ_RUNS, 1):
        cfg = copy.deepcopy(base)
        cfg["input"]["benchmark"] = "host/benchmarks/" + run["benchmark"]
        cfg["params"]["bins_per_row"] = run["bins_per_row"]
        cfg["params"]["random_seed"] = 42
        cfg["params"]["convergence_overflow_threshold"] = 0.04
        cfg["output"]["visualize"] = True     # dumps per-iter PNGs + auto-builds full_placement.gif
        cfg["output"]["quiet"] = True
        cfg["output"]["results_dir"] = out_root

        cfg_path = os.path.join(out_root, "configs", f"{run['label']}.json")
        with open(cfg_path, "w") as f:
            json.dump(cfg, f, indent=2, ensure_ascii=False)

        print(f"[gif] ({i}/{len(VIZ_RUNS)}) {run['label']} @ {run['bins_per_row']} — running...", flush=True)
        result = subprocess.run([EXE_PATH, cfg_path])
        status = "ok" if result.returncode == 0 else f"FAIL ({result.returncode})"
        print(f"[gif] ({i}/{len(VIZ_RUNS)}) {run['label']} — {status}", flush=True)

    print(f"[gif] done. GIFs at {out_root}/<benchmark>/<run>/full_placement.gif", flush=True)


if __name__ == "__main__":
    main()
