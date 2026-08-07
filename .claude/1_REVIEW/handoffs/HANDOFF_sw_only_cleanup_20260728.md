# Handoff — sw_only code cleanup / AIEplace.cpp readability pass — 2026-07-28

Continuation of the overnight cleanup (see `_NEW_overnight_cleanup_20260727.md`). This session's
work is **not yet committed** — everything below is a working-tree diff. `git status` shows 33
files changed (+995/−1919), plus 2 deleted files (`GraphDriver.h`/`.cpp`).

**Everything in this handoff has been build+regression verified at each step** (see "How to
verify" below) — the placement algorithm's numerical behavior has not changed at all this
session, only naming/structure/comments.

---

## 1. What happened this session, in order

1. **`run()` / `performIteration()` refactor** (approved pattern going forward): the top-level
   functions now read as a named sequence of calls — "table of contents" style — with detail
   pushed into small private helpers. `run()` is 8 lines. `performIteration()` is ~20 lines
   calling `initializeFirstIteration()` and `updateSchedule()`.
2. **`updateSchedule()`'s boolean rewritten** with De Morgan's law + named sub-conditions instead
   of a negated compound (`skip_update` → `perform_update` built from `past_warmup`,
   `forces_balanced`, `every_third_iter`). User liked this pattern enough to ask for a sweep of
   other unwieldy conditionals (done — see `updateDensityWeight`'s jolt/precond-escalation guards
   and `recordIterationResults`'s best_fallback tiebreak in Output.cpp, all now named booleans).
3. **Removed ALL markv1/AIE hardware-acceleration scaffolding from sw_only**, per explicit user
   directive ("All PL code belongs in pl_algo"). Investigated first: confirmed `pl_algo` already
   has its own, unrelated XRT device-init code (`host/src/pl_algo/src/Driver.cpp`, single unified
   "top" kernel) — nothing needed to be moved there. What got deleted was **markv1's** orphaned
   host driver (multi-CU `PartialsGraphDriver`/`DensityGraphDriver` streaming pattern), stranded
   in `sw_only` since the markv1→sw_only rename (no `host/src/markv1` exists to hold it; git
   history preserves it if ever needed):
   - Deleted `GraphDriver.h`/`.cpp` outright.
   - Removed `initializeAIEAccelerators()`, `computeElectricFields_AIE()`.
   - Removed `computeHpwlPartials_AIE()`/`computePartials()`/`receivePartials()` — **these were
     already dead**: `computeHpwlPartials()` called a function named `computeAllPartials_AIE()`
     that was declared but never defined; the real (never-called) definition was misnamed
     `computeHpwlPartials_AIE`. This hadn't compiled under `USE_XILINX_XRT` in a long time.
   - Removed the `Packet`/`PacketIndex` marshaling layer from `DataBase` (`initializePacketContents`,
     `prepareNetGroup`, `storeNetGroup`, `mv_packet`, `m_packet_count`) — existed only to feed the
     dead AIE path.
   - Removed the now-orphaned `Common.h` constants (`VEC_SIZE`, `NETS_PER_GROUP`, `LCM_BUFFSIZE`,
     `INPUT_PACKET_SIZE`, `OUTPUT_PACKET_SIZE`, `PARTIALS_GRAPH_COUNT`, `TEST_NET_SIZE`,
     `BINS_PER_ROW`/`COL`) and `DataBase.h`'s `MIN/MAX_AIE_NET_SIZE`. Kept
     `INITIAL_LOCAL_DENSITY_WEIGHT` (still used by `Grid::init`, unrelated to AIE).
   - `partials_method`/`density_method == "aie"` now fail with a message pointing at `pl_algo`
     instead of implying "recompile with XRT."
   - **Mid-flight mistake, caught by the build**: the first deletion pass in `DataBase.cpp` also
     swept up all the LEF/DEF/Bookshelf parser callback definitions (`lef_version_cbk`,
     `add_def_component`, etc.) — they were physically interleaved in the same file region and
     indented in a way that dodged the boundary check. Build failed immediately
     (`undefined reference to vtable for DataBase` — classic "a declared virtual has no
     definition anywhere" signature). Restored from a pre-edit backup and redid the removal with
     verified, unique line anchors. Worth knowing about if you see anything that looks
     LEF/DEF/Bookshelf-adjacent go missing — it didn't, but that's why the diff has a slightly
     unusual shape in `DataBase.cpp`.
4. **Removed two flagged-dead-code items** (user-approved after investigation):
   - `Net::sortPositionsMaxMinX/Y` + `DataBase::sortPositionsMaxMinX/Y` — the `DataBase`-level
     wrapper had **zero callers even before this session** (pre-existing dead code); `Net`'s
     versions became transitively dead once the AIE packet code (their only real caller) was
     removed.
   - The DSE CSV's `"Partials AIE Time (sec)"` column and its
     `Logger::getFunctionTime("computeAllPartials_AIE")` lookup.
5. **Constructor decomposition** (the big one). `Placer::Placer` went from ~160 lines of inline
   grid-sizing math, preconditioner-normalization loops, and gamma finalization down to reading
   like a table of contents. New private helpers (declared in `AIEplace.h` right after
   `loadConfiguration`, defined in `AIEplace.cpp`):
   - `bool resolveGridResolution()` — explicit `bins_per_row` override, or defer to formula.
   - `void loadDesignDatabase()` — read LEF/DEF, apply benchmark max_util, add fillers.
   - `void analyzeDesignArea(bool bins_auto)` — movable/fixed area stats, macro count, ePlace
     formula grid size (applies to `bins_per_row` when `bins_auto`).
   - `void configurePreconditioner()` — auto-enable decision from `num_movable_macros`.
   - `void configureGammaSchedule()` — grid-independent `base_gamma`, `gamma`/`inv_gamma`, LUT init.
   - `void initializeVisualization()` — wraps the `#ifdef CREATE_VISUALIZATION` block entirely
     (the `#ifdef` no longer leaks into the constructor's top-level flow).
   - `void setupDesign()` — wraps `loadConfiguration` + `resolveGridResolution` +
     `loadDesignDatabase` + `analyzeDesignArea` + `configurePreconditioner` as one `TIME_FUNCTION()`
     unit (see next item).
   - `void setupGrid()` — **added by the user directly** (not by me) — builds the `Grid`, sets
     clamp density, sets `die_size`. I added its header declaration and rebuilt/verified; I did
     not write its body.
6. **`db_IO_time` eliminated in favor of the existing `TIME_FUNCTION()`/`Logger::getFunctionTime()`
   idiom** (already used elsewhere in the codebase, e.g. `algo_time` reads
   `Logger::getFunctionTime("run")`). `setupDesign()` opens with `TIME_FUNCTION()`; both consumers
   (DSE CSV column, results table) now read `Logger::getFunctionTime("setupDesign") / 1.0e6`
   directly instead of a manually-threaded `pgrm_start_time`/`getInterval()` member. `pgrm_start_time`
   itself stays — it's also the anchor for `total_runtime` elsewhere, unrelated to this cleanup.

Current constructor (as of end of session):
```cpp
Placer::Placer(std::string config_filepath)
{
    m_config_filepath = config_filepath;

    setupDesign();
    Logger::log_info("Database setup time: " + std::to_string(Logger::getFunctionTime("setupDesign") / 1.0e6) + " s");

    setupGrid();
    createRunOutputStructure();
    configureGammaSchedule();
    initializeVisualization();
}
```

---

## 2. How to verify (re-run these before trusting any further changes)

Build (host-only, no XRT/AIE toolchain needed):
```bash
cd /home/msears/phd/AIEplace/vck5000/host && make HOST=sw_only clean && make HOST=sw_only
```
Expect: **0 warnings**, clean link.

Deterministic regression — trajectory has been bit-identical (same md5) after every single change
this session and last:
```bash
cd /home/msears/phd/AIEplace/vck5000
./build/hw/host/sw_only/aieplace_sw_only.exe /tmp/regression_config.json 2>&1 \
  | grep -iE 'HPWL \||Overflow \||Final HPWL' | sed -E 's/ +/ /g' > /tmp/check.txt
diff /tmp/baseline_trajectory.txt /tmp/check.txt && echo IDENTICAL
```
`/tmp/regression_config.json` = `run_config.json` with `random_seed:42`,
`min=max_iterations:60`, `benchmark:mgc_fft_1`, `visualize:false`. **Note**: `/tmp` is ephemeral —
if this machine/session has recycled, `/tmp/baseline_trajectory.txt` and
`/tmp/regression_config.json` won't exist; regenerate the config as above and capture a fresh
baseline from the current committed state before making further changes, or the diff will have
nothing to compare against.

---

## 3. Where things stand in `AIEplace.cpp` — and what's next

File is now ~1200 lines (was ~1207 before this session started, but the *shape* is completely
different — the constructor and top-level loop are now readable; the internals of several
mid-sized functions haven't been touched). Function inventory (line counts) was just surveyed;
biggest remaining functions: `checkConvergence` (121 lines), `updateDensityWeight` (93),
`initializePlacement` (73), `logStepDiagnostics` (64), `estimateInitialStep` (44),
`checkDivergence` (49), `updatePrecondWeights` (47), `computeLipschitzEstimate` (47).

**Discussed but not yet decided/started** — presented three options to the user, awaiting their
pick:

1. **Split out a `Schedule.cpp`** (biggest structural win, matches existing precedent —
   `Density.cpp`/`Partials.cpp`/`Output.cpp` were all split out of `AIEplace.cpp` the same way).
   Candidate contents: `updateSchedule`, `updateDensityWeight`, `updatePrecondWeights`,
   `checkConvergence`, `checkDivergence`, `checkOverflowPlateau`, `configureGammaSchedule` — the
   γ/λ/convergence *policy*, currently scattered through the file.
2. **Split out a `Step.cpp`/`Optimizer.cpp`** for the Nesterov/Barzilai-Borwein step machinery:
   `combineGradients`, `enforceDieBoundaries`, `advanceIterationState`, `stepAllNodes`,
   `estimateInitialStep`, `performNextStep`, `computeLipschitzEstimate`.
   Do both #1 and #2 and `AIEplace.cpp` shrinks to just: `run`, `performIteration`,
   `initializeFirstIteration`, the constructor + its setup helpers, `initializePlacement`,
   `initializeDensityWeight`, `iterationReset`, `snapshotBestPlacement`/`restoreBestPlacement`,
   `logStepDiagnostics` — a genuinely small "driver" file.
3. **`checkConvergence()` internals** (121 lines, the single biggest function in the file) — it's
   a staircase of independent stop-conditions (max-iter backstop, min-iter floor, NaN check,
   coarse divergence backstop, fine-grained divergence guard, overflow countdown) — same
   guard-clause-with-named-predicates pattern already applied to `performIteration()`:
   ```cpp
   bool Placer::checkConvergence()
   {
       if (reachedMaxIterations())      return true;
       if (iteration < min_iterations)  return false;
       if (hasNaNMetrics())             return true;
       if (hasCoarseDivergence())       return true;
       if (checkFineDivergenceGuard())  return true;
       return checkOverflowCountdown();
   }
   ```
   It also has a **~19-line dead, commented-out "stagnation detection" block** sitting in the
   middle (currently disabled, explicitly noted in-code as "need to let density weight grow
   longer" — a pre-existing TODO-ish block, not something this session touched or removed).

**Small unrelated leftover, not yet fixed**: there's a stale doc comment still sitting above
`loadConfiguration()` in `AIEplace.cpp` — a leftover `/** @brief Constructor... @param
config_filepath */` block from before the constructor got split up (several sessions ago),
immediately followed by the real `loadConfiguration()` doc comment. Harmless (just a dangling,
mismatched comment — Doxygen would attach it to nothing useful) but worth a two-line delete
whenever someone's in that area.

---

## 4. User's working style / preferences established this session (worth knowing)

- Wants top-level functions to read as a named sequence of calls ("pseudocode"/"table of
  contents") with detail hidden in small, well-named private helpers — 1, maybe 2 levels of
  actual logic per function, no more.
- Prefers rewriting boolean logic with De Morgan's + named sub-conditions over a bare `!(...)`
  negation.
- Explicitly OK with removing dead/superseded code entirely rather than keeping it "just in
  case" — reasoning given: "git history will preserve it if we ever need it."
- Wants every non-trivial change build + regression verified before moving on (this has been
  standard practice all session — don't skip it going forward).
- Reacted very positively to being shown "first impressions as a fresh reader" of the file — a
  useful framing if asked for another read of overall code quality.
