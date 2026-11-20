# File: generate_link_cfg.py
# Builds the config file link.cfg which specifies connections between PL and AIE
import re
import argparse

channels_per_instance = 96

def generate_link_cfg(file_path, partials_channels, density_channels):
    density_instances = int((density_channels+channels_per_instance-1)/channels_per_instance)
    with open(file_path, 'w') as f:
        top_name = "density_accelerator"

        f.write("[connectivity]\n")
        f.write("### Density Kernel connections ###\n")


        top_nk =  f"nk={top_name}:{density_instances}:"
        for i in range(density_instances):
            top_nk += f"{top_name}_{i}" + ("" if i+1 == density_instances else ".")
        f.write(top_nk + "\n")
        
        for i in range(density_instances):
          for j in range(channels_per_instance):
            port = i * channels_per_instance + j
        
            f.write(
              f"stream_connect={top_name}_{i}.o{j}:"
              f"ai_engine_0.fft_in{port}\n"
            )
            f.write(
              f"stream_connect=ai_engine_0.fft_out{port}:"
              f"{top_name}_{i}.i{j}\n"
            )

        f.write("\n[clock]\n")
        f.write(f"defaultFreqHz=225000000\n")

        print(f"Generated file: {file_path}")

def main():
    parser = argparse.ArgumentParser(
        description="Create link config file to connect PL and AIE"
    )

    # Positional argument
    parser.add_argument("path", type=str, help="Path of the link config file to create")

    # Optional arguments
    parser.add_argument("-p", "--partials_channels", type=int, default=1, help="Number of partials accelerator instances (default: 1)")
    parser.add_argument("-d", "--density_channels", type=int, default=16, help="Number of density accelerator channels (default: 16)")

    args = parser.parse_args()

    path = args.path
    partials_channels = args.partials_channels
    density_channels = args.density_channels

    generate_link_cfg(path, partials_channels, density_channels)

if __name__ == "__main__":
    main()
