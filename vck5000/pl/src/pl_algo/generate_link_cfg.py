# File: generate_link_cfg.py (pl_algo variant)
#
# The PL-centric pl_algo design has a SINGLE top-level kernel ("top") with fixed
# interfaces, so the connectivity is static -- unlike markv1, the -p/-d instance counts
# do not change the wiring. They are still accepted for compatibility with the top-level
# Makefile's link_config target, but ignored.
#
# AIE-side port names match the HpwlGradGraph PLIO (aie/src/pl_algo): a single
# instance exposes hpwl_grad_in_0 / hpwl_grad_out_0. The FFT pool lanes are added
# back here once the density_grad graph exists in the AIE variant.
import argparse

def generate_link_cfg(file_path, partials_instances, density_instances):
    with open(file_path, 'w') as f:
        f.write("[connectivity]\n")
        f.write("### Single top-level PL kernel ###\n")
        f.write("nk=top:1:top_1\n\n")

        f.write("### HPWL gradient graph (Milestone B: hpwl_manager pass-through) ###\n")
        f.write("stream_connect=top_1.hpwl_to_aie:ai_engine_0.hpwl_grad_in_0\n")
        f.write("stream_connect=ai_engine_0.hpwl_grad_out_0:top_1.hpwl_from_aie\n\n")

        # ### FFT pool ### -- deferred until the density_grad AIE graph exists.

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
