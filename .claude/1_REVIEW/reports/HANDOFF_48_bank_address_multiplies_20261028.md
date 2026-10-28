# HANDOFF #48: remove the bank-address multiplies from the bin-density producers

*2026-10-28. Branch `pl_algo`. Mark: "the next #48" after [[REPORT_48_multiply_bin_index_20261027.md]].
Context: [[EXPLAINER_48_bin_density_CU_overview_20261022.md]] slide 5/6, [[PLAN_48_bin_density_lanes_20261020.md]].*

## The problem

Every bin lives in one URAM bank, at one word inside that bank:

- bank    = `8*(col%8) + row%8` (banked design, 64 banks) or `4*(col%4) + row%4` (`bin_density_CU`, 16 banks)
- address = `(col/S) * (grid_bins/S) + row/S`, S = bank side (8 or 4)

`col/S` and `row/S` are shifts (S is a power of two). But `grid_bins/S` is a **run-time value**, so the
product is a real integer multiply, and it sits inside the per-bank `fill` loop that is fully unrolled:
one DSP per bank, per producer. C-synthesis of `pl/synth/bin_density_banked/banked_top.cpp`:
**64 of each producer's ~142 DSP**, ~512 of the deposit's 1,136. With them the deposit plus
`hpwl_gradient_CU` is ~94% of the chip's DSPs; without them ~67%.

Where it is: `banked_top.cpp` `producer_process` (the `to_bank[b].write(BankUpdate{...})` line), and
`pl/src/modules/bin_density_CU.hpp` `address_of` (16 banks, same form).

## The fix (recommended: fixed pitch)

The URAM banks are already sized for the largest grid: `BANK_WORDS = (GRID_MAX/S)^2`. So lay the words
out with the **fixed** pitch `GRID_MAX/S` instead of `grid_bins/S`:

    address = (col/S) * (GRID_MAX/S) + row/S   =   ((col >> log2 S) << log2(GRID_MAX/S)) | (row >> log2 S)

A constant power-of-two multiply is wiring: **0 DSP, ~0 LUT**. On a grid smaller than GRID_MAX some words go
unused, but they are allocated anyway. Nothing about the result changes (the address is private to the CU),
so bit-exactness is untouched; only the loops that walk the URAM (load_base, overflow reduce, drain,
`write_out`) must use the same layout.

Alternative (the explainer's): keep the run-time pitch, compute the column base once per cell (a 4-wide
window spans at most 2 values of `col/S`, so 2 multiplies per cell), and let each bank pick. ~2 DSP per
producer instead of 64. Only worth it if the fixed pitch turns out to cost something, e.g. if a walk over a
small grid gets slower; check that before choosing.

## Steps

1. `banked_top.cpp` + `banked_check.cpp`: fixed-pitch address; `write_out`/the check's unpacking to match.
   → `banked_check` 0 bins differ (and its mutant still caught).
2. C-synthesis (`synth_check.tcl`, local): DSP per producer and total. → expect ~78 per producer, ~616 total.
   State the before/after in the report from the csynth reports, not from memory.
3. `bin_density_CU.hpp` `address_of` and every URAM walk (load_base, `overflow_excess_sum`, drain): same
   change, so the 16-bank CU and the 64-bank model agree. → `make -C pl test`, `check-bin-density` 44/44.
4. RTL co-sim of the 16-bank CU (`pl/synth/bin_density_CU/cosim/run_depths.sh`): the pipeline may shorten;
   confirm the forwarding distance (depths 0/4 FAIL, 5/6 PASS today) and say if it moved.
5. Vivado numbers need the build server (portal opened by Mark: ask, don't retry). Optional for this item.

Out of scope (next items, do not start): the drain's −2.1 ns timing, the 64-bank co-sim, the
`(ow·w)·oh` reorder, macro tiles.

## Done means

banked_check + check-bin-density 44/44 + `make -C pl test` PASS; csynth DSP before/after recorded; co-sim
forwarding re-proven; tasks.md #48 and the Active Task line updated; this file `git mv`'d to `REPORT_...`.
Commit file by file (never `git add -A`: synth project dirs are untracked), don't push.
