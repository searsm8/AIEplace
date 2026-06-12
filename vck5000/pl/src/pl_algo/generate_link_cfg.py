# File: generate_link_cfg.py (pl_algo variant)
#
# The PL-centric pl_algo design has a SINGLE top-level kernel ("top") with fixed
# interfaces, so the connectivity is static -- unlike markv1, the -p/-d instance counts
# do not change the wiring. They are still accepted for compatibility with the top-level
# Makefile's link_config target, but ignored.
#
# NOTE: the AIE-side port names (ai_engine_0.*) are placeholders until aie/src/pl_algo
# is written; finalize them together with that variant.
import argparse

FFT_LANES = 8

def generate_link_cfg(file_path, partials_instances, density_instances):
    with open(file_path, 'w') as f:
        f.write("[connectivity]\n")
        f.write("### Single top-level PL kernel ###\n")
        f.write("nk=top:1:top_1\n\n")

        f.write("### HPWL gradient graph ###\n")
        f.write("stream_connect=top_1.hpwl_to_aie:ai_engine_0.hpwl_in\n")
        f.write("stream_connect=ai_engine_0.hpwl_out:top_1.hpwl_from_aie\n\n")

        f.write("### FFT pool ###\n")
        for i in range(FFT_LANES):
            f.write(f"stream_connect=top_1.fft_to_aie_{i}:ai_engine_0.fft_in_{i}\n")
            f.write(f"stream_connect=ai_engine_0.fft_out_{i}:top_1.fft_from_aie_{i}\n")
        f.write("\n")

        f.write("[vivado]\n")
        f.write("# improve hw_emu speed (platform-dependent)\n")
        f.write("prop=fileset.sim_1.xsim.elaborate.xelab.more_options={-override_timeprecision -timescale=1ns/1ps}\n")

        print(f"Generated file: {file_path}")

def main():
    parser = argparse.ArgumentParser(description="Create static PL<->AIE link config for pl_algo")
    parser.add_argument("path", type=str, help="Path of the link config file to create")
    parser.add_argument("-p", "--partials_instances", type=int, default=1, help="(ignored; fixed design)")
    parser.add_argument("-d", "--density_instances", type=int, default=1, help="(ignored; fixed design)")
    args = parser.parse_args()
    generate_link_cfg(args.path, args.partials_instances, args.density_instances)

if __name__ == "__main__":
    main()
