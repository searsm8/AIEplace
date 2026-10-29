# HANDOFF #48: fold the 64-bank deposit into `bin_density_CU`

*2026-10-29. Branch `pl_algo`. Mark: "Skip the model; fold the banked design into bin_density_CU now."
Evidence for the banked design: [[REPORT_48_bank_address_multiplies_20261028.md]] (656 DSP),
[[REPORT_48_drain_timing_20261028.md]] (Vivado synth 2.928 ns at 3.33), [[REPORT_48_banked_cosim_20261029.md]]
(RTL bit-exact, forwarding distance 4, depth 6). Overview: [[EXPLAINER_48_bin_density_CU_overview_20261022.md]].*

## Two designs today

- **`bin_density_CU`** (`pl/src/modules/bin_density_CU.hpp`) is the real module: complete, verified, slow.
  One cell per cycle (`deposit_fast`), 16 banks (4×4), no FIFOs. Phases: `load_base` (fixed base into the
  banks), cells, `overflow_excess_sum` (the B' barrier), fillers, `deposit_slow` (wide footprints, 1 bin per
  cycle), drain to `rho` beats. Checked bit-exact by `check-bin-density` 44/44, `bin_density_CU_test`, and
  RTL co-sim (`pl/synth/bin_density_CU/cosim/`).
- **`banked_top`** (`pl/synth/bin_density_banked/banked_top.cpp`) is the fast deposit, a costing harness.
  8 producers (`pl/synth/bin_density_producer/producer.hpp`), 8 × 64 private FIFOs, 64 banks (8×8), the
  drain with forwarding. It has none of the phases above: no base, no overflow barrier, no fillers phase, no
  slow path. It takes 8 pre-split `SlotIn` slices on 8 m_axi ports, clears the URAM and writes raw words.

**The fold:** `bin_density_CU` keeps its interface, phases and golden, and its fast deposit becomes the
banked one. After it, `bin_density_CU` is the 8-producer, 64-bank module, and `check-bin-density` is the
proof.

## What has to be designed (decide, then write the decision in the report)

1. **Feeding the producers from the slot beats.** The CU reads `pos_x`/`pos_y`/`footprint_idx` as 16-slot
   beats. Proposal: one reader process takes a beat (3 bursts) and deals its 16 slots to the 8 producers,
   2 each (lanes 2p, 2p+1), into small per-producer input FIFOs. 16 slots per beat against 8 per cycle
   consumed, so one beat every 2 cycles is enough. NO_DEPOSIT slots (slow cells, empty) are dropped by the
   producer as today. Producer imbalance from the dealing pattern is accepted; measure it later, not now.
2. **Footprint table:** one BRAM copy per producer (≤ 128 entries, loaded once, scaled by `fixed_scale`).
3. **Phases around the dataflow region:** `load_base` → deposit region (cell beats) → `overflow_excess_sum`
   → deposit region again (filler beats) → `deposit_slow` → drain. The region ends when every bank has seen
   every producer's `last` (as `banked_top`), which is exactly the barrier B' needs. All of these move from
   16 to 64 banks: bank `8·(col%8) + row%8`, address at the fixed pitch `GRID_MAX/8`.
   `deposit_slow` stays one bin per cycle (macro tiles are the next item). Mind the URAM: the banks are
   written by the drain inside the dataflow region and by the phases outside it. Keep one owner per phase.
4. **The drain to `rho` beats** (16 bins per beat, column-major as today): which banks a beat touches under
   8×8, and keep it at full rate.
5. **`hls::stream` in host-compiled code.** `check-bin-density` and the tier-1 harness compile the CU with
   plain g++. `hls_stream.h` is header-only (`-I$XILINX_HLS/include`, as `banked_check.cpp` uses).
   Either add that include path to both builds, or keep a tiny shim. Tier 1 must still run without Vitis
   sourced, or the report must say clearly that it now needs the header path.

## Steps

1. Restructure `bin_density_CU.hpp` (reuse `producer.hpp`'s `produce_cell`, `banked_top`'s drain).
   → `make -C pl test` PASS (`bin_density_CU_test`, grids 128 and 1024).
2. → `make -C host/sw_flat check-bin-density` **44/44 bit-equal** rho and excess, shuffled order included.
3. C-synthesis (`pl/synth/bin_density_CU/`): II=1 in producers and drain, timing estimate met, DSP ≈ 656 +
   the CU's base/reduce/slow/drain; URAM 128. Record the numbers.
4. RTL co-sim of the folded CU: adapt `pl/synth/bin_density_CU/cosim/` (its bench must now collide across
   producers; reuse `pl/synth/bin_density_banked/cosim/cosim_tb.cpp`'s sections). Depths 1..6: 1-3 FAIL,
   4 or more PASS expected; the code keeps 6.
5. Vivado synthesis on the build server (`impl_check.tcl` pattern). The portal is Mark's to open: ask once,
   don't retry. Optional for done.

Keep `pl/synth/bin_density_banked/` as the costing record (it is cited by three reports). Don't delete it.
Out of scope: macro tiles ([[HANDOFF_48_macro_tiles_20261022.md]]), the `(ow·w)·oh` reorder, the force
gather, place-and-route.

## Done means

`make -C pl test` and `check-bin-density` 44/44 bit-equal; csynth numbers recorded; RTL co-sim of the
folded CU passes at depth 6, with the minimum known. tasks.md #48 and the Active Task updated, a
milestones.md line if it lands, this file `git mv`'d to `REPORT_48_fold_banked_into_cu_20261029.md`.
Commit file by file (never `git add -A`); push `pl_algo`.
