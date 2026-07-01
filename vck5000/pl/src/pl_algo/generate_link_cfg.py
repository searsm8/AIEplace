# File: generate_link_cfg.py (pl_algo variant)
#
# The PL-centric pl_algo design has a SINGLE top-level kernel ("top"). With AIE=none
# (PL-only, e.g. HPWL) there are no PL<->AIE stream connects. With an AIE variant, the
# density FFT pool is wired: -d (density_instances == AIE_DENSITY_INSTANCES) sets the
# lane count and emits that many fft_to_aie_<i>/fft_from_aie_<i> <-> fft_in_<i>/
# fft_out_<i> stream_connect pairs. -p (partials) is unused (HPWL runs on the PL).
import argparse

def generate_link_cfg(file_path, aie, density_instances):
    with open(file_path, 'w') as f:
        f.write("[connectivity]\n")
        f.write("### Single top-level PL kernel ###\n")
        f.write("nk=top:1:top_1\n\n")

        if aie != "none":
            # Wire top's AIE FFT pool stream ports to the DensityFFTGraph PLIO. One
            # stream_connect pair per lane: top's array AXIS ports are fft_to_aie_<i>
            # / fft_from_aie_<i>; the AIE PLIO are fft_in_<i> / fft_out_<i>. Lane count
            # = density_instances (== AIE_DENSITY_INSTANCES). (HPWL runs on the PL, so
            # its parked graph is not wired.)
            f.write("### Density FFT pool (%d lanes) ###\n" % density_instances)
            for i in range(density_instances):
                f.write("stream_connect=top_1.fft_to_aie_%d:ai_engine_0.fft_in_%d\n" % (i, i))
                f.write("stream_connect=ai_engine_0.fft_out_%d:top_1.fft_from_aie_%d\n" % (i, i))
            f.write("\n")

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
    generate_link_cfg(args.path, args.aie, args.density_instances)

if __name__ == "__main__":
    main()
