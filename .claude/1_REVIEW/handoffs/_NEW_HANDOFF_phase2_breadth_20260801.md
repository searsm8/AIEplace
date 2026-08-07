# HANDOFF — phase 2: breadth and polish, 2026-08-01

Phase 2 is built, committed and working on newblue5. **What is missing is breadth, not mechanism.**
Nothing below is a blocker for the others; they are roughly independent and roughly in value order.

---

## 0. State of the tree — READ FIRST

- Branch `pl_algo`. Two commits landed today, **not pushed**:
  - `7a0d030` — XPlace-faithful filler sizing (TODO #13 prerequisite P1)
  - `a5c0dde` — mixed-size phase 2: P3 + P2 + S3 + S2 (TODO #13)
- Working tree is clean apart from the `macro_deposits_target_density` rename (below), which is
  verified bit-identical on adaptec1 but **not yet committed** at time of writing.
- `convergence_max_iterations` is now **10000** in `run_config.toml`. It is a whole-run runaway
  backstop spanning BOTH phases (XPlace's `args.inner_iter`, same default). The
  `tools/{verify,bench,profile}_swonly.sh` overrides to a small `$ITERS` are deliberate — those
  are fixed-length harnesses, leave them.
- **Renamed:** `macro_td_expand_ratio` -> `macro_deposits_target_density`. The old name described
  XPlace's internal `expand_ratio` field rather than the behaviour. Old logs, `config_used.toml`
  under `results/`, and the pre-08-01 reports still carry the old name — that is expected;
  `results/` is a historical record and must not be rewritten.

Build: `cd vck5000/host && make HOST=sw_only` (exe at
`vck5000/build/hw/host/sw_only/aieplace_sw_only.exe`).

---

## 1. Run the MMS suite through phase 2  ← highest value

**Only newblue5 has been run end to end.** Everything we claim about phase 2 rests on one design.
The other seven macro-heavy MMS designs (`adaptec5, newblue1, newblue2, newblue3, newblue4,
newblue6, newblue7`) are untested, as are the eight `target_density = 1.0` designs where phase 2
should be **completely inert** (no movable macros -> `beginFixedMacroPhase()` returns false).

What to produce: a table like the one in `NEW_REPORT_phase2_implemented_20260801.md` §1, per
design, against `2_ARTIFACTS/mms_baseline_20260731.tsv` and
`tools/benchmarks.py::_XPLACE_MMS_MIXED_GP`.

Two traps:
1. **`_XPLACE_MMS_MIXED_GP`'s overflow column is macro-EXCLUDED** — XPlace sets
   `zero_macro_grad = True` *before* its Mixed-GP eval. Our `sharp/+filler` is macro-INCLUDED.
   Comparing them directly is the mistake that produced the retracted "2–2.5x under-spread"
   claim. Use `/tmp/t8/overflow_variants.py` (or item 4 below) for a like-for-like number.
2. Set **both** `maximum_utilization` and `bins_per_row` from `tools/benchmarks.py::_ROWS`, and
   pin `random_seed`. Auto-memories `mms-needs-explicit-target-density`,
   `pin-random-seed-in-manual-ab`.

Budget: newblue5 took ~1900 iterations at ~1 s/iter. Expect a few hours for the tier, sequential
(`long-running-sweeps-on-this-box` — check `ps` first).

---

## 2. Re-run the #11b A/B and settle `macro_deposits_target_density`

Mark's standing decision: **expect this to be locked `true` and the legacy branch deleted**, once
the evidence is re-taken. It is XPlace-faithful (`database.py:921-923`) *and* correct on the
density accounting — a macro that fills a bin to exactly capacity is full, not overflowing, and
anything overlapping it still overflows immediately. With the flag off, a movable macro emits
`area * (1 - target_density)` of overflow permanently, irreducible by any movement.

The only thing holding it back is the original A/B's "mean +5.2% HPWL worse", and that A/B is
**confounded twice**: it ran on arms with ZERO fillers on the macro-heavy designs (fixed in
`7a0d030`), and its arms halted early on the stop criterion.

`2_ARTIFACTS/gen_footprint_ab_configs.py` is **already fixed** for this: the dead `#11a` arm is
gone (its config key no longer exists, so both old arms were silently identical), the key is
renamed, and the matrix is now `off`/`on` on the 8 `td < 1.0` designs plus one control arm each on
the rest. Runner: `2_ARTIFACTS/run_footprint_ab.sh` (sequential, resumable — that is deliberate
and measured, don't "optimize" it into concurrency).

---

## 3. Port the longest-path refinement into the macro legalizer

`placer/MacroLegalize.cpp` implements the constraint graph, a longest-path feasibility check, and
the LP. **What is not ported is the repair loop** (`longest_path_refinement`,
`macro_legalization.py:353`): when a chain of macros forced into one row is wider than the die, the
direction assignment is infeasible and XPlace migrates edges between the x and y graphs until the
slack is non-negative. We detect it, warn, and fall back to the (unrepaired, possibly still
overlapping) result.

newblue5 never hit this — it had **1 overlapping pair out of 91 macros** — but a macro-dense design
will. The algorithm is static timing analysis on the constraint DAG: `L` = arrival, `R` = required,
`slack = R - L`, repair while total negative slack < 0. Walkthrough in
`NEW_EXPLAINER_lp_macro_legalization.md` §5.

Also unported, in rough value order: **site/row alignment** after the LP (macros currently land at
LP coordinates, not on legal sites — XPlace runs `gpudp.macroLegalization` for this), the
`macro_legalization_xy` variant and the pick-the-lower-displacement driver, and the
retry-with-longer-time-limit loop.

---

## 4. Make the macro-excluded overflow a first-class metric

Right now the only way to get the number that is comparable to XPlace's phase-1 reference is a
post-hoc Python script over the exported DEF (`/tmp/t8/overflow_variants.py`, ephemeral). It should
be an argument to `Placer::computeOverflow` — it already takes `clamp` and `include_fillers`;
this is a third axis.

This is the config-gated diagnostic originally scoped in TODO #8 and deliberately *not* built then
(the need had not materialised). It has now materialised twice: once for the retracted
under-spread claim, once for reading phase-2 results.

While in there: **`Final Overflow (exact, +fillers)` is misleading on mixed-size designs.** It
deposits macros at weight 1 (the `macro_deposits_target_density` branch is clamp-only), so it
carries the spurious macro term described in item 2 — newblue5 phase 2 reads sharp 0.229 against
clamp 0.0844. XPlace sidesteps this by excluding macros from its phase-1 eval entirely.

---

## 5. Smaller items

- **Phase-1 numbers are logged but not reported.** `m_phase1_summary` is populated and a `[PHASE]`
  line is emitted, but neither reaches `run_summary.md` or `results.csv`. A two-phase sweep
  currently shows only the phase-2 endpoint, which hides the macro-placement quality phase 1 is
  responsible for. Add a phase-1 block to the summary table and columns to the CSV.
- **`convergence_include_fillers`** — TODO #13 defers this to "part of the phase-2 landing", with
  the reasoning that the phase-1 filler population is the wrong one to count. Phase 2 now exists
  and rebuilds the filler set, so **re-read that argument and decide**. Check the
  `clamp/no-filler` vs `clamp/+filler` split in `[OVFW-DIAG]` per design first.
- **Density-weight runaway (TODO #4, still unowned)** — `updateDensityWeight` has no upper bound.
  newblue5 phase 1 showed λ running ×700 across 370 flat iterations for 0.004 of overflow, costing
  17% HPWL. Raising `convergence_max_iterations` to 10000 makes bounding λ **more** important, not
  less: there is now far more room for a stuck run to inflate before the backstop fires.
- **`tools/verify_swonly.sh` cannot be diffed the way its own docstring says** — it collects
  `function_statistics.md`, which contains wall-clock timings, so `diff -r` reports a difference on
  every design every run. Diff `iterations.dat` and `RowBasedPlacement.def` directly. Worth fixing.
- **Two macro definitions are now one** (P2 landed), but `Placer::num_movable_macros` is still a
  *count* derived from the tag. If anything ever needs the macro *set* rather than the count, use
  `Node::isMovableMacro()` and do not reintroduce a threshold.

---

## 6. Things that are settled — do not redo

- **The zero-area `terminal` nodes in newblue5 are inert.** 0x0, FIXED, single-point (all pin
  offsets exactly (0,0)), contributing **exactly 0.0** fixed area. They cannot form a density
  hotspot. TODO #8 is answered; see `_NEW_REPORT_newblue5_todo8_20260731.md` §1.
- **P3 and P2 are verified no-ops** — adaptec1 and newblue5 bit-identical on `iterations.dat` and
  the placement DEF, independently for each.
- **Freezing the macros, not legalizing them, is what produces the phase-2 improvement.**
  Legalization moved newblue5's HPWL by 0.08%.
- **CBC is available** at `~/anaconda3/lib/python3.12/site-packages/pulp/solverdir/cbc/linux/i64/cbc`
  (the binary XPlace itself shells out to), self-contained, reads a plain `.lp`. `macro_lp_solver`
  overrides the path; auto-detect covers `$PATH` and the miniconda location too.
- **`.gitignore` depth-matching is fixed.** `vck5000/1_REVIEW` and `vck5000/2_ARTIFACTS` are
  ignored again (they were untracked-but-NOT-ignored, ~80 MB of GIFs one `git add -A` away). The
  pattern must stay BEFORE the `0_TODO` exceptions — git does not descend into an excluded
  directory, so a re-include inside one can never fire.
