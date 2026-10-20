---
title: "bin_density_CU banking, forwarding, and HLS co-sim traps"
author: Searsm8
date: 2026-10-20
branch: pl_algo
touches:
  - pl/synth/bin_density_CU/cosim/run_depths.sh
---

## Decided
- bin_density_CU's grid is 16 URAM banks, bank = 4*(col%4) + row%4, address (col/4, row/4): any <= 4x4 footprint window hits 16 distinct banks, so one cell per cycle (II=1). Footprints wider than 2.99 bins go to a slow list.
- Per-bank forwarding of the last BIN_FORWARD_DEPTH writes (default 6) makes `#pragma HLS DEPENDENCE ... inter false` safe. Scheduled read-to-write distance was 5 (read stage 112, write 117).
- Fixed-point F per design from the total-area bound (densrec::rho_frac_bits): no bin can overflow.

## Tried and ruled out
- Validating forwarding depth in C simulation: impossible, C runs iterations sequentially and passes at any depth. Only RTL co-sim catches it (pl/synth/bin_density_CU/cosim/run_depths.sh: depth 0 and 4 fail, 5 and 6 pass).

## Watch out
- Never name an HLS function parameter `bins` (or any SystemVerilog keyword): it becomes an RTL port and xsim fails to compile the wrapper. Applies to sub-functions too, not just the top.
- HLS cannot prove stores go to different partitions when the bank index is computed (bank_of(col, row + k)); write it in a form it can see (4*(col%4) + k) or the loop drops to II=4.
- Re-run run_depths.sh whenever deposit_fast changes: the needed depth follows the schedule.
