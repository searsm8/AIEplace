#!/usr/bin/env python3
"""
Simplified Design Space Exploration for AIEplace
Only handles parameter modification and run execution - all metrics handled by C++ code
"""

import json
import subprocess
import os
import time
from typing import Any, Union, List
from datetime import datetime
from pathlib import Path

class AIEplaceDSE:
    def __init__(self, config_path: str = "host/run_config.json"):
        self.config_path = config_path
        self.total_runs = 0
        self.completed_runs = 0
        
        # Create logs directory
        self.logs_dir = Path("results/dse_logs")
        self.logs_dir.mkdir(exist_ok=True)
        
        # Generate timestamp for this DSE session
        self.timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.log_file = self.logs_dir / f"dse_session_{self.timestamp}.log"
        
    def log_message(self, message: str):
        """Log message to both console and file"""
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        log_entry = f"[{timestamp}] {message}"
        print(log_entry)
        
        with open(self.log_file, 'a') as f:
            f.write(log_entry + "\n")
    
    def modify_config_parameter(self, param_name: str, new_value: Any, 
                              section_path: Union[str, List[str], None] = None) -> None:
        """Modify configuration parameter with error handling"""
        try:
            with open(self.config_path, 'r') as f:
                config = json.load(f)
            
            # Handle section path
            if section_path is None:
                section_path = ["params"]
            elif isinstance(section_path, str):
                section_path = section_path.split('.') if section_path else []
            
            # Navigate to target section
            current_section = config
            for section_name in section_path:
                if section_name not in current_section:
                    raise KeyError(f"Section '{section_name}' not found")
                current_section = current_section[section_name]
            
            # Update parameter
            current_section[param_name] = new_value
            
            # Save config
            with open(self.config_path, 'w') as f:
                json.dump(config, f, indent=2)
            
            self.log_message(f"Updated {'.'.join(section_path)}.{param_name} = {new_value}")
            
        except Exception as e:
            self.log_message(f"Error updating config: {str(e)}")
            raise
    
    def run_aieplace(self, timeout_sec: int = 600) -> bool:
        """Run AIEplace and return success status"""
        try:
            command = ['bin/AIEplace.exe']
            self.log_message(f"Executing: {' '.join(command)}")
            
            # Run AIEplace
            process = subprocess.Popen(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                universal_newlines=True
            )
            
            # Wait for completion with timeout
            try:
                stdout, _ = process.communicate(timeout=timeout_sec)
                return_code = process.returncode
                
                success = (return_code == 0)
                
                if success:
                    self.log_message("✓ AIEplace completed successfully")
                else:
                    self.log_message(f"✗ AIEplace failed with return code {return_code}")
                
                return success
                
            except subprocess.TimeoutExpired:
                process.kill()
                self.log_message(f"✗ Process timed out after {timeout_sec} seconds")
                return False
                
        except Exception as e:
            self.log_message(f"✗ Error running AIEplace: {str(e)}")
            return False
    
    def run_design_space_exploration(self):
        """Execute comprehensive design space exploration"""
        self.log_message("Starting Design Space Exploration")
        self.log_message(f"Config file: {self.config_path}")
        self.log_message(f"Session log: {self.log_file}")
        
        # Benchmark configurations
        benchmarks = [
            "host/benchmarks/ispd2005/adaptec1",
            "host/benchmarks/ispd2005/adaptec4", 
            "host/benchmarks/ispd2005/bigblue1",
            "host/benchmarks/ispd2005/bigblue4",
            "host/benchmarks/ispd2015/mgc_fft_1",
            "host/benchmarks/ispd2015/mgc_fft_b",
            "host/benchmarks/ispd2015/mgc_des_perf_1",
            "host/benchmarks/ispd2015/mgc_superblue19",
            "host/benchmarks/ispd2015/mgc_matrix_mult_1",
            "host/benchmarks/ispd2015/mgc_matrix_mult_c",
            "host/benchmarks/ispd2015/mgc_edit_dist_a",
            "host/benchmarks/ispd2015/mgc_pci_bridge32_a",
            "host/benchmarks/ispd2015/mgc_superblue11_a"
        ]
        
        # Method combinations
        method_combinations = [
            {"partials": "cpu", "density": "cpu"},      # Baseline CPU
            {"partials": "aie", "density": "cpu"},      # AIE partials only
            {"partials": "cpu", "density": "aie"},      # AIE density only
            {"partials": "aie", "density": "aie"},      # Full AIE
            {"partials": "simple", "density": "cpu"},   # Simple method baseline
        ]
        
        # Hyperparameters
        #learning_rates = [0.01, 0.02, 0.04]
        learning_rates = [ 0.02]
        gamma_values = [4]  # expand this as needed
        
        # Calculate total runs
        self.total_runs = len(benchmarks) * len(method_combinations) * len(learning_rates) * len(gamma_values)
        self.log_message(f"Total planned runs: {self.total_runs}")
        
        start_time = time.time()
        successful_runs = 0
        
        try:
            for benchmark in benchmarks:
                benchmark_name = os.path.basename(benchmark)
                self.log_message(f"\n--- Testing benchmark: {benchmark_name} ---")
                
                # Set benchmark in config
                self.modify_config_parameter("benchmark", benchmark, ["input"])
                
                for method_combo in method_combinations:
                    partials_method = method_combo["partials"]
                    density_method = method_combo["density"]
                    
                    # Set computation methods
                    self.modify_config_parameter("partials_compute_method", partials_method)
                    self.modify_config_parameter("density_compute_method", density_method)
                    
                    for lr in learning_rates:
                        for gamma in gamma_values:
                            self.completed_runs += 1
                            
                            # Set hyperparameters
                            self.modify_config_parameter("init_learning_rate", lr)
                            self.modify_config_parameter("gamma", gamma)
                            
                            # Log current configuration
                            config_desc = (f"Run {self.completed_runs}/{self.total_runs}: "
                                         f"{benchmark_name} | "
                                         f"partials:{partials_method} density:{density_method} | "
                                         f"lr:{lr} gamma:{gamma}")
                            
                            self.log_message(config_desc)
                            
                            # Run AIEplace
                            run_start_time = time.time()
                            success = self.run_aieplace(timeout_sec=1800)  # 30 min timeout for large designs
                            run_time = time.time() - run_start_time
                            
                            if success:
                                successful_runs += 1
                                self.log_message(f"   Completed in {run_time:.1f}s")
                            else:
                                self.log_message(f"   Failed after {run_time:.1f}s")
                            
                            # Progress update
                            progress = (self.completed_runs / self.total_runs) * 100
                            elapsed = time.time() - start_time
                            estimated_total = elapsed * self.total_runs / self.completed_runs
                            remaining = estimated_total - elapsed
                            
                            self.log_message(f"   Progress: {progress:.1f}% "
                                           f"({successful_runs}/{self.completed_runs} successful) "
                                           f"| ETA: {remaining/60:.1f} min")
                            
                            # Brief pause between runs
                            time.sleep(1)
                            
                            # Optional: Skip some configurations for very large benchmarks to save time
                            if "superblue" in benchmark_name and lr > 0.02:
                                self.log_message("   Skipping remaining LR values for large benchmark")
                                break
                        
                        if "superblue" in benchmark_name and lr > 0.02:
                            break
                            
        except KeyboardInterrupt:
            self.log_message("\n!!! Interrupted by user !!!")
        except Exception as e:
            self.log_message(f"\n!!! Unexpected error: {str(e)} !!!")
        
        # Final summary
        total_time = time.time() - start_time
        self.log_message(f"\n=== Design Space Exploration Complete ===")
        self.log_message(f"Total runs completed: {self.completed_runs}/{self.total_runs}")
        self.log_message(f"Successful runs: {successful_runs}")
        self.log_message(f"Success rate: {successful_runs/self.completed_runs*100:.1f}%")
        self.log_message(f"Total time: {total_time/3600:.2f} hours")
        self.log_message(f"Average time per run: {total_time/self.completed_runs:.1f} seconds")
        self.log_message(f"Session log saved to: {self.log_file}")
        
        return successful_runs, self.completed_runs

    def run_quick_test(self):
        """Run a quick test with a small benchmark"""
        self.log_message("Running quick test configuration")
        
        # Small benchmark for testing
        test_benchmark = "host/benchmarks/ispd2005/adaptec1"
        self.modify_config_parameter("benchmark", test_benchmark, ["input"])
        
        # Test one configuration
        self.modify_config_parameter("partials_compute_method", "cpu")
        self.modify_config_parameter("density_compute_method", "cpu")
        self.modify_config_parameter("init_learning_rate", 0.01)
        self.modify_config_parameter("gamma", 4)
        
        self.log_message("Testing CPU-only configuration on adaptec1")
        success = self.run_aieplace(timeout_sec=300)
        
        if success:
            self.log_message("✓ Quick test passed")
        else:
            self.log_message("✗ Quick test failed")
        
        return success


def main():
    """Main entry point for DSE"""
    dse = AIEplaceDSE()
    
    print("AIEplace Design Space Exploration")
    print("=" * 50)
    print("This script will:")
    print("- Modify configuration parameters systematically")
    print("- Launch AIEplace runs")
    print("- Log execution status")
    print("- All performance metrics are handled by C++ code")
    print()
    
    # Menu options
    print("Select execution mode:")
    print("1. Quick test (single small benchmark)")
    print("2. Full design space exploration")
    print("3. Custom configuration")
    
    try:
        choice = input("\nEnter choice (1-3): ").strip()
        
        if choice == "1":
            print("\nRunning quick test...")
            success = dse.run_quick_test()
            print("✓ Quick test completed" if success else "✗ Quick test failed")
            
        elif choice == "2":
            print("\nStarting full design space exploration...")
            print("This may take several hours depending on system performance.")
            response = input("Proceed? (y/n): ")
            
            if response.lower() == 'y':
                successful, total = dse.run_design_space_exploration()
                print(f"\n✓ DSE completed: {successful}/{total} successful runs")
            else:
                print("Aborted by user")
                
        elif choice == "3":
            print("\nCustom configuration mode:")
            print("Modify the script's configuration lists and re-run")
            
        else:
            print("Invalid choice")
            
    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
    except Exception as e:
        print(f"\nError: {str(e)}")
    
    print(f"\nSession logs available at: {dse.log_file}")


if __name__ == "__main__":
    main()