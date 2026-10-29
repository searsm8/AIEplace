---
title: "banked drain RTL co-sim: forwarding distance 4"
author: Searsm8
date: 2026-10-29
branch: pl_algo
touches:
  - pl/synth/bin_density_banked/banked_top.cpp
  - pl/synth/bin_density_banked/cosim/cosim.tcl
  - pl/synth/bin_density_banked/cosim/cosim_tb.cpp
  - pl/synth/bin_density_banked/cosim/cosim_top.cpp
  - pl/synth/bin_density_banked/cosim/run_depths.sh
  - pl/synth/bin_density_producer/impl_prj_div0/hls.app
  - pl/synth/bin_density_producer/impl_prj_div0/kernel.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.automg_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.autopilot_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_impl.xml
---

## Decided
- pl/synth/bin_density_banked/cosim/ proves drain_banks forwarding: depths 1-3 FAIL, 4-6 PASS in RTL (grid 64 and 60). BANKED_FORWARD_DEPTH stays 6 (2 spare). Matches the verbose schedule: read issued state 2 (t+1), deferred store issued by the NEXT iteration in state 5, lands t+6.
- cosim_top.cpp #includes ../banked_top.cpp and defines banked_cosim_top with bench-sized m_axi depth= (production depths would dump 1M-word memories).
## Tried and ruled out
- Depth 0 as the RTL negative control: since the deferred write-back, depth 0 fails already in C (slot 0 must be compared), and co-sim aborts when the C TB fails. Use depth 1.
- A bench where every producer has NO_DEPOSIT gaps: too weak to be sure of steady distance-k streams. Give producer 0 (highest pick priority) gap-free collision sections so its banks see unbroken distance-k.
## Watch out
- One run is ~21 min (csynth 11-13 + co-sim 8-10); 3 in parallel fit in 31 GB.
- In the tb, the cosim C pre-run prints PASS lines too; the RTL verdict is the second set plus 'C/RTL co-simulation finished'.
- pkill -f run_depths.sh from a wsl bash -c also kills the calling shell (its cmdline matches).
