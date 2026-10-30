---
title: "bin_density_CU deck brief and bank-layout figures (#48)"
author: Searsm8
date: 2026-10-30
branch: pl_algo
touches:
  - host/sw_flat/model/cycle_model.hpp
  - host/sw_flat/packer/beat_packer.hpp
  - pl/src/format/pin_record.hpp
  - pl/src/modules/bin_density_CU.hpp
  - pl/test/pin_record_test.cpp
  - host/sw_flat/schedule_trace.csv
  - pl/synth/bin_density_producer/impl_prj_div0/hls.app
  - pl/synth/bin_density_producer/impl_prj_div0/kernel.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.automg_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.autopilot_exit
---

## Decided
- The bin_density_CU deck brief is .claude/1_REVIEW/reports/PLAN_48_bin_density_deck_20261030.md; figures and make_diagrams.py are in the untracked .claude/2_ARTIFACTS/bin_density_diagrams_48/. The generator asserts every layout claim on the full 1024 grid before drawing.
- Bank = low 3 bits of col and row, address = high 7 bits of each: pure wiring. The deck quotes Vivado's 294 K LUT beside csynth's 517 K, and Vivado's 4.062 ns (not met) beside csynth's 2.571 ns.

## Tried and ruled out
- Describing the window as 8x8: the real fast window is 4x4 (WINDOW_BINS); 8x8 is only the largest the layout could serve (9x9 repeats a bank).

## Watch out
- Interleaving removes conflicts inside one cell only; between cells the FIFOs absorb them.
- The barrier is the dataflow region's return, which comes before the slow list, not right before the overflow sum.
- 5.8 updates per cell and the 5.2-8.3x speedup are pre-fold model numbers, not measured.
- Slide 15 must be refreshed with the style=frp Vivado result.
- SendUserFile refuses UNC paths: copy files to a Windows folder first. Heredocs through wsl -e bash -c break; write scripts with the Write tool.
