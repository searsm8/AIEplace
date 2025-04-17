# dse.py
# Design Space Exploration python script
# Runs bin/AIEplace.exe while modifying host/runtime_config.json between runs

import json
import sys
import subprocess
import os
from typing import Any, Union

def modify_config_parameter(
    config_path: str,
    param_name: str,
    new_value: Any,
    output_path: Union[str, None] = None
) -> None:
    """
    Modify a parameter in a JSON config file.
    
    Args:
        config_path (str): Path to the input config file
        param_name (str): Name of the parameter to modify
        new_value (Any): New value to set for the parameter
        output_path (str, optional): Path to save modified config. If None, overwrites input file.
    
    Raises:
        FileNotFoundError: If config file doesn't exist
        KeyError: If parameter name not found in config
        json.JSONDecodeError: If config file is not valid JSON
    """
    try:
        # Read the config file
        with open(config_path, 'r') as f:
            config = json.load(f)
        
        # Check if parameter exists
        if param_name not in config['params']:
            raise KeyError(f"Parameter '{param_name}' not found in config file")
            
        # Update the parameter
        config['params'][param_name] = new_value
        
        # Determine output path
        save_path = output_path if output_path else config_path
        
        # Save the modified config
        with open(save_path, 'w') as f:
            json.dump(config, f, indent=2)
            
        print(f"DSE: Updated param '{param_name}' to {new_value}")
        
    except FileNotFoundError:
        print(f"Error: Config file '{config_path}' not found")
        sys.exit(1)
    except json.JSONDecodeError:
        print(f"Error: '{config_path}' is not a valid JSON file")
        sys.exit(1)
    except KeyError as e:
        print(f"Error: {str(e)}")
        sys.exit(1)


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
benchmarks = [  #"ispd2015/mgc_fft_1", 
                #"ispd2015/mgc_fft_2", 
                "ispd2015/mgc_fft_a", 
                #"ispd2015/mgc_fft_b", 
                "ispd2015/mgc_des_perf_1",
                #"ispd2015/mgc_des_perf_a",
                #"ispd2015/mgc_des_perf_b",
                #"ispd2015/mgc_edit_dist_a",
                #"ispd2015/mgc_matrix_mult_1",
                "ispd2015/mgc_matrix_mult_2",
                #"ispd2015/mgc_matrix_mult_a",
                #"ispd2015/mgc_matrix_mult_b",
                #"ispd2015/mgc_matrix_mult_c",
                #"ispd2015/mgc_pci_bridge32_a",
                "ispd2015/mgc_pci_bridge32_b",
                "ispd2015/mgc_superblue11_a",
                "ispd2015/mgc_superblue12",
                "ispd2015/mgc_superblue14",
                "ispd2015/mgc_superblue16_a",
                "ispd2015/mgc_superblue19",
                "ispd2005/adaptec1",
                "ispd2005/adaptec2",
                "ispd2005/adaptec3",
                "ispd2005/adaptec4",
                "ispd2005/bigblue1",
                "ispd2005/bigblue2",
                "ispd2005/bigblue3",
                "ispd2005/bigblue4"
                ]
learning_rates = [0.02]#, 0.02, 0.03, 0.04, 0.06]

# TODO: expand DSE to use SA or ant colony optimization
# Design Space Exploration algorithm
def dse():
    config_path = "host/default_config.json"
    for benchmark in benchmarks:
        modify_config_parameter(config_path, "input_filepath", benchmark_path+benchmark)
        for learning_rate in learning_rates:
            modify_config_parameter(config_path, "init_learning_rate", learning_rate)
            modify_config_parameter(config_path, "use_aie_partials", True)
            run_AIEplace()
    


def main():
    dse()


if __name__=="__main__":
    main()