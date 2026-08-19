#!/usr/bin/env python3
"""dse.py — the launch point for running sw_only over many benchmarks / many configs.

    cd vck5000
    python3 tools/dse.py                                   # the 28-design suite, XPlace grids
    python3 tools/dse.py --designs tier3                   # the 16 MMS designs
    python3 tools/dse.py --designs ispd2005/adaptec1,mgc_fft_a \
                         --set enable_preconditioning=true,false      # a 2x2 A/B
    python3 tools/dse.py --designs mgc_fft_2 --grid 512,256,128
    python3 tools/dse.py --runset results/morris_<ts>/runset.json
    python3 tools/dse.py --resume results/DSE_<ts>
    python3 tools/dse.py --gp-only                         # GP only, skip the LG+DP score
    python3 tools/dse.py --designs tier1 --dry-run         # list the runs, launch nothing

Three ways to say what to run, and no others:
  --designs   a design list; grid and target_density come from tools/benchmarks.py (XPlace's
              own per-design tuned values). Accepts "all"; a whole tier by number or suite name
              (tier1/ispd2005, tier2/ispd2015, tier3/mms, case-insensitive); "+"-joined
              (tier1+tier2, ispd2005+mms); or comma-separated design names / "suite/design" paths.
  --set K=V   a config override. Comma-separated values sweep it; several --set flags take
              the Cartesian product. K may be dotted ("output.dump_positions=true") to pick
              the TOML section; a bare key means [params], and "benchmark" means [input].
  --runset    a JSON list of run dicts, {"label":..., "benchmark":..., <overrides>}. This is
              what tools/morris.py and tools/sobol.py emit.

By default each GP run is then legalized + detailed-placed through XPlace's OWN LG+DP, so the
headline is legal-vs-legal (post-DP HPWL, the metric the XPlace paper reports — a GP-vs-GP number
flatters whichever placer spread less, TODO #3). Pass --gp-only to stop after GP. LG+DP needs
XPlace's environment (CUDA + its conda python); see tools/lgdp.py.

What a sweep leaves behind, in results/DSE_<timestamp>/ — one file per WRITER, named for its owner:
  sweep.json      the manifest of what was launched — every run's label, config path, overrides
  configs/, logs/     one TOML / one stdout+stderr capture per run
  gp_only.csv     the EXE's raw GP record, one appended row per run (schema owned by Output.cpp)
  lgdp.json       the LG WORKER's per-run LG+DP result (unless --gp-only); XPlace logs in lgdp/<label>/
  dse_results.csv DSE.PY's product: gp_only.csv plus every XPlace comparison (GP Ratio, LG/DP), from
                  tools/benchmarks.py; rewritten whole after EVERY run, so killing a sweep partway
                  still leaves the table for what finished. Its `grid` column is the EFFECTIVE grid
                  the run used (the row cap can lower it, TODO #31), not the grid that was requested
                  -- gp_only.csv keeps the request, which is the identity

Three files, three owners, no shared writer: the exe only appends gp_only.csv, the LG worker only
writes lgdp.json, dse.py only writes dse_results.csv — so nothing clashes across a --resume, and
dse_results.csv is disposable (delete it and `analyze_dse.py` rebuilds it) while the other two are
the durable record. A run's identity is (Suite, Design, grid, swept values) — all visible columns
(dse.py emits `grid` + each swept param into DSE_info), so gp_only.csv and sweep.json rows join on
that tuple and --resume skips identities already in gp_only.csv.

GP runs one design at a time (the placer is OpenMP-threaded across all cores, so concurrent GPs
were measured — 9bea10e, 2026-07-31 — to give the same wall clock with more ways to go wrong), but
each design's LG+DP is pipelined: it runs on a background worker (GPU, XPlace) while the NEXT
design's GP runs (CPU, sw_only), hiding the GPU time. Designs run smallest-first so a bad config
fails in seconds, not after bigblue4.
"""

import argparse
import concurrent.futures
import copy
import datetime
import glob
import itertools
import json
import os
import re
import subprocess
import sys
import time
from collections import OrderedDict

import tomlkit

import benchmarks  # master manifest: design list, XPlace grid + target_density
import lgdp        # per-design legalization + detailed placement via XPlace (TODO #30)

EXE_PATH = "build/hw/host/sw_only/aieplace_sw_only.exe"
TEMPLATE_PATH = "host/src/sw_only/default_config.toml"
BENCH_ROOT = "host/benchmarks"

# CLAUDE CODE: one file per owner, and the name says who wrote it -- the exe appends gp_only.csv,
# the LG worker owns lgdp.json, dse.py produces this.
RESULTS_CSV = "dse_results.csv"

# Raw exe columns summarize depends on. Checked as a POSITIVE list: a name missing from
# dse_results.csv warns loudly, rather than the table silently blanking as it did when Output.cpp
# renamed its columns (TODO #28).
RESULT_COLS = ["Best Iter", "Best OVFW", "Best GP HPWL", "Final HPWL Exact"]


# =============================================================================
# Building the run list
# =============================================================================

def _is_float(text):
    try:
        float(text)
        return True
    except (TypeError, ValueError):
        return False


def coerce(text):
    """'true'/'42'/'1e-5'/'cpu' -> the TOML value it should become."""
    low = text.strip().lower()
    if low in ("true", "false"):
        return low == "true"
    for cast in (int, float):
        try:
            return cast(text)
        except ValueError:
            pass
    return text.strip()


def design_bytes(path):
    """Input size, used only to order the sweep smallest-first."""
    directory = os.path.join(BENCH_ROOT, path)
    if not os.path.isdir(directory):
        return 0
    return sum(entry.stat().st_size for entry in os.scandir(directory) if entry.is_file())


def parse_sets(set_args):
    """['k=v1,v2', 'a.b=x'] -> OrderedDict{k: [v1, v2], 'a.b': [x]}."""
    sets = OrderedDict()
    for arg in set_args:
        key, sep, values = arg.partition("=")
        if not sep:
            raise SystemExit(f"--set needs KEY=VALUE, got '{arg}'")
        sets[key.strip()] = [coerce(v) for v in values.split(",")]
    return sets


def grid_values(spec, meta):
    """--grid xplace | auto | 512,256 -> the bins_per_row values to run (None = auto-size)."""
    if spec == "auto":
        return [None]
    if spec == "xplace":
        return [meta["grid"]]
    return [int(v) for v in spec.split(",")]


def build_runs(args):
    """-> (runs, col_keys). runs = [(label, overrides, columns)]; columns is the {grid + swept}
    dict emitted into dse_results.csv (via DSE_info) and forming each run's identity. col_keys is that
    column order, shared across the sweep."""
    if args.runset:
        with open(args.runset) as f:
            specs = [dict(s) for s in json.load(f)]
        # A runset's "swept" columns are whatever varies across it (grid covers bins_per_row). The
        # `run` label is kept as a column here (unlike the CLI path): analyze_morris/analyze_sobol
        # join the results back to their sample matrix by it (label m0000 == X row 0).
        keys = {k for s in specs for k in s} - {"label", "benchmark", "bins_per_row"}
        swept = sorted(k for k in keys if len({str(s.get(k)) for s in specs}) > 1)
        col_keys = ["run", "grid"] + swept
        runs = []
        for i, spec in enumerate(specs):
            label = str(spec.pop("label", f"r{i:04d}"))
            spec["benchmark"] = benchmarks.resolve(spec["benchmark"])
            columns = {"run": label, "grid": spec.get("bins_per_row", "auto")}
            columns.update({k: spec.get(k, "") for k in swept})
            runs.append((label, spec, columns))
        return runs, col_keys

    sets = parse_sets(args.set)
    # Fail here, not in 2.7 hours. write_config() will happily write ANY key into the TOML and the
    # exe reads with value_or(default), so a misspelled --set produces a sweep whose arms are all
    # the same behaviour -- reported as a clean success. config_keys derives the readable set from
    # the sw_only sources, so it cannot drift from the code.
    if sets:
        checker = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config_keys.py")
        if subprocess.run([sys.executable, checker, "--check", *sets]).returncode:
            raise SystemExit("dse: refusing to launch -- see the unread --set key(s) above.")

    swept = [k for k, v in sets.items() if len(v) > 1]   # only these get a column / label suffix
    col_keys = ["grid"] + [k.rsplit(".", 1)[-1] for k in swept]
    designs = sorted(benchmarks.expand_designs(args.designs), key=design_bytes)

    runs = []
    for path in designs:
        meta = benchmarks.BENCHMARKS[path]
        for grid in grid_values(args.grid, meta):
            for combo in itertools.product(*sets.values()):
                overrides = {"benchmark": path,
                             "maximum_utilization": meta["target_density"],
                             "random_seed": args.seed}
                if grid is not None:
                    overrides["bins_per_row"] = grid
                overrides.update(zip(sets.keys(), combo))

                columns = {"grid": grid if grid is not None else "auto"}
                columns.update({k.rsplit(".", 1)[-1]: overrides[k] for k in swept})
                label = f"{meta['name']}@{columns['grid']}"
                label += "".join(f"_{k}={columns[k]}" for k in col_keys[1:])
                runs.append((label, overrides, columns))
    return runs, col_keys


# =============================================================================
# Writing configs
# =============================================================================

def section_for(key):
    """Which TOML table an override lands in. Dotted key wins; 'benchmark' is [input]."""
    if "." in key:
        return key.rsplit(".", 1)
    return ("input" if key == "benchmark" else "params"), key


def write_config(template, overrides, columns, path, run_num, total):
    config = copy.deepcopy(template)
    for key, value in overrides.items():
        section, param = section_for(key)
        if section not in config:
            config[section] = tomlkit.table()
        config[section][param] = f"{BENCH_ROOT}/{value}" if param == "benchmark" else value

    # The exe drops the first two DSE_info lines and turns the rest into dse_results.csv columns
    # (Output.cpp::parseDSEParams). We emit `grid` + each swept parameter, so the CSV columns
    # ARE the run's identity (Suite, Design, grid, swept-values) -- no opaque run label needed.
    lines = [f"DSE sweep progress={run_num} of {total}", f"benchmark={overrides['benchmark']}"]
    lines += [f"{k}={v}" for k, v in columns.items()]
    config["output"]["DSE_info"] = "\n".join(lines)
    with open(path, "w") as f:
        tomlkit.dump(config, f)


def prepare(runs, col_keys, sweep_dir):
    """Write configs/ + sweep.json. Returns the manifest entries, in launch order."""
    with open(TEMPLATE_PATH) as f:
        template = tomlkit.load(f)
    template["output"]["quiet"] = True
    template["output"]["interactive"] = False
    template["output"]["dump_positions"] = False   # tens of GB across a sweep (TODO #16)
    template["output"]["results_dir"] = sweep_dir

    os.makedirs(os.path.join(sweep_dir, "configs"), exist_ok=True)
    os.makedirs(os.path.join(sweep_dir, "logs"), exist_ok=True)

    manifest = []
    for n, (label, overrides, columns) in enumerate(runs, 1):
        config_path = os.path.join(sweep_dir, "configs", f"run_{n:03d}.toml")
        write_config(template, overrides, columns, config_path, n, len(runs))
        manifest.append({"n": n, "label": label, "config": config_path,
                         "log": os.path.join(sweep_dir, "logs", f"run_{n:03d}.log"),
                         "overrides": overrides, "columns": columns})
    with open(os.path.join(sweep_dir, "sweep.json"), "w") as f:
        json.dump({"created": datetime.datetime.now().isoformat(timespec="seconds"),
                   "exe": EXE_PATH, "col_keys": col_keys, "runs": manifest}, f, indent=2)
    return manifest


# =============================================================================
# Running
# =============================================================================

# A run's identity is (benchmark, its column values) — the same tuple whether read from a sweep.json
# entry or a dse_results.csv row, so the two join without an opaque label column.
def entry_ident(entry, col_keys):
    return (entry["overrides"]["benchmark"], tuple(str(entry["columns"][k]) for k in col_keys))

def row_ident(row, col_keys):
    return (f"{row.get('Suite', '')}/{row.get('Design', '')}",
            tuple(str(row.get(k, "")) for k in col_keys))


def read_gp(sweep_dir):
    """The exe's raw per-run GP records (gp_only.csv). dse.py enriches these into dse_results.csv but
    never writes gp_only.csv, so the exe can keep appending to it across --resume without a schema
    clash."""
    import csv
    path = os.path.join(sweep_dir, "gp_only.csv")
    if not os.path.exists(path):
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def resolve_gp_def(sweep_dir, entry, col_keys):
    """The GP `.def` this run wrote, matched by identity to gp_only.csv's Output Dir. Called on the
    MAIN thread right after the GP subprocess exits — no concurrent writer — so the LG+DP worker
    never races the exe appending the next run's row. Basename varies by tier (RowBasedPlacement.def,
    fft.def, …), so glob it, same as run_lgdp44.sh's newest_def()."""
    want = entry_ident(entry, col_keys)
    out_dir = next((r.get("Output Dir") for r in read_gp(sweep_dir)
                    if row_ident(r, col_keys) == want), None)
    defs = glob.glob(os.path.join(out_dir, "*.def")) if out_dir else []
    return defs[0] if defs else None


def legalize_run(sweep_dir, entry, gp_def, store):
    """Legalize one GP run (its `.def` already resolved) and record it in `store` -> lgdp.json.
    Runs on the background worker so its GPU LG+DP overlaps the next run's CPU GP."""
    label = entry["label"]
    if not gp_def:
        store[label] = {"status": "fail_no_gp_def"}
    else:
        store[label] = lgdp.legalize(entry["overrides"]["benchmark"], gp_def,
                                     os.path.join(sweep_dir, "lgdp", label))
    with open(os.path.join(sweep_dir, "lgdp.json"), "w") as f:
        json.dump(store, f, indent=2)
    r = store[label]
    print(f"DSE:      LG+DP {r['status']:<14} DP={r.get('dp') or '-':>12}  {label}"
          f"{('  ' + r['hint']) if 'hint' in r else ''}", flush=True)


def run_all(entries, total, sweep_dir, gp_only, col_keys):
    # Legalization is decoupled from the GP loop: it legalizes any completed GP run not yet in
    # lgdp.json. On a fresh sweep gp_only.csv is empty so this backfill is a no-op; on --resume it
    # picks up runs whose GP finished but whose LG did not (an interruption between the two).
    lgdp_store = {}
    if not gp_only:
        lgdp_path = os.path.join(sweep_dir, "lgdp.json")
        if os.path.exists(lgdp_path):
            with open(lgdp_path) as f:
                lgdp_store = json.load(f)
        with open(os.path.join(sweep_dir, "sweep.json")) as f:
            by_ident = {entry_ident(e, col_keys): e for e in json.load(f)["runs"]}
        for row in read_gp(sweep_dir):
            entry = by_ident.get(row_ident(row, col_keys))
            if entry and entry["label"] not in lgdp_store:
                legalize_run(sweep_dir, entry, resolve_gp_def(sweep_dir, entry, col_keys), lgdp_store)

    t0 = time.time()
    failed = 0
    # Pipeline the sweep: GP(n) (CPU, sw_only OpenMP) runs while LG+DP(n-1) (GPU, XPlace) finishes
    # on ONE background worker, hiding the GPU time behind the next design's placement. The worker's
    # result is collected before the next is submitted, so lgdp.json writes stay serial and the
    # main thread resolves each `.def` while no GP subprocess is touching gp_only.csv.
    with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
        pending = None
        for done, entry in enumerate(entries, 1):
            t_run = time.time()
            with open(entry["log"], "w") as log:
                result = subprocess.run([EXE_PATH, entry["config"]], stdout=log, stderr=log)
            ok = result.returncode == 0
            failed += not ok
            print(f"DSE: [{done}/{total}] {'DONE' if ok else 'FAIL'} "
                  f"{time.time() - t_run:7.1f}s  {entry['label']}"
                  f"{'' if ok else '  -> ' + entry['log']}", flush=True)
            if pending is not None:
                pending.result()          # prev LG+DP (overlapped this GP) — recorded itself
                pending = None
            # CLAUDE CODE: refresh dse_results.csv HERE, the one point per iteration where no LG+DP
            # worker is running — summarize() reads lgdp.json, which legalize_run() rewrites whole
            # from the worker thread, so anywhere else risks reading a half-written file. Same
            # quiescence argument as resolve_gp_def below. A sweep killed partway then leaves a
            # complete dse_results.csv for every run that finished, instead of none at all.
            summarize(sweep_dir, quiet=True)
            if ok and not gp_only:
                gp_def = resolve_gp_def(sweep_dir, entry, col_keys)
                pending = pool.submit(legalize_run, sweep_dir, entry, gp_def, lgdp_store)
        if pending is not None:
            pending.result()
            summarize(sweep_dir, quiet=True)   # the last design's LG+DP, collected after the loop
    return failed, time.time() - t0


# =============================================================================
# Summary
# =============================================================================

ENRICHED = ["XPlace GP HPWL", "GP Ratio", "Our LG HPWL", "Our DP HPWL", "XPlace DP HPWL", "DP Ratio"]
# Two overflow columns slotted next to our own smoothed "Best OVFW", so every design carries three
# verdicts on the SAME shipped geometry (TODO #3/#31): our EXACT overflow, and XPlace's exact
# overflow on the .def we handed it (lgdp.py `in_ovfl`, TODO #3's gp_ovfl_in). "Best OVFW" is our
# *smoothed* convergence signal and under-reads exact overflow by up to ~3x, so the exact column is
# the one to compare against XPlace's. (Confound to keep in mind: XPlace evaluates at its own
# target_density = 1.0 for mgc_*, TODO #25, and on its own row-capped grid, TODO #31.)
EXACT_COL = "Our Exact OVFW"
OVFL_COL = "XPlace In OVFW"


def _our_exact_overflow(out_dir):
    """Our exact (filler-excluded) overflow on the shipped placement, scraped from the run's
    summary. Post-#24 restore, this is measured on the same geometry the .def holds — the geometry
    XPlace re-evaluates for its `in_ovfl`. Not in gp_only.csv (only smoothed Best OVFW is), so it is
    read from run_summary.md here rather than adding an exe column."""
    if not out_dir:
        return None
    for name in ("run_summary.md", "run.log"):
        path = os.path.join(out_dir, name)
        if os.path.exists(path):
            with open(path) as f:
                m = re.search(r"Final Overflow \(exact, no fillers\)\s*\|\s*([0-9.eE+-]+)", f.read())
            if m:
                return m.group(1)
    return None


def _effective_grid(out_dir):
    """The grid the run ACTUALLY used. bins_per_row is capped at num_rows at run time (Setup.cpp,
    matching XPlace database.py:161), so a requested 512 on a low-row mgc_* design really ran at
    128/256 (TODO #31) and every number in the row was measured there. gp_only.csv only echoes back
    what DSE_info requested, so read it from the run dir, same as _our_exact_overflow."""
    if not out_dir:
        return None
    for name, pattern in (("run_summary.md", r"bins_per_row\s*\|\s*(\d+)"),
                          ("run.log", r"effective bins_per_row=(\d+)")):
        path = os.path.join(out_dir, name)
        if os.path.exists(path):
            with open(path) as f:
                m = re.search(pattern, f.read())
            if m:
                return m.group(1)
    return None


def summarize(sweep_dir, quiet=False):
    """(Re)write dse_results.csv from the exe's gp_only.csv, adding every XPlace-reference comparison
    from tools/benchmarks.py: GP Ratio (masked → xplace_gp_masked_in_sw_frame) and, once legalized,
    the LG/DP columns (DP HPWL is already in XPlace's frame). Prints the aggregate ratios only — the
    per-row table IS dse_results.csv, not re-dumped here. Idempotent.

    CLAUDE CODE: quiet=True writes the file and reports nothing. run_all() calls it once per
    iteration so a sweep killed partway still leaves a complete dse_results.csv for everything that
    finished; 28 copies of the aggregate report would bury the per-run progress lines."""
    import csv
    say = (lambda *a, **k: None) if quiet else print
    rows = read_gp(sweep_dir)
    if not rows:
        say(f"  (no gp_only.csv in {sweep_dir} — every run failed?)")
        return

    col_keys, by_ident = [], {}
    if os.path.exists(os.path.join(sweep_dir, "sweep.json")):
        with open(os.path.join(sweep_dir, "sweep.json")) as f:
            sweep = json.load(f)
        col_keys = sweep.get("col_keys", [])
        by_ident = {entry_ident(e, col_keys): e for e in sweep["runs"]}
    lgdp_store = {}
    if os.path.exists(os.path.join(sweep_dir, "lgdp.json")):
        with open(os.path.join(sweep_dir, "lgdp.json")) as f:
            lgdp_store = json.load(f)
    have_dp = bool(lgdp_store)

    for missing in [c for c in RESULT_COLS if c not in rows[0]]:
        say(f"  [warn] gp_only.csv has no column '{missing}' — Output.cpp schema changed; "
            f"update RESULT_COLS in tools/dse.py")

    fenced, unknown_grid = [], []
    for row in rows:
        path = f"{row.get('Suite', '')}/{row.get('Design', '')}"
        xgp = benchmarks.xplace_gp_masked_in_sw_frame(path)
        best_gp = float(row["Best GP HPWL"]) if _is_float(row.get("Best GP HPWL")) else None
        row["XPlace GP HPWL"] = f"{xgp:.4e}" if xgp else "N/A"
        row["GP Ratio"] = f"{best_gp / xgp:.4f}" if (best_gp and xgp) else "N/A"

        exact = _our_exact_overflow(row.get("Output Dir"))
        row[EXACT_COL] = f"{float(exact):.4f}" if _is_float(exact) else ""
        r = lgdp_store.get(by_ident.get(row_ident(row, col_keys), {}).get("label"), {})
        dp, xdp = r.get("dp"), benchmarks.BENCHMARKS.get(path, {}).get("xplace_dp_hpwl")
        row[OVFL_COL] = f"{float(r['in_ovfl']):.4f}" if _is_float(r.get("in_ovfl")) else ""
        row["Our LG HPWL"] = r.get("lg") or ""
        row["Our DP HPWL"] = dp or ""
        row["XPlace DP HPWL"] = f"{xdp:.4e}" if xdp else "N/A"
        row["DP Ratio"] = (f"{float(dp) / xdp:.4f}" if (dp and xdp)
                           else ("no-ref" if dp else r.get("status", "")))
        if r.get("variant") == "ispd2015_fix":
            fenced.append(row["Design"])

        # `grid` reports the EFFECTIVE grid, never the requested one -- a row whose numbers came
        # from a capped 128 must not read 512. Rewritten HERE, after both identity joins above,
        # which match on the requested value that gp_only.csv and sweep.json share. Consequence:
        # two runs of a --grid sweep whose requests cap to the same value show identical rows,
        # because they ARE the same placement.
        effective = _effective_grid(row.get("Output Dir"))
        if effective:
            row["grid"] = effective
        elif "grid" in row:
            unknown_grid.append(row.get("Design", "?"))

    rows.sort(key=lambda r: (r.get("Suite", ""), r.get("Design", ""),
                             tuple(r.get(k, "") for k in col_keys)))

    # dse_results.csv is dse.py's alone (the exe only appends to gp_only.csv), rewritten whole
    # each time, with the enriched columns slotted in after "Best GP HPWL" — next to the number
    # they compare.
    base = [c for c in rows[0] if c not in ENRICHED and c not in (EXACT_COL, OVFL_COL)]
    enriched = ENRICHED if have_dp else ENRICHED[:2]   # GP ratio always; DP cols only if legalized
    at = base.index("Best GP HPWL") + 1 if "Best GP HPWL" in base else len(base)
    fields = base[:at] + enriched + base[at:]
    # The two exact-overflow columns sit beside our smoothed "Best OVFW", in the order
    # smoothed | our-exact | XPlace-on-ours. Our exact is our own measurement (always present);
    # XPlace's comes from the LG run (only once legalized). Inserted after `enriched` (which lands
    # later in the row) so "Best OVFW"'s index is unshifted, and OVFL before EXACT so the final
    # left-to-right order is EXACT then OVFL.
    if "Best OVFW" in fields:
        if have_dp:
            fields.insert(fields.index("Best OVFW") + 1, OVFL_COL)
        fields.insert(fields.index("Best OVFW") + 1, EXACT_COL)
    with open(os.path.join(sweep_dir, RESULTS_CSV), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)

    def ratios(name):
        return sorted(float(r[name]) for r in rows if _is_float(r.get(name)))
    gp, dp = ratios("GP Ratio"), ratios("DP Ratio")
    if gp:
        say(f"  GP Ratio vs XPlace GP (masked): median {gp[len(gp)//2]:.4f} mean {sum(gp)/len(gp):.4f}"
            f"  ({len(gp)}/{len(rows)} designs; meaningful only where Best OVFW converged, TODO #3)")
    if dp:
        say(f"  DP Ratio vs XPlace post-DP:     median {dp[len(dp)//2]:.4f} mean {sum(dp)/len(dp):.4f}"
            f"  ({sum(abs(x-1)<=.02 for x in dp)}/{len(dp)} within 2%, better on {sum(x<1 for x in dp)}"
            f" — legal-vs-legal, the headline)")
    if unknown_grid:
        say(f"  [warn] effective grid unreadable for {len(unknown_grid)} run(s) -- showing the "
            "REQUESTED grid, which the row cap may have lowered: "
            + ", ".join(sorted(unknown_grid)))
    if fenced:
        say(f"  fence-stripped both sides ({len(fenced)}, TODO #26): {', '.join(sorted(fenced))}")
    unscored = [f"{r['Design']}({r['DP Ratio']})" for r in rows
                if have_dp and not _is_float(r["DP Ratio"]) and r["DP Ratio"] != "no-ref"]
    if unscored:
        say(f"  no post-DP ({len(unscored)}): {', '.join(sorted(unscored))}")


# =============================================================================

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog="\n".join(__doc__.split("\n")[1:]))
    ap.add_argument("--designs", default="tier1+tier2",
                    help="all | tier1/ispd2005 | tier2/ispd2015 | tier3/mms | comma/plus list of "
                         "designs (default: the 28-design suite)")
    ap.add_argument("--set", action="append", default=[], metavar="KEY=V[,V...]",
                    help="config override; comma-separated values sweep it, repeatable (Cartesian product)")
    ap.add_argument("--grid", default="xplace", metavar="SPEC",
                    help="xplace (per-design, from benchmarks.py) | auto (ePlace formula) | 512,256")
    ap.add_argument("--seed", type=int, default=42, help="params.random_seed (default 42)")
    ap.add_argument("--gp-only", action="store_true",
                    help="stop after global placement; skip the XPlace LG+DP legal-vs-legal score")
    ap.add_argument("--runset", metavar="JSON", help="run list from morris.py / sobol.py")
    ap.add_argument("--resume", metavar="DSE_DIR",
                    help="re-enter a sweep dir and run only what its gp_only.csv is missing")
    ap.add_argument("--dry-run", action="store_true", help="list the runs and exit")
    args = ap.parse_args()

    if args.resume:
        sweep_dir = args.resume.rstrip("/")
        with open(os.path.join(sweep_dir, "sweep.json")) as f:
            sweep = json.load(f)
        manifest, col_keys = sweep["runs"], sweep.get("col_keys", [])
        # The manifest is the authority on resume, so --designs/--set cannot silently redefine a
        # run mid-sweep. A run is done when its identity (benchmark + column values) is in the CSV.
        done = {row_ident(r, col_keys) for r in read_gp(sweep_dir)}
        entries = [e for e in manifest if entry_ident(e, col_keys) not in done]
        print(f"DSE: resuming {sweep_dir} — {len(manifest) - len(entries)} of {len(manifest)} "
              f"already recorded, {len(entries)} to go")
    else:
        runs, col_keys = build_runs(args)
        if not runs:
            raise SystemExit("DSE: nothing to run")
        sweep_dir = "results/DSE_" + datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        if args.dry_run:
            for n, (label, overrides, columns) in enumerate(runs, 1):
                print(f"  {n:3d}  {label:<40}  "
                      f"{'  '.join(f'{k}={v}' for k, v in overrides.items())}")
            print(f"\nDSE: {len(runs)} run(s); --dry-run, nothing launched")
            return
        entries = prepare(runs, col_keys, sweep_dir)
        print(f"DSE: {len(entries)} run(s) -> {sweep_dir}/")

    # Empty entries + not gp_only is a fully-GP'd resume: still fall through so run_all can
    # legalize any GP runs whose LG did not finish.
    if not entries and args.gp_only:
        print("DSE: nothing left to run")
        return

    failed, elapsed = run_all(entries, len(entries), sweep_dir, args.gp_only, col_keys)
    if entries:
        print(f"\n{'=' * 78}\n  DSE COMPLETE — {len(entries) - failed}/{len(entries)} succeeded, "
              f"{failed} failed  ({elapsed / 60:.1f} min)\n{'=' * 78}\n")
    summarize(sweep_dir)
    print(f"\n  {sweep_dir}/  (dse_results.csv = the table, sweep.json = what ran"
          f"{'' if args.gp_only else ', lgdp.json = LG+DP'})\n")


if __name__ == "__main__":
    main()
