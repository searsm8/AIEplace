# Handoff — sw_only readability refactor, round 3 — 2026-07-30

Continuation of `HANDOFF_sw_only_cleanup_20260728.md` (which itself continued
`_NEW_overnight_cleanup_20260727.md`). That handoff finished the doxygen/naming pass and constructor
split; this session finished the `AIEplace.cpp` split all the way down to a thin top module, then
started the same treatment on `Output.cpp`, then wandered into a Logger/console-output cleanup that
grew large on its own. **Goal for the next session: apply the same workflow to the remaining
source files.** Read this whole doc before touching anything — it's long because the *process*
matters as much as the *result*, and skipping straight to "the next file" without internalizing
why things were done a certain way will produce inconsistent work.

---

## 1. Everything is UNCOMMITTED — check first

`git log --oneline -1` → `44612cc sw_only refactor phase 1: ...` is the only commit from this
whole effort. **Every change described below, across two full sessions, is still sitting in the
working tree uncommitted.** Run `git status` before doing anything — don't assume a clean start.
Ask Mark whether to commit what's here before piling more on top; he's been explicit that commits
happen only when he says so, and he's commented "we've done too much already, commit" once already
this arc, so he may want another checkpoint before round 3 starts.

---

## 2. The workflow, spelled out

This is the loop that produced everything below. Follow it for every file/function you touch:

1. **Read the whole function first.** Don't guess at what it does from the name — actually read
   it, including comments (they often carry hard-won context, e.g. "this deviates from XPlace on
   purpose because...").
2. **Identify what it's actually doing** — is it one coherent job, or several jobs stacked in
   sequence? A function doing 3+ unrelated things (parse + validate + format + write, say) is a
   split candidate. A function that's just long because the *one thing it does* is inherently
   detailed (e.g. a big switch/case, a big math derivation) is not — don't split for line-count
   alone.
3. **Propose the split as named private helper methods**, one per job, called in sequence from
   the original function so the original reads as a "table of contents" — a flat list of what
   happens, in order, each line self-explanatory from the function name alone. This is the single
   most-repeated instruction from Mark this whole arc: *"I want the functions to read like a short
   series of steps."*
4. **Copy code verbatim into the new helper** — don't "improve" logic while moving it. If you spot
   a real bug or genuinely dead code while relocating something, **mention it, don't fix it inline**
   unless asked. (Exception: obviously-dead code that becomes provably unreachable *because of your
   own edit* — e.g. an unused include after you moved the one function that used it — clean that up
   as part of the same diff, since leaving it is just noise.)
5. **Rewrite negated compound boolean guards with De Morgan's law + named sub-conditions.** Mark
   explicitly likes this pattern: instead of `if (!(a && b))`, write `bool foo = a; bool bar = b; if
   (!foo || !bar)` — or better, invert the whole function into early-return guard clauses (see
   `checkConvergence()` in `Schedule.cpp` for the canonical example — a 121-line function became 5
   named-predicate helpers each doing one guard check).
6. **Update `AIEplace.h`** — add declarations for new private helpers near the function they
   support (there's a running convention of a `// <FunctionName>'s steps, broken out for
   readability` comment block right before the group). If a helper needs a small struct to bundle
   return values (see `FinalMetrics`, `GraphHyperlinks` — though the latter got deleted, see below),
   declare it as a nested struct in the same spot.
7. **If splitting into a new `.cpp` file** (not just a new function in the same file): add it to
   `HOST_SRCS` in `host/src/sw_only/makeflags.mk`. **This step is easy to forget and produces a
   silent linker failure, not a compile error** — the new `.o` never gets built, then the final
   link fails with "undefined reference." All seven `Placer::` files now live under
   `host/src/sw_only/src/placer/` (see §4) — if you're adding an eighth, put it there too and use
   the `placer/Foo.cpp` form in `HOST_SRCS` (the build system supports subdirectories fine, `git
   mv` handles renames-as-moves cleanly, no include-path changes needed since `-I` is used, not
   same-directory relative includes).
8. **Rebuild clean.** `cd host && make HOST=sw_only clean && make HOST=sw_only` — expect **0
   warnings**. Any warning is a regression from where this codebase currently stands.
9. **Verify the regression trajectory is bit-identical** (see §5 for the exact mechanism and an
   important caveat about when this check itself needs regenerating).
10. **Only then move to the next function/file.** Small verified steps, not big-bang rewrites.

---

## 3. Why `AIEplace.cpp` looks the way it does now

Started this session at ~528 lines (already down from ~1200 at the start of the whole arc). Ended
at **113 lines** — genuinely just: `run()`, `performIteration()`, `performIterationZero()`, the
constructor, `iterationReset()`, `snapshotBestPlacement()`/`restoreBestPlacement()`. That's the
entire top module. Everything else moved to a themed file, all now under
`host/src/sw_only/src/placer/`:

| File | Lines | Contents |
|---|---|---|
| `AIEplace.cpp` | 113 | driver: run loop, constructor, best-solution snapshot |
| `Setup.cpp` | 339 | constructor-phase bring-up (config parse, LEF/DEF read, grid sizing) + `initializePlacement`/`initializeDensityWeight` (moved here from AIEplace.cpp on request — they're pre-run setup, not loop machinery) |
| `Schedule.cpp` | 407 | γ/λ schedule policy, convergence/divergence checks |
| `Step.cpp` | 315 | Nesterov/Barzilai-Borwein step machinery + `logStepDiagnostics` (moved here since it's called from `performNextStep`, not the driver) |
| `Partials.cpp` | 259 | HPWL gradient (WA partials), CPU/simple backends |
| `Density.cpp` | 353 | electric field / density solve |
| `Output.cpp` | 802 | reporting, CSV, visualization export (see §4 below — biggest one, partially done) |

Notable renames/removals along the way:
- `initializeFirstIteration` → `performFirstIteration` → **`performIterationZero`** (final name).
  It's now called once from `run()` right after `initializePlacement()`, *not* gated inside
  `performIteration()` by an `if (iteration == 0)` check anymore — `run()` reads as
  `initializePlacement(); performIterationZero(); while(...) performIteration();`, which is the
  cleanest expression of "iteration zero is a bootstrap step, iterations 1..N are the loop."
- `compareHpwlPartials()` deleted entirely (a one-off CPU-vs-simple-gradient comparison harness,
  superseded by "we'll write real tests"). `compareDensityResults()` also deleted (declared, never
  defined or called — pure dead weight).
- `m_diverged` renamed to **`m_nan_detected`** — it only has one write site (a NaN check in
  `Partials.cpp`), and the old name collided conceptually with the *separate* trend-based
  "divergence guard" mechanism in `Schedule.cpp` (`checkDivergence`/`checkFineDivergenceGuard`),
  which is a different thing (HPWL/overflow trending away from best, not a hard NaN stop).
- `checkConvergence()` (121 lines) rewritten as a guard-clause staircase — see `Schedule.cpp` for
  the pattern to replicate elsewhere.

**Full verification discipline held throughout**: every single one of the above steps was
individually rebuilt (0 warnings) and regression-checked (bit-identical) before moving to the next.
Don't skip this even for renames — a rename that also silently breaks a call site is exactly the
kind of thing this catches.

---

## 4. `Output.cpp` — in progress, NOT finished

This is the biggest file (802 lines) and the one actively being worked when this session ended.
Original survey (before any changes) found two outsized functions:

- **`writeResultsCSV`** (was ~142 lines) → **DONE**. Split into `lookupXplaceReferenceHPWL`,
  `parseDSEParams`, `writeResultsCSVHeader`, `writeResultsCSVRow`. Also: the CSV's spreadsheet
  hyperlink columns (`HPWL_Graph`/`Combined_Graph`/`Placement_GIF`, a `GraphHyperlinks` struct +
  `buildGraphHyperlinkCells()` helper) were **deleted entirely** per Mark ("obsolete... only works
  on the system generated, not portable, never used them"). Note: any *pre-existing*
  `results.csv` file still has the old header with those 3 columns, so newly-appended rows are
  now 3 columns short — a ragged CSV. Not fixed (data-file concern, not code), flagged to Mark.
- **`printFinalResults`** (was ~160 lines) → **DONE**. Split into `restoreBestSolution`,
  `computeFinalMetrics` (returns a `FinalMetrics` struct), `logOverflowDiagnostics`,
  `dumpBestPlacementDensity`, `exportSummaryReports`, `exportVisualizationArtifacts`,
  `writeFinalDesignArtifacts`. Now 24 lines reading as a clean sequence.
- **`initializeFocus`** (was ~93 lines, 5 independent focus-selection blocks) → **DONE**. Split
  into `addNamedFocusNets`, `addRandomFocusNets`, `addRandomFocusNodes`, `addRandomMacroNets`,
  `addRandomFocusIO`, sharing one `std::mt19937& rng` passed by reference (RNG is genuinely
  random/unseeded here — order of the 5 calls doesn't affect algorithm determinism, only which
  nets get flagged for extra visualization detail).
- **`printIterationResults`** (was ~78 lines, 4 distinct jobs) → **DONE**, then iterated on
  heavily afterward (see §5). Split into `printDSEInfoTable`, `printIterationSummaryTable`,
  `exportIterationVisualization`, `appendIterationLog`.

**Not yet surveyed**: everything else in `Output.cpp` — `printWelcomeBanner`, `plotHistories`,
`createRunOutputStructure`, `escapeJsonString`, `generateRunId`, `getMemoryUsageMB`,
`recordInitialHPWL`, `recordIterationResults`. These are mostly small/coherent already from a
first skim, but haven't had the same close read as the rest of the file. Do that pass before
calling `Output.cpp` finished.

---

## 5. The Logger/console-output detour (also `Output.cpp` + `Logger.cpp`)

This wasn't originally planned — it grew out of "why is the console so noisy" and turned into a
real sub-arc. Relevant for the next session because **it changed the regression-verification
signal itself**, twice, and that's a trap worth knowing about in advance.

**What changed:**
- `Logger::setup_logging()` (`Logger.cpp`): the `DETAIL` key is now suppressed by default (was
  firing on *every iteration* via `"BEGIN iteration N"` and `"New steplength estimate"`, despite
  its own comment saying "usually off" — a real bug, now fixed).
- Two new independent config flags, both in `output.*`: `quiet` (pre-existing, global verbosity —
  quiet mode activates only `ERROR`/`CRITICAL`) and **`interactive`** (new — gates just the
  per-iteration live-status table). They're deliberately separate, not one rolled into the other —
  see the reasoning in the conversation if you need to revisit; short version, they answer
  different questions ("how much do I log" vs "am I watching this run live") and are genuinely
  orthogonal.
- `printIterationSummaryTable`: throttled to print every **10** iterations (was tried at 1, then
  100, settled on 10 per Mark), **plus always fires on iteration 1** regardless of throttle — this
  matters, see the "false hang" story below. Transposed to a 2-row table (header + values, was
  7 rows of label/value pairs). Header only prints once (`static bool header_printed`), not on
  every table. `Benchmark`/`BkTrk steps` columns removed (benchmark now lives once in the
  "Benchmark info" table instead). **Final form is plain fixed-width text via a `field()` lambda +
  `std::setw`, NOT a `tabulate::Table`** — see the tabulate gotcha below for why.
- "Initial Placement" console table deleted (`Setup.cpp::initializePlacement`) — pure decoration,
  no info not already covered elsewhere.
- `DataBase::printInfo()`'s table title renamed `"DataBase info"` → `"Benchmark info"`, and it
  now includes a `Benchmark` row (the design name) — added because the per-iteration table's
  `Benchmark` column was removed, so this is now the one place it's shown.
- A "Reading Input" section banner (bold, bordered `tabulate::Table`) added right at the `.def`
  parsing-success point in `DataBase.cpp::readDEF()` — marks the transition from "reading input"
  to "reporting on the parsed design."
- **`Logger::log()` now explicitly flushes `std::cout` after every print** (`Logger.cpp`) — this
  fixed a real bug, not a cosmetic one, see below.

**Two real bugs found and fixed along the way — both worth knowing about if you touch console
output anywhere else in the codebase:**

1. **stdout buffering.** Mark reported the tool "freezing then dumping everything at once" on a
   large design (`mgc_matrix_mult_b`, 146k components — much bigger than the `mgc_fft_1` used for
   regression testing). It wasn't hung — `ps` showed 99%+ CPU the whole time, actively computing.
   `std::cout` isn't line-buffered in every context (piped output, some terminal setups) — it's
   fully buffered, so output queues until the buffer fills or the process exits. Before this
   session, `DETAIL` printed every iteration, generating enough volume to flush incidentally,
   masking the issue. Cutting output down to once every 10 iterations stopped filling the buffer
   fast enough. Fix: explicit `std::cout.flush()` in `Logger::log()`. **If you add any other
   direct-to-cout printing anywhere (not through `Logger::log`), it needs the same treatment, or
   it'll reintroduce this exact symptom.**
2. **tabulate nested-`Table`-in-`Table` width drift.** Attempted to fix column-width consistency
   in `printIterationSummaryTable` (the first print, which includes a header row, rendered
   different column widths than later header-less prints) by setting explicit `.width(N)` per
   column on a `tabulate::Table`. This did NOT work reliably — traced into the vendored tabulate
   source (`third_party/tabulate/include/tabulate/`) far enough to confirm nested tables (our
   status table is itself a cell value inside `Logger::log()`'s outer key/message table) don't
   reproduce identical widths across separate `Table` object instances even with matching explicit
   widths set on every column. Gave up fighting it and switched to plain fixed-width text (manual
   `std::setw`/`std::left`), which is exact, trivial, and has no borders to hide as a bonus.
   **Lesson: don't nest `tabulate::Table`s when you need pixel-stable alignment across separate
   print calls — use plain formatted strings instead.** (Non-nested tables, like `DataBase::printInfo`
   or `printFinalResults`'s summary tables, are fine — the drift is specifically a nested-table
   thing.)
   - Related smaller gotcha: `table.row(0).format().font_style({...})` on its own silently
     re-enables that row's default (visible) border, overriding an earlier or later
     `table.format().hide_border()` call on the whole table — because calling `.format()` on a
     row/column for the first time appears to snapshot default values rather than deferring
     entirely to the table-level cascade. If you style a specific row/column AND want borders
     hidden, chain `.hide_border()` onto that same row/column's format call explicitly, don't rely
     on the table-level call alone.

**Regression-check caveat (important):** the standing verification harness
(`/tmp/regression_config.json` + `/tmp/baseline_trajectory.txt`, see §6) greps console output
lines matching `HPWL \|`/`Overflow \|`/`Final HPWL`. Several of the console-output changes above
(table cadence, table shape) changed what those grep patterns match — **not because the algorithm
changed, but because the log formatting did.** Each time this happened this session, it was
diagnosed by cross-checking the *actual* per-iteration numbers via `iterations.dat` (written by
`appendIterationLog`, unaffected by any console-formatting change, one row per iteration
regardless of throttle/interactive settings) to confirm the real trajectory was untouched, then
the baseline file was regenerated to match the new expected console format. **If your change
touches anything printed to console, expect the trajectory-diff to "fail," and don't panic — cross-
check `iterations.dat` first, and only regenerate the baseline after confirming the real numbers
didn't move.**

---

## 6. Verification commands (copy-paste ready)

```bash
# Build (host-only, no XRT/AIE toolchain needed)
cd /home/msears/phd/AIEplace/vck5000/host && make HOST=sw_only clean && make HOST=sw_only
# Expect: 0 warnings, clean link.
```

```bash
# Deterministic regression config (fixed seed, 60 iters, small design — regenerate if /tmp was
# recycled; /tmp is ephemeral across machine/session boundaries)
cat > /tmp/regression_config.json <<'EOF'
{
  "input": {
    "benchmark": "host/benchmarks/ispd2015/mgc_fft_1",
    "xclbin": "bin/aieplace.hw.xclbin"
  },
  "params": {
    "partials_compute_method": "cpu", "density_compute_method": "cpu", "wirelength_method": "HPWL",
    "ignore_net_degree": 100, "enable_backtracking": true, "enable_momentum": true,
    "enable_filler": true, "auto_enable_preconditioning": true, "precond_coef_escalation": true,
    "enable_density_clamp": true, "dct_normalize": true, "init_gamma": 4, "gamma_schedule": true,
    "gamma_bin_scaled": true, "gamma_ref_grid": 512, "max_threads": 1,
    "init_method": "random_center", "init_spread": 0.25, "init_step_seed": 0.01,
    "density_weight_init_multiplier": 8e-05, "density_weight_min_step": 0.95,
    "density_weight_max_step": 1.05, "adaptation_window": 25, "slow_improvement_threshold": 0.001,
    "high_overflow_threshold": 0.7, "backtrack_max_tries": 10, "backtrack_epsilon": 1.05,
    "convergence_min_iterations": 60, "convergence_max_iterations": 60, "convergence_window": 5,
    "convergence_hpwl_improvement_threshold": 0.01, "convergence_overflow_threshold": 0.07,
    "convergence_iterations": 30, "maximum_utilization": 1.0, "random_seed": 42
  },
  "output": {
    "visualize": false, "iterations_per_export": 10, "quiet": false, "interactive": true,
    "results_dir": "results/regression_baseline", "focus_nets": [], "rand_focus_IO": 0,
    "rand_focus_nets": 0, "rand_macro_nets": 3, "rand_focus_nodes": 0
  }
}
EOF
```

```bash
# Run + check console-scraped trajectory against baseline
cd /home/msears/phd/AIEplace/vck5000
./build/hw/host/sw_only/aieplace_sw_only.exe /tmp/regression_config.json 2>&1 \
  | grep -iE 'HPWL \||Overflow \||Final HPWL' | sed -E 's/ +/ /g' > /tmp/check.txt
diff /tmp/baseline_trajectory.txt /tmp/check.txt && echo IDENTICAL
# If baseline_trajectory.txt doesn't exist yet, or a console-format change legitimately altered
# what this grep matches (see §5 caveat): regenerate it — but ONLY after confirming the real
# numbers via iterations.dat below are unchanged.
```

```bash
# Ground-truth check independent of console formatting — always trust this over the grep above
LATEST=$(ls -td results/regression_baseline/mgc_fft_1/*/ | head -1)
tail -1 "$LATEST/iterations.dat"
# Expected for this exact config (seed 42, 60 iters, mgc_fft_1):
# 060, 3.559e+08, 6.031e-01, 1.461e+03, 4.994e-13, 0
```

---

## 7. What's left — file survey (line counts as of this handoff)

Everything below is `Placer::`-free (no methods on the placement class) except where noted, so
these are lower-stakes to touch than `Output.cpp` was — no risk of breaking the placement loop
itself, "just" readability. Still verify build+regression after each change regardless.

| File | Lines | Notes |
|---|---|---|
| `src/DataBase.cpp` | **948** | **Biggest untouched file.** LEF/DEF/bookshelf parsing, filler placement, `printInfo`/`printOverlaps`/`printNets`/etc. Very likely has 100+ line functions given the file size — start here. |
| `src/Visualizer.cpp` | 336 | Cairo-based PNG export. Only builds under `CREATE_VISUALIZATION` — can't be regression-tested numerically (it draws pictures), so verification here means "still builds, spot-check a generated image looks sane," not the HPWL-trajectory check. |
| `src/Logger.cpp` | 304 | We touched this a lot this session (setup_logging, the flush fix) — worth a read now that it's fresh, but may already be reasonably clean. Lower priority. |
| `src/Grid.cpp` | 205 | Bin/grid geometry. |
| `src/Net.cpp` | 144 | We touched this via the background-task Net::tally cleanup — check it's in the state you expect before assuming it's untouched. |
| `src/DCT.cpp` | 167 | FFT/DCT/IDXST transforms — math-heavy, likely legitimately dense rather than a split candidate; read carefully before assuming it needs work. |
| `src/JsonUtils.cpp` | 75 | Small, probably fine. |
| `src/Common.cpp` | 32 | Small, probably fine. |
| `src/main.cpp` | 20 | Already about as thin as it gets — `construct Placer, printInfo, run, plotHistories, printFinalResults`. Skip unless something jumps out. |

**Two files NOT in the build at all** (confirmed by `grep HOST_SRCS host/src/sw_only/makeflags.mk`
— neither `DebugFramework.cpp` (322 lines) nor `Debugger_orig.cpp` (492 lines) appears there):
investigate before touching. They may be genuinely dead (candidates for deletion, ask Mark first
per his standing "mention dead code, don't delete unless asked" preference) or may be meant for a
build config this session never exercised (e.g. behind `BUILD_XRT` or a debug target) — don't
assume either way without checking.

**One latent, pre-existing, unverified break** (not caused by this session, but worth flagging if
you're in the area): `makeflags.mk`'s `ifdef BUILD_XRT` block still does `HOST_SRCS += GraphDriver.cpp`
— but `GraphDriver.cpp`/`.h` were deleted from `sw_only` back in the original
`HANDOFF_sw_only_cleanup_20260728.md` pass (moved conceptually to `pl_algo`, never actually needed
there). Since `BUILD_XRT` is off by default and untested this whole arc, this would only surface as
a build failure if someone builds with XRT support — which the original handoff already flagged as
"not compile-verified." Not this session's job to fix, just know it's there.

---

## 8. Mark's working style — read this before starting

- **Wants functions to read as a named sequence of steps** — this is the single most-repeated
  instruction across both handoffs. When in doubt about whether to split something, this is the
  test: "does this read like a short list of what happens, in order?"
- **Verify every step, not just at the end.** Build clean + regression bit-identical (or
  ground-truth-verified via `iterations.dat` per the §5 caveat) after each meaningful change, not
  batched at the end of a session.
- **Surgical changes.** Touch only what the current task requires. If you notice unrelated dead
  code or a pre-existing bug while working nearby, *mention it, don't fix it* — unless he
  explicitly says to (he does say yes to these often, but wants to be asked/told, not surprised).
- **Explains actual behavior before proposing changes to it.** Several turns this session were
  Mark asking "why is X ordered this way" or "does this actually stop execution" before deciding
  what to do — answer the question fully (with file/line references) before jumping to a fix.
- **Pushes back on doing more than asked, appreciates it when you push back on him too.** E.g. he
  asked to literally move `++iteration` to the end of a function; that would have silently changed
  scheduling behavior elsewhere in the codebase (not just been a refactor), so the response
  explained the risk and implemented a safer equivalent instead of blindly complying. He was fine
  with that — the trust here is "tell me what's actually going on," not "do exactly what I said no
  matter what."
- **Iterates in small increments and changes his mind based on what he sees** — e.g. the
  `X` throttle value in `printIterationSummaryTable` went 1 → (his own edit) 10 → (my edit) 100 →
  (his instruction) 10 again over the course of a few turns, tuning by feel after seeing real
  output each time. Don't be precious about a value you picked two turns ago if he adjusts it.
- **Uses background task spawning** (`spawn_task`) for tangential low-priority cleanup — one
  landed *mid-session* in the same working tree (the `Net::tally`/debug-`cout` cleanup, touching
  `Net.h`/`Net.cpp`/`Grid.cpp`/`DataBase.cpp`). **Always check `git status` for surprises** — don't
  assume the tree only contains what you personally changed.
- **Commits only when explicitly asked**, and has asked for descriptive multi-paragraph commit
  messages explaining the *why*, not just the *what* (see the one existing commit, `44612cc`, as
  the template).

---

## 9. Suggested first move for the next session

Read `src/DataBase.cpp` end to end (948 lines, biggest untouched file, has real `Placer`-adjacent
weight since it's the design database everything else queries). Survey its functions the way §4
surveys `Output.cpp` — list function names + line counts, flag the 2-3 biggest/most-tangled ones,
propose splits, and present that survey to Mark before diving in, matching how every file-selection
decision happened this session (a quick "here's what's in this file, here's what I'd tackle first"
before writing code). Then follow the loop in §2.
