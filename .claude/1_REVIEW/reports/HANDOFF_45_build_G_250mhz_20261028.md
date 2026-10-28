# HANDOFF #45 — take build G (macro beats) to 250 MHz (2026-10-28)

*Mark, 10-28: "write a handoff and start a new session to work on building G." Build G = `6d3961c`, the
macro-beat `hpwl_gradient_CU`. Evidence so far: [[REPORT_45_macro_beats_20261027.md]] (routed, co-sim,
card sections). Earlier timing work: [[REPORT_45_hpwl_gradient_timing_20261027.md]].*

## Where it stands
- **230 MHz: done.** Routed WNS 0.000, 0 failing endpoints; on the VCK5000 4/4 bit-exact (synthetic, fft_a,
  mms/adaptec1, bigblue4); milestone logged. Commits `e9f798a`, `2df81ee` (pushed, `pl_algo`).
- **250 MHz: fails.** WNS −0.119 ns, TNS −157 ns, 2,840 endpoints (build F: −0.178 / −87 / 1,596). The
  macro/fold/refresh → URAM paths that held F are gone.
- **The wall is now the beat loop's DSP inputs:** ~1,650 endpoints, ~64% of TNS, in `wa_sums` and
  `wa_gradient` (`pl/src/modules/hpwl_gradient/wa_sums.hpp`, `wa_gradient.hpp`). Paths run from the fabric
  register holding an fadd/fmul operand (`grp_fu_*_p1`) to the DSP's `DSP_FPM_PIPEREG_INST`; 2–3 logic
  levels, ~70% route delay. Other groups (`merge_ops` ↔ `grad_URAM` −0.119, `load_slots` FSM → `pos_URAM`
  −0.110, `send_ops` −0.090) are smaller; all sit at −0.08..−0.12, i.e. die-wide congestion.

## Next action
1. **Register the operand into each `wa_*` DSP.** Options, cheapest first: add a latency stage on the
   operators (`BIND_OP ... impl=primitivedsp latency=N+1`, as `slot_arrays.hpp:169` does for the fold), or an
   explicit register before the DSP input. Keep **every loop II=1** and results **bit-exact** (only latency
   changes, not the arithmetic).
2. Verify in order: tier 1 (`make test` / the `hpwl_gradient_CU_test` harness), `check-record-stream`,
   C-synth at 2.5 ns (`pl/synth/hpwl_gradient_CU/synth_check.tcl`; every loop II=1), sw_emu, then link
   on `build` at 250 MHz and rerun the classification
   (`.claude/2_ARTIFACTS/scratch_45/timing_250/{classify_paths.tcl,group_paths.py}`).
3. If 250 closes: package, card run on `vck` like build G's (below), update report + milestone.

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
