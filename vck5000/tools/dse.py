# dse.py
# Design Space Exploration python script
# Runs bin/AIEplace.exe while modifying host/run_config.json between runs

import copy
import datetime
import itertools
import json
import os
import subprocess
import sys
import time
from collections import OrderedDict
from typing import Any, Union, List, Tuple

# Maximum number of parallel AIEplace processes.
# Set to 1 for sequential execution (original behavior).
# A good default 4 or 8 to speed up DSE on a typical multi-core machine without overwhelming it.
MAX_PARALLEL = 8

# Compiled placer binary (current sw_only build) and the base config it reads.
# Both paths are relative to vck5000/, which is where this script must be run from.
EXE_PATH = "build/hw/host/sw_only/aieplace_sw_only.exe"
CONFIG_PATH = "host/src/sw_only/run_config.json"


# =============================================================================
# DSE Sweep Configuration
# =============================================================================
# Each entry maps a parameter name to (section_path, [values_to_sweep]).
# The Cartesian product of all value lists is computed automatically.
# To add a new parameter sweep, add one line. To remove, delete or comment out.
# =============================================================================


dse_sweep = OrderedDict([
    # Full list of benchmarks, ordered largest-first (by node count) so the
    # longest-running jobs start first and smaller ones fill in around them.
    #("benchmark", (["input"], [
        #"ispd2005/bigblue4",            # 2.2M nodes
        #"ispd2015/mgc_superblue12",     # 1.3M nodes
        #"ispd2005/bigblue3",            # 1.1M nodes
        #"ispd2015/mgc_superblue11_a",   #  927K nodes
        #"ispd2015/mgc_superblue16_a",   #  681K nodes
        #"ispd2015/mgc_superblue14",     #  613K nodes
        #"ispd2005/bigblue2",            #  535K nodes
        #"ispd2015/mgc_superblue19",     #  506K nodes
        #"ispd2005/adaptec4",            #  495K nodes
        #"ispd2005/adaptec3",            #  451K nodes
        #"ispd2005/bigblue1",            #  278K nodes
        #"ispd2005/adaptec2",            #  254K nodes
        #"ispd2005/adaptec1",            #  211K nodes
        #"ispd2015/mgc_matrix_mult_1",   #  155K nodes
        #"ispd2015/mgc_matrix_mult_a",   #  150K nodes
        #"ispd2015/mgc_matrix_mult_b",   #  146K nodes
        #"ispd2015/mgc_matrix_mult_c",   #  146K nodes
        #"ispd2015/mgc_matrix_mult_2",   #  155K nodes
        #"ispd2015/mgc_edit_dist_a",     #  127K nodes
        #"ispd2015/mgc_des_perf_1",      #  113K nodes
        #"ispd2015/mgc_des_perf_a",      #  108K nodes
        #"ispd2015/mgc_des_perf_b",      #  113K nodes
        #"ispd2015/mgc_fft_1",           #   32K nodes
        #"ispd2015/mgc_fft_2",           #   32K nodes
        #"ispd2015/mgc_fft_a",           #   31K nodes
        #"ispd2015/mgc_fft_b",           #   31K nodes
        #"ispd2015/mgc_pci_bridge32_a",  #   30K nodes
    #    "ispd2015/mgc_pci_bridge32_b",  #   29K nodes
    #])),

    # (empty) — γ-ref-grid verification runs the per-design grids via explicit_runs below.

    # Uncomment to sweep additional parameters:
    # (<param_name>, ([section_path], [values_to_sweep]))
    ################################################################
    #("density_weight_init_multiplier", (["params"], [8e-6, 8e-5])),
    #("enable_backtracking",     (["params"], [True, False])),
    #("enable_momentum",  (["params"], [True, False])),
    #("enable_preconditioning",  (["params"], [True, False])),
    #("enable_filler",     (["params"], [True, False])),
    #("gamma_schedule",     (["params"], [True, False])),
    #("init_gamma",         (["params"], [1, 2, 4, 8, 16])),  # wa_coeff multiplier (with gamma_bin_scaled)
    #("init_method",        (["params"], ["uniform_box", "random_center"])),

    #("init_method",             (["params"], ["uniform_box", "random_center"])),
    #("init_step_length",        (["params"], [0.001, 0.01, 0.1, 1.0])),
    #("init_spread",              (["params"], [0.5, 0.4, 0.3, 0.25])),
    #("convergence_min_iterations", (["params"], [50, 100, 200])),
    #("density_weight_max_step", (["params"], [1.02, 1.05, 1.1])),
    #("bins_per_row",            (["params"], [64, 128, 256])),
    #("partials_compute_method", (["params"], ["cpu", "simple"])),

])


# =============================================================================
# Explicit one-off runs (in ADDITION to the Cartesian product above)
# =============================================================================
# Each dict is one run: a set of {param: value} overrides applied to the base
# run_config.json. Use this for specific configs the product can't express — e.g.
# per-benchmark grids, or a hand-picked combination. "benchmark" goes in the
# "input" section (and is prefixed with host/benchmarks/); every other key goes in
# "params". Add an optional "label" to name the run in the results table.
#
# Example — each benchmark at its own XPlace grid, in a single sweep:
#   explicit_runs = [
#     {"label": "adaptec1@512", "benchmark": "ispd2005/adaptec1", "bins_per_row": 512},
#     {"label": "adaptec2@1024","benchmark": "ispd2005/adaptec2", "bins_per_row": 1024},
#     {"label": "bigblue4@2048","benchmark": "ispd2005/bigblue4", "bins_per_row": 2048},
#   ]
def _full_suite():
    """Full 28-benchmark snapshot: every design with a known XPlace reference, each at
    its XPlace grid (setup_dataset.py). seed 42, stop smoothed-overflow 0.04. Ordered
    largest-first (by node count) so the long jobs start first (LPT makespan)."""
    grid = {  # design -> (benchmark path, XPlace num_bin)
        "bigblue4":        ("ispd2005/bigblue4",           2048),
        "mgc_superblue12": ("ispd2015/mgc_superblue12",    1024),
        "bigblue3":        ("ispd2005/bigblue3",           2048),
        "mgc_superblue11_a":("ispd2015/mgc_superblue11_a",  512),
        "mgc_superblue16_a":("ispd2015/mgc_superblue16_a",  512),
        "mgc_superblue14": ("ispd2015/mgc_superblue14",     512),
        "bigblue2":        ("ispd2005/bigblue2",           1024),
        "mgc_superblue19": ("ispd2015/mgc_superblue19",     512),
        "adaptec4":        ("ispd2005/adaptec4",           1024),
        "adaptec3":        ("ispd2005/adaptec3",           1024),
        "bigblue1":        ("ispd2005/bigblue1",            512),
        "adaptec2":        ("ispd2005/adaptec2",           1024),
        "adaptec1":        ("ispd2005/adaptec1",            512),
        "mgc_matrix_mult_1":("ispd2015/mgc_matrix_mult_1",  512),
        "mgc_matrix_mult_2":("ispd2015/mgc_matrix_mult_2",  512),
        "mgc_matrix_mult_a":("ispd2015/mgc_matrix_mult_a",  512),
        "mgc_matrix_mult_b":("ispd2015/mgc_matrix_mult_b",  512),
        "mgc_matrix_mult_c":("ispd2015/mgc_matrix_mult_c",  512),
        "mgc_edit_dist_a": ("ispd2015/mgc_edit_dist_a",     512),
        "mgc_des_perf_1":  ("ispd2015/mgc_des_perf_1",      512),
        "mgc_des_perf_b":  ("ispd2015/mgc_des_perf_b",      512),
        "mgc_des_perf_a":  ("ispd2015/mgc_des_perf_a",      512),
        "mgc_fft_1":       ("ispd2015/mgc_fft_1",           512),
        "mgc_fft_2":       ("ispd2015/mgc_fft_2",           512),
        "mgc_fft_a":       ("ispd2015/mgc_fft_a",           512),
        "mgc_fft_b":       ("ispd2015/mgc_fft_b",           512),
        "mgc_pci_bridge32_a":("ispd2015/mgc_pci_bridge32_a",512),
        "mgc_pci_bridge32_b":("ispd2015/mgc_pci_bridge32_b",512),
    }
    return [
        {"label": f"{name}@{n}", "benchmark": path, "bins_per_row": n,
         "convergence_overflow_threshold": 0.04, "random_seed": 42}
        for name, (path, n) in grid.items()
    ]

def _precond_ab():
    """A/B: enable_preconditioning OFF vs ON, each design at its XPlace grid.
    Verifies the BB-clamp removal: ON must converge (was stalling), OFF must
    match its pre-change baseline."""
    grid = {
        "adaptec1": ("ispd2005/adaptec1",  512),
        "adaptec2": ("ispd2005/adaptec2", 1024),
        "bigblue1": ("ispd2005/bigblue1",  512),
    }
    runs = []
    for name, (path, n) in grid.items():
        for precond in (False, True):
            runs.append({
                "label": f"{name}@{n}_{'ON' if precond else 'OFF'}",
                "benchmark": path,
                "bins_per_row": n,
                "enable_preconditioning": precond,
                "convergence_overflow_threshold": 0.04,
                "random_seed": 42,
            })
    return runs

def _precond_on_subset():
    """Preconditioning ON across a representative size-spanning subset, each design at
    its XPlace grid. Pairs against the full-suite OFF baseline (same seed/grid/threshold)
    for a direct A/B. Includes the three adaptec/bigblue designs from _precond_ab plus a
    large superblue and a standard-cell des_perf for coverage."""
    grid = {
        "adaptec1":        ("ispd2005/adaptec1",         512),
        "adaptec2":        ("ispd2005/adaptec2",        1024),
        "bigblue1":        ("ispd2005/bigblue1",         512),
        "mgc_superblue19": ("ispd2015/mgc_superblue19",  512),
        "mgc_des_perf_1":  ("ispd2015/mgc_des_perf_1",   512),
    }
    return [
        {"label": f"{name}@{n}_precondON", "benchmark": path, "bins_per_row": n,
         "enable_preconditioning": True,
         "convergence_overflow_threshold": 0.04, "random_seed": 42}
        for name, (path, n) in grid.items()
    ]

# Run-set selected by the DSE_RUN_SET env var so a follow-up sweep can be queued without
# editing this file between runs. Defaults to the full 28-design suite.
_RUN_SETS = {
    "full_suite": _full_suite,
    "precond_ab": _precond_ab,
    "precond_on": _precond_on_subset,
}
explicit_runs = _RUN_SETS[os.environ.get("DSE_RUN_SET", "full_suite")]()


# =============================================================================
# Utility Functions
# =============================================================================

def strip_json_comments(text: str) -> str:
    """
    Strip // and # comments from JSON text, respecting quoted strings.
    Mirrors the C++ JsonUtils::stripComments() so we can share one config file.
    """
    result = []
    i = 0
    in_string = False
    while i < len(text):
        c = text[i]
        # Handle escape sequences inside strings
        if in_string and c == '\\' and i + 1 < len(text):
            result.append(text[i:i+2])
            i += 2
            continue
        # Toggle string state on unescaped quotes
        if c == '"':
            in_string = not in_string
            result.append(c)
            i += 1
            continue
        # Inside a string, just copy
        if in_string:
            result.append(c)
            i += 1
            continue
        # // comment — skip to end of line
        if c == '/' and i + 1 < len(text) and text[i+1] == '/':
            while i < len(text) and text[i] != '\n':
                i += 1
            continue
        # # comment — skip to end of line
        if c == '#':
            while i < len(text) and text[i] != '\n':
                i += 1
            continue
        result.append(c)
        i += 1
    return ''.join(result)


def load_config(path: str) -> dict:
    """Load a JSON config file, stripping // and # comments and trailing commas."""
    import re
    with open(path, 'r') as f:
        raw = f.read()
    stripped = strip_json_comments(raw)
    # Remove trailing commas before } or ] (invalid JSON but common in hand-edited configs)
    stripped = re.sub(r',\s*([}\]])', r'\1', stripped)
    return json.loads(stripped)

def write_run_config(config: dict, config_path: str, run_num: int, total_runs: int,
                     columns: list, section_for, label=None) -> None:
    """
    Write a run's config with a DSE_info block, then save the file.

    The exe turns DSE_info lines 3+ into results.csv columns, and results.csv is
    append-mode with a single shared header — so EVERY run must emit the same column
    set in the same order or the CSV misaligns. `columns` is that shared set (the union
    of all params that vary across the whole sweep, incl. explicit runs); each run fills
    in its own value (blank if the param isn't present). Line 1 = progress, line 2 =
    benchmark (skipped by the exe; the Design column already carries it). If `label` is
    not None a "run" column is emitted for every run (empty for unlabeled product runs).
    """
    if "output" not in config:
        return

    bench = str(config.get("input", {}).get("benchmark", "")).rsplit('/', 1)[-1]
    parts = [f"DSE sweep progress={run_num} of {total_runs}", f"benchmark={bench}"]
    if label is not None:
        parts.append(f"run={label}")
    for param in columns:
        if param == "benchmark":
            continue  # represented by the Design column, not a data column
        section = config
        for key in section_for(param):
            section = section.get(key, {}) if isinstance(section, dict) else {}
        val = section[param] if isinstance(section, dict) and param in section else ""
        parts.append(f"{param}={val}")

    config["output"]["DSE_info"] = "\n".join(parts)

    with open(config_path, 'w') as f:
        json.dump(config, f, indent=2, ensure_ascii=False)


def modify_config_parameter(
    config_path: str,
    param_name: str,
    new_value: Any,
    output_path: Union[str, None] = None,
    section_path: Union[str, List[str], None] = None,
    quiet: bool = False
) -> None:
    """
    Modify a parameter in a JSON config file at any nested level.

    Args:
        config_path (str): Path to the input config file
        param_name (str): Name of the parameter to modify
        new_value (Any): New value to set for the parameter
        output_path (str, optional): Path to save modified config. If None, overwrites input file.
        section_path (str or List[str], optional): Path to the section containing the parameter.
                                                 Can be a string like "params" or "settings.database"
                                                 or a list like ["settings", "database"].
                                                 If None, defaults to "params" for backward compatibility.
        quiet (bool): If True, suppress print output. Default False.
    """
    try:
        config = load_config(config_path)

        # Handle section path
        if section_path is None:
            section_path = ["params"]
        elif isinstance(section_path, str):
            if section_path == "":
                section_path = []
            else:
                section_path = section_path.split('.')
        elif not isinstance(section_path, list):
            raise ValueError("section_path must be a string, list, or None")

        # Navigate to the target section
        current_section = config
        section_path_str = "root"

        for i, section_name in enumerate(section_path):
            if not isinstance(current_section, dict):
                raise KeyError(f"Cannot navigate to section '{section_name}' - parent is not a dictionary")
            if section_name not in current_section:
                raise KeyError(f"Section '{section_name}' not found in {section_path_str}")
            current_section = current_section[section_name]
            section_path_str = f"{section_path_str}.{section_name}" if section_path_str != "root" else section_name

        if not isinstance(current_section, dict):
            raise KeyError(f"Target section '{section_path_str}' is not a dictionary and cannot contain parameters")

        if param_name not in current_section:
            raise KeyError(f"Parameter '{param_name}' not found in section '{section_path_str}'")

        current_section[param_name] = new_value
        save_path = output_path if output_path else config_path

        with open(save_path, 'w') as f:
            json.dump(config, f, indent=2, ensure_ascii=False)

        if not quiet:
            display_path = ".".join(section_path) if section_path else "root"
            print(f"DSE: Updated '{param_name}' in '{display_path}' to {new_value}")

    except FileNotFoundError:
        print(f"Error: Config file '{config_path}' not found")
        sys.exit(1)
    except json.JSONDecodeError:
        print(f"Error: '{config_path}' is not a valid JSON file")
        sys.exit(1)
    except (KeyError, ValueError) as e:
        print(f"Error: {str(e)}")
        sys.exit(1)


def run_AIEplace(args=None):
    """Run the compiled program using a subprocess."""
    if args is None:
        args = []

    command = [EXE_PATH] + args
    print(f"DSE: Running {command}")

    result = subprocess.run(command)

    if result.returncode != 0:
        print(f"DSE: ERROR — process exited with code {result.returncode}")
        return False
    return True


class ParallelRun:
    """Tracks a single AIEplace subprocess."""
    def __init__(self, run_num, combo_str, config_path):
        self.run_num = run_num
        self.combo_str = combo_str
        self.config_path = config_path
        self.process = None
        self.t0 = None

    def start(self):
        """Launch the subprocess with stdout/stderr discarded."""
        self.t0 = time.time()
        self.process = subprocess.Popen(
            [EXE_PATH, self.config_path],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    def poll(self):
        """Check if process has finished. Returns None if still running."""
        return self.process.poll()

    def finish(self):
        """Clean up and return (run_num, combo_str, success, elapsed)."""
        elapsed = time.time() - self.t0
        success = self.process.returncode == 0
        return (self.run_num, self.combo_str, success, elapsed)


def write_markdown_summary(sweep_dir, rows, dse_cols):
    """
    Write results.md — a GitHub-flavored markdown table of the sweep, sorted by
    design then best HPWL. Persisted alongside results.csv so a human or an LLM can
    read the collated outcome after the run without re-parsing per-run files.
    """
    # Include the XPlace-comparison columns when the exe emitted them (we are chasing
    # XPlace, so ratio-to-XPlace is the headline metric when available).
    optional = [c for c in ("XPlace HPWL", "Ratio") if rows and c in rows[0]]
    cols = ["Design"] + dse_cols + ["Iters", "Best HPWL", "Best OVFW", "Best Iter"] + optional

    lines = [
        f"# DSE sweep results",
        "",
        f"Swept parameters: {', '.join(dse_cols) if dse_cols else '(none)'}  |  {len(rows)} runs",
        "",
        "| " + " | ".join(cols) + " |",
        "| " + " | ".join("---" for _ in cols) + " |",
    ]
    for r in rows:
        lines.append("| " + " | ".join(str(r.get(c, "")) for c in cols) + " |")

    hpwls = [float(r["Best HPWL"]) for r in rows if r.get("Best HPWL") not in (None, "", "N/A")]
    if hpwls:
        lines += ["", f"**HPWL range:** {min(hpwls):.3e} — {max(hpwls):.3e} "
                      f"({max(hpwls)/min(hpwls):.2f}x spread)"]

    md_path = os.path.join(sweep_dir, "results.md")
    with open(md_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"  Markdown summary:   {md_path}")


# =============================================================================
# DSE Main Loop
# =============================================================================

def dse():
    """
    Design Space Exploration — exhaustive sweep over all parameter combinations.

    Computes the Cartesian product of all value lists in dse_sweep and runs
    AIEplace once for each configuration. Runs up to MAX_PARALLEL processes
    concurrently, each with its own config file copy and log file.
    """
    # Load the single config file (strips // and # comments)
    base_config = load_config(CONFIG_PATH)

    # Section lookup for any parameter: sweep dims carry their own section; explicit-run
    # params default to "input" for benchmark and "params" for everything else.
    sweep_sections = {p: dse_sweep[p][0] for p in dse_sweep}
    def section_for(param):
        if param in sweep_sections:
            return sweep_sections[param]
        return ["input"] if param == "benchmark" else ["params"]

    # Build the full run list: Cartesian product of dse_sweep + explicit one-off runs.
    # Each run is (overrides {param: value}, label); product runs are unlabeled (None).
    param_names = list(dse_sweep.keys())
    value_lists = [dse_sweep[p][1] for p in param_names]
    runs = []
    if param_names:
        for combo in itertools.product(*value_lists):
            runs.append((dict(zip(param_names, combo)), None))
    for spec in explicit_runs:
        spec = dict(spec)
        label = str(spec.pop("label", "explicit"))
        runs.append((spec, label))
    total_runs = len(runs)

    if total_runs == 0:
        print("DSE: nothing to run (dse_sweep and explicit_runs are both empty).")
        return

    # Shared column set so the appended results.csv stays aligned: sweep params first
    # (order preserved), then any explicit-only params (first-seen). "run" label column
    # is emitted for all runs when any run is labeled (i.e. when there are explicit runs).
    columns = list(param_names)
    for overrides, _ in runs:
        for p in overrides:
            if p not in columns:
                columns.append(p)
    has_labels = any(lbl is not None for _, lbl in runs)

    n_product = total_runs - len(explicit_runs)
    print(f"DSE: {len(param_names)} swept param(s) -> {n_product} product run(s) "
          f"+ {len(explicit_runs)} explicit run(s) = {total_runs} total")

    parallel = min(MAX_PARALLEL, total_runs)
    print(f"DSE: Running with {parallel} parallel workers")

    # Give every DSE sweep its own subdirectory so runs never collide
    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    sweep_dir = f"results/DSE_{timestamp}"

    # Prepare per-run config files and ParallelRun objects
    pending = []
    config_dir = os.path.join(sweep_dir, "configs")
    os.makedirs(config_dir, exist_ok=True)

    # DSE overrides applied to every run (quiet + headless mode, DSE_info placeholder)
    base_config["output"]["quiet"] = True
    base_config["output"]["visualize"] = False   # sweeps are headless; per-frame render is slow
    base_config.setdefault("output", {})
    base_config["output"]["results_dir"] = sweep_dir
    base_config.setdefault("output", {}).setdefault("DSE_info", "")

    print(f"DSE: Preparing {total_runs} config files...")
    benchmark_path = "host/benchmarks/"
    for run_num, (overrides, label) in enumerate(runs, 1):
        # Deep-copy the base config for this run
        config = copy.deepcopy(base_config)

        # Apply this run's parameter overrides
        for param, value in overrides.items():
            actual_value = benchmark_path + value if param == "benchmark" else value
            target = config
            for key in section_for(param):
                target = target[key]
            target[param] = actual_value

        # Write the clean (comment-free) JSON config for this run
        run_config_path = os.path.join(config_dir, f"run_{run_num:03d}.json")
        write_run_config(config, run_config_path, run_num, total_runs, columns,
                         section_for, label=(label or "") if has_labels else None)

        combo_str = "  ".join(f"{p}={v}" for p, v in overrides.items())
        if label is not None:
            combo_str += f"  [{label}]"
        pending.append(ParallelRun(run_num, combo_str, run_config_path))

    # Launch runs with a simple Popen worker pool
    t0_sweep = time.time()
    active = []     # currently running
    completed = 0
    failed = 0

    print(f"DSE: Launching {total_runs} runs ({parallel} parallel)...\n")

    while pending or active:
        # Fill up to max_parallel active slots
        while pending and len(active) < parallel:
            run = pending.pop(0)
            run.start()
            active.append(run)

        # Poll active processes for completion
        still_active = []
        for run in active:
            if run.poll() is not None:
                run_num, combo_str, success, elapsed = run.finish()
                completed += 1
                status = "DONE" if success else "FAIL"
                if not success:
                    failed += 1
                print(f"DSE: ({(completed/total_runs)*100:4.1f}%) [{completed}/{total_runs}] {status} -- {elapsed:6.1f}s  {combo_str}")
            else:
                still_active.append(run)
        active = still_active

        # Avoid busy-waiting
        if active:
            time.sleep(0.5)

    elapsed_total = time.time() - t0_sweep

    # =========================================================================
    # Summary Banner
    # =========================================================================
    print()
    print("=" * 80)
    print(f"  DSE COMPLETE — {completed - failed}/{total_runs} succeeded, {failed} failed  ({elapsed_total:.1f}s)")
    print("=" * 80)

    # Try to read results CSV for a detailed summary table
    csv_path = os.path.join(sweep_dir, "results.csv")
    if os.path.exists(csv_path):
        import csv
        with open(csv_path) as f:
            reader = csv.DictReader(f)
            rows = sorted(list(reader), key=lambda r: (r.get("Design", ""), r.get("Final HPWL", "")))

        if rows:
            # Detect which DSE parameter columns exist (dynamic — works for any sweep)
            fixed_cols = {"Design", "Iters", "Final HPWL", "Best Iter", "Best HPWL", "Best OVFW",
                          "XPlace HPWL", "Ratio",
                          "Gamma", "Net Count", "Node Count", "HPWL_Graph", "Combined_Graph",
                          "Placement_GIF", "Total Runtime (sec)", "DB IO Time (sec)",
                          "Algorithm Time (sec)", "Iteration Avg (sec)", "Partials AIE Time (sec)",
                          "Memory Usage (MB)", "Output Dir", "Timestamp"}
            dse_cols = [k for k in rows[0].keys() if k and k not in fixed_cols]
            # XPlace-comparison columns (emitted by the exe when a reference HPWL is known);
            # shown last since ratio-to-XPlace is the headline metric while chasing XPlace.
            xplace_cols = [c for c in ("XPlace HPWL", "Ratio") if c in rows[0]]
            table_cols = ["Design"] + dse_cols + ["Iters", "Best HPWL", "Best Iter", "Best OVFW"] + xplace_cols

            # Build format string dynamically
            col_widths = {"Design": 22, "Iters": 5, "Best HPWL": 10, "Best Iter": 8,
                          "Best OVFW": 8}
            for dc in dse_cols + xplace_cols:
                col_widths[dc] = max(len(dc), max(len(str(r.get(dc, ""))) for r in rows)) + 1

            # Header
            header_parts = []
            for col in table_cols:
                w = col_widths.get(col, 12)
                header_parts.append(f"{col:<{w}}")
            header_line = "  ".join(header_parts)
            print()
            print(f"  {header_line}")
            print(f"  {'-' * len(header_line)}")

            # Data rows
            for r in rows:
                parts = []
                for col in table_cols:
                    w = col_widths.get(col, 12)
                    val = r.get(col, "")
                    parts.append(f"{val:<{w}}")
                print(f"  {'  '.join(parts)}")

            # Quick stats
            hpwls = [float(r["Best HPWL"]) for r in rows if r.get("Best HPWL") not in (None, "", "N/A")]
            ovfws = [float(r["Best OVFW"]) for r in rows if r.get("Best OVFW") not in (None, "", "N/A")]
            if hpwls:
                print()
                print(f"  HPWL range:     {min(hpwls):.3e} — {max(hpwls):.3e}  ({max(hpwls)/min(hpwls):.1f}x spread)")
            if ovfws:
                print(f"  Overflow range:  {min(ovfws):.3f} — {max(ovfws):.3f}")

            # Persist the same collated data as markdown for later human/LLM review.
            write_markdown_summary(sweep_dir, rows, dse_cols)
    else:
        print(f"\n  (No results.csv found — all runs may have failed)")

    print()
    print(f"  Results directory: {sweep_dir}/")
    print("=" * 80)
    print()


def main():
    dse()

if __name__ == "__main__":
    main()
