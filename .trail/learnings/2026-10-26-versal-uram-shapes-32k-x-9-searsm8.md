---
title: "Versal URAM shapes: 32K x 9 width-sliced banks, not 4K x 72"
author: Searsm8
date: 2026-10-26
branch: pl_algo
touches:
  - host/sw_flat/packer/beat_packer.hpp
  - pl/src/modules/bin_density_CU.hpp
  - pl/src/modules/hpwl_gradient/slot_arrays.hpp
  - pl/src/modules/hpwl_gradient/tail_exchange.hpp
  - pl/src/modules/hpwl_gradient_CU.hpp
  - pl/synth/bin_density_producer/impl_prj_div0/hls.app
  - pl/synth/bin_density_producer/impl_prj_div0/kernel.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.automg_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.autopilot_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_impl.xml
---

## Decided
- Versal's URAM (`URAM288E5`, VC1902 included) is NOT fixed at 4K x 72: it also runs 8K x 36, 16K x 18 and 32K x 9 (AMD AM007, "UltraRAM Block"). Measured in hpwl_gradient_CU's routed checkpoint (2026-10-26): all 256 URAMs are 32K x 9 (READ_WIDTH_A = WRITE_WIDTH_A = 9).
- A 32K-row float bank (pos_URAM / grad_URAM) is 4 URAMs side by side in WIDTH, one byte of every float each, all 32K rows deep. There is no depth mux inside a bank, so `cascade_height = 1` in the HLS RTL is irrelevant to timing.
- Count: 8K floats per URAM per array whatever the shape. The packer's `URAM_WORDS` x `FLOATS_PER_WORD` (4K x 2, a 4K x 72 model) became one `URAM_FLOATS = 8192` (host/sw_flat/packer/beat_packer.hpp). bin_density_CU already uses 8K x 36 (`URAM_WORD_WIDTH = 36`).

## Tried and ruled out
- Supersedes the 2026-10-24 learning's claim of "8 URAMs whose 8:1 read mux is in fabric": wrong on both counts (4 URAMs, sliced by width).

## Watch out
- To see a memory's real shape, query the routed DCP: `get_cells -hier -filter {REF_NAME =~ URAM*}`, then READ_WIDTH_A / WRITE_WIDTH_A. HLS's csynth report says 1 URAM per bank, which undercounts.
- The address and write data of each bank fan out to 4 URAMs; with 32 banks x 2 arrays, that input fan-out (not a read mux) is what the 250 MHz failures into pos_URAM / grad_URAM are.
