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
              own per-design tuned values). Accepts "all", "tier1".."tier3", "+"-joined
              (tier1+tier2), or comma-separated design names / "suite/design" paths.
  --set K=V   a config override. Comma-separated values sweep it; several --set flags take
              the Cartesian product. K may be dotted ("output.dump_positions=true") to pick
              the TOML section; a bare key means [params], and "benchmark" means [input].
  --runset    a JSON list of run dicts, {"label":..., "benchmark":..., <overrides>}. This is
              what tools/morris.py and tools/sobol.py emit.

By default each GP run is then legalized + detailed-placed through XPlace's OWN LG+DP, so the
headline is legal-vs-legal (post-DP HPWL, the metric the XPlace paper reports — a GP-vs-GP number
flatters whichever placer spread less, TODO #3). Pass --gp-only to stop after GP. LG+DP needs
XPlace's environment (CUDA + its conda python); see tools/lgdp.py.

What a sweep leaves behind, in results/DSE_<timestamp>/:
  sweep.json    the manifest — every run's label, config path and exact overrides. THIS is
                the record of what was launched; read it, not the configs, to see a sweep.
  configs/      one TOML per run, exactly as the exe read it
  logs/         one stdout/stderr capture per run
  gp_only.csv   the exe's own raw GP record, one appended row per run (schema owned by Output.cpp)
  results.csv   dse.py's product: gp_only.csv's columns with every XPlace comparison slotted in
                after Best GP HPWL (XPlace GP HPWL + GP Ratio, and the LG/DP columns) — all looked
                up from tools/benchmarks.py, the one manifest. Rewritten from gp_only.csv each time.
  lgdp.json     per-run LG+DP result (unless --gp-only); XPlace logs under lgdp/<label>/
  results.md    the collated table, same one printed at the end

Two CSVs, two owners: the exe only ever appends to gp_only.csv, dse.py only ever writes results.csv,
so neither clashes with the other's schema across a --resume. A run's identity is (Suite, Design,
grid, swept-param values) — every one a visible column. dse.py emits `grid` + each swept parameter
into the exe's DSE_info, so the columns ARE the identity; sweep.json and gp_only.csv rows join on
that tuple, and --resume skips a run whose identity is already in gp_only.csv. No run-label column.

SEQUENTIAL on purpose: the placer is OpenMP-threaded across all cores, and concurrent runs
were measured (9bea10e, 2026-07-31) to give the same total wall clock with more ways to go
wrong. Designs run smallest-first so a bad config fails in seconds, not after bigblue4.
"""

import argparse
import copy
import datetime
import glob
import itertools
import json
import os
import subprocess
import time
from collections import OrderedDict

import tomlkit

import benchmarks  # master manifest: design list, XPlace grid + target_density
import lgdp        # per-design legalization + detailed placement via XPlace (TODO #30)

EXE_PATH = "build/hw/host/sw_only/aieplace_sw_only.exe"
TEMPLATE_PATH = "host/src/sw_only/default_config.toml"
BENCH_ROOT = "host/benchmarks"

# Raw exe columns summarize depends on. Checked as a POSITIVE list: a name missing from
# results.csv warns loudly, rather than the table silently blanking as it did when Output.cpp
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


def expand_designs(spec):
    """'tier1+tier2' / 'all' / 'adaptec1,ispd2015/mgc_fft_a' -> ['suite/design', ...]."""
    tiers = {"tier1": 1, "tier2": 2, "tier3": 3}
    paths = []
    for token in spec.replace("+", ",").split(","):
        token = token.strip()
        if not token:
            continue
        if token == "all":
            paths += benchmarks.all_paths()
        elif token in tiers:
            paths += benchmarks.by_tier(tiers[token])
        else:
            paths.append(benchmarks.resolve(token))   # rejects typos at launch
    return list(dict.fromkeys(paths))                 # dedupe, order preserved


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
    dict emitted into results.csv (via DSE_info) and forming each run's identity. col_keys is that
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
    swept = [k for k, v in sets.items() if len(v) > 1]   # only these get a column / label suffix
    col_keys = ["grid"] + [k.rsplit(".", 1)[-1] for k in swept]
    designs = sorted(expand_designs(args.designs), key=design_bytes)

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

    # The exe drops the first two DSE_info lines and turns the rest into results.csv columns
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
# entry or a results.csv row, so the two join without an opaque label column.
def entry_ident(entry, col_keys):
    return (entry["overrides"]["benchmark"], tuple(str(entry["columns"][k]) for k in col_keys))

def row_ident(row, col_keys):
    return (f"{row.get('Suite', '')}/{row.get('Design', '')}",
            tuple(str(row.get(k, "")) for k in col_keys))


def read_gp(sweep_dir):
    """The exe's raw per-run GP records (gp_only.csv). dse.py enriches these into results.csv but
    never writes gp_only.csv, so the exe can keep appending to it across --resume without a schema
    clash."""
    import csv
    path = os.path.join(sweep_dir, "gp_only.csv")
    if not os.path.exists(path):
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def gp_output_dir(sweep_dir, want_ident, col_keys):
    """The run dir the exe wrote for a run, matched by identity to results.csv's Output Dir."""
    for row in read_gp(sweep_dir):
        if row_ident(row, col_keys) == want_ident:
            return row.get("Output Dir")
    return None


def legalize_run(sweep_dir, entry, col_keys, store):
    """Legalize one completed GP run and record it in `store` (persisted to lgdp.json)."""
    label = entry["label"]
    out_dir = gp_output_dir(sweep_dir, entry_ident(entry, col_keys), col_keys)
    # The DEF's basename varies by tier (RowBasedPlacement.def, fft.def, ...), so glob it rather
    # than hardcode a name — same reason run_lgdp44.sh's newest_def() does.
    defs = glob.glob(os.path.join(out_dir, "*.def")) if out_dir else []
    if not defs:
        store[label] = {"status": "fail_no_gp_def"}
    else:
        store[label] = lgdp.legalize(entry["overrides"]["benchmark"], defs[0],
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
                legalize_run(sweep_dir, entry, col_keys, lgdp_store)

    t0 = time.time()
    failed = 0
    for done, entry in enumerate(entries, 1):
        t_run = time.time()
        with open(entry["log"], "w") as log:
            result = subprocess.run([EXE_PATH, entry["config"]], stdout=log, stderr=log)
        ok = result.returncode == 0
        failed += not ok
        print(f"DSE: [{done}/{total}] {'DONE' if ok else 'FAIL'} "
              f"{time.time() - t_run:7.1f}s  {entry['label']}"
              f"{'' if ok else '  -> ' + entry['log']}", flush=True)
        if ok and not gp_only:
            legalize_run(sweep_dir, entry, col_keys, lgdp_store)
    return failed, time.time() - t0


# =============================================================================
# Summary
# =============================================================================

ENRICHED = ["XPlace GP HPWL", "GP Ratio", "Our LG HPWL", "Our DP HPWL", "XPlace DP HPWL", "DP Ratio"]


def summarize(sweep_dir):
    """Read the exe's gp_only.csv and write results.csv, adding every XPlace-reference comparison
    (GP + DP, all from tools/benchmarks.py). The GP number is masked and raw-DBU, so its reference
    goes through xplace_gp_masked_in_sw_frame(); the DP number comes from XPlace's own log so it
    needs no conversion (lgdp.py's frame note). Idempotent — safe to re-run."""
    import csv
    rows = read_gp(sweep_dir)
    if not rows:
        print(f"  (no gp_only.csv in {sweep_dir} — every run failed?)")
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
        print(f"  [warn] results.csv has no column '{missing}' — Output.cpp schema changed; "
              f"update RESULT_COLS in tools/dse.py")

    fenced = []
    for row in rows:
        path = f"{row.get('Suite', '')}/{row.get('Design', '')}"
        xgp = benchmarks.xplace_gp_masked_in_sw_frame(path)
        best_gp = float(row["Best GP HPWL"]) if _is_float(row.get("Best GP HPWL")) else None
        row["XPlace GP HPWL"] = f"{xgp:.4e}" if xgp else "N/A"
        row["GP Ratio"] = f"{best_gp / xgp:.4f}" if (best_gp and xgp) else "N/A"

        r = lgdp_store.get(by_ident.get(row_ident(row, col_keys), {}).get("label"), {})
        dp, lg = r.get("dp"), r.get("lg")
        xdp = benchmarks.BENCHMARKS.get(path, {}).get("xplace_dp_hpwl")
        row["Our LG HPWL"] = lg or ""
        row["Our DP HPWL"] = dp or ""
        row["XPlace DP HPWL"] = f"{xdp:.4e}" if xdp else "N/A"
        row["DP Ratio"] = (f"{float(dp) / xdp:.4f}" if (dp and xdp)
                           else ("no-ref" if dp else r.get("status", "")))
        if r.get("variant") == "ispd2015_fix":
            fenced.append(row["Design"])

    rows.sort(key=lambda r: (r.get("Suite", ""), r.get("Design", ""),
                             tuple(r.get(k, "") for k in col_keys)))

    # Write results.csv from gp_only.csv's columns, with the enriched columns slotted in right after
    # "Best GP HPWL" so every XPlace comparison sits next to the number it compares. Safe to insert
    # mid-row: results.csv is dse.py's alone (the exe only appends to gp_only.csv), rewritten whole
    # each time.
    base = [c for c in rows[0] if c not in ENRICHED]
    enriched = ENRICHED if have_dp else ENRICHED[:2]   # GP ratio always; DP cols only if legalized
    at = base.index("Best GP HPWL") + 1 if "Best GP HPWL" in base else len(base)
    fields = base[:at] + enriched + base[at:]
    with open(os.path.join(sweep_dir, "results.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)

    # Curated view for the console + results.md (the CSV holds the full column set).
    view = (["Suite", "Design"] + col_keys +
            ["Best Iter", "Best OVFW", "Best GP HPWL", "XPlace GP HPWL", "GP Ratio",
             "Final HPWL Exact"] +
            (["Our DP HPWL", "XPlace DP HPWL", "DP Ratio"] if have_dp else []))
    width = {c: max(len(c), max(len(str(r.get(c, ""))) for r in rows)) for c in view}
    table = ["  ".join(f"{c:<{width[c]}}" for c in view),
             "  ".join("-" * width[c] for c in view)]
    table += ["  ".join(f"{str(r.get(c, '')):<{width[c]}}" for c in view) for r in rows]

    def col_floats(name):
        return [float(r[name]) for r in rows if _is_float(r.get(name))]

    footer = []
    gp = col_floats("GP Ratio")
    if gp:
        gp.sort()
        footer.append(f"GP Ratio vs XPlace GP (masked): median {gp[len(gp) // 2]:.4f}, "
                      f"mean {sum(gp) / len(gp):.4f}, over {len(gp)}/{len(rows)} designs. "
                      f"Meaningful only where Best OVFW converged (TODO #3).")
    dp = col_floats("DP Ratio")
    if dp:
        dp.sort()
        footer.append(f"DP Ratio vs XPlace post-DP: median {dp[len(dp) // 2]:.4f}, "
                      f"mean {sum(dp) / len(dp):.4f}, {sum(abs(x - 1) <= 0.02 for x in dp)}/{len(dp)} "
                      f"within 2%, better on {sum(x < 1 for x in dp)} (legal-vs-legal, the headline).")
    if fenced:
        footer.append(f"Fence regions STRIPPED on both sides ({len(fenced)}, TODO #26): "
                      + ", ".join(sorted(fenced)))
    unscored = [f"{r['Design']} ({r['DP Ratio']})" for r in rows
                if have_dp and not _is_float(r["DP Ratio"]) and r["DP Ratio"] != "no-ref"]
    if unscored:
        footer.append(f"No post-DP ({len(unscored)}): " + ", ".join(sorted(unscored)))

    print("\n".join("  " + ln for ln in table + [""] + footer))

    md = [f"# DSE sweep — {os.path.basename(sweep_dir)}", "", f"{len(rows)} runs.", "",
          "| " + " | ".join(view) + " |", "|" + "|".join("---" for _ in view) + "|"]
    md += ["| " + " | ".join(str(r.get(c, "")) for c in view) + " |" for r in rows]
    md += [""] + [f"- {line}" for line in footer]
    with open(os.path.join(sweep_dir, "results.md"), "w") as f:
        f.write("\n".join(md) + "\n")


# =============================================================================

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog="\n".join(__doc__.split("\n")[1:]))
    ap.add_argument("--designs", default="tier1+tier2",
                    help="all | tier1..tier3 | comma/plus list of designs (default: the 28-design suite)")
    ap.add_argument("--set", action="append", default=[], metavar="KEY=V[,V...]",
                    help="config override; comma-separated values sweep it, repeatable (Cartesian product)")
    ap.add_argument("--grid", default="xplace", metavar="SPEC",
                    help="xplace (per-design, from benchmarks.py) | auto (ePlace formula) | 512,256")
    ap.add_argument("--seed", type=int, default=42, help="params.random_seed (default 42)")
    ap.add_argument("--gp-only", action="store_true",
                    help="stop after global placement; skip the XPlace LG+DP legal-vs-legal score")
    ap.add_argument("--runset", metavar="JSON", help="run list from morris.py / sobol.py")
    ap.add_argument("--resume", metavar="DSE_DIR",
                    help="re-enter a sweep dir and run only what its results.csv is missing")
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
    print(f"\n  {sweep_dir}/  (sweep.json = what ran, results.md = this table"
          f"{'' if args.gp_only else ', lgdp.json = LG+DP'})\n")


if __name__ == "__main__":
    main()
