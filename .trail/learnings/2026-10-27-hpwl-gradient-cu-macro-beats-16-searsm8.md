---
title: "hpwl_gradient_CU macro beats: 16-pin refresh/fold, HLS port and adder limits"
author: Searsm8
date: 2026-10-27
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
- hpwl_gradient_CU's macro-pin refresh and fold run on 16-pin "macro beats" (one macro per beat), lane j's pin in bank j or j+16, so each bank sees a 2:1 mux instead of a 16-way crossbar. That costs 8-40 % more beats than a crossbar but every MMS design still gets ~11-14x fewer macro-pin cycles (bigblue2 3.8x: 958 small macros, one per beat).
- The fold's per-beat sum is `macro_pin_tree` in slot_arrays.hpp; sw_flat's golden (record_stream.cpp) calls the same function, which is what keeps golden and CU bit-exact after a summation-order change.
- Fold beats of one macro are spaced MACRO_FOLD_DISTANCE = 8 (not HAZARD_DISTANCE 4) so the running-sum add pipelines; padding cost is a few hundred SKIP beats at most (bigblue3 315).

## Tried and ruled out
- One 128-byte MacroBeat per m_axi read: HLS issued two 512-bit reads per iteration (II=2) even with the port's `max_widen_bitwidth=1024` and `config_interface -m_axi_max_widen_bitwidth 1024`. Split into a 64-byte MacroBeat plus a parallel SlotBeat offsets array on a second port (gmem2, idle during refresh).
- Fold at distance 4: HLS chained URAM read + bank mux + 1-cycle add in one cycle, 4.26 ns estimated at a 2.5 ns target.
- `impl=fulldsp` for that add: estimate stayed 2.65 ns at latency 3 and 4.

## Watch out
- `primitivedsp` fadd accepts latency 0..3 only (latency 4 is a synthesis error).
- Before trusting a v++ sw_emu "12 s" build, check the xclbin is newer than the sources.
- hw_emu "Failed to connect to device process" = local disk full again; per-run scratch piles up in host/sw_flat/build/.run/<pid> (24 GB this time); delete dirs whose pid is dead.
