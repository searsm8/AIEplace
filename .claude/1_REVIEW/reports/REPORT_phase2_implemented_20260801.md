# Phase 2 implemented — newblue5 converges, and the gap to XPlace closes

**Date:** 2026-08-01 · **Where:** isolated worktree `/home/msears/phd/AIEplace_t8`, based on
`caa8f2b` **plus Mark's uncommitted filler work** (copied in, verified: 632,490 fillers on
newblue5, exactly matching XPlace's `#Fillers: 632490`). Mark's tree was never touched.
**Config throughout:** newblue5, grid 1024, `maximum_utilization = 0.5`, `random_seed = 42`.

---

## 1. Result

| | phase-1 only (before) | **phase 2, no legalization** | **phase 2 + LP legalization** | XPlace |
|---|---|---|---|---|
| iterations | 1070 | 1926 | 1912 | 2010 |
| HPWL | 4.891e8 *(at phase-1 end)* | **3.984e8** | **3.987e8** | 3.833e8 |
| stop reason | `divergence_guard`, **fallback** | **`converged`**, primary | **`converged`**, primary | `GP Stop` |
| smoothed (clamp/+filler) | 0.350 | **0.0844** | **0.0842** | — |
| exact (sharp/+filler) | 0.429 | **0.229** | **0.229** | — |
| **macro-excluded** (comparable to XPlace) | 0.1257 | **0.0756** | **0.0756** | **0.0452** |

**newblue5 now converges.** It stops on `converged` with a *primary* best instead of
`divergence_guard` with a *fallback* — the failure mode TODO #8 was opened to chase is gone.
Against the original zero-filler baseline (4.399e8 @ sharp 0.350) the run is **−9.4% HPWL and
−35% exact overflow**, and it lands **+3.9% HPWL** from XPlace's final number.

**The hypothesis held.** Freezing the macros — not legalizing them — is what produces the
improvement. Legalization changed HPWL by 0.08% and overflow by 0.0002. On newblue5 phase 1
already leaves the macros nearly legal: **1 overlapping pair out of 91 macros, total displacement
8.85** (XPlace's own newblue5 legalization moves 2484). So stage 2 is required for a *legal*
result, not a *better* one — exactly the ordering argument for doing stage 3 first.

**Where we still trail XPlace:** macro-excluded overflow 0.0756 vs 0.0452. Their phase 2 improves
that metric 3.75×, ours 1.66×. Worth its own look, but the shape is now right.

---

## 2. What was built

Four steps, each with its own gate. **P3 and P2 are verified no-ops** — both bit-identical on
adaptec1 *and* newblue5 (`iterations.dat` **and** `RowBasedPlacement.def`).

### P3 — phase-relative iteration counter
`m_phase_start_iter` + `phaseIteration()`, mirroring XPlace's `ps.init_iter`
(`param_scheduler.py:162/176`), which every schedule site there reads as
`self.iter - self.init_iter`. Converted: `updateSchedule` warmup and the every-3rd gate,
`updateDensityWeight`'s μ decay + warmup + precond escalation, `checkFineDivergenceGuard`'s
arming, `checkConvergence`'s `min_iterations`, and `estimateInitialStep`'s trigger (so phase 2
re-estimates its own initial step, as XPlace does at its reset).

Deliberately left **absolute**: `max_iterations` (XPlace's `inner_iter` spans both phases) and the
density-jolt cooldown (it measures elapsed time, and its anchor is recorded in the same frame).
Every *reported* number still uses `iteration`, so the trace stays continuous across the boundary.

### P2 — one macro definition
Four sites collapsed to one (`Node::isMovableMacro()`, XPlace's `is_mov_macro`):
`analyzeDesignArea` now counts the tag instead of applying its own `area > 0.02% × die`;
`Visualizer` colours on the tag; the dead `m_is_large`/`checkIfLarge()`/`isLarge()` and its
`bin_area_16th` are deleted (zero consumers).

Measured before touching it: the two rules disagree on 7 of 16 MMS designs (worst newblue1,
64 vs 53) **but produce an identical ePlace grid on 16/16**, and every functional use of
`num_movable_macros` is a `> 0` test. Confirmed in the binary: newblue1 64→53 and bigblue2
924→959 with the grid unchanged.

### S3 — the fixed-macro restart (`placer/Phase2.cpp`)
`run()` gains four lines: when `checkConvergence()` fires, `beginFixedMacroPhase()` gets first
refusal, and returning false ends the run exactly as before. The transition restores phase 1's
best, freezes the macros, rebuilds the fillers in the phase-2 frame, re-seeds the standard cells
(XPlace `randn_center`), resets the solver, re-derives `overflow_threshold` from config, and
re-bootstraps the gradients.

New in `DataBase`: `freezeMovableMacros()` and `rebuildFillers()`. These are the **only** place in
the codebase that violates `buildNodeIndex()`'s "no status ever changes, no node is ever added"
invariant, so both rebuild the flat index and the cached area split; the header comment now says so.

Also per Mark's ask: a `[PHASE]` line is emitted at the boundary carrying phase 1's iteration
count, stop reason, HPWL and both overflows, and the numbers are stashed for the final summary.

### S2 — LP macro legalization (`placer/MacroLegalize.cpp`)
Port of XPlace's formulation: build the constraint graph (per pair, pick the separation axis
needing the smaller push, oriented by current order, pruned when the projections don't overlap),
check each axis' longest path fits the die, then write an `.lp` and solve it with the **bundled
CBC** (`~/anaconda3/.../pulp/solverdir/cbc/linux/i64/cbc` — the same binary XPlace shells out to).
Objective is total L1 displacement with the standard `dx ≥ |x − x₀|` linearisation.

Falls back to the longest-path positions (feasible by construction, just not
displacement-optimal) when CBC is missing or returns non-optimal.

---

## 3. Two bugs found in review, before they could produce a wrong number

1. **Stale probe position on frozen macros.** `restoreBestPlacement()` writes only
   `next.node_pos`. Once frozen, a macro leaves `getMovableComponents()` and is never stepped or
   re-initialised again — so its `probe_pos` would have kept phase 1's last *lookahead* value
   forever. `computeNodeFootprint` deposits at the **probe** position, so every macro's density
   would have landed somewhere it no longer was. `freezeMovableMacros()` now collapses all four
   state fields onto the committed position.
2. **Unguarded best-solution restore.** `best_solution_pos` is only meaningful after
   `snapshotBestPlacement()` has run; a phase 1 that stopped before recording a best would have
   been "restored" to uninitialised positions. Now guarded by `bestReference().valid`.

A third was caught by the run itself: `legalizeMacros()` was originally called *before*
`freezeMovableMacros()`, so it saw an empty work set ("fewer than 2 macros"). Order swapped.

---

## 4. ⚠️ `convergence_max_iterations = 1200` is now too small — and this is a real decision

The first phase-2 run was **starved**: phase 1 ended at 1069, leaving 131 iterations, and phase 2
stopped at `max_iterations` with overflow 0.756 (essentially unspread from its fresh re-seed).
The results in §1 used **3000**.

TODO #4 decided "leave at 1200, do NOT raise" — but that was reasoned about a **single-phase** run
("newblue4 is stuck, not slow"), and it is correct for that case. With two phases the budget has to
cover both. XPlace's equivalent is `args.inner_iter`, **default 10000**, spanning both phases and
never binding; its newblue5 finishes at 2010.

**Not changed in the config** — flagging it rather than silently retuning a tuned default.
Recommended: raise to ~3000 so it is a runaway backstop rather than a schedule (which is what
TODO #4 already calls it), or make it phase-relative.

---

## 5. Not done / known gaps

- **Longest-path refinement is not ported.** XPlace repairs an infeasible direction assignment by
  migrating edges between the x and y graphs (the TNS/WNS loop). We detect infeasibility, warn,
  and fall back. newblue5 never hit it (1 overlap pair), but a macro-dense design will.
- **`macro_legalization_xy` / `_ilp` variants and the retry-with-longer-time-limit driver** are not
  ported — we run the single `mix` formulation once.
- **Site/row alignment after legalization** is not done (XPlace runs `gpudp.macroLegalization`).
  Macros land at LP coordinates, not on legal sites.
- **Only newblue5 has been run.** The other 7 macro-heavy MMS designs are untested, and no
  non-mixed-size regression beyond adaptec1.
- The phase-1 numbers are captured in `m_phase1_summary` and logged, but **not yet added to the
  run_summary table** or `results.csv`.

---

## 6. Files touched

```
include/AIEplace.h            phaseIteration, Phase enum, phase-2 decls, config members
include/DataBase.h            freezeMovableMacros, rebuildFillers, invariant comment
include/Node.h                deleted m_is_large / checkIfLarge / isLarge
include/Visualizer.h          drawMovable*(db) signatures
src/DataBase.cpp              freezeMovableMacros, rebuildFillers
src/Visualizer.cpp            colour on the macro tag
src/placer/AIEplace.cpp       run() transition hook; phaseIteration()==1 for initial step
src/placer/Schedule.cpp       6 schedule sites -> phaseIteration()
src/placer/Setup.cpp          analyzeDesignArea counts the tag; new config keys
src/placer/Phase2.cpp         NEW - the transition
src/placer/MacroLegalize.cpp  NEW - constraint graph + LP + CBC
makeflags.mk                  the two new sources
```

New config keys (all default to the phase-2 behaviour; `enable_phase2 = false` restores the old
single-phase run exactly): `enable_phase2`, `macro_legalization`, `macro_lp_solver`.
