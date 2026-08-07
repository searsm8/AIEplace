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

# "<suite>/<design>" -> XPlace GP masked_hpwl (pre-legalization). Snapshot of the 2026-07-10 runs;
# scan_xplace_results() overlays anything newer / additional found on disk.
#
# KEYED ON "<suite>/<design>", NOT the bare design name (fixed 2026-08-07). adaptec1-4 and
# bigblue1-4 exist in BOTH ispd2005 and mms, and their references differ by ~15%
# (ispd2005/adaptec1 7.060e7 vs mms/adaptec1 6.453e7). Keyed on the bare name, scan_xplace_results
# returned whichever suite happened to be run LAST -- it landed on the ispd2005 value only by the
# accident that a 07-25 ispd2005 run sorts after the 07-17 mms batch. One more mms run and every
# ispd2005 ratio would have silently moved 15%. Matches benchmarks.py's canonical path format.
XPLACE_GP_HPWL = {
    "ispd2005/adaptec1": 7.060218e7,
    "ispd2005/adaptec2": 7.893496e7,
    "ispd2005/adaptec3": 1.858436e8,
    "ispd2005/adaptec4": 1.675808e8,
    "ispd2005/bigblue1": 8.721903e7,
    "ispd2005/bigblue2": 1.298895e8,
}

_GP_STOP = re.compile(r"GP Stop!.*masked_hpwl:\s*([0-9.eE+]+)")
_DATASET = re.compile(r"--dataset\s+(\S+)")


def scan_xplace_results(result_dir=XPLACE_RESULT_DIR):
    """{"<suite>/<design>": gp_masked_hpwl} from the newest test.log per suite+design.

    Dirs are '<timestamp>_<design>' and the timestamp has no '_', so the design name comes from
    a single split -- but the SUITE is not in the directory name at all. It is read from the
    logged `--dataset` argument, which is the only thing that distinguishes an ispd2005 adaptec1
    from an mms adaptec1. A run whose log has no --dataset is skipped rather than guessed.
    """
    found = {}
    for log in sorted(glob.glob(os.path.join(result_dir, "*", "log", "test.log"))):
        run = os.path.basename(os.path.dirname(os.path.dirname(log)))
        design = run.split("_", 1)[1] if "_" in run else run
        try:
            with open(log) as f:
                text = f.read()
        except OSError:
            continue
        ds = _DATASET.search(text)
        if not ds:
            continue  # cannot place it in a suite -> cannot key it safely
        hpwl = None
        for m in _GP_STOP.finditer(text):
            hpwl = float(m.group(1))  # last GP Stop in the log (phase 2 for a mixed-size run)
        if hpwl is not None:
            found[f"{ds.group(1)}/{design}"] = hpwl  # sorted asc -> newest run wins
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
