#!/usr/bin/env python3
"""XPlace GP (pre-legalization) HPWL reference, for a true GP-vs-GP comparison.

The `XPlace HPWL` column in results.csv (emitted by Output.cpp) is XPlace's
GP + LEGALIZATION number from the TCAD 2023 paper. Legalization inflates HPWL, so
comparing our GP-only `Best HPWL` against it flatters us (reads ~2-3 points low).
XPlace's own GP HPWL is logged when we run it locally, at
    ~/phd/Xplace/result/<timestamp>_<design>/log/test.log
on the line:  'GP Stop! #Iters N masked_hpwl: X overflow: Y'.
`masked_hpwl` is XPlace's net-masked HPWL (nets with more pins than ignore_net_degree
excluded) -- the SAME metric sw_only reports with ignore_net_degree=100, so it is
directly comparable. That makes `Best HPWL / xplace_gp` an honest GP-vs-GP ratio.

XPLACE_GP_HPWL is a cached snapshot (from the 2026-07-10 XPlace runs). Call
scan_xplace_results() to refresh/extend it from the live result dirs.
"""
import glob
import os
import re

XPLACE_RESULT_DIR = os.path.expanduser("~/phd/Xplace/result")

# design -> XPlace GP masked_hpwl (pre-legalization). Snapshot of the 2026-07-10 runs;
# scan_xplace_results() overlays anything newer / additional found on disk.
XPLACE_GP_HPWL = {
    "adaptec1": 7.060218e7,
    "adaptec2": 7.893496e7,
    "adaptec3": 1.858436e8,
    "adaptec4": 1.675808e8,
    "bigblue1": 8.721903e7,
    "bigblue2": 1.298895e8,
}

_GP_STOP = re.compile(r"GP Stop!.*masked_hpwl:\s*([0-9.eE+]+)")


def scan_xplace_results(result_dir=XPLACE_RESULT_DIR):
    """{design: gp_masked_hpwl} from the newest test.log per design (dirs are
    '<timestamp>_<design>'; the timestamp has no '_', so split once on '_')."""
    found = {}
    for log in sorted(glob.glob(os.path.join(result_dir, "*", "log", "test.log"))):
        run = os.path.basename(os.path.dirname(os.path.dirname(log)))
        design = run.split("_", 1)[1] if "_" in run else run
        try:
            with open(log) as f:
                for line in f:
                    m = _GP_STOP.search(line)
                    if m:
                        found[design] = float(m.group(1))  # sorted asc -> newest wins
        except OSError:
            continue
    return found


def gp_ref(refresh=False):
    """GP reference dict (XPlace GP masked_hpwl, raw-DBU frame). Defaults to the curated
    ISPD2005 snapshot only: those share sw_only's raw-DBU HPWL frame, so the ratio is direct.
    ISPD2015 (mgc_*) XPlace HPWL is site-width-normalized (~/200), a different frame -> a naive
    ratio is off by site_width and must NOT be mixed in. `refresh` re-scans the result dirs but
    is opt-in for that reason (only safe for raw-DBU designs)."""
    ref = dict(XPLACE_GP_HPWL)
    if refresh:
        try:
            ref.update(scan_xplace_results())
        except OSError:
            pass
    return ref


if __name__ == "__main__":
    for design, hpwl in sorted(gp_ref().items()):
        print(f"{design:<20} {hpwl:.4e}")
