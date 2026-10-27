# REPORT #45 — macro-pin refresh and fold at 16 pins per cycle (2026-10-27)

*Mark, 10-27: "build 16-wide macro-pin beats. Write the math and the sw_flat golden first, then the packer
and the HLS." Context: [[REPORT_45_hpwl_gradient_timing_20261027.md]].*

## Result
- **Refresh and fold now handle a 16-pin beat per cycle instead of one pin.** Macro-pin cycles per call, each
  pass: adaptec1 16,265 → 1,203; bigblue4 62,662 → 4,956; newblue7 66,114 → 4,713; bigblue2 5,459 → 1,450
  (958 small macros, one macro per beat). About 11–14× on 15 of the 16 MMS designs, 3.8× on bigblue2.
  ISPD 2005/2015 have no movable macros and are unchanged.
- **Bit-exact throughout:** golden (`record_stream.cpp`) vs CU 6/6 (`make -C host/sw_flat check-record-stream`,
  incl. mms/adaptec1 and mms/bigblue4, 3 chunks); tier 1 4/4; packer check (MB1–MB3) on all 16 MMS designs;
  sw_emu 4/4 (synthetic, fft_a, mms/adaptec1, bigblue4); **hw_emu 2/2** (synthetic: 16 chunks, 5 macros; fft_a).
  `make -C host/sw_flat check-placer` (sw_flat vs sw_only, default config) still bit-identical.
- **C-synth at 2.5 ns: every loop II=1.** `fold_macros` estimated slack −0.01 ns (was −1.80, the CU's worst
  module); the CU's worst is now −1.06 ns (`wa_gradient`). Cost +29 DSP, ~+8 K LUT and FF (estimates).
- **Not yet routed.** The fold held the 250 MHz WNS in build F; whether this closes it needs a link build.

## Why
A movable macro's pins are their own slots (`pos[pin] = pos[macro] + offset`), so 16 pins of one macro can sit
in one beat on distinct banks. Before, refresh and fold handled **one pin per cycle** each while the beat loop
handled 16: 5–25 % of the compute pass on MMS (adaptec1 25 %, median ~11 %). The fold also held the 250 MHz
WNS (−0.178 ns) and with refresh ~28 % of TNS. Census tools: `.claude/2_ARTIFACTS/scratch_45/macro_pins/`.

## The math (per chunk, per axis)
Macro *m* has pins *p ∈ P(m)* with offsets *o_p*. A **macro beat** *t* belongs to one macro *m(t)* and holds up
to 16 of its pins; lane *j* holds a pin whose slot is in bank *j* or *j + 16*, or nothing.
- **Refresh:** every valid lane, `pos[slot_j] = pos[m(t)] + o_j`. One add per pin; no order to fix.
- **Fold:** beat *t*'s partial `s_t = tree16(g_0 … g_15)`, `g_j = grad[slot_j]`, `+0.0f` for an empty lane, tree
  `((g0+g1)+(g2+g3)) + ((g4+g5)+(g6+g7))` and likewise 8..15, then the halves (`macro_pin_tree`,
  `slot_arrays.hpp`). Macro *m*'s beats in list order: `S_1 = s_{t_1}`, `S_k = S_{k−1} + s_{t_k}`, `grad[m] = S_n`.
- **The summation order changed** from the old pin-by-pin chain, so results differ from before at float
  rounding; the golden calls the same `macro_pin_tree` and reads the same beat list, so golden and CU agree
  bit for bit.

## Packer rules (replace M1–M3)
- **MB1** every macro pin in exactly one lane; one macro per beat; lane *j*'s pin in bank *j* or *j+16*.
- **MB2** no macro within `MACRO_FOLD_DISTANCE` (8) beats of itself; SKIP beats pad.
- **MB3** FIRST on a macro's first beat, LAST on its last.
- Packing: each macro's pins bucketed by `bank % 16`; beat *i* takes the *i*-th pin of each bucket, so a macro
  needs as many beats as its fullest bucket. Then the existing cooldown scheduler at beat granularity.

## Device format and the two design turns
- **`MacroBeat`, 64 bytes:** `uint16 pin_row[16]` (bit 15 = bank *j+16*, bits 0–14 = row; `ROWS_PER_BANK` 32 K
  fits), `int32 macro_slot`, `uint16 lane_valid`, `uint16 flags`. **Offsets are a parallel `SlotBeat` array**
  (`macro_offsets`, new kernel argument on `gmem2`, idle during refresh). A single 128-byte beat took two bus
  reads per beat (II=2) even with `max_widen_bitwidth=1024` and a solution-wide 1024 widen cap. The split
  also halves the fold's DDR traffic: it never reads offsets.
- **Fold distance 8, not `HAZARD_DISTANCE` 4.** At 4, the running sum's URAM read, bank mux and add were
  chained in one cycle (4.26 ns estimated vs build F's 3.62). At 8 the add is `primitivedsp` latency 3
  (`fulldsp` stayed at 2.65 ns whatever its latency; `primitivedsp` maxes at 3). Extra SKIP beats: bigblue3 315,
  newblue6 133, bigblue4 64, the rest 0.
- **Bank mux, not crossbar:** pinning lane *j* to banks {*j*, *j+16*} costs 8–40 % more beats than a full
  crossbar (adaptec1 1,203 vs 1,039) but each bank sees a 2:1 choice instead of a 16-way one.
- **No operator sharing:** the fold's 16 adders (15 tree + 1 accumulate) sit in its own HLS module.

## Files
`pl/src/format/pin_record.hpp` (MacroBeat, MACRO_FOLD_DISTANCE, ChunkDesc `macro_beat_offset / num_macro_beats`),
`pl/src/modules/hpwl_gradient/slot_arrays.hpp` (`macro_pin_tree`, refresh, fold), `hpwl_gradient_CU.hpp`,
`pl/synth/hpwl_gradient_CU/` (top + cosim: `macro_offsets` port), `host/sw_flat/packer/beat_packer.hpp`
(`MacroBeatPlan`, `schedule_macro_pins`, `macro_beat_records`, check), `host/sw_flat/placer/` (golden fold),
`host/sw_flat/model/cycle_model.hpp`, tests (`pin_record_test`, `hpwl_gradient_CU_test`, `record_stream_check`,
`hpwl_gradient_xrt`, `cosim_tb`).

## Next
- A routed build: 230 MHz to confirm no regression, and 250 MHz to see what the fold change buys.
- Card run (new xclbin interface: the 230 MHz xclbin from `b0e2290` does not match this host).
- A placer run on an MMS design: trajectory within float noise of before (summation order only).
