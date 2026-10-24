---
title: "hpwl_gradient_CU hardware flow: v++/XRT, 200 MHz baseline, card host"
author: Searsm8
date: 2026-10-24
branch: pl_algo
touches:
  - pl/src/modules/bin_density_CU.hpp
  - pl/src/modules/hpwl_gradient/hpwl_CU.hpp
  - pl/src/modules/hpwl_gradient_CU.hpp
  - pl/synth/bin_density_producer/impl_prj_div0/hls.app
  - pl/synth/bin_density_producer/impl_prj_div0/kernel.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.automg_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.autopilot_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_impl.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_syn.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/__ctype_info__.xml
---

## Decided
- `hpwl_gradient_CU` hardware flow: `make -C pl xclbin TARGET=sw_emu|hw_emu|hw KERNEL_FREQ_HZ=...` (v++ -c/-l/-p into pl/build/$TARGET), XRT host `make -C host/sw_flat hpwl-gradient-xrt TARGET=...`. The check is device grad image + int64 HPWL bit-equal to the same CU in g++, owned slots bit-equal to sw_flat. `synthetic` mode (co-sim design, 16 chunks) is the only thing small enough for chunked hw_emu.
- Baseline clock is 200 MHz (WNS +0.031 ns); passed bit-exact on the real VCK5000 (synthetic, fft_a, mms/adaptec1, bigblue4).
- `hpwl_total_fixed` is m_axi on its own bundle gmem5: on s_axilite a pointer becomes a write-only register XRT cannot read; on gmem9 it would widen the port and cost the 32-bit tables their bursts.

## Tried and ruled out
- 250 MHz: routed WNS -0.729 ns; v++ refuses (kernel clock `clkout1_primitive` is unscalable on this platform), it does not lower the clock.
- Running real benchmarks in hw_emu beyond the smallest: sim time scales with record beats + slot beats + tail ops; fft_a / pci_bridge32_b are ~4 min per axis, the rest are hours.

## Watch out
- The VCK5000 cards are on `ssh vck` (hacc-gpu-u55c-01, booked by Mark), NOT on `ssh build` (build only). Shared NFS home, XRT 2.18 at /opt/xilinx/xrt; a 2022.2 xclbin loads fine.
- Server checkouts lack benchmarks and the tabulate submodule headers (Logger.h needs them): copy them over.
- `v++ -p` drops hw_emu boot/sim files into the cwd unless `--package.out_dir` is given.
- hw_emu "Simulation engine failed to start" was a full disk, not a mismatch.
