# File: generate_link_cfg.py
# Builds the config file link.cfg which specifies connections between PL and AIE
# reads from "Common.h" the number of Partials Graphs exist in the design
import re

def read_partials_graph_count(header_file):
    with open(header_file, 'r') as f:
        content = f.read()
        match = re.search(r'#define\s+PARTIALS_GRAPH_COUNT\s+(\d+)', content)
        if match:
            return int(match.group(1))
        else:
            raise ValueError("PARTIALS_GRAPH_COUNT not found in the header file.")

def generate_link_cfg(file_path):
    with open(file_path, 'w') as f:
        f.write("platform=xilinx_vck5000_gen4x8_xdma_2_202210_1\n\n")
        f.write("[connectivity]\n")
        f.write("### Partials Kernel connections ###\n")

        partials_graph_count = read_partials_graph_count("../host/src/include/Common.h")
        mm2s_name = "partials_mm2s"
        s2mm_name = "partials_s2mm"

        mm2s_nk =  f"nk={mm2s_name}:{partials_graph_count}:"
        s2mm_nk =  f"nk={s2mm_name}:{partials_graph_count}:"
        for i in range(partials_graph_count):
            mm2s_nk += f"{mm2s_name}_{i}" + ("" if i+1 == partials_graph_count else ".")
            s2mm_nk += f"{s2mm_name}_{i}" + ("" if i+1 == partials_graph_count else ".")
        f.write(mm2s_nk + "\n")
        f.write(s2mm_nk + "\n")
        
        for i in range(partials_graph_count):
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
    generate_link_cfg("link.cfg")

if __name__ == "__main__":
    main()