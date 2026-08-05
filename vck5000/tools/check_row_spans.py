#!/usr/bin/env python3
"""Report cells placed outside the legal site span of the row they sit in.

sw_only models the die as a rectangle and `enforceDieBoundaries` clamps to that
rectangle, but a bookshelf `.scl` gives every CoreRow its own SubrowOrigin/NumSites.
On the 11 MMS designs whose rows do not all share one span the core is a staircase,
so "inside the die bounding box" is weaker than "inside a row" and a global placement
can leave cells with nowhere legal to go. That is invisible to us until a legalizer
chokes on it -- adaptec3 segfaults inside XPlace's greedyLegalization (TODO #3).

Usage: check_row_spans.py <design> <placement.pl> [<placement.pl> ...]
       check_row_spans.py adaptec3 /tmp/lgdp/pl/adaptec3.pl
"""
import sys
from pathlib import Path

BENCH = Path("/home/msears/phd/AIEplace/vck5000/host/benchmarks/mms")


def read_rows(scl_path):
    """[(y, height, x_start, x_end)] for every CoreRow, in file order."""
    rows, cur = [], {}
    for line in open(scl_path):
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "Coordinate":
            cur["y"] = int(fields[2])
        elif fields[0] == "Height":
            cur["height"] = int(fields[2])
        elif fields[0] == "SubrowOrigin":
            cur["x_start"], cur["num_sites"] = int(fields[2]), int(fields[5])
        elif fields[0] == "End" and "y" in cur:
            rows.append((cur["y"], cur["height"], cur["x_start"],
                         cur["x_start"] + cur["num_sites"]))
            cur = {}
    return rows


def read_sizes(nodes_path):
    sizes = {}
    for line in open(nodes_path):
        fields = line.split()
        if len(fields) >= 3 and fields[0].startswith("o"):
            sizes[fields[0]] = (int(fields[1]), int(fields[2]))
    return sizes


def scan(pl_path, rows, sizes):
    """(outside_die_bbox, outside_own_row, worst_overhang) for one placement."""
    y_lo = min(r[0] for r in rows)
    y_hi = max(r[0] + r[1] for r in rows)
    x_lo = min(r[2] for r in rows)
    x_hi = max(r[3] for r in rows)
    row_height = rows[0][1]

    outside_die = outside_row = 0
    worst = 0.0
    for line in open(pl_path):
        fields = line.split()
        if len(fields) < 3 or not fields[0].startswith("o"):
            continue
        try:
            x, y = float(fields[1]), float(fields[2])
        except ValueError:
            continue
        width, height = sizes.get(fields[0], (0, 0))
        if x < x_lo or x + width > x_hi or y < y_lo or y + height > y_hi:
            outside_die += 1
        index = (int(y) - y_lo) // row_height
        if 0 <= index < len(rows):
            _, _, row_start, row_end = rows[index]
            overhang = max(row_start - x, (x + width) - row_end)
            if overhang > 0:
                outside_row += 1
                worst = max(worst, overhang)
    return outside_die, outside_row, worst


def main():
    design, placements = sys.argv[1], sys.argv[2:]
    rows = read_rows(BENCH / design / f"{design}.scl")
    sizes = read_sizes(BENCH / design / f"{design}.nodes")
    ragged = len({(r[2], r[3]) for r in rows}) > 1
    print(f"{design}: {len(rows)} rows, ragged={ragged}")
    for pl in placements:
        outside_die, outside_row, worst = scan(pl, rows, sizes)
        print(f"  {Path(pl).name:24s} outside die bbox: {outside_die:8d}   "
              f"outside own row: {outside_row:8d}   worst overhang: {worst:.0f}")


if __name__ == "__main__":
    main()
