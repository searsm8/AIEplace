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

What a sweep leaves behind, in results/DSE_<timestamp>/:
  sweep.json    the manifest — every run's label, config path and exact overrides. THIS is
                the record of what was launched; read it, not the configs, to see a sweep.
  configs/      one TOML per run, exactly as the exe read it
  logs/         one stdout/stderr capture per run
  results.csv   appended by the exe, one row per run (schema owned by Output.cpp)
  results.md    the collated table, same one printed at the end

The only thing dse.py writes into a run's DSE_info is `run=<label>`, so results.csv has one
fixed schema forever and swept values are joined back from sweep.json at summary time.

SEQUENTIAL on purpose: the placer is OpenMP-threaded across all cores, and concurrent runs
were measured (9bea10e, 2026-07-31) to give the same total wall clock with more ways to go
wrong. Designs run smallest-first so a bad config fails in seconds, not after bigblue4.
"""

import argparse
import copy
import datetime
import itertools
import json
import os
import subprocess
import time
from collections import OrderedDict

import tomlkit

import benchmarks  # master manifest: design list, XPlace grid + target_density

EXE_PATH = "build/hw/host/sw_only/aieplace_sw_only.exe"
TEMPLATE_PATH = "host/src/sw_only/default_config.toml"
BENCH_ROOT = "host/benchmarks"

# Result columns rendered in the summary, as a POSITIVE list. A denylist here is what let the
# table silently blank out when Output.cpp renamed its columns (TODO #28); a name that is not
# in results.csv now warns loudly instead.
RESULT_COLS = ["Iters", "Best Iter", "Best OVFW", "Best GP HPWL", "XPlace GP HPWL", "Ratio",
               "Final HPWL Exact", "Total Runtime (sec)"]
PHASE1_COLS = ["Phase1 Iters", "Phase1 HPWL", "Phase1 OVFW Exact", "Phase1 Stop Reason"]


# =============================================================================
# Building the run list
# =============================================================================

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
    """-> [(label, {param: value})], one entry per run. Labels are unique within a sweep."""
    if args.runset:
        with open(args.runset) as f:
            specs = json.load(f)
        runs = []
        for i, spec in enumerate(specs):
            spec = dict(spec)
            label = str(spec.pop("label", f"r{i:04d}"))
            spec["benchmark"] = benchmarks.resolve(spec["benchmark"])
            runs.append((label, spec))
        return runs

    sets = parse_sets(args.set)
    swept = [k for k, v in sets.items() if len(v) > 1]   # only these earn a place in the label
    designs = sorted(expand_designs(args.designs), key=design_bytes)

    runs = []
    for path in designs:
        meta = benchmarks.BENCHMARKS[path]
        grids = grid_values(args.grid, meta)
        for grid in grids:
            for combo in itertools.product(*sets.values()):
                overrides = {"benchmark": path,
                             "maximum_utilization": meta["target_density"],
                             "random_seed": args.seed}
                if grid is not None:
                    overrides["bins_per_row"] = grid
                overrides.update(zip(sets.keys(), combo))

                label = f"{meta['name']}@{grid if grid else 'auto'}"
                label += "".join(f"_{k.rsplit('.', 1)[-1]}={overrides[k]}" for k in swept)
                runs.append((label, overrides))
    return runs


# =============================================================================
# Writing configs
# =============================================================================

def section_for(key):
    """Which TOML table an override lands in. Dotted key wins; 'benchmark' is [input]."""
    if "." in key:
        return key.rsplit(".", 1)
    return ("input" if key == "benchmark" else "params"), key


def write_config(template, overrides, path, label, run_num, total):
    config = copy.deepcopy(template)
    for key, value in overrides.items():
        section, param = section_for(key)
        if section not in config:
            config[section] = tomlkit.table()
        config[section][param] = f"{BENCH_ROOT}/{value}" if param == "benchmark" else value

    # The exe drops the first two DSE_info lines and turns the rest into results.csv columns
    # (Output.cpp::parseDSEParams), so this emits exactly one extra column: `run`.
    config["output"]["DSE_info"] = (f"DSE sweep progress={run_num} of {total}\n"
                                    f"benchmark={overrides['benchmark']}\n"
                                    f"run={label}")
    with open(path, "w") as f:
        tomlkit.dump(config, f)


def prepare(runs, sweep_dir):
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
    for n, (label, overrides) in enumerate(runs, 1):
        config_path = os.path.join(sweep_dir, "configs", f"run_{n:03d}.toml")
        write_config(template, overrides, config_path, label, n, len(runs))
        manifest.append({"n": n, "label": label, "config": config_path,
                         "log": os.path.join(sweep_dir, "logs", f"run_{n:03d}.log"),
                         "overrides": overrides})
    with open(os.path.join(sweep_dir, "sweep.json"), "w") as f:
        json.dump({"created": datetime.datetime.now().isoformat(timespec="seconds"),
                   "exe": EXE_PATH, "runs": manifest}, f, indent=2)
    return manifest


# =============================================================================
# Running
# =============================================================================

def run_all(entries, total):
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
    return failed, time.time() - t0


# =============================================================================
# Summary
# =============================================================================

def summarize(sweep_dir):
    """Print and write results.md: sweep.json's swept params joined onto the exe's results.csv."""
    csv_path = os.path.join(sweep_dir, "results.csv")
    if not os.path.exists(csv_path):
        print(f"  (no {csv_path} — every run failed?)")
        return
    import csv
    with open(csv_path, newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        return

    # Which overrides actually varied across the sweep -> those are the sweep's parameters.
    # Taken from sweep.json, never inferred from the CSV header: inferring is what made result
    # columns show up as "swept parameters" and blanked the HPWL column (TODO #28).
    swept, by_label = [], {}
    manifest_path = os.path.join(sweep_dir, "sweep.json")
    if os.path.exists(manifest_path):
        with open(manifest_path) as f:
            entries = json.load(f)["runs"]
        by_label = {e["label"]: e["overrides"] for e in entries}
        keys = {k for e in entries for k in e["overrides"]}
        swept = sorted(k for k in keys - {"benchmark"}
                       if len({str(e["overrides"].get(k)) for e in entries}) > 1)

    present = [c for c in RESULT_COLS if c in rows[0]]
    for missing in [c for c in RESULT_COLS if c not in rows[0]]:
        print(f"  [warn] results.csv has no column '{missing}' — Output.cpp schema changed; "
              f"update RESULT_COLS in tools/dse.py")
    if any(r.get("Phase1 Iters", "N/A") not in ("N/A", "", None) for r in rows):
        present += [c for c in PHASE1_COLS if c in rows[0]]

    cols = ["Design"] + (["run"] if "run" in rows[0] else []) + swept + present
    rows.sort(key=lambda r: (r.get("Design", ""), r.get("run", "")))

    def cell(row, col):
        if col in swept:
            return str(by_label.get(row.get("run", ""), {}).get(col, ""))
        return str(row.get(col, ""))

    width = {c: max(len(c), max(len(cell(r, c)) for r in rows)) for c in cols}
    lines = ["  ".join(f"{c:<{width[c]}}" for c in cols)]
    lines.append("  ".join("-" * width[c] for c in cols))
    lines += ["  ".join(f"{cell(r, c):<{width[c]}}" for c in cols) for r in rows]

    def numbers(col):
        return [float(r[col]) for r in rows if r.get(col) not in (None, "", "N/A")]

    footer = []
    hpwl, ratios = numbers("Best GP HPWL"), numbers("Ratio")
    if hpwl:
        footer.append(f"Best GP HPWL: {min(hpwl):.3e} - {max(hpwl):.3e} "
                      f"({max(hpwl) / min(hpwl):.2f}x spread)")
    if ratios:
        footer.append(f"Ratio vs XPlace GP: {min(ratios):.3f} - {max(ratios):.3f}, "
                      f"mean {sum(ratios) / len(ratios):.3f} over {len(ratios)}/{len(rows)} designs")
    footer.append("Ratio is only meaningful where Best OVFW converged — an under-spread run "
                  "flatters its own HPWL (TODO #3).")

    print("\n".join("  " + ln for ln in lines + [""] + footer))

    md = [f"# DSE sweep — {os.path.basename(sweep_dir)}", "",
          f"{len(rows)} runs.  Swept: {', '.join(swept) if swept else '(designs only)'}", "",
          "| " + " | ".join(cols) + " |",
          "|" + "|".join("---" for _ in cols) + "|"]
    md += ["| " + " | ".join(cell(r, c) for c in cols) + " |" for r in rows]
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
    ap.add_argument("--runset", metavar="JSON", help="run list from morris.py / sobol.py")
    ap.add_argument("--resume", metavar="DSE_DIR",
                    help="re-enter a sweep dir and run only what its results.csv is missing")
    ap.add_argument("--dry-run", action="store_true", help="list the runs and exit")
    args = ap.parse_args()

    if args.resume:
        sweep_dir = args.resume.rstrip("/")
        with open(os.path.join(sweep_dir, "sweep.json")) as f:
            manifest = json.load(f)["runs"]
        # The manifest is the authority on resume, so --designs/--set cannot silently redefine
        # a run mid-sweep. Identity is the label, and the config it names already exists.
        done = set()
        csv_path = os.path.join(sweep_dir, "results.csv")
        if os.path.exists(csv_path):
            import csv
            with open(csv_path, newline="") as f:
                done = {(r.get("run") or "").strip() for r in csv.DictReader(f)}
        entries = [e for e in manifest if e["label"] not in done]
        print(f"DSE: resuming {sweep_dir} — {len(manifest) - len(entries)} of {len(manifest)} "
              f"already recorded, {len(entries)} to go")
    else:
        runs = build_runs(args)
        if not runs:
            raise SystemExit("DSE: nothing to run")
        sweep_dir = "results/DSE_" + datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        if args.dry_run:
            for n, (label, overrides) in enumerate(runs, 1):
                print(f"  {n:3d}  {label:<40}  "
                      f"{'  '.join(f'{k}={v}' for k, v in overrides.items())}")
            print(f"\nDSE: {len(runs)} run(s); --dry-run, nothing launched")
            return
        entries = prepare(runs, sweep_dir)
        print(f"DSE: {len(entries)} run(s) -> {sweep_dir}/")

    if not entries:
        print("DSE: nothing left to run")
        return

    failed, elapsed = run_all(entries, len(entries))
    print(f"\n{'=' * 78}\n  DSE COMPLETE — {len(entries) - failed}/{len(entries)} succeeded, "
          f"{failed} failed  ({elapsed / 60:.1f} min)\n{'=' * 78}\n")
    summarize(sweep_dir)
    print(f"\n  {sweep_dir}/  (sweep.json = what ran, results.md = this table)\n")


if __name__ == "__main__":
    main()
