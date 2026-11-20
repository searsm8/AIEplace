# File: generate_link_cfg.py
# Builds the config file link.cfg which specifies connections between PL and AIE
import re
import argparse

channels_per_instance = 16

def generate_link_cfg(file_path, partials_channels, density_channels):
    density_instances = int((density_channels+channels_per_instance-1)/channels_per_instance)
    with open(file_path, 'w') as f:
        f.write("[connectivity]\n")
        f.write("### Partials Kernel connections ###\n")

        mm2s_name = "density_mm2s"
        s2mm_name = "density_s2mm"

        mm2s_nk =  f"nk={mm2s_name}:{density_instances}:"
        s2mm_nk =  f"nk={s2mm_name}:{density_instances}:"
        for i in range(density_instances):
            mm2s_nk += f"{mm2s_name}_{i}" + ("" if i+1 == density_instances else ".")
            s2mm_nk += f"{s2mm_name}_{i}" + ("" if i+1 == density_instances else ".")
        f.write(mm2s_nk + "\n")
        f.write(s2mm_nk + "\n")
        
        for i in range(density_instances):
          for j in range(channels_per_instance):
            port = i * channels_per_instance + j
        
            f.write(
              f"stream_connect={mm2s_name}_{i}.s{j + 1}:"
              f"ai_engine_0.fft_in{port}\n"
            )
            f.write(
              f"stream_connect=ai_engine_0.fft_out{port}:"
              f"{s2mm_name}_{i}.s{j + 1}\n"
            )

        # TODO: Are these slr and sp lines necessary?
        #for i in range(partials_graph_count):
        #    f.write(f"slr = partials_mm2s_{i}:SLR1\n")
        #    f.write(f"slr = partials_s2mm_{i}:SLR0\n")
        #    
        #for i in range(partials_graph_count):
        #    f.write(f"sp = partials_mm2s_{i}.m_axi_gmem:MC_NOC0\n")
        #    f.write(f"sp = partials_s2mm_{i}.m_axi_gmem:MC_NOC0\n")
        
        
        #f.write("[vivado]\n")
        #f.write("# use following line to improve the hw_emu running speed affected by platform\n")
        #f.write("prop=fileset.sim_1.xsim.elaborate.xelab.more_options={-override_timeprecision -timescale=1ns/1ps}\n\n")
        
        #f.write("[profile]\n")
        #f.write("# enable hardware trace\n")
        #f.write("data=all:all:all\n")
        #f.write("xrt.init\n")

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
