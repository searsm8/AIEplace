#!/usr/bin/env python3
"""Separate "our placer is better" from "we ignore the fence" (TODO #26 step 3).

Our ISPD2015 placements ignore fence regions; the ISPD2015 contest's own legalized solution
(`after_legalized.ntup.fix.def`, shipped with every design) honours them. Comparing the two
directly on the 9 fence designs conflates two things: our placer being better than a 2015-era
one, and our solution being cheaper because it is illegal.

The 11 designs with NO fence regions calibrate exactly that confound: there, any gap is placer
quality alone. If our advantage over the contest solution is the SAME on both groups, the fence
is not buying us anything measurable. If it is markedly larger on the fenced 9, the difference is
the constraint's price -- and our ISPD2015 numbers on those designs are optimistic by that much.

Both sides run through the same XPlace LG+DP path so the frames match (site units, both from
"After DP, HPWL"). Inputs are the two TSVs produced by run_lgdp44.sh.

Usage: analyze_fence_cost.py <ours.tsv> <contest.tsv>
"""
import csv
import statistics
import sys


def load(path):
    out = {}
    with open(path) as f:
        for r in csv.DictReader(f, delimiter="\t"):
            if r["suite"] == "ispd2015" and r["status"].startswith("done") and r["dp_hpwl"] != "NA":
                out[r["design"]] = (float(r["dp_hpwl"]), r["variant"])
    return out


def main():
    ours, contest = load(sys.argv[1]), load(sys.argv[2])
    shared = sorted(set(ours) & set(contest))

    groups = {"fenced": [], "unfenced": []}
    print("%-22s %14s %14s %8s" % ("design", "ours_post_dp", "contest_post_dp", "ours/contest"))
    for d in shared:
        our_dp, variant = ours[d]
        ratio = our_dp / contest[d][0]
        key = "fenced" if variant == "ispd2015_fix" else "unfenced"
        groups[key].append((d, ratio))
        print("%-22s %14.6e %14.6e %8.4f%s"
              % (d, our_dp, contest[d][0], ratio, "  †" if key == "fenced" else ""))

    print()
    for key in ("unfenced", "fenced"):
        rs = [r for _, r in groups[key]]
        if rs:
            print("%-10s n=%2d  median %.4f  mean %.4f  min %.4f  max %.4f"
                  % (key, len(rs), statistics.median(rs), statistics.mean(rs), min(rs), max(rs)))
    if groups["fenced"] and groups["unfenced"]:
        fenced = statistics.median(r for _, r in groups["fenced"])
        unfenced = statistics.median(r for _, r in groups["unfenced"])
        print("\n[info] median advantage over the contest solution: %.1f%% unfenced, %.1f%% fenced."
              % (100 * (1 - unfenced), 100 * (1 - fenced)))
        print("[info] difference-in-differences = %.1f percentage points%s"
              % (100 * (unfenced - fenced),
                 " (fenced advantage is LARGER -- consistent with the fence costing wirelength)"
                 if fenced < unfenced else
                 " (fenced advantage is NOT larger -- no measurable fence dividend)"))


if __name__ == "__main__":
    main()
