#!/usr/bin/env python3
"""Quick DSE results analyzer — pass the results.csv path as argument.

The CSV's built-in `Ratio` is our GP-only HPWL / XPlace GP+LEGALIZATION (flatters us).
When an XPlace GP log exists for a design (see xplace_gp_ref), this also prints a true
GP-vs-GP ratio (`GP Ratio`) against XPlace's pre-legalization masked_hpwl."""
import csv, sys, os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xplace_gp_ref import gp_ref

XPLACE_GP = gp_ref(refresh=True)  # design -> XPlace GP (pre-LG) masked_hpwl

csv_path = sys.argv[1] if len(sys.argv) > 1 else "results.csv"
with open(csv_path) as f:
    reader = csv.DictReader(f)
    rows = sorted(list(reader), key=lambda r: (
        r.get("Design", ""),
        float(r.get("Best HPWL", "1e30") if r.get("Best HPWL") not in (None, "", "N/A") else "1e30")
    ))

# Add a GP-vs-GP column where we have an XPlace GP reference for the design.
for r in rows:
    design = r.get("Design", "")
    best = r.get("Best HPWL")
    if design in XPLACE_GP and best not in (None, "", "N/A"):
        r["XPlace GP"] = f"{XPLACE_GP[design]:.3e}"
        r["GP Ratio"] = f"{float(best) / XPLACE_GP[design]:.2f}"
    else:
        r["XPlace GP"] = ""
        r["GP Ratio"] = ""

fixed_cols = {
    "Design", "Iters", "Final HPWL", "Best Iter", "Best HPWL", "Best OVFW",
    "XPlace HPWL", "Ratio",
    "Gamma", "Net Count", "Node Count", "HPWL_Graph", "Combined_Graph",
    "Placement_GIF", "Total Runtime (sec)", "DB IO Time (sec)",
    "Algorithm Time (sec)", "Iteration Avg (sec)", "Partials AIE Time (sec)",
    "Memory Usage (MB)", "Output Dir", "Timestamp"
}
fixed_cols |= {"XPlace GP", "GP Ratio"}  # computed above, not real DSE params
dse_cols = [k for k in rows[0].keys() if k and k not in fixed_cols]
print(f"{len(rows)} runs, DSE params: {dse_cols}\n")

cols = ["Design"] + dse_cols + ["Iters", "Best OVFW", "Best HPWL", "Ratio",
                                "XPlace GP", "GP Ratio"]
cw = {"Design": 22, "Iters": 5, "Best HPWL": 10, "Best Iter": 9,
      "Best OVFW": 9, "Ratio": 6, "XPlace GP": 10, "GP Ratio": 8}
for dc in dse_cols:
    cw[dc] = max(len(dc), max(len(str(r.get(dc, ""))) for r in rows)) + 1

hdr = "  ".join(f"{c:<{cw.get(c, 12)}}" for c in cols)
print(hdr)
print("-" * len(hdr))
for r in rows:
    print("  ".join(f"{r.get(c, ''):<{cw.get(c, 12)}}" for c in cols))

hpwls = [float(r["Best HPWL"]) for r in rows if r.get("Best HPWL") not in (None, "", "N/A")]
ovfws = [float(r["Best OVFW"]) for r in rows if r.get("Best OVFW") not in (None, "", "N/A")]
ratios = [float(r["Ratio"]) for r in rows if r.get("Ratio") not in (None, "", "N/A")]
iters = [int(r["Iters"]) for r in rows]
print(f"\nHPWL range:  {min(hpwls):.3e} -- {max(hpwls):.3e}  ({max(hpwls)/min(hpwls):.1f}x)")
print(f"OVFW range:  {min(ovfws):.3f} -- {max(ovfws):.3f}")
if ratios:
    print(f"Ratio range: {min(ratios):.2f} -- {max(ratios):.2f}  (mean {sum(ratios)/len(ratios):.2f}x vs XPlace GP+LG)")
gp_ratios = [float(r["GP Ratio"]) for r in rows if r["GP Ratio"]]
if gp_ratios:
    print(f"GP Ratio:    {min(gp_ratios):.2f} -- {max(gp_ratios):.2f}  "
          f"(mean {sum(gp_ratios)/len(gp_ratios):.2f}x vs XPlace GP, {len(gp_ratios)} designs) "
          f"<- true apples-to-apples")
print(f"Iter range:  {min(iters)} -- {max(iters)}")
