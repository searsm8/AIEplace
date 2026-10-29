# REPORT #45 — build H: `hpwl_gradient_CU` at 250 MHz, routed and on the card (2026-10-29)

*Mark, 10-28: "write a handoff and start a new session to work on building G." Build G = `6d3961c`, the
macro-beat `hpwl_gradient_CU`. This was that handoff; it became the build H report once the 250 MHz route
closed. Context: [[REPORT_45_macro_beats_20261027.md]] (routed G/H, co-sim, card), earlier timing work
[[REPORT_45_hpwl_gradient_timing_20261027.md]].*

## Result
- **250 MHz closes, routed:** WNS **0.000 ns** on the 4.000 ns kernel clock, **0 failing endpoints** (G: −0.119 ns,
  TNS −157 ns, 2,840). The worst endpoint is `grad_URAM_16` `ADDR_A[13]`. `classify_paths.tcl` with G's filter finds 0
  paths. Packaged: `build:~/AIEplace_timing/expH/pl/build/run/hpwl_gradient_CU_top.xclbin`.
- **The margin is thin:** 556 endpoints sit within 0.1 ns of failing (`run/paths_H250_near.csv`, slack < 0.1).
  The tightest are memory inputs at +0.001..+0.003 ns: `offset_BRAM` (65), `pos_URAM` (DSP output and `dout_vld`,
  53), `send_ops`' RAM (34), and `grad_URAM` from `wa_gradient`'s stage-56 compare/enable registers (~50). The old
  operand → DSP group is no longer critical. What remains of it is pipeline-enable fan-out into `grp_fu_*`
  operands at +0.03..+0.08 ns. A different seed or a small RTL change could tip these back below zero.
- **Cost (routed kernel, vs G):** REG 225,600 → 237,262, LUT 145,805 → 151,359, DSPFP32 738 → 826, BRAM 232 and
  URAM 256 unchanged.
- **Bit-exact:** only operator latency changed. Tier 1 4/4, `check-record-stream` 6/6, C-synth at 2.5 ns 24/24
  loops II=1.
- **On the card (10-29): 4/4 bit for bit at 250 MHz** (below). `KERNEL_FREQ_HZ` = 250 MHz, milestone logged. Not done: sw_emu.

## On the card (2026-10-29, `vck`, build H at 250 MHz)
**4/4 PASS, bit for bit.** `XRT_DEVICE_BDF=0000:c1:00.1 TARGET=hw bash host/sw_flat/test/hpwl_gradient_xrt.sh` in the card
checkout at `e9f798a`, so the reference g++ CU is build G's source: H matches G's arithmetic exactly. Card
`pl/build/hw/hpwl_gradient_CU_top.xclbin` is now H (md5 `77f160cbbb002886eb45f23a88cfb02a`); G is kept beside it as
`.G230.xclbin`. Output: `~/AIEplace_timing/expH/card_H250.out`.

| design | grad image vs g++ CU | owned slots vs sw_flat | kernel run per axis, H 250 | G 230 (10-28) |
|---|---|---|---|---|
| synthetic | 0 / 17,344 | (n/a) | 9.2 / 0.6 ms | 10.1 / 0.7 ms |
| mgc_fft_a | 0 / 34,592 | 0 / 30,625 × 2 | 0.2 / 0.1 ms | 0.2 / 0.1 ms |
| mms/adaptec1 | 0 / 227,712 | 0 / 210,967 × 2 | 0.7 / 0.6 ms | 0.7 / 0.6 ms |
| bigblue4 | 0 / 2,616,832 | 0 / 2,169,183 × 2 | **7.6 / 7.5 ms** | 7.8 / 7.8 ms |

- bigblue4 is 3–4% faster. A 230 → 250 MHz clock alone would give 8% (7.2 ms), so part of the call does not scale
  with the kernel clock (DDR transfer). The host timer has a 0.1 ms resolution, which hides the change on the small designs.
- `pl/Makefile` `KERNEL_FREQ_HZ` is now 250 MHz.

## Where G stood (the starting point)
- **230 MHz: done.** Routed WNS 0.000; on the VCK5000 4/4 bit-exact; commits `e9f798a`, `2df81ee`.
- **250 MHz failed on the beat loop's DSP inputs:** ~1,650 endpoints, ~64% of TNS, in `wa_sums` and `wa_gradient`.
  They ran from the fabric register holding an fadd/fmul operand (`grp_fu_*_p1`) to the DSP's
  `DSP_FPM_PIPEREG_INST`, with 2–3 logic levels and ~70% route delay. The other groups (`merge_ops` ↔ `grad_URAM`,
  `load_slots` → `pos_URAM`, `send_ops`) sat at −0.08..−0.12 ns. Build H did not touch those modules, yet they
  close too. That fits the congestion reading (the DSP group was most of the failing logic), but it is not
  proven: this route is a single seed.

## The change (`wa_sums.hpp`, `wa_gradient.hpp`)
- **Change:** `wa_sums.hpp` gains `beat_add`/`beat_sub`/`beat_mul`, bound `primitivedsp` at latency 2/2/3, the
  floating-point IP's maximum (production was 1/1/2). Every float op of `wa_sums` and the muls/subs of
  `wa_gradient` use them. `wa_gradient`'s adds use `gradient_add` (latency 1, bound).
- **Latency map (HLS 2022.2, probe in `scratch_45/latprobe/`):** BIND fadd/fsub latency L → IP `c_latency`
  L for L ≤ 2; L=3 → IP 2 + a fabric `din_buf` register; 4 is rejected. fmul: L ≤ 3 → IP L; 4 → IP 3 + `din_buf`.
- **Two HLS traps found:**
  - HLS builds every fadd of one loop as ONE RTL module. `beat_add` in `wa_gradient` was scheduled at
    latency 2 but instantiated as the `grad_URAM +=` add's latency-1 module.
  - Raising that `+=` add to latency 2 gives `wa_gradient_loop` II=2 (distance-4 RMW). So all of
    `wa_gradient`'s adds stay at latency 1, bound explicitly, which also stops fmul+fadd fusing into fmadd.
- **Verified:** tier 1 4/4, `check-record-stream` 6/6 bit-exact; C-synth 2.5 ns (`scratch_45/synth_H/synth_400.tcl`,
  project `pl/synth/hpwl_gradient_CU/synth_400H_prj`) **24/24 loops II=1**. `wa_sums_loop` depth 57 → 73,
  `wa_gradient_loop` 58 → 61. HLS estimate vs the 10-26 synth: DSP 709 → 826 (the 88 fmadd/fmsub split),
  FF +35 K, LUT +14 K.
- **Build H:** G's `run/run.sh` (HLS 400 MHz, link 250 MHz with `compiler.skipTimingCheckAndFrequencyScaling=1`)
  plus a package step; local copy `scratch_45/synth_H/run.sh`. Link finished 2026-10-29 02:36 CET (~4 h 15 min).
  Vivado writes only the platform's `bb_locked` timing summary in this flow (13 K endpoints, it reads +0.139 and
  is **not** the kernel's): read kernel slack from the routed dcp (`run/worst.tcl`, `classify_paths.tcl`).

## Next action
- None for 250 MHz. Loose ends: an MMS placer run to check the trajectory (below), and the thin margin above if
  a later source change tips the route.

## How build G was built (reuse for build H)
- On `build`: `~/AIEplace_timing/expG/pl/build/go.sh`; outputs in `run/` (250 MHz, timing check off via
  `compiler.skipTimingCheckAndFrequencyScaling=1`) and `run230/` (230 MHz with `pl/synth/hpwl_gradient_CU/no_bufg.tcl`
  hook, packaged). HLS at 400 MHz. Copy `expG` → `expH` at the new commit; rsync **excluding `*_prj*`**
  (pl/synth holds 5.4 GB of HLS projects).
- Card: `ssh vck`, `cd ~/AIEplace`, `XRT_DEVICE_BDF=0000:c1:00.1 TARGET=hw bash test/hpwl_gradient_xrt.sh`.
  `pl/build/hw/hpwl_gradient_CU_top.xclbin` is G; F kept as `.F230.xclbin`.
- **`build` and `vck` access is opened/booked by Mark: ask once, don't retry or poll.**
- Don't `pkill -f`/`pgrep -f` with a pattern your own shell's command line contains (it matches itself).

## Loose ends (not blocking)
- G's routed reports were not archived to `~/timing_tools/reports/G` before the tunnel closed; they are in
  `expG/pl/build/{run,run230}/`.
- Refresh/fold each pay ~80 cycles fetching their beat list per call; a prefetch overlapping
  `load_pos_zero_grad` would hide it (co-sim section of the report).
- MMS placer run to confirm the trajectory stays within float noise (fold summation order changed).
