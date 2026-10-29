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

## Routed (build G, 2026-10-28)
Build G is `6d3961c`, built with build F's scripts unchanged: HLS at 400 MHz, `no_bufg.tcl` on the 230 MHz link, and the 250 MHz link with the timing check off. Output is on `build` in `~/AIEplace_timing/expG/pl/build/{run,run230}/`. Failing paths were grouped with `scratch_45/timing_250/{classify_paths.tcl,group_paths.py}` (`run/paths_G250.csv`).

| build | clock | WNS (ns) | TNS (ns) | failing endpoints | REG | LUT | BRAM |
|---|---|---|---|---|---|---|---|
| F | 230 | +0.001 | 0 | 0 | 220,211 | 145,460 | 228 |
| **G** | **230** | **0.000** | **0** | **0** | | | |
| F | 250 | −0.178 | −87 | 1,596 | | | |
| **G** | **250** | **−0.119** | **−157** | **2,840** | 225,600 | 145,805 | 232 |

- **230 MHz still closes.** The xclbin is packaged at `run230/hpwl_gradient_CU_top.xclbin` (md5 `12675424bc115b0b005761d4be96b16d`).

## Routed (build H, 2026-10-28/29): 250 MHz closes
Build H is G plus the `wa_*` DSP change: every float op in `wa_sums`, and the muls and subs in `wa_gradient`, is bound `primitivedsp` at the floating-point IP's maximum latency (add/sub 2, mul 3; production was 1/2). `wa_gradient`'s adds stay at latency 1, because the `grad_URAM` read-add-write must fit `HAZARD_DISTANCE`. Details and the two HLS traps are in [[REPORT_45_build_H_250mhz_20261028.md]]. It was built with G's 250 MHz script plus a package step (`build:~/AIEplace_timing/expH/pl/build/run/`), and the link finished 02:36 CET.

| build | clock | WNS (ns) | failing endpoints | REG | LUT | DSPFP32 | BRAM | URAM |
|---|---|---|---|---|---|---|---|---|
| G | 250 | −0.119 | 2,840 | 225,600 | 145,805 | 738 | 232 | 256 |
| **H** | **250** | **0.000** | **0** | 237,262 | 151,359 | 826 | 232 | 256 |

- **WNS 0.000 on the 4.000 ns kernel clock** (`clkout1_primitive`). The worst endpoint is a `grad_URAM_16` address pin. `classify_paths.tcl` with the same filter that found G's 2,840 endpoints finds 0 (`run/paths_H250.csv`). The xclbin is packaged as `run/hpwl_gradient_CU_top.xclbin`, but `KERNEL_FREQ_HZ` is not changed and it has not been on the card.
- **Cost:** +11.7 K REG, +5.6 K LUT. DSPs go up by 88 because HLS no longer fuses those fmul+fadd pairs into fmadd/fmsub. BRAM and URAM are unchanged.
- **Bit-exact by construction and by test:** only operator latency changed. Tier 1 is 4/4 and `check-record-stream` 6/6; C-synth at 2.5 ns has 24/24 loops at II=1 (`wa_sums_loop` depth 57 → 73, `wa_gradient_loop` 58 → 61).
- **Not verified:** sw_emu and the card run.

## On the card (2026-10-28, `vck`, build G at 230 MHz)
**4/4 PASS, bit for bit** against the g++ CU. Run with `XRT_DEVICE_BDF=0000:c1:00.1 TARGET=hw bash test/hpwl_gradient_xrt.sh` at `e9f798a`. The card checkout's `pl/build/hw/` now holds build G; F's xclbin is kept beside it as `hpwl_gradient_CU_top.F230.xclbin`.

| design | grad image vs g++ CU | kernel run per axis, G | F, 230 MHz (10-27) |
|---|---|---|---|
| synthetic (16 chunks, 5 macros) | 0 / 17,344 | 10.1 / 0.7 ms | 9.5 / 0.6 ms |
| mgc_fft_a | 0 / 34,592 | 0.2 / 0.1 ms | 0.2 / 0.1 ms |
| mms/adaptec1 | 0 / 227,712 | **0.7 / 0.6 ms** | 0.8 / 0.7 ms |
| bigblue4 (3 chunks) | 0 / 2,616,832 | 7.8 / 7.8 ms | 7.9 / 7.8 ms |

- **adaptec1 is 0.1 ms faster per axis.** That matches the cycle count: ~30 K fewer macro-pin cycles per call (refresh + fold, 15 K each) is 0.13 ms at 230 MHz. The host timer reads in 0.1 ms steps.
- The other designs are unchanged within that resolution. Synthetic axis 0 includes the first call's setup; its axis 1 (0.6 → 0.7 ms) is within the timer step, and on this design the ~80 cycles of fixed fetch latency per call outweigh its few macro pins (see the co-sim section below).
- **At 250 MHz the macro and exchange paths went away, and the beat loop's DSPs are now the wall.**

  | path group (source → destination) | F endpoints / WNS | G endpoints / WNS |
  |---|---|---|
  | `grad_URAM` → `fold_macros` | 63 / −0.178 | gone |
  | `fold_macros` → `pos_URAM` | 235 / −0.176 | gone (fold internal: 24 / −0.054) |
  | `load_slots` → `pos_URAM` | 70 / −0.175 | ~50 / −0.110 (FSM → URAM) |
  | `send_ops` → `send_ops` | 456 / −0.172 | 75 / −0.090 |
  | `merge_ops` ↔ `grad_URAM` | 68 / −0.174 | 32 / −0.119 |
  | `wa_sums` / `wa_gradient` → their DSPs | 459 / −0.078 | **~1,650 / −0.119, ~64% of TNS** |

- The failing DSP paths start at the fabric register holding an fadd/fmul operand (`grp_fu_*_p1`) and end at the DSP's input pipeline register (`DSP_FPM_PIPEREG_INST`). They have 2–3 logic levels and are about 70% route delay. These are the same `wa_sums` paths as in F, now spread across `wa_gradient` as well. WNS is between −0.08 and −0.12 ns in every group, which points to whole-die congestion rather than one bad structure. The kernel's +5 K REG and +29 DSP land on a die where it already holds 55% of the URAMs.
- **Next for 250 MHz:** register the operand into each `wa_*` DSP (one more operator latency stage, or a register before the DSP input). Then rerun the classification.

## Measured: RTL co-simulation (2026-10-28)
Both runs use the same synthetic design (`cosim_tb "1536 4"`: 16 chunks, 5 movable macros) at a 400 MHz HLS target. Before is the 10-26 waveform; after is `6d3961c`.

**Figures:** [[.claude/2_ARTIFACTS/hpwl_gradient_timing_20261028/page.html]]. Scripts are in `scratch_45/waveform/`: `cosim_setup_mb.tcl`, `wave_vcd_mb.tcl`, `render_mb.py` (cuts both timelines with windows derived from the signals) and `build_page_mb.py`.

| | before | after |
|---|---|---|
| compute pass, chunk 0 | 1,452 | **782** cycles |
| compute pass, 16 chunks | 18,097 | **14,528** (−20%) |
| refresh busy, whole pass | 2,654 | 1,810 |
| fold busy, whole pass | 2,594 | 2,002 |
| one call (axis 0) | 142,280 | 138,715 |

- **Refresh and fold now pay a fixed ~80 cycles per call** before the first beat arrives, because each one fetches its own beat list from DDR. Before, the pin list arrived alongside `load_pos_zero_grad`. As a result:
  - chunks with a few hundred macro pins got about 3× faster (chunk 0: refresh 450 → 141 cycles);
  - chunks with a handful got slower (chunk 4: 2 → 81).

  On MMS designs, with 1,000–5,000 beats per pass, the beats dominate. Hiding the fetch would need a prefetch of the beat list that overlaps `load_pos_zero_grad` (not done).
- The beat loop itself is unchanged (4,413 cycles). The two exchange passes are still most of a call on this design; B4 (#47) moves them out of this CU.

## Next
- 250 MHz: routed closure in build H (above). Still to do: the card run on `vck` (4 designs, bit-exact vs the g++ CU), then set `KERNEL_FREQ_HZ` to 250 MHz and log the milestone.
- A placer run on an MMS design: the trajectory should stay within float noise of before (only the summation order changed).
