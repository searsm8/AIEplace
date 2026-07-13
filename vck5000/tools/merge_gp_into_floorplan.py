#!/usr/bin/env python3
"""Tier-1 OpenDP bootstrap: overwrite a floorplan DEF's COMPONENT placements with AIEplace
(sw_only) global-placement coordinates, so OpenROAD can legalize + detail-place our GP result.

The shipped ISPD2015 `floorplan.def` already carries ROWS / DIEAREA / PINS / NETS and the exact
LEF macro/instance names OpenROAD needs; only its COMPONENTS are UNPLACED. sw_only's own
`writeDEF` output (`<design>.def`) has every instance PLACED at our GP coords but NO ROWS. This
script splices the two: base = floorplan.def (structure), coords = our GP DEF.

Usage:
    python merge_gp_into_floorplan.py OUR_GP.def FLOORPLAN.def OUT.def

Instance names must match between the two DEFs (verified: identical sets for mgc_des_perf_1).
A component present in the floorplan but missing from the GP DEF is left UNPLACED and counted;
a nonzero missing count is reported as a warning (OpenROAD would then place it from scratch).
"""
import re
import sys

COMP_LINE = re.compile(r"^\s*-\s+(\S+)\s+(\S+)")           # "   - <inst> <macro> ..."
# a placement status line inside a component record: UNPLACED / PLACED / FIXED
STATUS_LINE = re.compile(r"^(\s*\+\s*)(UNPLACED|PLACED|FIXED)\b(.*?)(;?)\s*$")


def load_gp_coords(path):
    """inst -> (x, y, orient) from an AIEplace writeDEF output."""
    coords = {}
    in_comps = False
    cur = None
    with open(path) as f:
        for line in f:
            s = line.strip()
            if s.startswith("COMPONENTS"):
                in_comps = True
                continue
            if s.startswith("END COMPONENTS"):
                break
            if not in_comps:
                continue
            m = COMP_LINE.match(line)
            if m:
                cur = m.group(1)
            # coords may be on the "- ..." line or the following "+ PLACED ( x y ) O ;" line.
            # sw_only's writeDEF emits FRACTIONAL DBU coords, and large values in scientific
            # notation ("1.46115e+06"); DEF requires integers, so accept both forms and round.
            num = r"-?[\d.]+(?:[eE][+-]?\d+)?"
            pm = re.search(rf"(PLACED|FIXED)\s*\(\s*({num})\s+({num})\s*\)\s*(\w+)", line)
            if pm and cur:
                x = str(round(float(pm.group(2))))
                y = str(round(float(pm.group(3))))
                coords[cur] = (x, y, pm.group(4))
                cur = None
    return coords


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    gp_def, floorplan_def, out_def = sys.argv[1:4]
    coords = load_gp_coords(gp_def)
    print(f"loaded {len(coords)} GP placements from {gp_def}")

    placed = missing = 0
    cur = None
    with open(floorplan_def) as fin, open(out_def, "w") as fout:
        for line in fin:
            m = COMP_LINE.match(line)
            if m:
                cur = m.group(1)
                fout.write(line)
                continue
            sm = STATUS_LINE.match(line)
            if sm and cur is not None:
                xyo = coords.get(cur)
                if xyo:
                    x, y, o = xyo
                    fout.write(f"{sm.group(1)}PLACED ( {x} {y} ) {o} ;\n")
                    placed += 1
                else:
                    fout.write(line)          # leave as-is (UNPLACED)
                    missing += 1
                cur = None
                continue
            fout.write(line)

    print(f"wrote {out_def}: {placed} components placed from GP, {missing} left unplaced (missing)")
    if missing:
        print(f"WARNING: {missing} floorplan components had no GP coordinate", file=sys.stderr)


if __name__ == "__main__":
    main()
