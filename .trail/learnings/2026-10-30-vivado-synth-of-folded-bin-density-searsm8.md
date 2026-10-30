---
title: "Vivado synth of folded bin_density_CU fails on shared URAM port mux (#48)"
author: Searsm8
date: 2026-10-30
branch: pl_algo
touches:
  - host/sw_flat/model/cycle_model.hpp
  - host/sw_flat/packer/beat_packer.hpp
  - pl/src/format/pin_record.hpp
  - pl/test/pin_record_test.cpp
  - host/sw_flat/schedule_trace.csv
  - pl/synth/bin_density_producer/impl_prj_div0/hls.app
  - pl/synth/bin_density_producer/impl_prj_div0/kernel.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.automg_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.autopilot_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_impl.xml
---

## Decided
- Vivado synth of the folded bin_density_CU (pl/synth/bin_density_CU/impl_check.tcl, build server, 2026-10-30) does NOT meet 3.33 ns: 4.062 ns, WNS -0.732, 504 failing endpoints, 294 K LUT / 687 DSP / 128 URAM.
- All 504 are one family: drain_banks' drain-loop done_reg -> bank-FIFO stall logic (pipeline enable, fanout 2,771) -> the CU-level port mux that shares rho_URAM between drain_banks, load_base, drain, overflow_excess_sum and deposit_slow -> URAM EN_A (each bank is two cascaded URAMs, so the address top bit is the enable).
- Next fix to try: style=frp on drain_banks' drain loop (no C change); fallback: register the priority pick one cycle earlier (re-sweep forwarding depth in co-sim).

## Tried and ruled out
- Nothing fixed yet. banked_top met the same chain at +0.40 ns only because its URAMs had a single owner; the shared port mux is what the fold added.

## Watch out
- csynth's LUT is ~1.75x Vivado's here (517 K vs 294 K); quote Vivado's.
- One impl_check run of the CU takes ~91 min. Run it from a scratch copy (~/impl48_cu) when the server checkout is on another session's branch.
- fail_buckets.tcl (.claude/2_ARTIFACTS/drain_timing_48_vivado/) works on any HLS synth checkpoint: open project.runs/bd_0_hls_inst_0_synth_1/bd_0_hls_inst_0.dcp.
