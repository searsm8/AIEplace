#!/usr/bin/env python3
"""Patch a bookshelf .pl with placed coords from a sw_only output DEF.

Reads sw_only's RowBasedPlacement.def (COMPONENTS: name + PLACED (x y) orient),
and rewrites the original bookshelf .pl replacing coords of *movable* nodes
(lines without /FIXED) with the DEF coords (rounded to int). /FIXED lines are
left untouched so terminals keep their exact benchmark positions.

Usage: def_to_bookshelf_pl.py OUT.def ORIG.pl NEW.pl
"""
import re
import sys


def parse_def_coords(def_path):
    coords = {}
    name = None
    with open(def_path) as f:
        for line in f:
            s = line.strip()
            if s.startswith("- "):
                # "- o0 macro_8_12"
                name = s.split()[1]
            elif name is not None and "PLACED" in s:
                m = re.search(r"PLACED\s*\(\s*([-\d.]+)\s+([-\d.]+)\s*\)", s)
                if m:
                    coords[name] = (float(m.group(1)), float(m.group(2)))
                name = None
    return coords


def patch_pl(orig_pl, coords, out_pl):
    n_patched = 0
    n_fixed = 0
    n_missing = 0
    with open(orig_pl) as fin, open(out_pl, "w") as fout:
        for line in fin:
            stripped = line.strip()
            if (not stripped) or stripped.startswith("#") or stripped.startswith("UCLA"):
                fout.write(line)
                continue
            parts = stripped.split()
            name = parts[0]
            if "/FIXED" in stripped:
                fout.write(line)
                n_fixed += 1
                continue
            if name in coords:
                x, y = coords[name]
                fout.write("%s\t%d\t%d\t: N\n" % (name, round(x), round(y)))
                n_patched += 1
            else:
                fout.write(line)
                n_missing += 1
    return n_patched, n_fixed, n_missing


if __name__ == "__main__":
    def_path, orig_pl, out_pl = sys.argv[1], sys.argv[2], sys.argv[3]
    coords = parse_def_coords(def_path)
    p, fx, miss = patch_pl(orig_pl, coords, out_pl)
    print("DEF coords: %d | patched(movable): %d | fixed(kept): %d | missing(kept): %d"
          % (len(coords), p, fx, miss))
