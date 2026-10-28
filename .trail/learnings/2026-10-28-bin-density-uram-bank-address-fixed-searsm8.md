---
title: "bin-density URAM bank address: fixed GRID_MAX pitch"
author: Searsm8
date: 2026-10-28
branch: pl_algo
touches:
  - pl/src/modules/bin_density_CU.hpp
  - pl/src/modules/hpwl_gradient/wa_gradient.hpp
  - pl/src/modules/hpwl_gradient/wa_sums.hpp
  - pl/synth/bin_density_banked/banked_check.cpp
  - pl/synth/bin_density_banked/banked_top.cpp
  - pl/synth/bin_density_producer/impl_prj_div0/hls.app
  - pl/synth/bin_density_producer/impl_prj_div0/kernel.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.automg_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.autopilot_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_impl.xml
---

## Decided
- Bank address is (col/S)*(GRID_MAX/S) + row/S (constant power-of-two pitch, a shift), not (col/S)*(grid_bins/S): the run-time pitch cost one DSP per bank per producer. Applied in pl/src/modules/bin_density_CU.hpp (address_of, BANK_PITCH) and pl/synth/bin_density_banked/banked_top.cpp (#48, 2026-10-28).
- On grids below GRID_MAX the unused words are simply skipped; load_base/drain loop counts are unchanged, and overflow_excess_sum walks (tile_col, tile_row) with counters so it neither divides nor visits unloaded words.

## Tried and ruled out
- Per-cell column bases (2 multiplies per cell) were the alternative; not needed, since the fixed pitch costs no cycles on small grids.

## Watch out
- A check run only at grid == GRID_MAX cannot see an address-pitch bug: the two layouts coincide there. banked_check now runs at 1024, 512 and 336; the old run-time pitch passes 1024 and fails the other two.
- The RTL co-sim (cosim/, GRID_MAX=64, BINS=64) also runs at grid == GRID_MAX, so it proves only the forwarding depth, not the layout.
- overflow_excess_sum must not loop a < tiles over flat addresses any more: with the fixed pitch the live words are not contiguous.
