#!/usr/bin/env python3
"""Legalize + detailed-place ONE sw_only GP placement through XPlace's own LG+DP (TODO #30).

The per-design core dse.py calls after each GP run. Ports the run_one() of tools/run_lgdp44.sh
(ispd2005 + ispd2015) and tools/run_lgdp_suite.sh (mms) into one callable so a sweep can score
legal-vs-legal without a second suite runner.

    from lgdp import legalize
    res = legalize("ispd2015/mgc_fft_a", gp_def_path, work_dir)
    #   -> {"lg": "3.09e6", "dp": "3.06e6", "variant": "ispd2015", "status": "done"}

XPlace legalizes a GIVEN placement with `main.py --global_placement False --given_solution
<file>`, no source changes (run_placement_nesterov.py:15-25). It needs CUDA + its own conda
python, so only a run that actually legalizes imports that cost; a --gp-only sweep never calls
this.

THE FRAME NOTE that keeps burning people: `dp` here is scraped from XPlace's OWN log ("After DP,
HPWL"), so it is already in XPlace's frame — site units on tier 2. The ratio against
benchmarks.py::xplace_dp_hpwl therefore needs NO site_width conversion, unlike the raw-DBU GP
number. (analyze_full44.py's frame rule.) This module does not compute the ratio; it returns the
number in XPlace's frame and lets the caller pair it with the like-framed reference.

Three input paths, because the tiers are different formats (all inherited from run_lgdp44.sh):
  ispd2005 / mms : bookshelf — patch the .pl the design's .aux NAMES (not always <design>.pl;
                   the wrong template silently drops /FIXED markers). def_to_bookshelf_pl.py.
  ispd2015       : LEF/DEF — patch the ORIGINAL floorplan.def's COMPONENT placements, because
                   sw_only writes no ROW statements and the legalizer needs the site rows.
                   def_patch_placement.py. Splits again on fence regions (TODO #26):
                     no REGIONS/GROUPS -> --custom_path onto our raw files;
                     REGIONS/GROUPS    -> --dataset ispd2015_fix (XPlace's fence-stripped data;
                                          it raises NotImplementedError on the raw fenced files).
"""
import os
import re
import subprocess
from pathlib import Path

REPO = Path("/home/msears/phd/AIEplace/vck5000")
XPLACE = Path("/home/msears/phd/Xplace")
PY = os.path.expanduser("~/anaconda3/bin/python")
CUDA_HOME = "/usr/local/cuda-12.3"


def _xplace_env():
    env = dict(os.environ)
    env["CUDA_HOME"] = CUDA_HOME
    env["PATH"] = f"{CUDA_HOME}/bin:{os.path.dirname(PY)}:" + env.get("PATH", "")
    return env


def _bookshelf_template(suite, design):
    """The .pl the design's .aux NAMES — not always <design>.pl, and the wrong one drops the
    /FIXED terminal markers (that was adaptec3's legalizer segfault, TODO #3)."""
    design_dir = REPO / "host/benchmarks" / suite / design
    names = re.findall(r"[A-Za-z0-9._]+\.pl", (design_dir / f"{design}.aux").read_text())
    return design_dir / names[0]


def _has_fence(def_path):
    with open(def_path) as f:
        return any(line.startswith("REGIONS") for line in f)


def _patch(script, args, log_path):
    with open(log_path, "a") as log:
        return subprocess.run(["python3", str(REPO / "tools" / script), *map(str, args)],
                              stdout=log, stderr=log).returncode == 0


def _last(text, pattern):
    hits = re.findall(pattern, text)
    return hits[-1] if hits else None


def legalize(bench_path, gp_def, work_dir, seed=42):
    """Legalize one GP DEF. Returns {lg, dp, variant, status}; status "done" iff XPlace exited 0
    and produced a post-DP HPWL. Never raises for an expected failure (missing data, patch fail,
    XPlace crash) — it records the reason in `status` so one bad design cannot abort a sweep."""
    suite, design = bench_path.split("/")
    # XPlace runs with cwd=XPLACE, so every path handed to it (given_solution, custom_path def:)
    # must be absolute — a path relative to the caller's cwd would resolve against the wrong root.
    gp_def = Path(gp_def).resolve()
    work = Path(work_dir).resolve()
    work.mkdir(parents=True, exist_ok=True)
    run_log = work / "xplace.log"
    patch_log = work / "patch.log"

    if suite in ("ispd2005", "mms"):
        given = work / f"{design}.pl"
        if not _patch("def_to_bookshelf_pl.py",
                      [gp_def, _bookshelf_template(suite, design), given], patch_log):
            return {"variant": "bookshelf", "status": "fail_patch"}
        cmd = (["--dataset", "mms", "--design_name", design, "--mixed_size", "True"]
               if suite == "mms" else ["--dataset", "ispd2005", "--design_name", design])
        variant = "bookshelf"
    else:  # ispd2015
        raw = XPLACE / "data/raw/ispd2015" / design
        given = work / f"{design}.def"
        if _has_fence(raw / "floorplan.def"):
            fix = XPLACE / "data/raw/ispd2015_fix" / design
            if not (fix / f"{design}.def").exists():
                # Missing _fix is the recurrence mode (fresh box / re-download). Fail loudly and
                # name the fix — silently skipping the 9 fenced designs sent TODO #22 round twice.
                return {"variant": "ispd2015_fix", "status": "fail_no_fix_data",
                        "hint": f"cd {XPLACE}/data && python3 fix_ispd2015_route.py"}
            template = fix / f"{design}.def"
            cmd = ["--dataset", "ispd2015_fix", "--design_name", design]
            variant = "ispd2015_fix"
        else:
            template = raw / "floorplan.def"
            cmd = ["--custom_path", f"tech_lef:{raw}/tech.lef,cell_lef:{raw}/cells.lef,"
                   f"def:{raw}/floorplan.def,design_name:{design},benchmark:ispd2015"]
            variant = "ispd2015"
        if not _patch("def_patch_placement.py", [gp_def, template, given], patch_log):
            return {"variant": variant, "status": "fail_patch"}

    full = [PY, "-u", "main.py", *cmd, "--load_from_raw", "True", "--global_placement", "False",
            "--given_solution", str(given), "--num_threads", "8", "--seed", str(seed)]
    with open(run_log, "w") as log:
        ec = subprocess.run(full, cwd=XPLACE, env=_xplace_env(),
                            stdout=log, stderr=log, stdin=subprocess.DEVNULL).returncode

    text = run_log.read_text()
    lg = _last(text, r"Finish Legalization, HPWL: ([0-9.E+-]+)")
    dp = _last(text, r"After DP, HPWL: ([0-9.E+-]+)")
    status = "done" if ec == 0 else f"exit{ec}"
    if not dp:
        status += "_nodp"
    return {"lg": lg, "dp": dp, "variant": variant, "status": status}


if __name__ == "__main__":
    import json
    import sys
    if len(sys.argv) < 4:
        sys.exit("usage: lgdp.py <suite/design> <gp_result.def> <work_dir>")
    print(json.dumps(legalize(sys.argv[1], sys.argv[2], sys.argv[3]), indent=2))
