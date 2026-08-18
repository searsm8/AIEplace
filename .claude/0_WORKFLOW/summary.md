# Summary — project status at a glance
*Updated 2026-08-17. Branch `pl_algo`. If this file and the code disagree, the code wins — say so.*
> **Soft cap — one in, one out.** Current state only, ~2 screens. To add a line, remove one:
> superseded snapshots & dated "Closed" narration → [[journal.md]]; finished task sections → [[history.md]].
> If it's done and no longer live context, it isn't "where things stand" — evict it.

## Two threads
- **sw_only** — CPU golden reference; goal is to match XPlace. **FROZEN 2026-08-17 (Mark's call).**
  Parity reached; no further algorithm/behaviour changes without an explicit decision. Cleanup,
  tooling, docs and tests are NOT frozen. `make test-regress` bit-identical is now the contract.
- **pl_algo** — move the whole placement iteration onto the PL. **The active thread**, starting at
  #20 step 1 (restore `dumpScheduleTrace()`; `sched_verify` currently validates a 07-18 golden and
  always will). Why freezing came first: pl_algo's algorithm is pinned to the 2026-07-14 sw_only,
  so every further sw_only change was another port.

## Where sw_only stands (frozen — this is the final state, not a waypoint)
- **Median HPWL ratio 1.0097, mean 1.0126, over ALL 28 ISPD designs** (legal-vs-legal;
  `results/DSE_20260817_223934`, 2026-08-17, 28/28 succeeded in 80.6 min). 22/28 within ±2%,
  **better than XPlace on 6**. Nothing is unscored — #26 closed the 9-design fence hole.

  | | 2026-08-15 (`DSE_20260815_161306`) | **now (`DSE_20260817_223934`)** |
  |---|---|---|
  | **ISPD2005 (8)** | 1.0053 / 1.0052 | 1.0057 / 1.0057 |
  | **ISPD2015 (20)** | 1.0113 / 1.0138 | 1.0101 / **1.0153** |
  | **all ISPD (28)** | 1.0095 / 1.0113 | **1.0097 / 1.0126** |

  ⚠️ **This run is slightly WORSE on the mean (+0.13 pp) and flat on the median, and that is the
  honest result of three faithfulness fixes** (#32's 7a+7b, #3's cap→scale) landed together on
  2026-08-17. **18 designs worse, 7 better, 3 unchanged**; within-2% 23→22; still better than XPlace
  on 6. Biggest movers: `mgc_pci_bridge32_a` +1.11 pp and `mgc_superblue19` +1.04 pp against
  `mgc_superblue12` −0.70 pp. **Matching XPlace more closely did not score better here** — which is
  the expected shape of a faithfulness change, not a defect, and `CLAUDE.md`'s standing rule is to
  prefer XPlace's formulation over an ad-hoc win. Recorded rather than tuned away.

  **Attribution is partial, because the three fixes were bundled into one run (Mark's call, to
  avoid paying for two 80-minute suites).** One clean split does survive: **all 8 ISPD2005 designs
  run at `td = 1.0`, where #3's `min(ρ,1)·td` and the old `min(ρ,td)` are identically equal**, so #3
  is a *provable* no-op there — confirmed independently by `mms_adaptec1`'s regress baseline coming
  back bit-identical. Therefore **ISPD2005's +0.05 pp is 7a/7b alone**, and only the ISPD2015 delta
  carries #3. Separating #3 from 7a/7b on ISPD2015 needs one more suite run with #3 reverted.

  *(median / mean.)* **The mean is quotable** as of #27's fix. **#31 grid cap (2026-08-15):** sw_only
  now caps ANY grid at `num_rows` like XPlace (`Setup.cpp`; was applied only to the auto path). 13 of
  20 ISPD2015 designs are row-capped and were running at 512 — now 128/256. Net: ISPD2015 mean
  1.0189 → 1.0138, **GP-ratio mean 1.0223 → 1.0047**, and the overflow *signal* (which drives the γ/λ
  schedule) now matches XPlace's on the std-cell designs. Mixed per-design (fft_a −2.7pp, fft_2
  −1.4pp; des_perf_1 +1.8pp, fft_1 +0.6pp), net better. ISPD2005 bit-identical (no cap fires).
  → [[_NEW_REPORT_31_overflow_stall_grid_20260815.md]] ⚠️ **The 9 fence designs are scored on
  the fence-STRIPPED variant on both sides** (as the XPlace paper's †-marked table is) — a fair
  tool-vs-tool comparison, but **not** legal ISPD2015 solutions: we place 59–94% of their constrained
  cells outside their fence. ⚠️ The v3/v4 TSVs deliberately still hold the broken `matrix_mult_a`
  row — see #27.
  → [[_NEW_REPORT_26_fence_regions_20260811.md]], [[REPORT_26_precond_always_on_20260811.md]],
  [[REPORT_27_matrix_mult_a_stray_space_20260811.md]]
  <details><summary>Superseded headline snapshots (1.0113/1.1218 · 1.0090/33-designs) → [[journal.md]]</summary>

  Moved to [[journal.md]] to keep summary.md under its soft cap — the retraction trail is preserved
  there verbatim, dated most-recent-first.
  </details>
- ⚠️ **MMS (16 designs) IS UNMEASURED ON THE FROZEN BINARY — the last numbers are 2026-08-14
  (16/16, DP ratio median 1.0138 / mean 1.0161) and they no longer describe the code.** This is the
  one gap left in the freeze. **The 28-design ISPD re-run on 08-17 did not include MMS**
  (`DSE_20260817_223934` is tier1+tier2 only), and both of that day's algorithm changes reach MMS:
  - **#32's 7a/7b** (best-solution tracking on `v_k`) affects **all 16** — it is design-independent.
  - **#3's cap→scale** affects the **8 that run at td<1**: `adaptec5`/`newblue4`/`newblue5` (0.5),
    `newblue1`/`newblue3`/`newblue6`/`newblue7` (0.8), `newblue2` (0.9). The other 8 are td=1.0,
    where `min(ρ,1)·td` and `min(ρ,td)` are identically equal — which is exactly why
    `mms_adaptec1`'s regress baseline came back bit-identical and is **not** evidence for the rest.
  **Fix: `make dse --designs tier3`.** Until then MMS has no quotable number.
  <details><summary>Superseded 2026-08-18: "Still valid — checked 2026-08-17 … NOT stale"</summary>

  That check was correct **about #31 only** — the grid cap does fire on zero of the 16 (every MMS
  design's requested grid is at or below its cap; tightest `newblue1` 930 rows → cap 512, requested
  512; loosest `newblue3` 4182 → 4096, requested 2048; re-derive from `benchmarks._ROWS` vs
  `CoreRow` counts in the bookshelf `.scl`). It was written before #32/7a-7b and #3 landed later the
  same day, and those are what invalidate the numbers. **The lesson: "verified still valid" carries
  the date of the thing it was checked against, not the date it was written.**
  </details>
  ⚠️ The only MMS run since is `newblue1` under `.claude/2_ARTIFACTS/GIFS_20260817_211039/` — a
  **viz/#14 run**, not a suite result; don't score from it.
- Landed: two-phase mixed-size flow + LP macro legalization; #19's two XPlace faithfulness fixes
  (overflow excludes fillers; the γ/λ throttle gates on preconditioner κ). Both toggles retired
  2026-08-07 — faithful behaviour is now unconditional.
- **The preconditioner is ON for every design as of `3c70b38`** — `auto_enable_preconditioning` is
  gone. It had been OFF on all 28 ISPD designs since 638b9a8, which also froze `precond_coef` at
  1.0, which is the *only* thing that carries κ out of the γ/λ throttle window. Setting
  `enable_preconditioning = false` is now a diagnostic only, and a trap.
- **#23 — FIXED 2026-08-10: `init_step_seed` is in SITE WIDTHS, not raw DBU.** Committed `ba0ce6a`.
  **4 of the 5 dead ISPD2015 designs now converge** (`mgc_superblue11_a` 842 it, `12` 921, `14` 782,
  `16_a` 772 — all previously frozen at ~2135 iterations of `nan_metrics`). The 5th,
  `mgc_des_perf_b`, **places but does not converge** — `divergence_guard` at 889. XPlace prescales
  all coordinates by site width, so its `args.lr = 0.01` always meant 0.01 *sites*; ours meant
  0.01 DBU and underflowed. **Bookshelf `Sitewidth = 1` ⇒ the whole MMS suite is bit-unchanged**;
  only the two ISPD2015 regress baselines were regenerated.
  ⚠️ Units, **not** precision — scaling coordinates buys nothing in float32; shifts do (that is #15).
  ⚠️ Rising HPWL on the recovered designs is **not** a regression: the old number was the untouched
  initial placement (cells stacked at centre, overflow 0.9998).
  → [[REPORT_23_site_width_seed_20260810.md]]. Re-run + re-score **DONE 2026-08-10**
  → [[_NEW_REPORT_performance_snapshot_20260810.md]]
  <details><summary>Superseded: "`mgc_des_perf_b` converges in 825 iters, `mgc_superblue11_a` in 849"</summary>

  `mgc_des_perf_b` **does not reproduce** as converging: under the manifest's own config
  (`gen_suite_configs.py`, seed 42) it reaches `divergence_guard` at 889 iterations. Verified twice
  — standalone and in the 28-design suite. Which config produced the 825-iteration claim is
  unknown. `mgc_superblue11_a`'s iteration count also differs (842, not 849).
  </details>
- **#26 — fence regions: scored, measured, priced, and CLOSED** (2026-08-12). **Decision (Mark): we
  do NOT implement fence regions — we ignore them, as XPlace does, and say so.** ⚠️ `CLAUDE.md`
  carries only the two *operational* rules (how to regenerate `ispd2015_fix`, keep the fenced
  originals); the **decision and its reasoning live in `history.md` #26 and the report**, so this
  bullet is the always-loaded statement of it. Three things worth carrying forward:
  - **`ispd2015_fix` is GENERATED, not downloaded** — `cd ~/phd/Xplace/data && python3
    fix_ispd2015_route.py` builds all 20 from the raw data (a symlink to our own benchmarks).
    The regenerated `mgc_pci_bridge32_b` DEF is byte-identical to the copy its reference came from.
  - **We violate the fences badly: 59–94% of constrained cells land outside their region.** The
    contest's own legalized solutions score 0 of 190,010 through the same checker
    (`vck5000/tools/fence_check.py --expect-legal`), which is what makes that a measurement.
  - **~10 pp of our margin on those 9 is the constraint, not the placer** — we beat the contest's
    legal solutions by 2.6% on the 11 unfenced designs and 12.5% on the fenced 9.
  → [[_NEW_REPORT_26_fence_regions_20260811.md]]

## Where pl_algo stands — THE ACTIVE THREAD as of 2026-08-17
- All datapath modules written, HLS C-synthesis clean, each verified against the sw_only golden.
- **Start at #20 step 1.** Two decisions are still open and step 1 wants them answered (#20 §10):
  is v1 *"phase-1 GP, device-resident, bit-comparable"* or does it include phase 2 + backtracking;
  and does pl_algo pin to a named sw_only commit or chase HEAD? The freeze makes pinning cheap —
  pin to the frozen HEAD and `sched_verify` becomes meaningful again.
- Items re-filed here from the sw_only list on 2026-08-17 (marked **↪ pl_algo** in tasks.md):
  **#15** entirely (net-local frames — PL precision, expects no sw_only HPWL movement), **#23**'s
  initial-step mirror, **#19**'s two remaining bullets (the live pre-#19 dff gate in
  `host/src/pl_algo/`, and the fixture-trace regeneration blocked on step 1), and **#6c** operator
  skipping (now an *Improvements* bullet, but wanted during step 6, not after).
- **#20 — do NOT compose Stage 5 first.** pl_algo's algorithm is frozen at the **2026-07-14**
  sw_only, and `dumpScheduleTrace()` — the mechanism that would catch the drift — was deleted from
  sw_only as dead code on 07-28. So `make test`'s green `sched_verify` checks a **07-18 golden and
  always will**. Restore the trace and the tier-1 coverage (**3 of 17 modules today**) first.
  → [[_NEW_REPORT_pl_algo_stage5_assessment_20260806.md]]
- `top.cpp` is still a mode-switch bring-up scaffold; the host owns the γ/λ schedule, one
  round-trip per iteration.
- `make host HOST=pl_algo` needs one `make clean HOST=pl_algo` first (stale `.d`, not a source break).

## Closed 2026-08-12
- **`tools/` triaged; every survivor now carries a status** (`331f1df`, closes a #1 bullet). The dir
  had grown past the point where useful and stale were distinguishable by inspection. 5 stale tools
  deleted — `xplace_gp_ref.py`, `collate_mms.py`, `make_scorecard.py`, `legalize_swonly_mms.sh`,
  `bench_swonly.sh` — each with zero references from any code, Makefile or skill, and each
  superseded by a named replacement. 2.1 MB of `adaptec1_*.png` run output was **moved, not
  destroyed**, to `.claude/2_ARTIFACTS/legacy_density_heatmaps/`. `tools/README.md` now has a
  **live / dormant** row for all 38 survivors, checked mechanically, so an unlisted tool is a
  visible defect. Kept as *dormant* rather than deleted: the OpenROAD opendp island (independent
  legalizer, binary still installed), `eval_overflow_xplace.sh`, `vcd_to_svg.py`.
- **#26 — fence regions** (details above). Decision: ignore them, document it, keep both benchmark
  variants. Guards against re-deriving this: the operational rules in `CLAUDE.md`, a warning in every
  run log, and both ISPD2015 harnesses now failing loudly (with the regeneration command) when
  `ispd2015_fix` is missing or older than the raw data.
- **The scoring pipeline is now TRACKED, in `vck5000/tools/`** — 10 scripts moved out of the
  gitignored `.claude/2_ARTIFACTS/`, where the guards above would have protected exactly one machine.
  `tools/README.md` has the run order and the rule for what belongs there: *anything that produces a
  number we quote is tracked; one-off experiment runners stay with the output.* Code moved, results
  did not — every runner writes to `$ARTIFACTS`, still defaulting to `.claude/2_ARTIFACTS/`.
  The move surfaced three live path bugs, all fixed: `run_suite.sh` and `run_lgdp_suite.sh` defaulted
  to `vck5000/2_ARTIFACTS/` (gone since 08-07), and `run_xplace_ref.sh` wrote ISPD2005 references to
  a **different file** than `run_xplace_ref_2015.sh` wrote the ISPD2015 ones. `analyze_full44.py` now
  exits loudly on a missing artifacts dir instead of printing a table of dashes.

## Closed 2026-08-17
- **#24 CLOSED** — best-solution tracking now matches XPlace's `get_best_solution`: three trackers
  (`best_primary`/`best_aux`/`best_rollback`), each with its own geometry buffer, one shared
  selection rule. Fixed a shared-buffer defect (17/29 runs shipped a placement the log didn't name)
  and a torn-restore defect (reported overflow described the last iteration, not the shipped one).
  MMS suite re-run 2026-08-14: 16/16, DP ratio median 1.0138 / mean 1.0161. Two remaining
  faithfulness gaps (snapshot position u-vs-v; `BEST_SOL_MIN_ITER` absolute-vs-phase-relative) and
  the A/B's n=2 spun off to **#32** rather than left open here.
  → [[_NEW_REPORT_24_best_solution_trackers_20260810.md]]. Superseded prior narration: [[journal.md]].

## Open
- **#14 — zoomable visualizer: CLOSED 2026-08-17**, archived to [[history.md]]. Node-lock
  (`generate_viz.py --lock <name>|index:N|most-moved`) re-centres the window on one tracked cell
  every frame — verified at **0.0000 px** from the reticle across all 31 newblue1 frames and all
  three generations. `--add-view` renders N windows in one pass (byte-identical to N separate
  invocations). `MIN_SIZE` cleared: at zoom it floors **0%** of std cells, fillers and macros; the
  only nodes floored are 337 **zero-area** bookshelf terminals, where that is correct.
  ⚠️ **The dump format grew a file**: `names_gen<N>.txt` (sparse `<index> <name>`, no fillers),
  written per generation because the phase-2 boundary reshuffles indices. Dumps made before
  2026-08-17 have no names and `--lock` will refuse them — re-run the placement.

*Rewritten 2026-08-17. This section was headed "Newly open" and 4 of its 6 entries (#25, #28, #29,
#31) were **closed and already archived to [[history.md]]** — the most-read file in the repo was
advertising finished work as open. Their full text is in history.md; only live items are below.*

- **#32 — 7a and 7b DONE 2026-08-17, suite re-run, committed; only the A/B is left.** Mark's call, which is
  the explicit decision the freeze requires. **The u-vs-v question is settled: we track on v.**
  `snapshotBestPlacement()` stores `probe_pos` and `recordIterationResults()` measures HPWL there
  (new `at_probe` arg threaded through `computeTotalWirelength`/`computeWirelength_HPWL` in
  `common/`, defaulting false so the pl_algo host is untouched), so HPWL, overflow and the stored
  solution finally describe **one** position — XPlace's single `p`/`v_k`. 7b made
  `BEST_SOL_MIN_ITER` phase-relative (`phaseIteration()`), matching `param_scheduler.py:393`;
  `Schedule.cpp` already did this everywhere and `Output.cpp` was the lone holdout.
  **`mms_adaptec1`: 1259 → 1274 iterations, HPWL 6.366e7 → 6.344e7 (−0.35%), overflow 0.0405 →
  0.0453.** All three regress baselines regenerated; both tiers, pl_algo tier-1 and the pl_algo
  host build all green.
  **Also deleted `syncProbeToCommitted()`** — folded into `restoreBestPlacement()`, which now
  restores BOTH halves of the (node_pos, probe_pos) pair. The comment forbidding that fold claimed
  it perturbs phase 2 (`1325 → 1288 iters`); an A/B isolating the fold from 7a/7b found it
  **bit-exact identical** on all three designs, because the phase-2 restore is immediately followed
  by `freezeMovableMacros()` + `reinitializeStdCells()`, which overwrite `probe_pos` before anything
  reads it. That claim is now retracted in the code.
  ⚠️ **Suite cost, measured:** bundled with #3 into `DSE_20260817_223934` — mean **1.0113 → 1.0126**,
  median flat. ISPD2005 (#3 is a no-op there, td=1) moved **+0.05 pp on 7a/7b alone**. Faithful, and
  slightly more expensive; kept per `CLAUDE.md`'s prefer-XPlace rule. Details in the headline above.
  ⚠️ **Still open: widen the `best_aux_max_hpwl_ratio` A/B (n=2).** Needs ~8–10 designs run blind;
  the trace projection cannot identify flippers near the 0.5% budget. → tasks.md #32.
  ⚠️ **pl_algo inherits this**: its resident loop must snapshot v, not u.
- **#3 — fixed-density cap-vs-scale: CLOSED 2026-08-17.** Now a scale (`min(ρ,1)·td`), matching
  `initializer.py:82`; was a cap (`min(ρ,td)`). Bundled into the same suite re-run as #32's 7a/7b
  (Mark's call). Provably a no-op at td=1, so all 8 ISPD2005 designs are untouched by it —
  `mms_adaptec1` re-baselined bit-identical. The remaining open item in #3 is the **per-row site
  model** (ragged cores on 11 of 16 MMS designs), unrelated. See tasks.md #3.
- **#30 — collapse the two suite runners.** Unblocked by the passed cross-check; awaiting Mark's
  go-ahead, MMS/tier-3 spot-check first. Everything else in #30 landed.

## Also open
- **#21 — repo restructure** (host to top level, one host binary). Proposal only, nothing started.
  **Merge `origin/geert` before anything else** — one `.gitignore` conflict today, 25 hand-moved
  files after.

