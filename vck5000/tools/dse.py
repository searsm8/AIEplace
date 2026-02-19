# dse.py
# Design Space Exploration python script
# Runs bin/AIEplace.exe while modifying host/runtime_config.json between runs

import json
import sys
import subprocess
import os
from typing import Any, Union

import json
import sys
from typing import Any, Union, List


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


# @brief: Run the compiled program using a subprocess
def run_AIEplace(args=None):
    if args is None:
        args = []
    
    try:
        # Create the command with executable and any arguments
        command = ['bin/AIEplace.exe'] + args
        
        # Run the program with additional configurations
        print(f"DSE: {command}")
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,  # This is the older equivalent of text=True
            bufsize=1  # Line buffered
        )
        
        # Capture output in real-time
        output = []
        error_output = []
        
        # Read both stdout and stderr simultaneously
        print(f"DSE: Reading stdout and stderr in real-time...")
        while True:
            stdout_line = process.stdout.readline()
            stderr_line = process.stderr.readline()
            
            if stdout_line:
                output.append(stdout_line)
                sys.stdout.flush()  # Ensure immediate console output
            
            if stderr_line:
                error_output.append(stderr_line)
                sys.stderr.flush()  # Ensure immediate console output
            
            # Check if process has finished
            if process.poll() is not None and not stdout_line and not stderr_line:
                break
        
        # Wait for the process to complete
        print(f"DSE: Waiting for process to finish...")
        return_code = process.wait()
        
        if return_code != 0:
            error_message = ''.join(error_output)
            print(f"Runtime error (code {return_code}):\n{error_message}")
            return None
        
        return ''.join(output)
        
    except Exception as e:
        print(f"Error running program: {str(e)}")
        return None



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
                #"ispd2015/mgc_matrix_mult_a",
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
                #"ispd2005/bigblue1",
                #"ispd2005/bigblue2",
                #"ispd2005/bigblue3",
                #"ispd2005/bigblue4"
                ]
learning_rates = [10, 50, 100, 200, 300, 400, 500, 800, 1000]

def run_all_benchmarks(config_path):
    for benchmark in benchmark_list:
        modify_config_parameter(config_path, "input_filepath", benchmark_path+benchmark)
        run_AIEplace()

# TODO: expand DSE to use SA or ant colony optimization
# Design Space Exploration algorithm
def dse():
    config_path = "host/run_config_dse.json"

    # Run all benchmarks with simple and cpu partials_compute_method
    for benchmark in benchmark_list:
        for lr in learning_rates:
            modify_config_parameter(config_path, "benchmark", benchmark_path+benchmark, section_path="input")
            modify_config_parameter(config_path, "partials_compute_method", "cpu", section_path="params")
            modify_config_parameter(config_path, "init_learning_rate", lr, section_path="params")
            run_AIEplace()
            run_AIEplace()
            run_AIEplace()

            #modify_config_parameter(config_path, "partials_compute_method", "simple", section_path="params")
            #run_AIEplace()
            #modify_config_parameter(config_path, "partials_compute_method", "orig", section_path="params")
            #run_AIEplace()
    

def main():
    dse()


if __name__=="__main__":
    main()