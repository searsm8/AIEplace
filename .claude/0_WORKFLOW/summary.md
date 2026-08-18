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
- **Median HPWL ratio 1.0096, mean 1.0113, over ALL 28 ISPD designs** (legal-vs-legal; GP from the
  2026-08-15 run `results/DSE_20260815_161306` on the #31 universal grid-cap fix). 22/28 within ±2%,
  **better than XPlace on 6**. **Nothing is unscored any more** — #26 closed the 9-design fence hole.

  | | pre-#31 (2026-08-14) | **28 designs (now, #31 grid cap)** |
  |---|---|---|
  | **ISPD2005 (8)** | 1.0053 / 1.0052 | 1.0054 / 1.0052 |
  | **ISPD2015 (20)** | 1.0163 / 1.0189 | **1.0129 / 1.0138** |
  | **all ISPD (28)** | 1.0106 / 1.0149 | **1.0096 / 1.0113** |

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
- **MMS (16 designs): 16/16, DP ratio median 1.0138 / mean 1.0161, from the 2026-08-14 re-run.**
  **Still valid — checked 2026-08-17, and it is NOT stale despite predating #31.** The #31 grid cap
  (`row_cap = 2^floor(log2(num_rows))`) **fires on zero of the 16**: every MMS design's requested
  grid is already at or below its cap — tightest is `newblue1` (930 rows → cap 512, requested 512),
  loosest `newblue3` (4182 → 4096, requested 2048) — and the 8 that overlap tier-1 were shown
  bit-identical by #31 itself. Re-derive with `benchmarks._ROWS` grids vs `CoreRow` counts in the
  bookshelf `.scl`. **No new MMS data exists** — the
  2026-08-15 headline run (`DSE_20260815_161306`) is tier1+tier2 only, 8 ISPD2005 + 20 ISPD2015.
  ⚠️ The only MMS run since is today's `newblue1` under `.claude/2_ARTIFACTS/GIFS_20260817_211039/`
  — a **viz/#14 run from the other session**, not a suite result; don't score from it.
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

## Closed 2026-08-11
- **#22 — the 8 designs with no XPlace reference.** Resolved by #26: neither of that entry's two
  options was the answer — `ispd2015_fix` is generated by XPlace's own `data/fix_ispd2015_route.py`,
  not downloaded and not hand-built. All 8 now have a reference; the suite is 28 of 28.
- **#27 — `mgc_matrix_mult_a` was a stray space, not an algorithm failure.** Its
  `placement.constraints` was 25 bytes — `maximum_utilization=60% \n`, one trailing space more than
  every other design's. `readPlacementConstraints` tests `back() == '%'` to decide whether to divide
  by 100; the space defeats it, `stof("60% ")` returns **60.0**, and the design placed at
  `target_density = 60`: **29,779,040 fillers** for 149,650 movable cells, a filler area ~20× the
  die. **Fixed by deleting the space** (file now byte-identical to `mgc_matrix_mult_b`'s), and
  `readPlacementConstraints` now hard-errors on any value outside (0, 1].
  **3.2669 → 1.0171**; `divergence_guard` at 271 → **converged** at 715.
  ⚠️ **The benchmark fix is NOT tracked** — `benchmarks/.gitignore` ignores `ispd2015`, so a fresh
  clone or re-download reintroduces the bad file. Check:
  `wc -c vck5000/host/benchmarks/ispd2015/mgc_matrix_mult_a/placement.constraints` must be **24**.
  The new hard-error is what makes that recoverable rather than silently wrong.
  ⚠️ Its apparent +7.71% response to the preconditioner change was **noise** — a broken landscape
  reshuffling. Check a design's inputs before reading its response to an algorithm change.
  → [[_NEW_REPORT_27_matrix_mult_a_stray_space_20260811.md]]
- **Preconditioner always on + escalation unthrottled** (`3c70b38`). Two coupled faithfulness fixes,
  both about `precond_coef` — which feeds the per-node `precond_weight` **and** `precond_kappa`, and
  κ gates the every-3rd-iteration γ/λ throttle for every design.
  **(A)** `precond_coef` escalation hoisted out of `updateDensityWeight()` into `updatePrecondCoef()`,
  called outside the `perform_update` gate. XPlace's `step_precond_coef` is the one member of its
  `step()` trio with **no** `skip_update` guard — deliberately, since it is what ends the throttle.
  Gated, our `%20` grid could only fire where it met `%3`: **every 60 iterations, not 20**.
  **(B)** `auto_enable_preconditioning` removed (above).
  Suite effect: median **1.0113 → 1.0095**, ISPD2005 mean 1.0111 → **1.0052**, ISPD2015 ~0.4% worse,
  `divergence_guard` 10/28 → 8/28. `bigblue3` **−4.30%** (1.0565 → 1.0111, and it now *converges*);
  `mgc_des_perf_1` also recovered and now beats XPlace by 1.9%.
  ⚠️ **Not a uniform win.** Three designs moved the wrong way — `mgc_matrix_mult_a` +7.71% (but see
  #27, it is a parser bug), `mgc_superblue19` +2.26%, `mgc_superblue14` +0.79%.
  ⚠️ (A) is provably a **no-op** when the preconditioner is off — verified by reproducing a frozen
  baseline bit-for-bit with `enable_preconditioning = false` on the new binary.
  → [[_NEW_REPORT_26_precond_always_on_20260811.md]]

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
*Rewritten 2026-08-17. This section was headed "Newly open" and 4 of its 6 entries (#25, #28, #29,
#31) were **closed and already archived to [[history.md]]** — the most-read file in the repo was
advertising finished work as open. Their full text is in history.md; only live items are below.*

- **#32 — best-solution tracking: two remaining XPlace divergences + widen the A/B** (opened
  2026-08-17, spun off #24's close). Snapshot position u-vs-v, `BEST_SOL_MIN_ITER`
  absolute-vs-phase-relative, and `best_aux_max_hpwl_ratio`'s A/B still rests on n=2. None of these
  are regressions from #24 — they're gaps its faithfulness audit found and #24's own fixes don't
  reach. → [[_NEW_REPORT_24_best_solution_trackers_20260810.md]] §7
  ⚠️ **Being handled in a separate session (2026-08-17), along with #14.** Don't start it here.
  ⚠️ **The u-vs-v question is filed in three places** — this entry, the #24 report §7, and the
  "deposit at `node_pos` vs `probe_pos`" bullet under *Topics for investigation*. Same question:
  XPlace optimizes v_k directly so it snapshots, measures HPWL and deposits density all at **v**;
  we deposit at v but snapshot and measure HPWL at **u**. It is the one open faithfulness question
  that **changes the shipped `.def`**, and the one place the freeze could bite — decided wrong (or
  not at all), pl_algo inherits the inconsistency and hard-codes it into the resident loop.
- **#3 — fixed-density cap-vs-scale, the last known XPlace divergence in the density path.**
  ❓Needs Mark's decision *because* of the freeze. XPlace `initializer.py:82` scales
  (`min(ρ,1)·td`); our `Grid::clampFixedDensity` (`Grid.cpp:139`) caps (`min(ρ,td)`). Equal at
  td=1; at td=0.65, ρ=0.5 → XPlace 0.325 vs ours 0.50, so we read high in macro-perimeter bins.
  Macro designs at td<1 only. **One-line edit, but it reddens `make test-regress` and invalidates
  the 28-design headline until a full `make dse` re-run** — fix-and-re-run, or record as
  known-and-accepted. See tasks.md #3.
- **#30 — collapse the two suite runners.** Unblocked by the passed cross-check; awaiting Mark's
  go-ahead, MMS/tier-3 spot-check first. Everything else in #30 landed.

## Also open
- **#21 — repo restructure** (host to top level, one host binary). Proposal only, nothing started.
  **Merge `origin/geert` before anything else** — one `.gitignore` conflict today, 25 hand-moved
  files after.

