# File: generate_link_cfg.py
# Builds the config file link.cfg which specifies connections between PL and AIE
import re
import argparse

def generate_link_cfg(file_path, partials_instances, density_instances):
    with open(file_path, 'w') as f:
        f.write("[connectivity]\n")
        f.write("### Partials Kernel connections ###\n")

        mm2s_name = "partials_mm2s"
        s2mm_name = "partials_s2mm"

        mm2s_nk =  f"nk={mm2s_name}:{partials_instances}:"
        s2mm_nk =  f"nk={s2mm_name}:{partials_instances}:"
        for i in range(partials_instances):
            mm2s_nk += f"{mm2s_name}_{i}" + ("" if i+1 == partials_instances else ".")
            s2mm_nk += f"{s2mm_name}_{i}" + ("" if i+1 == partials_instances else ".")
        f.write(mm2s_nk + "\n")
        f.write(s2mm_nk + "\n")
        
        for i in range(partials_instances):
            f.write(f"stream_connect=partials_mm2s_{i}.stream_pl2aie:ai_engine_0.x_in_{i}\n")
            f.write(f"stream_connect=ai_engine_0.outplio_partials_{i}:partials_s2mm_{i}.stream_aie2pl\n")

        # TODO: Are these slr and sp lines necessary?
        #for i in range(partials_graph_count):
        #    f.write(f"slr = partials_mm2s_{i}:SLR1\n")
        #    f.write(f"slr = partials_s2mm_{i}:SLR0\n")
        #    
        #for i in range(partials_graph_count):
        #    f.write(f"sp = partials_mm2s_{i}.m_axi_gmem:MC_NOC0\n")
        #    f.write(f"sp = partials_s2mm_{i}.m_axi_gmem:MC_NOC0\n")
        
        
        f.write("\n### Density Kernel connections ###\n")
        f.write("nk=density_mm2s:3:density_mm2s_0.density_mm2s_1.density_mm2s_2\n")
        f.write("nk=density_s2mm:3:density_s2mm_0.density_s2mm_1.density_s2mm_2\n\n")
        f.write("stream_connect = density_mm2s_0.stream_pl2aie:ai_engine_0.DCT_in\n")
        f.write("stream_connect = ai_engine_0.DCT_out:density_s2mm_0.stream_aie2pl\n")
        f.write("stream_connect = density_mm2s_1.stream_pl2aie:ai_engine_0.IDCT_in\n")
        f.write("stream_connect = ai_engine_0.IDCT_out:density_s2mm_1.stream_aie2pl\n")
        f.write("stream_connect = density_mm2s_2.stream_pl2aie:ai_engine_0.IDXST_in\n")
        f.write("stream_connect = ai_engine_0.IDXST_out:density_s2mm_2.stream_aie2pl\n\n")
        
        f.write("[vivado]\n")
        f.write("# use following line to improve the hw_emu running speed affected by platform\n")
        f.write("prop=fileset.sim_1.xsim.elaborate.xelab.more_options={-override_timeprecision -timescale=1ns/1ps}\n\n")
        
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
    parser.add_argument("-p", "--partials_instances", type=int, default=1, help="Number of partials accelerator instances (default: 1)")
    parser.add_argument("-d", "--density_instances", type=int, default=1, help="Number of density accelerator instances (default: 1)")

    args = parser.parse_args()

    path = args.path
    partials_instances = args.partials_instances
    density_instances = args.density_instances

    generate_link_cfg(path, partials_instances, density_instances)

if __name__ == "__main__":
    main()
