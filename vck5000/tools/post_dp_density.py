#!/usr/bin/env python3
"""Density metrics for a LEGALIZED / detail-placed bookshelf placement.

TODO #3: post-DP HPWL on its own flatters an under-spread GP that still legalizes
(adaptec5 lands -7.4% HPWL vs XPlace off a phase-1 divergence at GP overflow 0.44), so the
full-pipeline scorecard needs a density axis too.

Deliberately computes BOTH sides -- ours and XPlace's own `placement_<design>_dp.pl` -- with
this one implementation. The unresolved 2-4x gap between XPlace's reported overflow and
sw_only's `computeOverflow` (TODO #3) means neither tool's own number can be trusted across
the boundary yet; running one formula over both placements sidesteps that entirely, because
any definitional quirk applies equally to both and cancels in the comparison.

Metrics, all on the design's XPlace bin grid (tools/benchmarks.py):
  overflow   sum over bins of max(0, bin_area_used - bin_capacity) / total movable area,
             where bin_capacity = bin area * target_density. The ePlace/XPlace form, sharp
             (exact cell size, weight 1). Post-DP placements contain no fillers, so this is
             the filler-excluded variant on both sides.
  max_util   single busiest bin. This is a LEGALITY CHECK, not a quality one: non-overlapping
             cells cannot exceed the area of the bin containing them, so any value above 1.0
             means the "legalized" placement still has overlaps.

Note carefully what a post-DP density metric can and cannot say, because it is less than you
would expect and the limits are structural, not fixable by picking a better statistic:

  * Legalization removes overlap, so bin occupancy is capped at 1.0. At target_density = 1.0
    the capacity IS 1.0, so overflow is **identically zero** and carries no information. It
    discriminates only on the 8 MMS designs with target_density < 1.0. On the other 8 the
    density question is answered by legalization itself and HPWL is the whole story.
  * Even there, overflow is partly a property of the design (a design 64% utilised cannot fit
    under a 50% cap at any quality), so read it only as us-vs-XPlace on the same design.
  * A "top 5% bin utilisation" congestion proxy was tried and **dropped**: it reads exactly
    1.000 for both tools on every design, at both the GP grid and a coarse 64x64 one, because
    the busiest bins are movable-macro interiors, which are 100% occupied by definition. It
    measures macro presence, not placement quality.

Fixed cells are included in bin occupancy for both metrics (a bin under a fixed macro really
is occupied); the overflow denominator is movable area only, per the ePlace convention.

Usage: post_dp_density.py <design> <label>=<placement.pl> [<label>=<placement.pl> ...]
"""
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import benchmarks
from check_row_spans import read_rows, read_sizes

BENCH = Path("/home/msears/phd/AIEplace/vck5000/host/benchmarks/mms")



def read_fixed(pl_path):
    return {line.split()[0] for line in open(pl_path) if "/FIXED" in line}


def read_placement(pl_path):
    coords = {}
    for line in open(pl_path):
        fields = line.split()
        if len(fields) >= 3 and fields[0].startswith("o"):
            try:
                coords[fields[0]] = (float(fields[1]), float(fields[2]))
            except ValueError:
                pass
    return coords


def deposit(cells, grid, die):
    """Exact area deposit onto a grid x grid bin map. cells = (x, y, w, h) arrays.

    Cells spanning at most 2 bins per axis -- every standard cell at these grids -- are done
    vectorised over the 2x2 block; the overlap expression is clipped at zero, so the second
    bin contributes nothing when the cell does not actually reach it. Macros spanning more
    fall back to a per-cell loop (a few hundred per design).
    """
    x_lo, y_lo, bin_w, bin_h = die
    x, y, w, h = cells
    density = np.zeros((grid, grid), dtype=np.float64)

    xl, yl = x - x_lo, y - y_lo
    xh, yh = xl + w, yl + h
    c0 = np.clip((xl / bin_w).astype(np.int64), 0, grid - 1)
    c1 = np.clip((np.nextafter(xh, -np.inf) / bin_w).astype(np.int64), 0, grid - 1)
    r0 = np.clip((yl / bin_h).astype(np.int64), 0, grid - 1)
    r1 = np.clip((np.nextafter(yh, -np.inf) / bin_h).astype(np.int64), 0, grid - 1)

    small = (c1 - c0 <= 1) & (r1 - r0 <= 1)
    for dc in (0, 1):
        for dr in (0, 1):
            col = np.minimum(c0[small] + dc, c1[small])
            row = np.minimum(r0[small] + dr, r1[small])
            overlap_x = np.minimum(xh[small], (col + 1) * bin_w) - np.maximum(xl[small], col * bin_w)
            overlap_y = np.minimum(yh[small], (row + 1) * bin_h) - np.maximum(yl[small], row * bin_h)
            # a duplicated (dc,dr) index for a 1-bin-wide cell must not double-count
            unique = np.ones(col.shape, dtype=bool)
            if dc == 1:
                unique &= c1[small] > c0[small]
            if dr == 1:
                unique &= r1[small] > r0[small]
            area = np.where(unique, np.clip(overlap_x, 0, None) * np.clip(overlap_y, 0, None), 0.0)
            np.add.at(density, (col, row), area)

    for i in np.flatnonzero(~small):
        for col in range(c0[i], c1[i] + 1):
            overlap_x = min(xh[i], (col + 1) * bin_w) - max(xl[i], col * bin_w)
            if overlap_x <= 0:
                continue
            for row in range(r0[i], r1[i] + 1):
                overlap_y = min(yh[i], (row + 1) * bin_h) - max(yl[i], row * bin_h)
                if overlap_y > 0:
                    density[col, row] += overlap_x * overlap_y
    return density


def metrics(design, pl_path):
    meta = benchmarks.BENCHMARKS[f"mms/{design}"]
    grid, target_density = meta["grid"], meta["target_density"]

    rows = read_rows(BENCH / design / f"{design}.scl")
    x_lo = min(r[2] for r in rows)
    x_hi = max(r[3] for r in rows)
    y_lo = min(r[0] for r in rows)
    y_hi = max(r[0] + r[1] for r in rows)
    bin_w, bin_h = (x_hi - x_lo) / grid, (y_hi - y_lo) / grid

    sizes = read_sizes(BENCH / design / f"{design}.nodes")
    fixed = read_fixed(BENCH / design / f"{design}.pl")
    placed = read_placement(pl_path)

    names = [n for n in placed if n in sizes and sizes[n][0] > 0 and sizes[n][1] > 0]
    x = np.array([placed[n][0] for n in names], dtype=np.float64)
    y = np.array([placed[n][1] for n in names], dtype=np.float64)
    w = np.array([sizes[n][0] for n in names], dtype=np.float64)
    h = np.array([sizes[n][1] for n in names], dtype=np.float64)
    is_movable = np.array([n not in fixed for n in names])

    density = deposit((x, y, w, h), grid, (x_lo, y_lo, bin_w, bin_h))
    bin_area = bin_w * bin_h
    movable_area = float((w[is_movable] * h[is_movable]).sum())
    overflow = float(np.clip(density - bin_area * target_density, 0, None).sum()) / movable_area
    max_util = float((density / bin_area).max())
    return overflow, max_util, len(names)


def main():
    design = sys.argv[1]
    print(f"{design} (grid {benchmarks.BENCHMARKS[f'mms/{design}']['grid']}, "
          f"target_density {benchmarks.BENCHMARKS[f'mms/{design}']['target_density']:g})")
    for arg in sys.argv[2:]:
        label, _, path = arg.partition("=")
        overflow, peak, n = metrics(design, path)
        legal = "" if peak <= 1.0 + 1e-9 else "  <-- OVERLAPS, not legal"
        print(f"  {label:12s} overflow {overflow:7.4f}   max_util {peak:6.3f}   "
              f"cells {n}{legal}")


if __name__ == "__main__":
    main()
