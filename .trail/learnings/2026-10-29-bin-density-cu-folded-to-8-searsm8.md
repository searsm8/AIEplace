---
title: "bin_density_CU folded to 8 producers x 64 banks (#48)"
author: Searsm8
date: 2026-10-29
branch: pl_algo
touches:
  - host/sw_flat/model/cycle_model.hpp
  - host/sw_flat/packer/beat_packer.hpp
  - pl/src/format/density_footprint.hpp
  - pl/src/format/pin_record.hpp
  - pl/src/modules/bin_density_CU.hpp
  - pl/synth/bin_density_CU/cosim/cosim.tcl
  - pl/synth/bin_density_CU/cosim/cosim_tb.cpp
  - pl/synth/bin_density_CU/cosim/cosim_top.cpp
  - pl/synth/bin_density_CU/cosim/run_depths.sh
  - pl/synth/bin_density_CU/synth_check.tcl
---

## Decided
- bin_density_CU (pl/src/modules) is the 8-producer, 64-bank deposit since 2026-10-29; pl/synth/bin_density_banked stays as the costing record, not the source.
- Producers are fed by deal_slots: one 16-slot beat per cycle, lanes 2p,2p+1 to producer p as one SlotPair FIFO word. Each producer gets its own 128-entry footprint table through a stream (FOOTPRINT_TABLE_MAX is 128 now, measured 4..105); deposit_slow reads its footprint from DDR and scales it with the same single multiply.
- rho_URAM is ram_t2p: load_base and the rho drain move a whole beat per cycle (two words in each of the 8 banks of bank column col%8). Write them with CONSTANT bank indices (read/write all 64, select the bank column afterwards), or HLS sees 16 dynamic accesses per bank.
- Host g++ builds get hls::stream from a std::deque shim inside bin_density_CU.hpp, used only when <hls_stream.h> is not on the include path (__has_include), so pl tier 1 and check-bin-density need no Vitis.
- Forwarding distance in the folded CU is 4 (co-sim depths 1-3 FAIL, 4-6 PASS); BIN_FORWARD_DEPTH stays 6.

## Tried and ruled out
- Nothing failed. A CU-level shared footprint table read by 8 dataflow processes was avoided on purpose (multiple readers of one array in a DATAFLOW region).

## Watch out
- csynth: 689 DSP, 517 K LUT (57 %) for the CU alone; LUT, not DSP, is the resource to watch beside hpwl_gradient_CU.
- The co-sim bench must lay per-producer lists out by lane (producer p's j-th cell at beat j/2, lane 2p+j%2) to make bank words collide across producers; slot-order collisions alone mostly hit one producer.
- One co-sim run of the CU is ~28 min wall (csynth ~13 + sim ~12); csynth alone ~17 min.
- Inside wsl -e bash -c "...", backticks in a heredoc are still command-substituted: write files with the Write tool.
