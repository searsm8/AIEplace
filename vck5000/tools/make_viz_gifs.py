#!/usr/bin/env python3
"""Run MMS benchmarks with visualization ON so the exe auto-builds a placement GIF for each
(Output.cpp calls tools/gif_builder.py on the dumped per-iteration PNGs).

Defaults to designs where the mixed-size phase 2 (TODO #13) actually fires, so the GIF shows the
full two-phase trajectory: mixed-size global placement -> macro legalization -> the standard-cell
re-seed -> phase 2. The visualizer marks these frames (phase banner in the overlay, frozen macros
in purple) and Phase2.cpp forces an extra frame on each side of the transition, which the regular
iterations_per_export cadence would otherwise straddle.

Runs sequentially -- per-frame cairo rendering of a large design is heavy and there is no benefit
to overlapping it. Grid and target_density come from tools/benchmarks.py's XPlace values, and the
seed is pinned, so the animated trajectory matches the swept result
(2_ARTIFACTS/phase2_suite_results.tsv). Must be run from vck5000/, same as dse.py.

Usage:
  python3 tools/make_viz_gifs.py                       # the default 3-design set
  python3 tools/make_viz_gifs.py --designs newblue5    # one design
  python3 tools/make_viz_gifs.py --every 20            # fewer, coarser frames
  python3 tools/make_viz_gifs.py --zoom                # + a zoom GIF of the die centre
  python3 tools/make_viz_gifs.py --zoom 0.25,0.75,0.02 # + a zoom GIF of a chosen region
"""
import argparse
import datetime
import os
import subprocess

import tomlkit

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE = os.path.join(REPO, "host/src/sw_only/run_config.toml")
EXE = os.path.join(REPO, "build/hw/host/sw_only/aieplace_sw_only.exe")

# design -> (xplace_grid, xplace_target_density), from tools/benchmarks.py _ROWS (mms tier).
MMS = {
    "adaptec1": (512, 1.0),  "adaptec2": (1024, 1.0), "adaptec3": (1024, 1.0),
    "adaptec4": (1024, 1.0), "adaptec5": (1024, 0.5), "bigblue1": (512, 1.0),
    "bigblue2": (1024, 1.0), "bigblue3": (2048, 1.0), "bigblue4": (2048, 1.0),
    "newblue1": (512, 0.8),  "newblue2": (1024, 0.9), "newblue3": (2048, 0.8),
    "newblue4": (1024, 0.5), "newblue5": (1024, 0.5), "newblue6": (2048, 0.8),
    "newblue7": (2048, 0.8),
}

# Chosen for what each one SHOWS, not for coverage -- rendering every design is hours of cairo.
#   newblue5 - the flagship phase-2 design (memory phase2-implemented-newblue5-converges)
#   newblue1 - smallest grid that still takes a clean two-phase run; quickest turnaround
#   adaptec2 - the only design whose macro legalization needed the longest-path repair loop
#              (-3.53e3 total negative slack -> 0, then 42 overlapping pairs -> 0)
# adaptec5 is deliberately absent: its phase 1 ends diverged_hpwl, so phase 2 is skipped by design.
DEFAULT_DESIGNS = ["newblue1", "adaptec2", "newblue5"]


def write_config(design, cfg_dir, out_root, seed, every, zoom):
    grid, target_density = MMS[design]
    with open(TEMPLATE) as f:
        cfg = tomlkit.parse(f.read())

    cfg["input"]["benchmark"] = f"host/benchmarks/mms/{design}"

    params = cfg["params"]
    params["bins_per_row"] = grid
    params["maximum_utilization"] = target_density
    params["random_seed"] = seed

    out = cfg["output"]
    out["visualize"] = True            # per-iter PNGs + auto-built full_placement.gif
    out["iterations_per_export"] = every
    out["quiet"] = True
    out["interactive"] = False
    out["results_dir"] = os.path.join(out_root, design)

    # Zoom view (TODO #14): a second PNG stream in placement_zoom/, auto-built into
    # zoom_placement.gif by the same exe. Doubles the cairo cost per frame, so it is opt-in.
    if zoom is not None:
        center_x, center_y, span = zoom
        out["zoom"] = True
        out["zoom_center_x"] = center_x
        out["zoom_center_y"] = center_y
        out["zoom_span"] = span

    path = os.path.join(cfg_dir, f"{design}.toml")
    with open(path, "w") as f:
        f.write(tomlkit.dumps(cfg))
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--designs", nargs="*", default=DEFAULT_DESIGNS)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--every", type=int, default=10, help="iterations_per_export (frame cadence)")
    ap.add_argument("--out-root", default=None, help="default: 2_ARTIFACTS/GIFS_<timestamp>")
    ap.add_argument("--zoom", nargs="?", const="0.5,0.5,0.05", default=None,
                    metavar="CX,CY,SPAN",
                    help="also render the zoom view (TODO #14) into zoom_placement.gif. "
                         "CX/CY are the window centre as a fraction of die width/height and "
                         "SPAN its side as a fraction of the shorter die dimension. "
                         "Bare --zoom means the die centre at 0.05 (1/400th of the die area).")
    args = ap.parse_args()

    zoom = None
    if args.zoom is not None:
        try:
            center_x, center_y, span = (float(v) for v in args.zoom.split(","))
        except ValueError:
            raise SystemExit(f"--zoom wants CX,CY,SPAN as three floats, got: {args.zoom}")
        zoom = (center_x, center_y, span)

    for design in args.designs:
        if design not in MMS:
            raise SystemExit(f"unknown MMS design: {design}")

    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    # 2_ARTIFACTS is gitignored -- GIF sets run to tens of MB and must not reach the repo.
    out_root = args.out_root or os.path.join(REPO, "2_ARTIFACTS", f"GIFS_{timestamp}")
    cfg_dir = os.path.join(out_root, "configs")
    os.makedirs(cfg_dir, exist_ok=True)

    for i, design in enumerate(args.designs, 1):
        cfg_path = write_config(design, cfg_dir, out_root, args.seed, args.every, zoom)
        grid, td = MMS[design]
        print(f"[gif] ({i}/{len(args.designs)}) {design} @ grid {grid}, td {td} — running...",
              flush=True)
        result = subprocess.run([EXE, cfg_path], cwd=REPO)
        status = "ok" if result.returncode == 0 else f"FAIL ({result.returncode})"
        print(f"[gif] ({i}/{len(args.designs)}) {design} — {status}", flush=True)

    print(f"[gif] done. GIFs at {out_root}/<design>/<benchmark>/<run>/full_placement.gif",
          flush=True)


if __name__ == "__main__":
    main()
