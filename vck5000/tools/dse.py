# dse.py
# Design Space Exploration python script
# Runs bin/AIEplace.exe while modifying host/runtime_config.json between runs

import csv
import datetime
import json
import os
import subprocess
import sys
from typing import Any, Union, List, Tuple


# Registry of DSE-controlled parameters: list of (section_path_list, param_name) tuples.
# Populated by dse() before the sweep begins; used by update_info_string().
_dse_params: List[Tuple[List[str], str]] = []

# Overall sweep progress, written into DSE_info by update_info_string().
_dse_current_run: int = 0
_dse_total_runs: int = 0


def update_info_string(config: dict, config_path: str) -> None:
    """
    Build a DSE_info string from all DSE-controlled parameters and write it
    into config["output"]["DSE_info"], then save the config file.

    The string has the form:
        "param1=value1 param2=value2 ..."
    in the order that parameters were registered in _dse_params.

    This is a no-op if _dse_params is empty or if the config has no "output" section.
    """
    if not _dse_params or "output" not in config:
        return

    parts = []
    for section_list, param_name in _dse_params:
        section = config
        for key in section_list:
            section = section.get(key, {})
        if isinstance(section, dict) and param_name in section:
            value = section[param_name]
            if param_name == "benchmark":
                value = str(value).rsplit('/', 1)[-1]
            parts.append(f"{param_name}={value}")

    if _dse_total_runs > 0:
        parts.insert(0, f"DSE sweep progress={_dse_current_run} of {_dse_total_runs}")

    config["output"]["DSE_info"] = "\n".join(parts)

    with open(config_path, 'w') as f:
        json.dump(config, f, indent=2, ensure_ascii=False)


def modify_config_parameter(
    config_path: str,
    param_name: str,
    new_value: Any,
    output_path: Union[str, None] = None,
    section_path: Union[str, List[str], None] = None
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
    
    Raises:
        FileNotFoundError: If config file doesn't exist
        KeyError: If parameter name or section path not found in config
        json.JSONDecodeError: If config file is not valid JSON
    
    Examples:
        # Original usage (backward compatible)
        modify_config_parameter("config.json", "timeout", 30)
        
        # Modify in different section
        modify_config_parameter("config.json", "host", "localhost", section_path="database")
        
        # Modify in nested section
        modify_config_parameter("config.json", "port", 5432, section_path="database.connection")
        modify_config_parameter("config.json", "port", 5432, section_path=["database", "connection"])
        
        # Modify at root level
        modify_config_parameter("config.json", "version", "1.2.0", section_path="")
    """
    try:
        # Read the config file
        with open(config_path, 'r') as f:
            config = json.load(f)
        
        # Handle section path
        if section_path is None:
            # Default to "params" for backward compatibility
            section_path = ["params"]
        elif isinstance(section_path, str):
            # Convert string path to list
            if section_path == "":
                section_path = []  # Root level
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
        
        # Check if the current section is a dictionary
        if not isinstance(current_section, dict):
            raise KeyError(f"Target section '{section_path_str}' is not a dictionary and cannot contain parameters")
        
        # Check if parameter exists in the target section
        if param_name not in current_section:
            raise KeyError(f"Parameter '{param_name}' not found in section '{section_path_str}'")
            
        # Update the parameter
        current_section[param_name] = new_value
        
        # Determine output path
        save_path = output_path if output_path else config_path
        
        # Save the modified config
        with open(save_path, 'w') as f:
            json.dump(config, f, indent=2, ensure_ascii=False)
        
        # Create a display path for logging
        display_path = ".".join(section_path) if section_path else "root"
        if display_path:
            print(f"DSE: Updated param '{param_name}' in section '{display_path}' to {new_value}")
        else:
            print(f"DSE: Updated param '{param_name}' at root level to {new_value}")

        update_info_string(config, save_path)
        
    except FileNotFoundError:
        print(f"Error: Config file '{config_path}' not found")
        sys.exit(1)
    except json.JSONDecodeError:
        print(f"Error: '{config_path}' is not a valid JSON file")
        sys.exit(1)
    except (KeyError, ValueError) as e:
        print(f"Error: {str(e)}")
        sys.exit(1)


# Convenience function for backward compatibility
def modify_params_parameter(config_path: str, param_name: str, new_value: Any, output_path: Union[str, None] = None) -> None:
    """
    Legacy function for modifying parameters in the 'params' section.
    This is just a wrapper around the main function for backward compatibility.
    """
    modify_config_parameter(config_path, param_name, new_value, output_path, section_path="params")


# @brief: Run the compiled program using a subprocess.
# stdout/stderr are inherited from the parent process so output appears in the
# terminal in real-time without any pipe-deadlock risk.
def run_AIEplace(args=None):
    if args is None:
        args = []

    command = ['bin/AIEplace.exe'] + args
    print(f"DSE: Running {command}")

    result = subprocess.run(command)

    if result.returncode != 0:
        print(f"DSE: ERROR — process exited with code {result.returncode}")
        return False
    return True



benchmark_path = "host/benchmarks/"
benchmark_list = [
                #"ispd2015/mgc_fft_1",
                #"ispd2015/mgc_fft_2",
                #"ispd2015/mgc_fft_a",
                #"ispd2015/mgc_fft_b",
                #"ispd2015/mgc_des_perf_1",
                #"ispd2015/mgc_des_perf_a",
                #"ispd2015/mgc_des_perf_b",
                #"ispd2015/mgc_edit_dist_a",
                #"ispd2015/mgc_matrix_mult_1",
                #"ispd2015/mgc_matrix_mult_2",
                "ispd2015/mgc_matrix_mult_a",
                #"ispd2015/mgc_matrix_mult_b",
                #"ispd2015/mgc_matrix_mult_c",
                #"ispd2015/mgc_pci_bridge32_a",
                #"ispd2015/mgc_pci_bridge32_b",
                #"ispd2015/mgc_superblue11_a",
                #"ispd2015/mgc_superblue12",
                #"ispd2015/mgc_superblue14",
                #"ispd2015/mgc_superblue16_a",
                #"ispd2015/mgc_superblue19",
                "ispd2005/adaptec1",
                #"ispd2005/adaptec2",
                #"ispd2005/adaptec3",
                #"ispd2005/adaptec4",
                "ispd2005/bigblue1",
                #"ispd2005/bigblue2",
                #"ispd2005/bigblue3",
                #"ispd2005/bigblue4"
                ]
#step_length_values = [10, 50, 100, 200, 300, 400, 500, 800, 1000]
step_length_values = [50, 100, 200]
#step_length_values = [50]
RUNS_PER_CONFIG = 2


# Design Space Exploration
# Runs AIEplace with different configurations by modifying the JSON config file between runs
# TODO: expand DSE to use SA or ant colony optimization
def dse():
    global _dse_params, _dse_current_run, _dse_total_runs
    config_path = "host/run_config_dse.json"
    args = [config_path]

    # Register all parameters under DSE control so update_info_string() can build DSE_info.
    _dse_params = [
        (["input"],  "benchmark"),
        (["params"], "partials_compute_method"),
        (["params"], "init_step_length"),
    ]

    # Total runs = product of all swept list sizes times repeats per config.
    _dse_total_runs = len(benchmark_list) * len(step_length_values) * RUNS_PER_CONFIG
    _dse_current_run = 0

    # Give every DSE sweep its own subdirectory so runs never collide
    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    modify_config_parameter(config_path, "results_dir", f"results/DSE_{timestamp}", section_path="output")

    for benchmark in benchmark_list:
        print(f"\n=== Starting DSE for benchmark: {benchmark} ===")
        # Modification of parameters between runs
        modify_config_parameter(config_path, "benchmark", benchmark_path + benchmark, section_path="input")
        modify_config_parameter(config_path, "partials_compute_method", "cpu", section_path="params")

        for step_length in step_length_values:
            modify_config_parameter(config_path, "init_step_length", step_length, section_path="params")

            for run_num in range(1, RUNS_PER_CONFIG + 1):
                _dse_current_run += 1
                # Re-sync DSE_info with the updated run counter before launching.
                with open(config_path, 'r') as f:
                    config = json.load(f)
                update_info_string(config, config_path)
                print(f"\nDSE: benchmark={benchmark}  init_step_length={step_length}  run={run_num}/{RUNS_PER_CONFIG}")
                run_AIEplace(args)




def main():
    dse()

if __name__=="__main__":
    main()