#!/usr/bin/env python3
"""Render a run's convergence-history plots from iterations.dat.

Reads <run_dir>/iterations.dat ("Iter, HPWL, OVFW, step_len, density_weight, BkSteps", written
unconditionally by Placer::appendIterationLog every iteration -- no visualization toggle gates
it) and produces the same 5 PNGs the old cairo CairoPlotter did (TODO #16 step 5 ported this out
of Visualizer.h; TODO #18 set the design this matches): one chart per metric, plus a stacked
overview sharing one x-axis.

X-axis is the 1-based row index (iterations since run start), not iterations.dat's own Iter
column -- that column is phase-relative and goes non-monotonic across a phase-2 restart.

Usage:
    python3 tools/plot_histories.py <run_dir> [--out DIR]
"""
import argparse
import csv
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# Identity colors, one per metric -- dataviz-skill categorical palette (TODO #18): first three
# fixed-order slots (blue/orange/aqua) plus violet in place of yellow for density_weight, whose
# contrast against a white background measured too low for a 2px line.
COLORS = {
    "hpwl":           "#2A78D6",
    "overflow":       "#EB6834",
    "step_length":    "#1BAF7A",
    "density_weight": "#4A3AA7",
}
PANELS = [  # (key, title, ylabel, log_scale, filename) -- shared by the individual charts and overview
    ("hpwl",           "HPWL Convergence",     "HPWL",           False, "hpwl_history.png"),
    ("overflow",       "Overflow",             "Overflow",       False, "ovfw_history.png"),
    ("step_length",    "Step Length History",  "Step Length",    False, "step_length_history.png"),
    ("density_weight", "Density Weight",       "Density Weight", True,  "density_weight_history.png"),
]


def read_iterations_dat(path):
    """iterations.dat -> {hpwl, overflow, step_length, density_weight} float lists, row order."""
    cols = {"hpwl": [], "overflow": [], "step_length": [], "density_weight": []}
    with open(path, newline="") as f:
        reader = csv.reader(f)
        next(reader)  # header
        for row in reader:
            cols["hpwl"].append(float(row[1]))
            cols["overflow"].append(float(row[2]))
            cols["step_length"].append(float(row[3]))
            cols["density_weight"].append(float(row[4]))
    return cols


def plot_one(data, title, ylabel, color, log_scale, out_path):
    fig, ax = plt.subplots(figsize=(8, 6), dpi=100)
    ax.plot(range(1, len(data) + 1), data, color=color, linewidth=2)
    if log_scale:
        ax.set_yscale("log")
    ax.set_title(title + (" (log scale)" if log_scale else ""), fontsize=14, fontweight="bold")
    ax.set_xlabel("Iteration")
    ax.set_ylabel(ylabel)
    ax.grid(True, color="#e0e0e0", linewidth=0.8)
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)


def plot_overview(cols, out_path):
    """Four panels stacked on one shared x-axis, in place of the old normalize-and-overlay
    combined_history.png (dropped 2026-08-05, TODO #18 -- overlaying differently-scaled series
    risks manufacturing a correlation that isn't really there)."""
    fig, axes = plt.subplots(len(PANELS), 1, figsize=(9, 9), dpi=100, sharex=True)
    for ax, (key, _title, label, log_scale, _filename) in zip(axes, PANELS):
        data = cols[key]
        ax.plot(range(1, len(data) + 1), data, color=COLORS[key], linewidth=1.5)
        if log_scale:
            ax.set_yscale("log")
        ax.set_ylabel(label + (" (log)" if log_scale else ""), fontsize=9)
        ax.grid(True, color="#e0e0e0", linewidth=0.8)
    axes[-1].set_xlabel("Iteration")
    fig.suptitle("Placement Convergence Overview", fontsize=14, fontweight="bold")
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run_dir", help="run directory containing iterations.dat")
    ap.add_argument("--out", default=None, help="default: <run_dir>/graphs/")
    args = ap.parse_args()

    dat_path = os.path.join(args.run_dir, "iterations.dat")
    if not os.path.exists(dat_path):
        raise SystemExit(f"no iterations.dat in {args.run_dir}")

    cols = read_iterations_dat(dat_path)
    if not cols["hpwl"]:
        raise SystemExit(f"{dat_path} has no data rows")

    out_dir = args.out or os.path.join(args.run_dir, "graphs")
    os.makedirs(out_dir, exist_ok=True)

    for key, title, label, log_scale, filename in PANELS:
        plot_one(cols[key], title, label, COLORS[key], log_scale, os.path.join(out_dir, filename))
    plot_overview(cols, os.path.join(out_dir, "overview.png"))

    print(f"Wrote 5 PNGs to {out_dir}")


if __name__ == "__main__":
    main()
