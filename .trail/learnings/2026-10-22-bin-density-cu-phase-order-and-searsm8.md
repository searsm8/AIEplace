---
title: bin_density_CU phase order and DDR transfer rate
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
- Phase order is load_base -> load_table -> cells (fast, slow) -> overflow_excess_sum -> fillers (fast, slow) -> drain. Deposits are split in two only because XPlace's overflow excludes fillers: the excess must be summed between them.
- The fast-path bank loop is inverted: each bank b finds the window column/row it owns, dc = (b/4 - col_lo) & 3, dr = (b%4 - row_lo) & 3. Non-hit banks still read address 0 and push an invalid forwarding entry.
- overflow_excess_sum reads one address across all 16 banks = one aligned 4x4 tile, 16 bins/cycle, integer adder tree.

## Tried and ruled out
- Nothing tried this session (read-only walkthrough).

## Watch out
- load_base and drain move only 4 bins/cycle: a DDR beat is 16 rows of one column, so only 4 banks share an address per beat. load_base also re-reads the same fixed_base_DDR beat on 4 consecutive iterations; check whether HLS merges them when the FFT-facing DDR format is revisited.
- FOOTPRINT_TABLE_MAX = 4096 is assumed, not measured on the 44 designs; idx is not bounds-checked on device.
