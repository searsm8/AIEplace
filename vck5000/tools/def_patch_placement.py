#!/usr/bin/env python3
"""Patch a DEF's COMPONENT placements with sw_only's, preserving everything else.

The DEF analogue of tools/def_to_bookshelf_pl.py. Needed for the ISPD2015 tier, which is
LEF/DEF rather than bookshelf, so there is no `.pl` to patch.

Why not just hand XPlace our own written DEF: sw_only writes COMPONENTS/PINS/NETS but **no ROW
statements** (mgc_fft_a: 400 rows in the original, 0 in ours), and the legalizer needs the site
rows. So we take the ORIGINAL floorplan.def and rewrite only the placement clause of each
movable component -- rows, pins, nets, vias, blockages and the die area all stay byte-identical.

Two details that are easy to get wrong:
  * The original marks movable cells `+ UNPLACED ;` and fixed ones `+ FIXED ( x y ) N ;`.
    Only UNPLACED entries are rewritten; a FIXED cell did not move and its original line is kept
    verbatim, so we can never displace a fixed macro by rounding it.
  * sw_only writes FRACTIONAL coordinates (`83228.1`); DEF coordinates are integer DBU. They are
    ROUNDED here, not truncated. At ISPD2015's site_width of 100-200 DBU a <=0.5 DBU shift is far
    below one site and legalization snaps to sites anyway, but it is a real (tiny) difference
    between what we measured and what XPlace legalizes -- worth knowing, not worth avoiding.

Usage: def_patch_placement.py <ours.def> <original.def> <out.def>
"""
import re
import sys

_COMP_START = re.compile(r"^\s*-\s+(\S+)\s")
_PLACED = re.compile(r"^\s*\+\s*(PLACED|FIXED|UNPLACED|COVER)\b(.*)$")
# Coordinates must accept SCIENTIFIC NOTATION. sw_only writes large coordinates as e.g.
# "( 623953 1.26917e+06 )" -- a plain [-0-9.]+ pattern silently skips those entries, which on
# mgc_superblue19 dropped 63% of the components (and 100% on the two largest) and left them
# UNPLACED for XPlace to place itself. That produced an LG/DP number that was partly XPlace's own
# placement. Caught 2026-08-07 only because those runs happened to crash; a design that survived
# would have reported a plausible, wrong number.
_XY = re.compile(r"\(\s*([-+0-9.eE]+)\s+([-+0-9.eE]+)\s*\)\s*(\w+)")


def read_placements(path):
    """{component: (x, y, orient)} from a sw_only-written DEF."""
    out, name, in_comps = {}, None, False
    with open(path) as f:
        for line in f:
            if line.startswith("COMPONENTS"):
                in_comps = True; continue
            if line.startswith("END COMPONENTS"):
                break
            if not in_comps:
                continue
            m = _COMP_START.match(line)
            if m:
                name = m.group(1); continue
            p = _PLACED.match(line)
            if p and name:
                xy = _XY.search(p.group(2))
                if xy:
                    out[name] = (float(xy.group(1)), float(xy.group(2)), xy.group(3))
                name = None
    return out


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    ours_path, orig_path, out_path = sys.argv[1:4]
    ours = read_placements(ours_path)

    patched = kept_fixed = missing = 0
    name, in_comps = None, False
    with open(orig_path) as fin, open(out_path, "w") as fout:
        for line in fin:
            if line.startswith("COMPONENTS"):
                in_comps = True
            elif line.startswith("END COMPONENTS"):
                in_comps = False

            if in_comps:
                m = _COMP_START.match(line)
                if m:
                    name = m.group(1)
                    fout.write(line); continue
                p = _PLACED.match(line)
                if p and name is not None:
                    status = p.group(1)
                    if status == "UNPLACED" and name in ours:
                        x, y, o = ours[name]
                        fout.write("      + PLACED ( %d %d ) %s ;\n" % (round(x), round(y), o))
                        patched += 1
                    else:
                        if status in ("FIXED", "COVER"):
                            kept_fixed += 1
                        elif name not in ours:
                            missing += 1
                        fout.write(line)
                    name = None
                    continue
            fout.write(line)

    print("patched(movable): %d | kept fixed: %d | movable with no coord (left UNPLACED): %d"
          % (patched, kept_fixed, missing))
    if missing:
        # Hard failure, not a warning: any component left UNPLACED is placed by XPLACE, so the
        # result would be a blend of the two placers reported as ours.
        raise SystemExit(
            "ERROR: %d components have no coordinate in %s and would be left UNPLACED.\n"
            "       XPlace would place them itself, so the LG/DP number would not be ours.\n"
            "       Check the coordinate format in the source DEF (scientific notation?)."
            % (missing, ours_path))


if __name__ == "__main__":
    main()
