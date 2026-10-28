---
title: "Bin index by multiply (#48): drift, forwarding, how it was measured"
author: Searsm8
date: 2026-10-28
branch: pl_algo
touches:
  - CLAUDE.md
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
- Mark, 2026-10-27: the bin index is (int)(edge * inv_bin_w), inv_bin_w = 1.0f / bin_w computed once on the host, in sw_flat ([sw_flat] bin_index = "multiply") and bin_density_CU / the producer. A registered divergence from XPlace (CLAUDE.md); sw_only still divides. Step 6's golden is pl+Bprime+fixed+mul.
## Tried and ruled out
- Expecting the forwarding depth to change: removing the dividers shortened deposit_fast_loop 117 -> 104 stages, but the dividers sit BEFORE the URAM read, so the read->write distance stayed 5 (RTL: depth 0 and 4 FAIL, 5 and 6 PASS).
## Watch out
- Multiply and divide indices are not always equal: 0 of 4.4 G on fft_a, pci_bridge32_b, adaptec1, mms/adaptec1, but 497,831 of 30.8 G (1.6e-5) on bigblue4 at grid 2048, always the multiply one bin higher. A synthetic test (producer_check, 0 of 200 K) does not show this; count on bigblue4.
- sum_order_drift.py: put the designs BEFORE --variants (nargs=+ swallows them).
- To count differing indices, instrument a copy of density.cpp outside the repo and link it with build/*.o; do not rebuild build/sw_flat_place while a drift run is using it.
