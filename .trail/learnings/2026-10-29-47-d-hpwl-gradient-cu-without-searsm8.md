---
title: "#47 D: hpwl_gradient_CU without B3 passes; exchange tested around the CU with M2"
author: Searsm8
date: 2026-10-29
branch: pl_algo
touches:
  - host/sw_flat/README.md
  - host/sw_flat/borrowed_fusion.hpp
  - host/sw_flat/test/hpwl_gradient_xrt.cpp
  - host/sw_flat/test/record_stream_check.cpp
  - pl/src/modules/hpwl_gradient/borrowed_copies.hpp
  - pl/src/modules/hpwl_gradient/slot_arrays.hpp
  - pl/src/modules/hpwl_gradient_CU.hpp
  - pl/synth/hpwl_gradient_CU/cosim/cosim.tcl
  - pl/synth/hpwl_gradient_CU/cosim/cosim_tb.cpp
  - pl/synth/hpwl_gradient_CU/cosim/cosim_top.cpp
---

## Decided
- 2026-10-29 (Mark): hpwl_gradient_CU is the compute pass only. send_borrowed_pass, merge_borrowed_pass, the chunked branch and the borrowed_blocks/borrowed_ops ports are deleted; so are B3's borrowed_copies.hpp, borrowed_copies_test and load_slot_array. The exchange is iteration_update_CU's (STEP sends before the call, COMBINE merges after).
- Every harness that runs the CU (hpwl_gradient_CU_test, record_stream_check, hpwl_gradient_xrt, cosim_tb) does the exchange with the M2 golden: sw_flat::fused_send on dev.slot_images before the call, fused_merge on the grad image after. chunked_device_arrays leaves borrowed rows unfilled by default, so a missing send fails.
- Kernel args are now 16 (offset_table 7, exp_lut 9, grad 11, hpwl_total_fixed 15). Build H's 250 MHz xclbin no longer matches; a new hw build is needed before any card run.
## Tried and ruled out
- Making borrowed_fusion.hpp C++14 for the co-sim bench: not needed. beat_packer.hpp already uses structured bindings and g++ -std=c++14 accepts them with a warning.
## Watch out
- The packer and pin_record.hpp still emit B3's BorrowedBlockDesc / borrowed_ops and ChunkDesc first_borrowed_block/num_borrowers. Nothing on the device reads them. ChunkDesc is a shared device struct, so removing those fields is a separate change.
- pkill -f <pattern> inside a wsl bash -c also matches the bash -c command line itself and kills it.
