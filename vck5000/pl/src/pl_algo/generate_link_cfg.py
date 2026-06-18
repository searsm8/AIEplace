# File: generate_link_cfg.py (pl_algo variant)
#
# The PL-centric pl_algo design has a SINGLE top-level kernel ("top") with fixed
# interfaces, so the connectivity is static -- unlike markv1, the -p/-d instance counts
# do not change the wiring. They are still accepted for compatibility with the top-level
# Makefile's link_config target, but ignored.
#
# HPWL now runs entirely on the PL (no AIE), so the default build is AIE=none: just
# the top kernel, no PL<->AIE stream connects. The AIE branch (stream_connect to the
# HpwlGradGraph PLIO) is kept for a future AIE-using variant but is off by default.
import argparse

def generate_link_cfg(file_path, aie):
    with open(file_path, 'w') as f:
        f.write("[connectivity]\n")
        f.write("### Single top-level PL kernel ###\n")
        f.write("nk=top:1:top_1\n\n")

        if aie != "none":
            # Future AIE-using variant: wire top's stream ports to the AIE graph
            # PLIO (aie/src/pl_algo HpwlGradGraph: hpwl_grad_in_0 / hpwl_grad_out_0).
            f.write("### HPWL gradient graph ###\n")
            f.write("stream_connect=top_1.hpwl_to_aie:ai_engine_0.hpwl_grad_in_0\n")
            f.write("stream_connect=ai_engine_0.hpwl_grad_out_0:top_1.hpwl_from_aie\n\n")
            # ### FFT pool ### -- deferred until the density_grad AIE graph exists.

        f.write("[vivado]\n")
        f.write("# improve hw_emu speed (platform-dependent)\n")
        f.write("prop=fileset.sim_1.xsim.elaborate.xelab.more_options={-override_timeprecision -timescale=1ns/1ps}\n")

        print(f"Generated file: {file_path} (aie={aie})")

def main():
    parser = argparse.ArgumentParser(description="Create static PL<->AIE link config for pl_algo")
    parser.add_argument("path", type=str, help="Path of the link config file to create")
    parser.add_argument("-p", "--partials_instances", type=int, default=1, help="(ignored; fixed design)")
    parser.add_argument("-d", "--density_instances", type=int, default=1, help="(ignored; fixed design)")
    parser.add_argument("--aie", type=str, default="none",
                        help="AIE variant ('none' = PL-only, no stream connects)")
    args = parser.parse_args()
    generate_link_cfg(args.path, args.aie)

if __name__ == "__main__":
    main()
