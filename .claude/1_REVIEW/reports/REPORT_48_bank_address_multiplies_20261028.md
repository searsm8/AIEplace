# REPORT #48: bank address at a fixed pitch, no multiplies

*2026-10-28. Branch `pl_algo`. Mark: "the next #48" after [[REPORT_48_multiply_bin_index_20261027.md]]. Started
as the handoff of the same name (2346f78); this is that file, finished. The original brief is folded below.*

## Result

- The bank word address is now `(col/S) * (GRID_MAX/S) + row/S`: the pitch is a constant power of two, so
  the multiply becomes wiring. Done in `pl/synth/bin_density_banked/banked_top.cpp` (S = 8, `b1091ce`)
  and `pl/src/modules/bin_density_CU.hpp` (S = 4, `address_of(col, row)`, `BANK_PITCH`, `cc0f32a`).
- **Banked deposit: 1,168 -> 656 DSP; per producer 146 -> 82** (exactly the 64 bank lanes). Beside
  `hpwl_gradient_CU`'s 708 that is **1,364 of the VC1902's 1,968 DSP (69 %)**, down from 1,876 (95 %).
- `bin_density_CU` (16 banks): 121 -> 102 DSP (`deposit_fast` 98 -> 82, `deposit_slow` 9 -> 8,
  `load_base` 1 -> 0, `drain` 4 -> 3).
- Bit-exact everywhere: `banked_check` 0 words differ at grids 1024, 512, 336; `check-bin-density` **44/44**;
  `make -C pl test` PASS (`bin_density_CU_test` runs grids 128 and 1024).
- RTL co-sim forwarding distance **still 5**: depths 0 and 4 FAIL, 5 and 6 PASS. `BIN_FORWARD_DEPTH = 6` stands.

## Why fixed pitch, not per-cell column bases

The fixed pitch costs nothing on a smaller grid. The URAM was already sized `(GRID_MAX/S)^2` and is allocated
either way. No loop got longer: `load_base` and `drain` still run `grid_bins^2 / 4` quads, and the overflow
reduce still runs `(grid_bins/4)^2` tiles. The reduce used to walk a flat address `a < tiles`, but below
GRID_MAX the live words are no longer contiguous, so it now steps `(tile_col, tile_row)` counters (no divide)
and never touches the unloaded words. `banked_top`'s clear and write_out always walked every word. So the
alternative (2 multiplies per cell, then a per-bank select) has nothing to gain.

## Measurements

C-synthesis, Vitis HLS 2022.2, xcvc1902, 3.33 ns, both run fresh on 2026-10-28 (the 10-22 banked report predated
the bin-index multiply, which is why the handoff's 1,136 / 142 read lower). Reports:
`.claude/2_ARTIFACTS/scratch_48/csynth_bank_pitch_20261028/{before,after}_{banked,CU}/`.

| design | module | DSP before | DSP after | FF before -> after | LUT before -> after |
|---|---|---|---|---|---|
| banked (8 prod., 64 banks) | banked_top | 1,168 | **656** | 278,135 -> 269,559 | 454,003 -> 454,115 |
| | one producer_process | 146 | **82** | 24,512 -> 23,440 | 39,408 -> 39,422 |
| | drain_banks | 0 | 0 | 49,074 | 63,148 |
| bin_density_CU (16 banks) | bin_density_CU_top | 121 | **102** | 79,121 -> 78,784 | 100,574 -> 100,338 |
| | deposit_fast | 98 | 82 | 23,672 -> 23,243 | 25,778 -> 25,505 |

LUT is flat: csynth's LUT for a run-time multiply mapped to a DSP is small. Vivado numbers were not taken
(the build server portal is Mark's to open; optional for this item).

Timing estimates, unchanged where they matter: `drain_banks` -2.14 ns before and after (the next item). The
CU's `reduce` now estimates -0.02 ns (was +0.55 slack in its pipeline; iteration latency 5 -> 6) because of the
tile counters. That is csynth's estimate on a 65 K-cycle loop that runs once per evaluation; confirm in Vivado.

Checks:

| check | result | artifact |
|---|---|---|
| `banked_check` (now grids 1024, 512, 336; words past the grid must be 0) | 0 of 1,048,576 differ on all three | — |
| mutant: the old run-time pitch in banked_top | **caught**: 1024 PASS (layouts coincide), 512 FAIL 388,823, 336 FAIL 185,471 | — |
| `make -C host/sw_flat check-bin-density` | **44 of 44 PASS** | `scratch_48/bin_density_check_44_fixed_pitch_20261028.txt` |
| `make -C pl test` | 4/4 harnesses PASS | — |
| `cosim/run_depths.sh` | depth 0 FAIL (79 bins), 4 FAIL (32), 5 PASS, 6 PASS | `scratch_48/cosim_depths_fixed_pitch_20261028.txt` |

The co-sim runs at `PL_DENSITY_GRID_MAX = 64` with a 64-bin grid, where both layouts coincide: it proves
the forwarding depth, not the layout. The layout is proven by the C checks at grids below GRID_MAX.

## Not done / follow-ups

- `pl/synth/bin_density_producer/producer_top.cpp` (the round-2 single-producer costing) still computes the
  run-time-pitch address in its output. Left as is: out of this brief, and its numbers are historical.
- `run_depths.sh` is not executable (run it with `bash run_depths.sh`).
- Out of scope, untouched: the drain's -2.14 ns, the 64-bank co-sim, the `(ow·w)·oh` reorder, macro tiles.

<details><summary>Original brief (HANDOFF, 2026-10-28)</summary>

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
</details>
Meow.
