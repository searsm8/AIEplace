#!/usr/bin/env python3
"""Measure how badly a placement violates the DEF's fence-region constraints (TODO #26).

sw_only discards REGIONS/GROUPS at parse time (DataBase::add_def_region/add_def_group are empty
stubs), so on the 9 ISPD2015 designs that carry them we solve an UNCONSTRAINED problem. XPlace
does the same -- it refuses to run on the fence-carrying data at all and its released
`ispd2015_fix` strips the constraint -- so the tool-vs-tool comparison stays fair. This script
measures the other question: how far our placement is from what the ISPD2015 contest asked for.

A cell is "constrained" when its instance name matches one of its group's wildcard patterns; it
is "outside" when its placed origin falls in no rectangle of that group's region.

APPROXIMATION, stated on purpose: containment is tested on the DEF PLACED point (the cell's
lower-left corner), not the cell's full bounding box. A cell straddling a fence edge by less than
its own width counts as inside. That understates violation by at most one cell width -- irrelevant
at the scale this reports (cells sitting on the far side of the die), and it keeps the script free
of any LEF dependency.

The parser is controlled against a placement known to honour the fences: each ISPD2015 design
ships the contest's own legalized solution as `after_legalized.ntup.fix.def`, and

    fence_check.py <design>/floorplan.def <design>/after_legalized.ntup.fix.def --expect-legal

must report ZERO violations. It does, on all 9 fence designs -- 190,010 constrained cells, not one
outside -- while our own placements on the same 9 violate 59-94%. That contrast is what makes the
violation counts a measurement rather than a parsing artifact.

Usage: fence_check.py <original.def> <placed.def> [--csv] [--expect-legal]
Exits non-zero on a parse failure, or under --expect-legal if any cell is outside its fence.
Without that flag it is a measurement, not a verdict -- see #26 step 3.
"""
import fnmatch
import re
import sys

_POINT = re.compile(r"\(\s*([-+0-9.eE]+)\s+([-+0-9.eE]+)\s*\)")
_COMP_START = re.compile(r"^\s*-\s+(\S+)\s")
_PLACED = re.compile(r"^\s*\+\s*(PLACED|FIXED|COVER)\s*\(\s*([-+0-9.eE]+)\s+([-+0-9.eE]+)\s*\)")


def _section(lines, header):
    """Yield the logical entries of a DEF section, one string per '- ...;' statement."""
    inside, buf = False, []
    for line in lines:
        stripped = line.strip()
        if not inside:
            if re.match(r"^%s\s+\d+\s*;" % header, stripped):
                inside = True
            continue
        if stripped.startswith("END %s" % header):
            break
        buf.append(stripped)
        if stripped.endswith(";"):
            yield " ".join(buf)
            buf = []


def parse_regions(lines):
    """region name -> [(xlo, ylo, xhi, yhi), ...]. Points come in pairs, each pair a rectangle."""
    regions = {}
    for entry in _section(lines, "REGIONS"):
        name = entry.split()[1]
        pts = [(float(x), float(y)) for x, y in _POINT.findall(entry)]
        rects = []
        for i in range(0, len(pts) - 1, 2):
            (x0, y0), (x1, y1) = pts[i], pts[i + 1]
            rects.append((min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)))
        regions[name] = rects
    return regions


def parse_groups(lines):
    """group name -> (region name, [instance-name patterns])."""
    groups = {}
    for entry in _section(lines, "GROUPS"):
        head, _, tail = entry.partition("+ REGION")
        if not tail:
            continue
        tokens = head.split()
        groups[tokens[1]] = (tail.replace(";", "").strip(), tokens[2:])
    return groups


def parse_placement(lines):
    """instance name -> (x, y). Reads whichever of PLACED/FIXED the entry carries.

    Scoped to COMPONENTS on purpose: DEF PINS entries carry `+ PLACED ( x y ) N` in the same
    shape, and counting them inflated the instance total by every pin in the design.
    """
    placement, current, inside_components = {}, None, False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("COMPONENTS "):
            inside_components = True
            continue
        if stripped.startswith("END COMPONENTS"):
            break
        if not inside_components:
            continue
        start = _COMP_START.match(line)
        if start:
            current = start.group(1)
        if current is None:
            continue
        placed = _PLACED.match(line)
        if placed:
            placement[current] = (float(placed.group(2)), float(placed.group(3)))
            current = None
    return placement


def inside(point, rects):
    x, y = point
    return any(x0 <= x <= x1 and y0 <= y <= y1 for x0, y0, x1, y1 in rects)


def distance_outside(point, rects):
    """Shortest distance from the point to the nearest rectangle of the region (0 if inside)."""
    x, y = point
    best = float("inf")
    for x0, y0, x1, y1 in rects:
        dx = max(x0 - x, 0.0, x - x1)
        dy = max(y0 - y, 0.0, y - y1)
        best = min(best, (dx * dx + dy * dy) ** 0.5)
    return best


def rect_union_area(rects):
    """Exact union area by x-coordinate sweep -- the ISPD2015 regions genuinely do overlap."""
    xs = sorted({x for x0, _, x1, _ in rects for x in (x0, x1)})
    total = 0.0
    for xa, xb in zip(xs, xs[1:]):
        spans = sorted((y0, y1) for x0, y0, x1, y1 in rects if x0 <= xa and x1 >= xb)
        covered, cursor = 0.0, None
        for y0, y1 in spans:
            if cursor is None or y0 > cursor:
                covered += y1 - y0
                cursor = y1
            elif y1 > cursor:
                covered += y1 - cursor
                cursor = y1
        total += covered * (xb - xa)
    return total


def die_area(lines):
    for line in lines:
        if line.strip().startswith("DIEAREA"):
            pts = [(float(x), float(y)) for x, y in _POINT.findall(line)]
            (x0, y0), (x1, y1) = pts[0], pts[-1]
            return abs(x1 - x0) * abs(y1 - y0)
    return None


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    original, placed = sys.argv[1], sys.argv[2]
    as_csv = "--csv" in sys.argv
    expect_legal = "--expect-legal" in sys.argv

    with open(original) as f:
        original_lines = f.readlines()
    with open(placed) as f:
        placement = parse_placement(f.readlines())

    regions = parse_regions(original_lines)
    groups = parse_groups(original_lines)
    if not regions or not groups:
        print("no REGIONS/GROUPS in %s -- design is unconstrained" % original)
        return
    if not placement:
        sys.exit("FAIL: parsed 0 placements out of %s" % placed)

    total_die = die_area(original_lines)
    rows, matched_total, outside_total = [], 0, 0
    for group_name, (region_name, patterns) in sorted(groups.items()):
        rects = regions.get(region_name)
        if rects is None:
            sys.exit("FAIL: group %s names region %s, which is not in REGIONS" % (group_name, region_name))
        matcher = re.compile("|".join(fnmatch.translate(p) for p in patterns))

        distances = [distance_outside(xy, rects)
                     for name, xy in placement.items() if matcher.match(name)]
        out = [d for d in distances if d > 0.0]
        matched_total += len(distances)
        outside_total += len(out)
        area = rect_union_area(rects)
        rows.append((group_name, region_name, len(distances), len(out),
                     100.0 * len(out) / len(distances) if distances else 0.0,
                     max(out) if out else 0.0,
                     sum(out) / len(out) if out else 0.0,
                     100.0 * area / total_die if total_die else float("nan")))

    if as_csv:
        print("group,region,cells,outside,pct_outside,max_dist_dbu,mean_dist_dbu,region_pct_of_die")
        for r in rows:
            print("%s,%s,%d,%d,%.2f,%.0f,%.0f,%.2f" % r)
    else:
        print("%-10s %-8s %9s %9s %8s %12s %12s %10s" %
              ("group", "region", "cells", "outside", "%out", "max_dist", "mean_dist", "%of_die"))
        for r in rows:
            print("%-10s %-8s %9d %9d %7.2f%% %12.0f %12.0f %9.2f%%" % r)
        print("[info] %d of %d placed instances are fence-constrained (%.1f%%); %d violate (%.1f%%)"
              % (matched_total, len(placement), 100.0 * matched_total / len(placement),
                 outside_total, 100.0 * outside_total / matched_total if matched_total else 0.0))

    if expect_legal and outside_total:
        sys.exit("FAIL: %d of %d fence-constrained cells are outside their region in a placement "
                 "asserted to be fence-legal" % (outside_total, matched_total))


if __name__ == "__main__":
    main()
