---
title: sw_flat loop rotated to the device shape; B4 exchange lives in iteration_update
author: Searsm8
date: 2026-10-28
branch: pl_algo
touches:
  - host/sw_flat/README.md
  - host/sw_flat/model/bank_fifo_model.cpp
  - host/sw_flat/model/lane_model.cpp
  - host/sw_flat/placer/placer.cpp
  - host/sw_flat/placer/placer.hpp
  - host/sw_flat/placer/record_stream.cpp
  - host/sw_flat/placer/step.cpp
  - host/sw_flat/test/bin_density_check.cpp
  - host/sw_flat/test/gradient_bench.cpp
  - host/sw_flat/test/hpwl_gradient_xrt.cpp
---

## Decided
- Mark, 2026-10-27: the top-level loop in sw and hw is initialization(); while (running) { compute_hpwl_partials(); compute_electric_fields(); iteration_update(); }. sw_flat's Placer::run now has exactly that shape (sw_only's loop rotated; same float ops in the same order, check-placer still bit-identical).
- iteration_update does #47 B4's exchange in sw_flat: merge_borrowed_gradients first, then combine / BB / record / schedule / stop test, then take_step, whose last act is send_borrowed_positions. Under wa_sums = tree the record lanes read positions from their own chunk's slots (lane_node_slot: borrowed copies, a macro pin reads its macro), so the send is load-bearing.
- The hardware loop is a SKELETON only (Mark chose that): host/sw_flat/device_loop.hpp, same call names as Placer. The kernel top (pl/src/top.cpp) is not written.

## Tried and ruled out
- Keeping perform_iteration as a wrapper: its end state (combined gradient at the new point) no longer exists in the rotated loop. Tests use Placer::run_until(n) instead, which stops once iteration n has its gradient (positions and the last deposit agree).

## Watch out
- After run_until, owner gradients do NOT include borrowed contributions (the merge is in iteration_update). Device checks comparing against hpwl_gradient_CU (still B3, merges inside the CU) must call merge_borrowed_gradients() first; record_stream_check and hpwl_gradient_xrt do.
- check-placer runs the default config, which has no record stream: it cannot see the send. The stream path was A/B'd by hand (bigblue3, mms/bigblue3 at K=2, traces identical; a send-before-step mutant fails at iteration 1). There is no standing gate for the stream configs.
- lane_model / bank_fifo_model sample at the loop top; since the rotation they can take one extra final sample when the placer's own stop test ends the run.
