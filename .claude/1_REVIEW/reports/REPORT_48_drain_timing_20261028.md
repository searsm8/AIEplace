# REPORT #48: timing closed on the 64-bank drain (Vivado synthesis 2.93 ns at 3.33, WNS +0.40 ns)

*2026-10-28/29. Branch `pl_algo`. Was the handoff of the same name (kept below as the brief).*

## Result

**Vivado out-of-context synthesis on the build server** (`impl_check.tcl`, 2022.2, whole `banked_top`):

| Vivado synth, 3.33 ns | csynth fixes only (run 1) | **+ exit from a register (final)** | + `style=frp` (not taken) |
|---|---|---|---|
| post-synth period | 4.938 ns, NOT met | **2.928 ns, met** | 2.880 ns, met |
| WNS / failing endpoints | −1.608 ns / 20,995 | **+0.402 ns / 0** | +0.450 ns / 0 |
| LUT / FF / DSP / URAM | 291,311 / 184,270 / 656 / 128 | **287,689 / 182,695 / 656 / 128** | 287,516 / 182,706 / 656 / 128 |
| `drain_banks` LUT / FF | 21,515 / 39,332 | 18,957 / 37,783 | |

**Run 1 failed only in the drain's exit.** Bucketing every failing endpoint by its worst source
(`.claude/2_ARTIFACTS/drain_timing_48_vivado/fail_buckets.tcl`, run on the synth checkpoint): **all 20,995 start at
`finished[]`**. The path is counts → compare → 64-way AND → loop exit → the pipeline enable (HLS: fanout 14,208)
→ every URAM `EN_A` and pipeline register: 11 logic levels, 78 % routing. csynth models neither routing nor
fanout, so it showed this path as met. **Edit 4** below makes the exit a flop. The final worst path is a producer
FIFO `full_n` → m_axi FIFO pointer, not the drain. `style=frp` (free-running pipeline) gains only 0.05 ns more,
so it was not taken. Reports: `.claude/2_ARTIFACTS/drain_timing_48_vivado/{run1_csynth_fix,run2_A_registered_exit,
run2_B_frp}/`. A run takes 77 min (csynth 18 + Vivado 58).

**C-synthesis** (column "after" = edits 1–3; the final build's csynth is the same 2.33 ns, +0.10 ns):

| `pl/synth/bin_density_banked` csynth, 3.33 ns, 0.90 ns uncertainty | before | after |
|---|---|---|
| `drain` pipeline estimate | 4.169 ns | **2.329 ns** (II = 1, depth 6) |
| `drain_banks` slack | **−2.14 ns** | **+0.10 ns** |
| `drain_banks` FF / LUT | 49,074 / 63,148 | 42,706 / 62,700 |
| `banked_top` DSP / FF / LUT / URAM | 656 / 269,559 / 454,115 / 128 | 656 / 263,191 / 453,667 / 128 |
| `banked_top` slack | −2.14 | −0.00 (the producers, unchanged: they were −0.00 before too) |

Reports: `.claude/2_ARTIFACTS/drain_timing_48_{before,after}/` (csynth.rpt, the drain csynth reports and the
verbose schedule). HLS Fmax estimate 218.7 → 411.4 MHz.

## What changed (`drain_banks`, four edits)

1. **Exit on registered counts.** `all_finished` is the AND of `finished[b] == PRODUCERS` taken at the top of
   the iteration, before that cycle's reads. The 64-way AND now starts from registers (state 2, 2.21 ns).
   Enough for csynth, not for Vivado: see edit 4.
2. **One-hot pick, OR-merge.** `take = !got && !empty`; only the taken read is nonzero, so the update is the
   OR of the 8 reads. The 8-deep `u.last` phi chain is gone (state 1 now: a 1 ns mux before `r.addr`).
3. **Deferred write-back. The handoff missed this one.** Before, state 5 was also over budget: **3.13 ns**
   = URAM dout 0.97 + forward select 0.45 + add 0.91 + URAM store 0.80, all chained. Now each cycle writes
   the PREVIOUS cycle's sum from forwarding slot 0 (`if (fwd_valid[b][0]) rho_URAM[b][fwd_addr[b][0]] =
   fwd_value[b][0]`), so the store data comes from a register: state 5 = 2.33 ns, store alone in state 6.
   No update is left unwritten: a bank's real updates always come before its 8th `last`, and the loop runs
   at least one cycle after that.
4. **Exit from a register (after Vivado run 1).** `done = all_finished; all_finished = AND(...)`: the exit
   reads the AND from the previous cycle, so the enable fanout starts at a flop; `finished` narrowed from `int`
   to `uint8_t`. Two idle cycles at the end.

## Verification

- `banked_check` (C, 400 K cells over 8 producers): **0 of 1,048,576 words differ at grids 1024, 512, 336**, after edits 1–3 and again after edit 4.
- Mutants, all caught:
  - old run-time pitch (`col/8 * grid_bins/8`): 1024 PASS (the two layouts coincide), 512 FAIL 388,823, 336 FAIL 185,471 (same counts as before);
  - one-hot broken (`got` never set): the drain never exits (two `last`s in one cycle count once);
  - write-back dropped: FAIL at all three grids (881,982 / 261,831 / 112,896).

## Forwarding depth: the co-sim item must check this

The URAM access **states did not move**: the load is issued in state 2 (4-cycle, `latency=3`) and the store in
state 5, before and after (checked again on the final build). **But the store now carries the previous iteration's sum, so the read→write distance in
iterations is one longer.** `bin_density_CU`'s co-sim measured distance 5 (depth 4 fails, 5 passes) with the old
write. If the banked drain matched that, it now needs 6, which is exactly `BANKED_FORWARD_DEPTH` = 6, with no
margin. C simulation cannot see this (it runs iteration by iteration). The 64-bank co-sim (next item) must sweep the
depth as `run_depths.sh` does. If 6 fails, **raise** the depth (to 7: 64 × 51 b ≈ 3.3 K FF). Do not lower it.

---

# Original brief (handoff)


*2026-10-28. Branch `pl_algo`. Mark: "the next #48" after [[REPORT_48_bank_address_multiplies_20261028.md]].
Context: [[EXPLAINER_48_bin_density_CU_overview_20261022.md]] slide 6 step 2, [[PLAN_48_bin_density_lanes_20261020.md]].*

## The problem

`pl/synth/bin_density_banked/banked_top.cpp` `drain_banks` is the consumer side of the banked deposit: 64
URAM banks, each taking one update per cycle from the first of its 8 private producer FIFOs and doing a
read-add-write with forwarding. It is pipelined at II=1, depth 6, and C-synthesis (3.33 ns target, 0.90 ns
uncertainty) estimates **4.17 ns for the `drain` pipeline, 4.57 ns for `drain_banks`**: −2.14 ns slack.
Report: `pl/synth/bin_density_banked/synth_check_prj/sol1/syn/report/drain_banks_Pipeline_drain_csynth.rpt`;
paths: `.autopilot/db/drain_banks_Pipeline_drain.verbose.sched.rpt`.

**The URAM read-add-write is not the problem** (its states are 1.3–1.5 ns). The critical path is control,
all in one cycle (states 1–2, 3.0 + 4.2 ns):

1. **Pick** (line ~108): a priority chain over the 8 FIFOs' `empty()` flags (3 chained selects).
2. **Read** (line ~112): `for p: if (pick == p) u = from[p][b].read();` becomes a CHAIN of 8 muxes (the
   `u.last` phi chain, 0.47 ns each), not one 8:1 mux.
3. **Termination**: `u.last` → `finished[b]++` → `finished[b] == PRODUCERS` → a 64-input AND
   (`all_finished`) → `done` → the `while (!done)` exit test, in the same cycle as the read.

So one cycle carries FIFO status → pick → 8-deep mux chain → counter → compare → 64-way AND → loop exit.

## Directions (choose by measurement, simplest first)

- **Take termination off the read path.** Compute `all_finished` from the PREVIOUS cycle's `finished[]`
  (registered), so the exit costs one extra idle cycle, harmless. Or count `last` tokens separately.
  The AND over 64 banks can also be a registered tree.
- **Make the read one mux, not a chain.** One-hot pick, then a balanced 8:1 select of the read data; or
  read through `read_nb` per FIFO only when picked. Check the verbose schedule that the chain is gone.
- **If still short:** a merge tree per bank (8→4→2→1 FIFOs, each 2:1 merge a tiny II=1 stage). That adds
  latency, not throughput, and keeps every stage's logic small. Bigger change; only if the first two
  are not enough.

The order in which updates reach a bank does not matter (integer adds), so any of these keeps the result
bit-exact. **Do not lower `BANKED_FORWARD_DEPTH`.** Restructuring may change the read→write distance, and
only RTL co-sim can prove the depth (that is the next item, the 64-bank co-sim). Leave it at 6, and say in
the report whether the URAM access stages moved.

## Steps

1. Restructure `drain_banks`. → `banked_check` 0 words differ at grids 1024/512/336, mutant still caught.
2. C-synthesis (`synth_check.tcl`). → `drain` and `drain_banks` estimate ≤ 3.33 ns with the 0.90 ns
   uncertainty, II=1 kept. Record before/after from the reports; DSP/LUT/FF must not grow noticeably.
3. Vivado synthesis on the build server is the real timing verdict (csynth's estimate is pessimistic).
   The portal is Mark's to open: ask him, don't retry. If it is not open, say so and stop at step 2.

Out of scope (later items): the 64-bank co-sim, the `(ow·w)·oh` reorder, macro tiles, folding the banked
design into `bin_density_CU`.

## Done means

banked_check PASS; csynth timing met (or Vivado, if run); numbers before/after in the report; tasks.md #48
and the Active Task line updated; this file `git mv`'d to `REPORT_...`. Commit file by file (never
`git add -A`: synth project dirs and another session's `hpwl_gradient` edits are untracked/uncommitted
in this checkout; leave them alone). Push `pl_algo` when done (Mark, 2026-10-28: "commit and push as needed").
