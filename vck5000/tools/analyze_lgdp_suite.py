#!/usr/bin/env python3
"""Collate .claude/2_ARTIFACTS/lgdp_suite_results.tsv against the XPlace reference.

Reports post-DP HPWL -- the legal-vs-legal number TODO #3 asks for -- next to
XPlace's own post-DP HPWL for the same design, plus the HPWL each side pays for
legalization (post-LG / post-GP), which is where an under-spread GP shows up.

With --density it also runs tools/post_dp_density.py over both tools' written post-DP
placements, so an HPWL win that was actually bought with density is visible. Read that
module's docstring before quoting the numbers: post-DP overflow is identically zero at
target_density 1.0, so it only discriminates on the 8 MMS designs below 1.0.

Usage: analyze_lgdp_suite.py [results.tsv] [--density]
"""
import csv
import glob
import os
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import benchmarks
import post_dp_density

DEFAULT_TSV = Path(__file__).resolve().parent / "lgdp_suite_results.tsv"
XPLACE_RESULT = Path.home() / "phd/Xplace/result"
OURS_RUN_GLOB = os.environ.get("LGDP_OURS_GLOB", "2026-08-04*")  # our skip-GP runs over our placements
REF_RUN_GLOB = "2026-07-17*"       # the reference full XPlace runs


def newest_dp_placement(run_glob, design):
    hits = sorted(glob.glob(str(XPLACE_RESULT / f"{run_glob}_{design}"
                                / "output" / f"placement_{design}_dp.pl")))
    return hits[-1] if hits else None


def num(text):
    try:
        return float(text)
    except (TypeError, ValueError):
        return None


def density_table(rows):
    print("\n### Post-DP density (both sides computed by tools/post_dp_density.py)\n")
    print("| design | td | our overflow | XPlace overflow | Δ | our max_util | XPlace max_util |")
    print("|---|---|---|---|---|---|---|")
    for row in rows:
        design = row["design"]
        meta = benchmarks.BENCHMARKS.get(f"mms/{design}")
        if meta is None:
            continue
        target_density = meta["target_density"]
        ours = newest_dp_placement(OURS_RUN_GLOB, design)
        ref = newest_dp_placement(REF_RUN_GLOB, design)
        if ours is None or ref is None:
            print(f"| {design} | {target_density:g} | — | — | no post-DP placement written | — | — |")
            continue
        if target_density >= 1.0:
            print(f"| {design} | 1.0 | n/a | n/a | *zero by construction* | — | — |")
            continue
        our_ovf, our_peak, _ = post_dp_density.metrics(design, ours)
        ref_ovf, ref_peak, _ = post_dp_density.metrics(design, ref)
        delta = 100.0 * (our_ovf - ref_ovf) / ref_ovf if ref_ovf else float("nan")
        print(f"| {design} | {target_density:g} | {our_ovf:.4f} | {ref_ovf:.4f} | "
              f"{delta:+.1f}% | {our_peak:.3f} | {ref_peak:.3f} |")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    want_density = "--density" in sys.argv
    tsv = Path(args[0]) if args else DEFAULT_TSV
    with open(tsv) as f:
        # A retried design appends a second row (the runner only skips rows that succeeded),
        # so collapse to the last row per design.
        by_design = {r["design"]: r for r in csv.DictReader(f, delimiter="\t")}
    rows = [by_design[d] for d in sorted(by_design)]

    print(f"| design | our GP | our LG | our DP | XPlace DP | ΔDP vs XPlace | our LG cost | "
          f"XPlace LG cost |")
    print("|---|---|---|---|---|---|---|---|")

    deltas = {}
    for row in rows:
        design = row["design"]
        meta = benchmarks.BENCHMARKS.get(f"mms/{design}")
        gp, lg, dp = num(row["gp_hpwl_in"]), num(row["lg_hpwl"]), num(row["dp_hpwl"])
        if meta is None or dp is None:
            print(f"| {design} | {row['gp_hpwl_in']} | — | — | — | **{row['status']}** | — | — |")
            continue
        ref_dp = meta["xplace_dp_hpwl"]
        ref_gp, ref_lg = meta["xplace_final_hpwl"], meta["xplace_lg_hpwl"]
        delta = 100.0 * (dp - ref_dp) / ref_dp
        deltas[design] = delta
        ours_cost = 100.0 * (lg - gp) / gp
        ref_cost = 100.0 * (ref_lg - ref_gp) / ref_gp
        print(f"| {design} | {gp:.4g} | {lg:.4g} | {dp:.4g} | {ref_dp:.4g} | "
              f"{delta:+.2f}% | +{ours_cost:.1f}% | +{ref_cost:.1f}% |")

    if deltas:
        values = list(deltas.values())
        print(f"\n{len(values)} designs: mean {statistics.mean(values):+.2f}%, "
              f"median {statistics.median(values):+.2f}%, "
              f"best {min(values):+.2f}% ({min(deltas, key=deltas.get)}), "
              f"worst {max(values):+.2f}% ({max(deltas, key=deltas.get)})")
        # The adaptec5 carve-out is retained only as a historical cross-check, and its ORIGINAL
        # rationale is dead: "phase-1 divergence -> under-spread GP, so its win is not like-for-like"
        # described the pre-#11b placement. adaptec5 has converged since 2026-08-04 and its post-DP
        # density is +0.0% vs XPlace (was +14.0%), so it is a like-for-like design now and the
        # headline should be read from the all-designs line above.
        clean = {d: v for d, v in deltas.items() if d != "adaptec5"}
        print(f"for reference, excluding adaptec5 (no longer an outlier -- it converges and is at "
              f"density parity; kept only to compare against older reports): "
              f"mean {statistics.mean(clean.values()):+.2f}% over {len(clean)} designs")

    if want_density:
        density_table(rows)


if __name__ == "__main__":
    main()
