# Summary — project status at a glance
*Updated 2026-08-19. Branch `pl_algo`. If this file and the code disagree, the code wins — say so.*
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
- 🏆 **GOLDEN — median HPWL ratio 1.0097, mean 1.0126, over ALL 28 ISPD designs** (legal-vs-legal,
  2026-08-17, 28/28 in 80.6 min). 22/28 within ±2%, **better than XPlace on 6**. Nothing is
  unscored — #26 closed the 9-design fence hole.
  **Archived 2026-08-19 to `.claude/2_ARTIFACTS/results/GOLDEN_sw_only_frozen_20260817/`** (was
  `vck5000/results/DSE_20260817_223934`, which no longer exists). **Read the README there before
  quoting it** — it carries the two caveats, the per-tier split, and the reproduce command. Stats
  were recomputed from its `results.csv` at archive time, not copied from here, and they match.
  ⚠️ The data was produced at `821a9c8`; the frozen commit is `64cfa0e`, two later and
  **behaviour-neutral by its own acceptance criterion** (no value changed, `make test-regress`
  bit-identical). Mark's call 2026-08-19, over re-running 80 minutes on the exact commit. If
  sw_only behaviour ever disagrees with these numbers, suspect that assumption first.

  | | 2026-08-15 (`DSE_20260815_161306`) | **GOLDEN (2026-08-17)** |
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
  **RUN LANDED 2026-08-19 (`vck5000/results/DSE_20260819_152124`, 16/16) — and it REGRESSED:
  median 1.0137 → 1.0192, mean 1.0161 → 1.0351.** → **#34**, opened for it.
  **NOT archived as golden, deliberately.** The split by td is clean and #3's cap→scale carries it:
  **td<1 (8 designs, #3 active) all 8 worse, mean +4.99 pp** (`adaptec5` +15.21) while **td=1
  (8 designs, #3 a provable no-op) mean −1.19 pp**, i.e. 7a/7b alone is a *win* on MMS. A real
  inconsistency was found while checking it — `computeOverflow` still uses the pre-#3 cap under a
  comment claiming it mirrors `clampFixedDensity` — so **#3 has not yet been measured cleanly.**
  ⚠️ **Do not quote 1.0351 as the frozen MMS number and do not delete `DSE_20260814_152306`**
  (the pre-fix run) until #34 is settled: it is the only before-half of this comparison.
  ✅ Side effect: **#30's never-done tier-3 spot-check PASSES** — 16/16 references match
  `benchmarks._XPLACE_MMS_FINAL`.
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

- **#32 — CLOSED 2026-08-17**, archived to [[history.md]]. All three items done. **The u-vs-v
  question is settled: we track on `v`.** `snapshotBestPlacement()` stores `probe_pos` and HPWL is
  measured there too (new `at_probe` arg on `computeTotalWirelength`/`computeWirelength_HPWL`), so
  HPWL, overflow and the stored solution describe **one** position — XPlace's single `p`/`v_k`.
  `BEST_SOL_MIN_ITER` is phase-relative. `syncProbeToCommitted()` deleted, folded into
  `restoreBestPlacement()` (restores both halves) after its blocking comment's claimed perturbation
  measured **bit-exact identical** — retracted in the code.
  **A/B settled: KEEP 1.005** (`DSE_20260818_113716`, 28 designs × 2 arms, 56/56, 159.6 min).
  1.005 → 1.0097 / 1.0126; 1.010 → 1.0097 / 1.0128 (median / mean DP).
  ⚠️ **The real finding is that the knob barely binds: 1 design of 28 selects differently**
  (ISPD2005 byte-identical across arms). So the effective n is **1, not 28**, and widening the
  design set cannot help — the set was already everything. Where it binds (`mgc_des_perf_a`) 1.005
  wins by 0.71 pp post-DP, and **DP amplified the penalty rather than absorbing it**, reversing the
  #24 report §5 story that "more spread legalizes better". Unexplained: the three designs that
  flipped in the 2026-08-10 A/B no longer do — plausibly #31's grid cap moving the overflow gate,
  but that is a hypothesis, untested.
  ⚠️ **pl_algo inherits the u-vs-v decision** — flagged in tasks.md #20 step 6, with the specific
  trap: `sched_verify` checks the schedule, not the geometry, so it cannot catch a wrong choice.
- **#33 — the aux ACCEPT budget is hardcoded and has never been swept** (opened 2026-08-17, from
  #32). XPlace has **two** 0.5% budgets: an accept rule in `update_best_sol`
  (`param_scheduler.py:436` — ours is a hardcoded `1.005f` in `Output.cpp`) and the preference test
  #32 just settled (`:567` — our `best_aux_max_hpwl_ratio`). They are independent knobs that share
  a literal upstream. Next step is a cheap diagnostic (how often does the accept rule fire?) before
  spending another suite on it. ⚠️ Do **not** collapse the two onto one config value — that asserts
  an equality XPlace does not.
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

