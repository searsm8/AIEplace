---
title: XPlace density scatter/gather internals and GPU cost across 44 designs
author: Searsm8
date: 2026-10-22
branch: pl_algo
touches:
  - pl/synth/bin_density_producer/impl_prj_div0/hls.app
  - pl/synth/bin_density_producer/impl_prj_div0/kernel.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.automg_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.autopilot_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_impl.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_syn.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/__ctype_info__.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/a.g
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/a.g.0.bc
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/a.g.1.bc
---

## Decided
- XPlace's deterministic density scatter = global uint64 atomicAdd in 32.32 fixed point (density_map_cuda_kernel.cu:79-118), 2x2 threads/node; sort_node_map is by AREA (warp balance), not by bin. Same integer-commutativity argument as bin_density_CU.
- Whole-GP profile of all 44 designs: scatter+gather share of XPlace GP wall = ISPD2005 median 20%, ISPD2015 3.6%, MMS 54% (REPORT_48_xplace_scatter_gather_suite_20261020.md). Data + scripts: .claude/2_ARTIFACTS/xplace_profile_suite/.
- MMS cost is the deterministic gather's one-thread-per-node loop over movable macros (adaptec2 largest macro 150K bins: 11.9 ms/gather launch in phase 1 vs 0.097 ms in phase 2).
## Tried and ruled out
- Profiling a fixed iteration window: replaced by wrapping global_placement_main (CUDA-only profiler) so whole runs incl. MMS phase 1 are counted.
## Watch out
- Our slow-path list (footprint > 2.99 bins) is much longer than XPlace's macro count (adaptec2 956 vs 127); at 1 bin/cycle it costs up to 8.8x the cell time on MMS phase 1. Macros need ~64 bins/cycle.
- Golden overlap of an interior bin is (col*bw+bw)-col*bw, NOT exactly bw in float, so a constant-interior macro shortcut is not bit-exact without a golden change.
- System python3 is 3.8 (no statistics.correlation); use ~/anaconda3/bin/python.
