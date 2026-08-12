#!/usr/bin/env python3
"""TODO #3 -- build XPlace-readable inputs from sw_only GP results.

Finds the newest RowBasedPlacement.def per design under a sw_only results tree and
patches each design's original bookshelf .pl with those coordinates
(tools/def_to_bookshelf_pl.py), writing /tmp/lgdp/pl/<design>.pl for
tools/run_lgdp_suite.sh to feed to XPlace's --given_solution.

sw_only's DEF is written back in the original benchmark frame (DataBase::writeDieArea
un-shifts by m_die_shift) and Node::node_pos is the cell's LOWER-LEFT corner
(Grid.cpp:38 forms the centre as pos + size/2), which is exactly bookshelf .pl
semantics -- so the coordinates transfer with no scaling.

Usage: gen_lgdp_inputs.py [results_root ...]
"""
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path("/home/msears/phd/AIEplace/vck5000")
BENCH = REPO / "host/benchmarks/mms"
PATCHER = REPO / "tools/def_to_bookshelf_pl.py"
OUT = Path(os.environ.get("LGDP_PL", "/tmp/lgdp/pl"))

DESIGNS = ["adaptec1", "adaptec2", "adaptec3", "adaptec4", "adaptec5",
           "bigblue1", "bigblue2", "bigblue3", "bigblue4",
           "newblue1", "newblue2", "newblue3", "newblue4", "newblue5",
           "newblue6", "newblue7"]

DEFAULT_ROOTS = ["/tmp/phase2_suite/results"]


def newest_def(roots, design):
    """Newest RowBasedPlacement.def whose run dir belongs to `design`."""
    hits = []
    for root in roots:
        for p in Path(root).glob(f"**/{design}/*/RowBasedPlacement.def"):
            hits.append(p)
    return max(hits, key=lambda p: p.stat().st_mtime) if hits else None


def template_pl(design):
    """The .pl the .aux actually names -- NOT <design>.pl.

    Three MMS designs point their .aux somewhere else, and adaptec3's default
    adaptec3.pl carries none of the 665 `/FIXED` markers that adaptec3.2.pl does.
    Patching the wrong template hands XPlace 665 movable 0x0 terminals, which
    segfaults its greedy legalizer (TODO #3).
    """
    aux = BENCH / design / f"{design}.aux"
    names = re.findall(r"[A-Za-z0-9._]+\.pl", aux.read_text())
    return BENCH / design / names[0]


def main():
    roots = sys.argv[1:] or DEFAULT_ROOTS
    OUT.mkdir(parents=True, exist_ok=True)
    for design in DESIGNS:
        src = newest_def(roots, design)
        if src is None:
            print(f"{design:10s} SKIP  no RowBasedPlacement.def found")
            continue
        orig_pl = template_pl(design)
        if not orig_pl.exists():
            print(f"{design:10s} SKIP  missing {orig_pl}")
            continue
        res = subprocess.run(
            ["python3", str(PATCHER), str(src), str(orig_pl), str(OUT / f"{design}.pl")],
            capture_output=True, text=True)
        print(f"{design:10s} {res.stdout.strip()}  <- {src.parent.name}")


if __name__ == "__main__":
    main()
