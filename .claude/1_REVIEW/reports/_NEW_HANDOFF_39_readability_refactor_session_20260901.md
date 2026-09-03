# HANDOFF — sw_only readability refactor session (#1, #39)

**Dates:** 2026-08-31 → 2026-09-02 · **Branch:** `pl_algo` · **Commits:** `1ed9ea7`, `652ba25`,
`5867600` (08-31→09-01 session below), then `a2a2c1b`, `df6a2c2`, `3ef1d61`, `5cf7491`, `5fbd182`
(09-02 session, own heading below) · **Status:** all four "suggestions for next session" items but
one landed; `make test-regress[-slow]` green throughout. Not yet closed — see Open below.

This is a handoff-in-progress in the sense CLAUDE.md now defines it: written at the end of an
active session for the next one to pick up from. If #39 concludes cleanly, fold the durable
findings into tasks.md/CLAUDE.md and this file can be retired; don't let it accumulate.

## What this session was

Mark is presenting this codebase for his thesis and needs to know what's in it and be able to
explain it. The concrete mechanism: walk the high-level `Placer` files with a fine-tooth comb,
compare code snippets against a hierarchical class map, and check each is in the right file, right
order, right name. Functionality stayed frozen throughout (CLAUDE.md's freeze section); readability
was explicitly in scope. Every move was meant to be verified bit-identical before landing.

Session artifact: **"Placer Class Map"** (published as a Claude Artifact 2026-08-31) — all ~85
`Placer` methods grouped by responsibility along the dataflow, each tagged with its defining file,
misplacements flagged. That map is what drove every move below; re-read it before continuing #39,
since some of it is now stale (BestSolution.cpp didn't exist yet when it was drawn).

## What landed (3 commits)

1. **`1ed9ea7` — Drop `m_` prefix from Placer data members.** Mechanical, from-`HEAD` rename
   (`m_phase`→`phase`, `m_stop_reason`→`stop_reason`, `m_nan_detected`→`nan_detected`,
   `m_pos_dump`→`pos_dump`, `m_phase_start_iter`→`phase_start_iter`,
   `m_phase1_summary`→`phase1_summary`, `m_config_filepath`→`config_filepath`,
   `m_pin_partials`→`pin_partials`, `m_net_pin_offset`→`net_pin_offset`,
   `m_ordered_reduce`→`ordered_reduce`), landed in a parallel session and reconstructed here as its
   own commit for bisection purposes (see below). One real fix needed during reconstruction: the
   constructor's parameter was also named `config_filepath`, so the blind rename created a
   self-assignment shadowing bug (`config_filepath = config_filepath;`) — fixed by renaming the
   parameter to `config_filepath_arg`, matching what the actual session code already did.
   Independently verified bit-identical (`make test-regress-slow`) as its own isolated commit.

2. **`652ba25` — BestSolution.cpp, gamma consolidation, Phase2 rename, Step/Schedule reorder.**
   - New `placer/BestSolution.cpp`: gathers `snapshotBestPlacement`/`restoreBestPlacement`/
     `bestSlotPos` (were in `AIEplace.cpp`), `selectBestSolution`/`bestReference` (were in
     `Schedule.cpp`), `restoreBestSolution` (was in `Output.cpp`) — one concept, one file. This is
     why `BestPlacement` felt wrong at the top level when Mark first asked.
   - `updateGamma` moved out of `Partials.cpp` into `Schedule.cpp`, beside `configureGammaSchedule`.
   - `run()` reads plainly: `beginFixedMacroPhase()` (one function, deciding *and* acting) split
     into `readyForPhase2()` (eligibility only) + `beginPhase2()` (transition only), gated in
     `run()` by `phase == Phase::MIXED_SIZE`.
   - `FixedMacroPhase` renamed to `Phase2` throughout.
   - `Step.cpp` reordered: `performNextStep()` (Algorithm 2, the heart of the algorithm) and
     `estimateInitialStep()` moved to the top of the file, ahead of the shared primitives they
     call; `logStepDiagnostics()` stays last as pure debug output.
   - `Schedule.cpp` reordered: `updateSchedule()` and `checkConvergence()` — both called directly
     from `run()`/`performIteration()` — lead the file, each immediately followed by its dispatched
     callees in call order; `checkOverflowPlateau`/`checkDivergence` (used by callees under both
     entry points) moved to the end as shared primitives.

3. **`5867600` — checkForNaN() consolidation, dumpScheduleTrace self-gate, iterationReset/
   performIterationZero reuse.**
   - `checkForNaN()`: the two scattered `nan_detected` checks (one in `run()` setting `stop_reason`
     and breaking, one at the tail of `performIteration()` only logging) consolidated into one
     `Schedule.cpp` function, called once from `run()`. It sits beside `checkConvergence()`'s callee
     group but is *not* one of its callees — `run()` calls it directly, bypassing
     `checkConvergence()` entirely, because it guards a numerically catastrophic case (a NaN inside
     an OpenMP loop, where `return` is illegal), not the gradual metrics NaN `hasNaNMetrics()`
     covers.
   - `dumpScheduleTrace()` self-gates: the `cfg["output"]["dump_schedule_trace"]` check moved inside
     the function; the call site in `performIteration()` is now an unconditional call.
   - `iterationReset()` moved from `AIEplace.cpp` to `Step.cpp`, beside its sibling primitive
     `advanceIterationState()`. Two of its three call sites already lived in Step.cpp, and every
     call site is immediately followed by `computeHpwlPartials()`+`computeElectricFields()` — it's
     part of that shared "clear scratch state before a gradient eval" step, not top-level
     orchestration.
   - `beginPhase2()` now calls `performIterationZero()` instead of hand-duplicating its four-line
     body. The duplicate was a silent-drift risk — nothing would have caught the two copies
     disagreeing if `performIterationZero()`'s body ever changed. `performIterationZero()`'s doc
     comment updated to describe both callers (`run()` after `initializePlacement()`; `beginPhase2()`
     after freezing macros / re-seeding std cells) rather than assuming the first one.
   - `Phase2.cpp`'s file header now states explicitly how XPlace's three stages
     (`run_placement_nesterov.py:167-230`) map onto our two-state `Phase` enum: Mixed-GP is phase 1;
     macro legalization + the second GP pass together are phase 2, with no separate `Phase`
     enumerator for the legalization step.

**Note for whoever reads the commit graph:** this is 3 commits, not the 4 originally planned (see
next section for why) — a `git log` search for "commit 3" won't find one; commit `5867600` covers
what was going to be commits 3 *and* 4.

## The bisection episode — what happened and why it matters

Partway through, `make test-regress-slow` failed for real: `density_weight` (the λ scheduling
value) came out ~0.3% off from iteration 3 onward, on **every** design tested — including
`mgc_fft_a`, which has no macros and never touches the phase-2 code. HPWL/overflow themselves
matched at the log's printed precision (4 significant figures) at every iteration; only the
internal scheduling value visibly drifted, and only because `updateDensityWeight()`'s formula
(`mu = dw_max_step^(-rel_worsening·100)`) is exponential and therefore amplifies anything below
that printed precision (the regression harness's own README documents this: a 2-ULP float32
perturbation to a config constant was measured to move the printed trajectory by iteration 65 —
see `test/regress/README.md`).

Mark asked to use this as an opportunity to actually exercise `git bisect`, which requires commits.
The session's whole diff (11 files, everything from 2026-08-31 onward, both Mark's and Claude's
edits interleaved with no intermediate commits) was retroactively split into the planned 4 commits
by reconstructing each intermediate state directly (not through git's automatic 3-way merge, which
only got partway there — `AIEplace.cpp`, `Phase2.cpp`, and `Step.cpp` needed hand-resolved
conflicts because structural edits sat close to renamed lines).

Each reconstructed checkpoint was independently rebuilt clean and run through
`make test-regress-slow`:

| checkpoint | content | result |
|---|---|---|
| commit 1 | rename only | **PASS**, bit-identical |
| commit 2 | + BestSolution/gamma/Phase2-rename/Step+Schedule reorder | **PASS**, bit-identical |
| commit 3 (uncommitted checkpoint) | + checkForNaN + dumpScheduleTrace self-gate | **PASS**, bit-identical |
| + Setup.cpp log-line move alone | | **PASS** |
| + iterationReset() move alone (on top of the above) | | **PASS** |
| + beginPhase2()/performIterationZero() swap (= full commit-4 content) | | **PASS** — unexpected, see below |

The last row was the surprise: with *every* planned readability change applied, freshly rebuilt,
full suite — it passed. This directly contradicted the original failing run. Repeat-run testing (3x
back to back) confirmed the passing build was itself perfectly deterministic, ruling out a genuine
race condition. That meant the reconstruction differed from the true failing state somewhere, and
the missing piece had to be found by comparison rather than assumption.

It was a **statement-order swap in `performIteration()`** that had nothing to do with any of the
four planned commits: Mark's own edit (grouping calls "by function type," moving the output calls
to the end) had put `printIterationResults()` after `updateSchedule()` instead of before it. Direct
A/B testing — flip the order, rebuild, retest; flip it back, rebuild, retest — reproduced and fixed
the exact failure signature reproducibly, across multiple clean rebuilds. That is now a *confirmed
fact*, not a hypothesis.

**What is not known:** *why*. `printIterationResults()` and its callees
(`printDSEInfoTable`/`printIterationSummaryTable`/`dumpIterationPositions`/`appendIterationLog`)
were read line by line and none of them write any state `updateSchedule()`/`updateDensityWeight()`
reads (`hpwl_history`, `ovfw_history`, `phaseIteration()`, config values) — they're pure
reporting/logging. Neither function is inlined into the other (different translation units,
no LTO), so ordinary compiler-codegen reasoning doesn't obviously apply either. The best remaining
hypothesis is that the heavy `iostream`/`std::to_string` formatting in the print path perturbs FPU
state (most likely the rounding mode) in a way that changes how `updateDensityWeight()`'s
`std::pow()` call rounds immediately afterward — consistent with the extreme sensitivity documented
in `test/regress/README.md` — but this was not proven, only the order-dependency itself.

**Decision (Mark's call):** since the effect is negligible on the one benchmark actually measured
(final HPWL/overflow unaffected; only the internal scheduling value drifts) and the mechanism is
unexplained, the **original order is kept** — `recordIterationResults()` → `printIterationResults()`
→ `updateSchedule()` → `dumpScheduleTrace()` — specifically to leave the committed
`test/regress/golden/*.baseline` files unchanged rather than risk a baseline refresh for a change
nobody fully understands yet. The formal `git bisect` walk was skipped once the cause was found by
hand; that's why this session produced 3 commits instead of the planned 4 — commits that would have
separately isolated `checkForNaN()`/`dumpScheduleTrace` from `iterationReset`/`beginPhase2` stopped
being independently useful once bisect itself was no longer going to run over them.

## Session 2026-09-02 — picked up items 2-4, left item 1 open

Continuation of the same #39 thread, same mechanism (every move verified `make test-regress[-slow]`
bit-identical before landing; `make host` clean first). Five commits, each independently verified:

- **`a2a2c1b` — `Setup.cpp` reordered to match call order.** Item 2 above. `setupDesign()`'s
  callees (`loadConfiguration`/`resolveGridResolution`/`loadDesignDatabase`/`tagMovableMacros`/
  `createFillers`) now follow it in call order, same convention as Step.cpp/Schedule.cpp.
- **`df6a2c2` — `configureThreadPool()` relocated + `setupGrid()` folded into `setupDesign()` +
  `recordIterationResults()` moved to `BestSolution.cpp`.** Three separate asks from Mark in the
  same session: (a) `configureThreadPool()` (a static helper with one caller) moved down to its
  call-order slot right after `loadConfiguration()`, behind a forward declaration, so
  `setupDesign()` leads the file; (b) `setupGrid()`'s call moved from the constructor into
  `setupDesign()` as its last step — safe because nothing ran between the two calls in the
  constructor; (c) item 3 above — `recordIterationResults()` drives the same three best-solution
  trackers the rest of `BestSolution.cpp` manages, not output/reporting, so it moved there, right
  before its callee `snapshotBestPlacement()`. A breadcrumb left in `Output.cpp`, matching the
  existing `restoreBestSolution()` one.
- **`3ef1d61` — `@file` doc comments + README refresh.** Not one of the original 4 items — a
  separate ask, same session: `AIEplace.h` (every file includes it, had zero doc) and
  `AIEplace.cpp` (the loop skeleton every sibling file already pointed readers to) got brief
  `@file` blocks; `Density.cpp`'s plain header upgraded to the same style as `Partials.cpp`.
  `README.md` fixed a stale line count, added the missing `BestSolution.cpp` row, and marked
  which five files are the core algorithm vs. supporting machinery.
- **`5cf7491` — `ConfigUtils::require` moved to the end of `AIEplace.h`.** Also a separate ask.
  It's a template with no `Placer` dependency used from 5 different `.cpp` files, so it has to
  stay in a header — "move it to where config is read" (`Setup.cpp`) would have broken the other
  four translation units. Relocated past the `Placer` class instead, so the header opens directly
  with the class it exists to declare.
- **`5fbd182` — item 4: swept `Density.cpp`/`Partials.cpp`/`Phase2.cpp`/`PositionDump.cpp`.**
  `PositionDump.cpp` checked clean (its four entry points are already lifecycle-ordered, each
  callee already follows its sole caller) — not touched, per the "verify before reordering, don't
  manufacture churn" instruction. Three real mismatches found and fixed:
  - `Density.cpp`: `computeOverlaps()` is `computeElectricFields()`'s FIRST call but sat ~200 lines
    below it, after the naive/DCT reference pair. Moved up. Left naive-before-DCT alone —
    deliberate reference-first pattern (README says so explicitly), and naive is unreachable from
    config, so there's no live call order there to violate.
  - `Partials.cpp`: `computeHpwlPartials()` checks `"cpu"` before `"simple"`, and `cpu` is the
    default/golden path (README + `default_config.toml` + the function's own doc comment all
    agree) — but the file presented `simple`'s LUT machinery first. Swapped the two backend blocks.
  - `Phase2.cpp`: `reportPhaseSummary()` is `beginPhase2()`'s first local callee but sat at the very
    end of the file. Moved up to lead the block.

All landed, `tasks.md` #39 updated to match (each item marked done, dated, commit-linked).

## Open — what's left before #39 can close

1. **Understand the print/updateSchedule order-sensitivity mechanism** (item 1, carried over
   unchanged from the 08-31→09-01 session — still not investigated, still not urgent). See the
   bisection section above for what's known: the effect, not yet the cause.
2. **Fold #39's findings back into `#1`** once (1) is either resolved or explicitly deferred — `#1`
   already has a stale-marked pointer waiting for this. That is also the trigger to retire this
   handoff (convert to REPORT or delete, per the CLAUDE.md policy this file itself follows).

Both are #1's/#39's own open bullets in tasks.md — check there for current status before assuming
this file is authoritative; tasks.md is updated same-session, this handoff is not always.

## How to verify this session's work independently

```bash
cd vck5000
make test-regress-slow   # ~7 min; all 3 designs must show bit-identical
git log --oneline -8      # 5fbd182, 5cf7491, 3ef1d61, 8fcc0fa, df6a2c2, ffe7227, a2a2c1b, 0f3416e
```

If either check disagrees with what's written here, trust the check and update this file or flag
it — don't propagate a stale claim.
