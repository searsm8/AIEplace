#!/usr/bin/env python3
"""Quick DSE results analyzer — pass the results.csv path as argument."""
import csv, sys, os

csv_path = sys.argv[1] if len(sys.argv) > 1 else "results.csv"
with open(csv_path) as f:
    reader = csv.DictReader(f)
    rows = sorted(list(reader), key=lambda r: (
        r.get("Design", ""),
        float(r.get("Best HPWL", "1e30") if r.get("Best HPWL") not in (None, "", "N/A") else "1e30")
    ))

fixed_cols = {
    "Design", "Iters", "Final HPWL", "Best Iter", "Best HPWL", "Best OVFW",
    "XPlace HPWL", "Ratio",
    "Gamma", "Net Count", "Node Count", "HPWL_Graph", "Combined_Graph",
    "Placement_GIF", "Total Runtime (sec)", "DB IO Time (sec)",
    "Algorithm Time (sec)", "Iteration Avg (sec)", "Partials AIE Time (sec)",
    "Memory Usage (MB)", "Output Dir", "Timestamp"
}
dse_cols = [k for k in rows[0].keys() if k and k not in fixed_cols]
print(f"{len(rows)} runs, DSE params: {dse_cols}\n")

cols = ["Design"] + dse_cols + ["Iters", "Best OVFW", "Best HPWL", "Ratio"]
cw = {"Design": 22, "Iters": 5, "Best HPWL": 10, "Best Iter": 9,
      "Best OVFW": 9, "Ratio": 6}
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
    print(f"Ratio range: {min(ratios):.2f} -- {max(ratios):.2f}  (mean {sum(ratios)/len(ratios):.2f}x vs XPlace)")
print(f"Iter range:  {min(iters)} -- {max(iters)}")
