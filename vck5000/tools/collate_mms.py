#!/usr/bin/env python3
"""Collate sw_only MMS suite results vs the XPlace reference.

For each design under results/mms_suite_precondON/<design>/<ts>_cpu_cpu/, read
run_summary.md for final exact HPWL / overflow / best solution, and compare to
the XPlace GP + legal reference table.
"""
import glob
import os
import re

REPO = "/home/msears/phd/AIEplace/vck5000"
SUITE_DIR = os.path.join(REPO, "results/mms_suite_precondON")

# XPlace reference: design -> (GP exact HPWL, legal-after-DP HPWL)
XPLACE = {
    "adaptec1": (6.238e7, 6.814e7), "adaptec2": (7.270e7, 7.618e7),
    "adaptec3": (1.544e8, 1.591e8), "adaptec4": (1.370e8, 1.414e8),
    "adaptec5": (3.098e8, 3.131e8), "bigblue1": (8.333e7, 8.567e7),
    "bigblue2": (1.217e8, 1.257e8), "bigblue3": (2.628e8, 2.767e8),
    "bigblue4": (6.271e8, 6.464e8), "newblue1": (5.837e7, 6.005e7),
    "newblue2": (1.487e8, 1.524e8), "newblue3": (2.692e8, 2.727e8),
    "newblue4": (2.299e8, 2.298e8), "newblue5": (3.846e8, 3.899e8),
    "newblue6": (4.032e8, 4.083e8), "newblue7": (8.657e8, 8.803e8),
}


def latest_run(design):
    if design == "adaptec1":
        pat = os.path.join(REPO, "results/mms_B_precond_s1", design, "**", "run_summary.md")
    else:
        pat = os.path.join(SUITE_DIR, design, "**", "run_summary.md")
    summaries = sorted(glob.glob(pat, recursive=True), key=os.path.getmtime)
    return os.path.dirname(summaries[-1]) if summaries else None


def parse_summary(run_dir):
    """Return (final_hpwl, final_ovfw_exact, best_iter, best_hpwl, best_ovfw)."""
    path = os.path.join(run_dir, "run_summary.md")
    if not os.path.exists(path):
        return None
    txt = open(path).read()
    def grab(pat):
        m = re.search(pat, txt)
        return m.group(1) if m else None
    fh = grab(r"Final HPWL \(exact, all nets\)\s*\|\s*([\d.eE+]+)")
    fo = grab(r"Final Overflow \(exact\)\s*\|\s*([\d.eE+-]+)")
    best = grab(r"Best Solution\s*\|\s*(iter [^|]+?)\s*\|")
    return fh, fo, best


def main():
    print(f"{'design':10} {'swGP':>10} {'xpGP':>10} {'GP%':>7} {'swOvfw':>7} {'xpLegal':>10} {'best':>40}")
    for d in sorted(XPLACE):
        rd = latest_run(d)
        if not rd:
            print(f"{d:10} {'--- no run ---':>10}")
            continue
        res = parse_summary(rd)
        if not res or res[0] is None:
            print(f"{d:10} (running / no summary)")
            continue
        fh, fo, best = res
        sw = float(fh)
        xp_gp, xp_lg = XPLACE[d]
        gp_pct = 100.0 * (sw - xp_gp) / xp_gp
        print(f"{d:10} {sw:10.3e} {xp_gp:10.3e} {gp_pct:+6.1f}% {float(fo):7.3f} {xp_lg:10.3e} {best or '':>40}")


if __name__ == "__main__":
    main()
