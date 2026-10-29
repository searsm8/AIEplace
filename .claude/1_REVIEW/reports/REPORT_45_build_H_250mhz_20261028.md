# REPORT #45 — build H: `hpwl_gradient_CU` closes at 250 MHz (routed, 2026-10-29)

*Mark, 10-28: "write a handoff and start a new session to work on building G." Build G = `6d3961c`, the
macro-beat `hpwl_gradient_CU`. This was that handoff; it became the build H report once the 250 MHz route
closed. Context: [[REPORT_45_macro_beats_20261027.md]] (routed G/H, co-sim, card), earlier timing work
[[REPORT_45_hpwl_gradient_timing_20261027.md]].*

## Result
- **250 MHz closes, routed:** WNS **0.000 ns** on the 4.000 ns kernel clock, **0 failing endpoints** (G: −0.119 ns,
  TNS −157 ns, 2,840). The worst endpoint is `grad_URAM_16` `ADDR_A[13]`. `classify_paths.tcl` with G's filter finds 0
  paths. Packaged: `build:~/AIEplace_timing/expH/pl/build/run/hpwl_gradient_CU_top.xclbin`.
- **Cost (routed kernel, vs G):** REG 225,600 → 237,262, LUT 145,805 → 151,359, DSPFP32 738 → 826, BRAM 232 and
  URAM 256 unchanged.
- **Bit-exact:** only operator latency changed. Tier 1 4/4, `check-record-stream` 6/6, C-synth at 2.5 ns 24/24
  loops II=1.
- **Not yet done:** the card run on `vck`, the `KERNEL_FREQ_HZ` bump to 250 MHz, sw_emu, and the milestone.

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
1. **Card run on `vck`** (Mark books it): copy `expH/.../run/hpwl_gradient_CU_top.xclbin` to the card checkout's
   `pl/build/hw/` (keep G as `.G230.xclbin`), then `XRT_DEVICE_BDF=0000:c1:00.1 TARGET=hw bash test/hpwl_gradient_xrt.sh`,
   expecting 4/4 bit-exact.
2. If it passes: set `KERNEL_FREQ_HZ` to 250 MHz, add the milestone, record per-axis kernel times.

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
