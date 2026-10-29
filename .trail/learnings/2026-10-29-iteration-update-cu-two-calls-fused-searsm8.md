---
title: "iteration_update_CU: two calls, fused borrowed exchange, device sum order"
author: Searsm8
date: 2026-10-29
branch: pl_algo
touches:
  - host/sw_flat/placer/step.cpp
  - pl/src/modules/iteration_update/slot_reduce.hpp
  - pl/src/modules/iteration_update_CU.hpp
  - pl/test/iteration_update_CU_test.cpp
  - host/sw_flat/schedule_trace.csv
  - pl/synth/bin_density_producer/impl_prj_div0/hls.app
  - pl/synth/bin_density_producer/impl_prj_div0/kernel.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.automg_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.autopilot_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_impl.xml
---

## Decided
- iteration_update_CU is two calls per iteration (UPDATE_COMBINE, then the host's schedule, then UPDATE_STEP): the BB step needs a sum over every slot before any slot can step. The host's [current, next] pointer swap before STEP replaces cache_state.
- #47 B4 = A is built inside iteration_update_CU as one host-built 64-bit op list per owner (pin_record.hpp FUSED_*): GROUPs of <= 4 entries in distinct borrower banks, BEAT half-rows, a ring per borrower, borrowers served in turn. Not stream processes: in DATAFLOW the merge would read grad DDR while the update writes it.
- Device sums use slot_reduce.hpp's order (16-lane tree, 16 rotating partials, final tree). sw_flat models it with [sw_flat] reductions = "lanes" (default off), so check-iteration-update is bit-exact over whole iterations.
- The force gather is an input array (density_force) until #48 builds one.

## Tried and ruled out
- Rotating partials as partial[beat % 16]: HLS sees a distance-1 carried dependence (step_beats II=2). A 16-deep shift register (add into acc[15], shift in at acc[0]) gives the same sums at II=1.

## Watch out
- Dropping ONE send BEAT is not a working mutant: the ring keeps the half-row and the next BEAT writes it; only a block's last half-row is lost, often padding. Drop all of one borrower's BEATs instead.
- Small bare-cut synthetic designs give owners up to 19 borrowers; FUSED_MAX_BORROWERS is 4 and fused_owner_ops exits. Real designs at 1 M slots have K <= 3.
- `pkill -f <pattern>` inside `wsl -e bash -c "..."` matches its own command line and kills the shell (exit 15).
