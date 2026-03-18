# dse.py
# Design Space Exploration python script
# Runs bin/AIEplace.exe while modifying host/runtime_config.json between runs

import datetime
import itertools
import json
import os
import shutil
import subprocess
import sys
import time
from collections import OrderedDict
from typing import Any, Union, List, Tuple

# Maximum number of parallel AIEplace processes.
# Set to 1 for sequential execution (original behavior).
# A good default 4 or 8 to speed up DSE on a typical multi-core machine without overwhelming it.
MAX_PARALLEL = 8


# =============================================================================
# DSE Sweep Configuration
# =============================================================================
# Each entry maps a parameter name to (section_path, [values_to_sweep]).
# The Cartesian product of all value lists is computed automatically.
# To add a new parameter sweep, add one line. To remove, delete or comment out.
#
# "benchmark" is special: values are prefixed with benchmark_path automatically.
# =============================================================================

benchmark_path = "host/benchmarks/"

dse_sweep = OrderedDict([
    # Full list of benchmarks from ISPD 2015 and 2005 contests.
    #("benchmark", (["input"], [
    #    "ispd2015/mgc_fft_1",
    #    "ispd2015/mgc_fft_2",
    #    "ispd2015/mgc_fft_a",
    #    "ispd2015/mgc_fft_b",
    #    "ispd2015/mgc_des_perf_1",
    #    "ispd2015/mgc_des_perf_a",
    #    "ispd2015/mgc_des_perf_b",
    #    "ispd2015/mgc_edit_dist_a",
    #    "ispd2015/mgc_matrix_mult_1",
    #    "ispd2015/mgc_matrix_mult_2",
    #    "ispd2015/mgc_matrix_mult_a",
    #    "ispd2015/mgc_matrix_mult_b",
    #    "ispd2015/mgc_matrix_mult_c",
    #    "ispd2015/mgc_pci_bridge32_a",
    #    "ispd2015/mgc_pci_bridge32_b",
    #    "ispd2015/mgc_superblue11_a",
    #    "ispd2015/mgc_superblue12",
    #    "ispd2015/mgc_superblue14",
    #    "ispd2015/mgc_superblue16_a",
    #    "ispd2015/mgc_superblue19",
    #    "ispd2005/adaptec1",
    #    "ispd2005/adaptec2",
    #    "ispd2005/adaptec3",
    #    "ispd2005/adaptec4",
    #    "ispd2005/bigblue1",
    #    "ispd2005/bigblue2",
    #    "ispd2005/bigblue3",
    #    "ispd2005/bigblue4",
    #])),

    # Smaller subset of benchmarks for quick DSE runs during development:
    ("benchmark", (["input"], [
        "ispd2015/mgc_fft_1",
        "ispd2015/mgc_fft_a",
        "ispd2015/mgc_des_perf_1",
        "ispd2015/mgc_edit_dist_a",
        "ispd2015/mgc_matrix_mult_1",
        "ispd2015/mgc_matrix_mult_b",
        "ispd2015/mgc_pci_bridge32_a",
        "ispd2015/mgc_superblue11_a",
        "ispd2015/mgc_superblue14",
        "ispd2005/adaptec1",
        "ispd2005/adaptec4",
        "ispd2005/bigblue1",
        "ispd2005/bigblue3",
    ])),

    # Uncomment to sweep additional parameters:
    # (<param_name>, ([section_path], [values_to_sweep]))
    ################################################################
    #("partials_compute_method", (["params"], ["cpu", "aie"])),
    #("enable_backtracking",     (["params"], [True, False])),
    #("enable_filler",     (["params"], [True, False])),
    #("init_step_length",        (["params"], [0.001, 0.01, 0.1, 1.0])),
    #("convergence_min_iterations", (["params"], [50, 100, 200])),
    #("density_weight_max_step", (["params"], [1.02, 1.05, 1.1])),
    ("density_weight_init_multiplier", (["params"], [8e-05, 8e-04, 8e-03])),
    #("bins_per_row",            (["params"], [64, 128, 256])),
])


# =============================================================================
# Utility Functions
# =============================================================================

def update_info_string(config: dict, config_path: str, run_num: int, total_runs: int) -> None:
    """
    Build a DSE_info string from the current config values of all swept parameters
    and write it into config["output"]["DSE_info"], then save the config file.
    """
    if "output" not in config:
        return

    parts = [f"DSE sweep progress={run_num} of {total_runs}"]
    for param_name, (section_list, _) in dse_sweep.items():
        section = config
        for key in section_list:
            section = section.get(key, {})
        if isinstance(section, dict) and param_name in section:
            value = section[param_name]
            if param_name == "benchmark":
                value = str(value).rsplit('/', 1)[-1]
            parts.append(f"{param_name}={value}")

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
        with open(config_path, 'r') as f:
            config = json.load(f)

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

    command = ['bin/AIEplace.exe'] + args
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
            ['bin/AIEplace.exe', self.config_path],
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
    config_path = "host/run_config_dse.json"

    # Extract param names, sections, and value lists from the sweep config
    param_names = list(dse_sweep.keys())
    sections = [dse_sweep[p][0] for p in param_names]
    value_lists = [dse_sweep[p][1] for p in param_names]

    # Cartesian product of all parameter value lists
    all_combos = list(itertools.product(*value_lists))
    total_runs = len(all_combos)

    print(f"DSE: {len(param_names)} parameters, {total_runs} total configurations")
    for p in param_names:
        n = len(dse_sweep[p][1])
        print(f"  {p}: {n} value{'s' if n != 1 else ''}")

    parallel = min(MAX_PARALLEL, total_runs)
    print(f"DSE: Running with {parallel} parallel workers")

    # Give every DSE sweep its own subdirectory so runs never collide
    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    sweep_dir = f"results/DSE_{timestamp}"

    # Prepare per-run config files and ParallelRun objects
    pending = []
    config_dir = os.path.join(sweep_dir, "configs")
    os.makedirs(config_dir, exist_ok=True)

    print(f"DSE: Preparing {total_runs} config files...")
    for run_num, combo in enumerate(all_combos, 1):
        # Copy the base config for this run
        run_config_path = os.path.join(config_dir, f"run_{run_num:03d}.json")
        shutil.copy2(config_path, run_config_path)

        # Set results dir (shared sweep dir — each run writes its own results row)
        modify_config_parameter(run_config_path, "results_dir", sweep_dir,
                                section_path="output", quiet=True)

        # Apply each parameter value
        for param_name, section, value in zip(param_names, sections, combo):
            actual_value = value
            if param_name == "benchmark":
                actual_value = benchmark_path + value
            modify_config_parameter(run_config_path, param_name, actual_value,
                                    section_path=section, quiet=True)

        # Update DSE_info
        with open(run_config_path, 'r') as f:
            config = json.load(f)
        update_info_string(config, run_config_path, run_num, total_runs)

        combo_str = "  ".join(f"{p}={v}" for p, v in zip(param_names, combo))
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
                status = "OK" if success else "FAIL"
                if not success:
                    failed += 1
                print(f"DSE: ({(completed/total_runs)*100:4.1f}%) [{completed}/{total_runs}] {status}  {elapsed:6.1f}s  {combo_str}")
            else:
                still_active.append(run)
        active = still_active

        # Avoid busy-waiting
        if active:
            time.sleep(0.5)

    elapsed_total = time.time() - t0_sweep
    print(f"\nDSE complete: {completed - failed}/{total_runs} succeeded in {elapsed_total:.1f}s")
    print(f"Results: {sweep_dir}/")


def main():
    dse()

if __name__ == "__main__":
    main()
