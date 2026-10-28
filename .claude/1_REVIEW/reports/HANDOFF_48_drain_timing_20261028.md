# HANDOFF #48: close timing on the 64-bank drain (`drain_banks`, −2.14 ns)

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
