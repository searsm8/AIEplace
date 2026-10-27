---
title: "hpwl_gradient_CU timing at 250 MHz: URAM latency, checkpoint capture, build noise"
author: Searsm8
date: 2026-10-24
branch: pl_algo
touches:
  - pl/src/modules/bin_density_CU.hpp
  - pl/synth/bin_density_producer/impl_prj_div0/hls.app
  - pl/synth/bin_density_producer/impl_prj_div0/kernel.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.automg_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.autopilot_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_impl.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_syn.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/__ctype_info__.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/a.g
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/a.g.0.bc
---

## Decided
- To classify routed timing failures, v++ must keep the checkpoint: link with `--save-temps --advanced.param compiler.skipTimingCheckAndFrequencyScaling=1`. Without `--save-temps` the impl_1 dir (and `top_wrapper_routed.dcp`) is deleted; without the skip param a failing run stops before writing an xsa. Routed DCP lands in `_x/link/vivado/vpl/prj/prj.runs/impl_1/`.
- Classifier scripts: `build:~/timing_tools/{classify_paths.tcl,group_paths.py}`. Opening the full-platform DCP takes a while; run it in tmux.

## Tried and ruled out
- `BIND_STORAGE ... impl=uram latency=2` on pos_URAM and grad_URAM (hpwl_gradient_CU, 2026-10-22): C-synth II=1 everywhere, sw_emu 4/4 bit-exact, but routed WNS at 250 MHz got WORSE: -0.947 ns / 14,702 endpoints vs -0.378 ns / 11,114 for the unchanged source built the same day.
- `latency=3` on pos_URAM breaks `fold_macros` to II=2: pos_URAM doubles as fold's accumulator, and read+fadd+write no longer fits HAZARD_DISTANCE=4.

## Watch out
- HLS emits each 32K x 32 URAM bank with `cascade_height = 1`: 8 URAMs whose 8:1 read mux is in fabric. latency=2 adds a `retiming_backward` output reg; latency=3 also registers the address.
- Same source, same 250 MHz, two builds: WNS -0.729 (10-19) vs -0.378 (10-22). Build-to-build noise is ~0.35 ns, so one build per variant cannot rank small changes.
- Another session's sweeping commit can pick up your uncommitted edits (ded5e04 took the latency change). Check `git log -S` before assuming an edit is lost.
