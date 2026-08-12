#!/usr/bin/env python3
"""Collate the full 44-design snapshot: sw_only vs XPlace, all three tiers.

Inputs (all in .claude/2_ARTIFACTS/, overridable with $ARTIFACTS):
    full44_suite_results.tsv     our GP, ispd2005 + ispd2015   (tools/run_suite.sh)
    faithful_suite_results.tsv   our GP, mms                   (the TODO #19 arm)
    lgdp44_results.tsv           our post-LG/DP, ispd2005+2015 (tools/run_lgdp44.sh)
    lgdp_faithful_results.tsv    our post-LG/DP, mms           (tools/run_lgdp_suite.sh)
Reference: tools/benchmarks.py (_XPLACE_ISPD_FINAL + _XPLACE_MMS_FINAL).

### THE FRAME RULE -- the one thing to get right here
XPlace divides ISPD2015 HPWL by site_width (database.py:602), so tier 2 lives in "site units"
while sw_only reports raw DBU. That means the two comparisons need DIFFERENT handling:

  post-GP : ours is sw_only's own output  -> RAW DBU
            reference is XPlace's         -> site units
            => convert the reference up, via benchmarks.xplace_hpwl_in_sw_frame().
  post-DP : ours comes out of XPlace's OWN log ("After DP, HPWL") because XPlace legalized our
            placement -> ALREADY site units
            reference is XPlace's         -> site units
            => same frame, NO conversion. Converting here would inflate our ratio by site_width.

site_width is 200 for the 15 mgc_* and 100 for the 5 mgc_superblue* -- never hardcode it.
Tiers 1 and 3 are bookshelf and share sw_only's frame, so the conversion is a no-op there.

Usage: analyze_full44.py [--md]
"""
import argparse
import csv
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import benchmarks as B  # noqa: E402

# This script is tracked (vck5000/tools/); the TSVs it reads are not (.claude/2_ARTIFACTS/).
# It pointed at vck5000/2_ARTIFACTS until 2026-08-12, a directory that stopped existing when the
# artifacts moved under .claude/ on 08-07 -- every input silently read as empty, with no error.
A = os.environ.get("ARTIFACTS", os.path.expanduser("~/phd/AIEplace/.claude/2_ARTIFACTS"))


def _rows(fn):
    # A missing artifacts dir used to read as "every input is empty" and print a table of dashes.
    # Silence is the wrong answer here: it is indistinguishable from a suite that produced nothing.
    if not os.path.isdir(A):
        sys.exit("no artifacts directory at %s -- set $ARTIFACTS to where the result TSVs live" % A)
    p = os.path.join(A, fn)
    if not os.path.exists(p):
        return []
    with open(p) as f:
        return list(csv.DictReader(f, delimiter="\t"))


def collect(gp_file="full44_suite_results.tsv", lgdp_file="lgdp44_results.tsv"):
    """{"suite/design": {...}} merged across the four inputs."""
    out = {}

    def put(key, **kw):
        out.setdefault(key, {}).update({k: v for k, v in kw.items() if v not in (None, "NA", "")})

    for r in _rows(gp_file):
        if r["status"] != "done":
            continue
        put(f"{r['suite']}/{r['design']}", gp=r["hpwl_exact"], iters=r["iters"],
            stop=r["stop_reason"], secs=r["runtime_s"])
    for r in _rows("faithful_suite_results.tsv"):
        if r["status"] != "done":
            continue
        put(f"mms/{r['design']}", gp=r["hpwl_exact"], iters=r["iters"], stop=r["stop_reason"])
    for r in _rows(lgdp_file):
        if not r["status"].startswith("done"):
            continue
        put(f"{r['suite']}/{r['design']}", dp=r["dp_hpwl"], lg=r["lg_hpwl"],
            variant=r.get("variant"))
    for r in _rows("lgdp_faithful_results.tsv"):
        if not r["status"].startswith("done"):
            continue
        put(f"mms/{r['design']}", dp=r["dp_hpwl"], lg=r["lg_hpwl"])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--md", action="store_true", help="markdown table for the snapshot")
    ap.add_argument("--gp", default="full44_suite_results.tsv")
    ap.add_argument("--lgdp", default="lgdp44_results.tsv")
    args = ap.parse_args()

    data = collect(args.gp, args.lgdp)
    sep = " | " if args.md else "  "
    lead = "| " if args.md else ""
    tail = " |" if args.md else ""
    if args.md:
        print("| design | tier | td | grid | iters | stop | our post-DP | XPlace post-DP | ratio |")
        print("|---|---:|---:|---:|---:|---|---:|---:|---:|")

    ratios_by_tier, all_ratios, crashed = {}, [], []
    for path in sorted(B.BENCHMARKS, key=lambda p: (B.BENCHMARKS[p]["tier"], p)):
        m = B.BENCHMARKS[path]
        d = data.get(path, {})
        ref_dp = m["xplace_dp_hpwl"]
        our_dp = float(d["dp"]) if "dp" in d else None
        # post-DP: BOTH sides come out of XPlace's own log -> same frame, no conversion.
        ratio = (our_dp / ref_dp) if (our_dp and ref_dp) else None
        # `nan_metrics` means the GP never moved a cell (TODO #23) -- there is no placement to
        # score, so its "ratio" would be the ratio of the random initial placement. Excluded from
        # the statistics and listed separately; NOT silently dropped.
        if d.get("stop") == "nan_metrics":
            if ratio:
                crashed.append((path, ratio))
            ratio = None
        if ratio:
            all_ratios.append(ratio)
            ratios_by_tier.setdefault(m["tier"], []).append(ratio)
        # Dagger = scored on the fence-STRIPPED ispd2015_fix data, both sides of the ratio. Same
        # mark, same meaning as the TCAD paper's Table III/V (TODO #26).
        label = path.split("/")[1] + ("†" if d.get("variant") == "ispd2015_fix" else "")
        cells = [label, str(m["tier"]), f"{m['target_density']:g}", str(m["grid"]),
                 d.get("iters", "-"), d.get("stop", "-"),
                 f"{our_dp:.4e}" if our_dp else "-",
                 f"{ref_dp:.4e}" if ref_dp else "no ref",
                 f"{ratio:.4f}" if ratio else "-"]
        print(lead + sep.join(cells) + tail)

    print()
    for tier in sorted(ratios_by_tier):
        r = ratios_by_tier[tier]
        print(f"tier {tier}: n={len(r):2}  mean {statistics.mean(r):.4f}  median {statistics.median(r):.4f}"
              f"  min {min(r):.3f}  max {max(r):.3f}")
    if all_ratios:
        r = all_ratios
        print(f"ALL   : n={len(r):2}  mean {statistics.mean(r):.4f}  median {statistics.median(r):.4f}"
              f"  min {min(r):.3f}  max {max(r):.3f}")
        print(f"        within 2%: {sum(abs(x-1)<=.02 for x in r)}/{len(r)}   "
              f"within 5%: {sum(abs(x-1)<=.05 for x in r)}/{len(r)}   "
              f"better than XPlace: {sum(x<1 for x in r)}")
    if crashed:
        print("\nEXCLUDED -- GP crashed, no placement produced (TODO #23, `nan_metrics`): "
              + ", ".join("%s (would read %.1fx)" % (p.split('/')[1], r) for p, r in crashed))
    worst = sorted(((p, r) for p, r in
                    ((p, (float(data[p]["dp"]) / B.BENCHMARKS[p]["xplace_dp_hpwl"]))
                     for p in B.BENCHMARKS
                     if "dp" in data.get(p, {}) and B.BENCHMARKS[p]["xplace_dp_hpwl"]
                     and data[p].get("stop") != "nan_metrics")), key=lambda t: -t[1])[:3]
    print("worst 3 of the scored set: " + ", ".join("%s %.4f" % (p.split('/')[1], r) for p, r in worst))
    noref = [p for p in B.BENCHMARKS if B.BENCHMARKS[p]["xplace_dp_hpwl"] is None]
    nodp = [p for p in B.BENCHMARKS if B.BENCHMARKS[p]["xplace_dp_hpwl"] and "dp" not in data.get(p, {})]
    fenced = sorted(p for p in data if data[p].get("variant") == "ispd2015_fix")
    if fenced:
        print(f"\n† fence regions STRIPPED on both sides ({len(fenced)}, TODO #26): "
              + ", ".join(p.split('/')[1] for p in fenced))
    if noref:
        print(f"\nno XPlace reference ({len(noref)}): " + ", ".join(p.split('/')[1] for p in sorted(noref)))
    if nodp:
        print(f"no post-DP from us ({len(nodp)}): " + ", ".join(sorted(nodp)))


if __name__ == "__main__":
    main()
