#!/usr/bin/env python3
# make_scorecard.py -- collate a DSE sweep's results.csv into a review-ready markdown
# scorecard (ratio vs XPlace published HPWL, sorted, with suite stats). Reads the newest
# results/DSE_* unless a dir is given. Written for the full-suite performance snapshot.
import csv, glob, os, sys, statistics

d = sys.argv[1] if len(sys.argv) > 1 else sorted(glob.glob("results/DSE_2*"))[-1]
rows = list(csv.DictReader(open(os.path.join(d, "results.csv"))))

def g(r, k, default=""):
    return r.get(k, default)

# group by ISPD2005 vs ISPD2015 via the run label suffix / design name
def fnum(x):
    try: return float(x)
    except: return None

rows.sort(key=lambda r: g(r, "run") or g(r, "Design"))
ratios = [fnum(g(r, "Ratio")) for r in rows if fnum(g(r, "Ratio"))]

lines = [
    "# markv1 GP performance snapshot — full ISPD2005 + ISPD2015 suite",
    "",
    f"Source: `{d}`  |  {len(rows)} designs completed  |  best defaults "
    "(grid-indep γ, dct_normalize, pin offsets; precond OFF), each at its XPlace grid, "
    "seed 42, stop masked-overflow 0.04.",
    "",
    "Ratio = markv1 GP best HPWL / XPlace **published** reference (post-DP for ISPD2005). "
    "markv1 is global placement only (no detailed placement), so ~1.0 is at-parity and "
    "<1.0 means markv1's GP HPWL is below XPlace's *legalized* number.",
    "",
    "| Design | Grid | Iters | Best HPWL | Overflow | XPlace ref | Ratio |",
    "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
]
for r in rows:
    label = g(r, "run") or g(r, "Design")
    grid = g(r, "bins_per_row")
    lines.append(f"| {label} | {grid} | {g(r,'Iters')} | {g(r,'Best HPWL')} | "
                 f"{g(r,'Best OVFW')} | {g(r,'XPlace HPWL')} | {g(r,'Ratio')} |")

if ratios:
    within2 = sum(1 for x in ratios if 0.98 <= x <= 1.02)
    within5 = sum(1 for x in ratios if 0.95 <= x <= 1.05)
    lines += [
        "",
        f"**Suite stats ({len(ratios)} with an XPlace ref):** "
        f"mean ratio {statistics.mean(ratios):.3f}, median {statistics.median(ratios):.3f}, "
        f"min {min(ratios):.3f}, max {max(ratios):.3f}. "
        f"Within ±2%: {within2}/{len(ratios)}. Within ±5%: {within5}/{len(ratios)}.",
    ]

out = os.path.join(d, "scorecard.md")
open(out, "w").write("\n".join(lines) + "\n")
print("wrote", out)
print("\n".join(lines))
