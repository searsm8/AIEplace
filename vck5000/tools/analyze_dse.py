#!/usr/bin/env python3
"""Quick DSE results analyzer — pass the results.csv path as argument.

The CSV columns are now GP-vs-GP honest: `Best GP HPWL` is our global-placement HPWL (masked,
nets>ignore_net_degree dropped) and `XPlace GP HPWL` is XPlace's pre-legalization masked_hpwl
(GP Stop), so `Ratio` = Best GP / XPlace GP is a true apples-to-apples GP comparison. Only designs
with a measured XPlace GP reference (ISPD2005, raw-DBU frame) have a non-N/A ratio; see Output.cpp."""
import csv, sys

csv_path = sys.argv[1] if len(sys.argv) > 1 else "results.csv"
with open(csv_path) as f:
    rows = sorted(csv.DictReader(f), key=lambda r: (
        r.get("Design", ""),
        float(r["Best GP HPWL"]) if r.get("Best GP HPWL") not in (None, "", "N/A") else 1e30))

fixed_cols = {
    "Design", "Iters", "Final HPWL", "Final HPWL Exact", "Best Iter", "Best GP HPWL", "Best OVFW",
    "XPlace GP HPWL", "Ratio",
    "Gamma", "Net Count", "Node Count", "HPWL_Graph", "Combined_Graph",
    "Placement_GIF", "Total Runtime (sec)", "DB IO Time (sec)",
    "Algorithm Time (sec)", "Iteration Avg (sec)", "Partials AIE Time (sec)",
    "Memory Usage (MB)", "Output Dir", "Timestamp"
}
dse_cols = [k for k in rows[0].keys() if k and k not in fixed_cols]
print(f"{len(rows)} runs, DSE params: {dse_cols}\n")

cols = ["Design"] + dse_cols + ["Iters", "Best OVFW", "Best GP HPWL", "XPlace GP HPWL", "Ratio"]
cw = {"Design": 22, "Iters": 5, "Best GP HPWL": 12, "XPlace GP HPWL": 14,
      "Best OVFW": 9, "Ratio": 6}
for dc in dse_cols:
    cw[dc] = max(len(dc), max(len(str(r.get(dc, ""))) for r in rows)) + 1

hdr = "  ".join(f"{c:<{cw.get(c, 12)}}" for c in cols)
print(hdr)
print("-" * len(hdr))
for r in rows:
    print("  ".join(f"{r.get(c, ''):<{cw.get(c, 12)}}" for c in cols))

hpwls = [float(r["Best GP HPWL"]) for r in rows if r.get("Best GP HPWL") not in (None, "", "N/A")]
ovfws = [float(r["Best OVFW"]) for r in rows if r.get("Best OVFW") not in (None, "", "N/A")]
ratios = [float(r["Ratio"]) for r in rows if r.get("Ratio") not in (None, "", "N/A")]
iters = [int(r["Iters"]) for r in rows]
print(f"\nHPWL range:  {min(hpwls):.3e} -- {max(hpwls):.3e}  ({max(hpwls)/min(hpwls):.1f}x)")
print(f"OVFW range:  {min(ovfws):.3f} -- {max(ovfws):.3f}")
if ratios:
    print(f"GP Ratio:    {min(ratios):.2f} -- {max(ratios):.2f}  (mean {sum(ratios)/len(ratios):.2f}x "
          f"vs XPlace GP, {len(ratios)} designs) <- true apples-to-apples GP-vs-GP")
print(f"Iter range:  {min(iters)} -- {max(iters)}")
