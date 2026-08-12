# Summary — project status at a glance
*Updated 2026-08-12 16:47 EDT. Branch `pl_algo`. If this file and the code disagree, the code wins — say so.*

## Two threads
- **sw_only** — CPU golden reference; goal is to match XPlace. The active thread.
- **pl_algo** — move the whole placement iteration onto the PL. Blocked behind #20, deliberately.

## Where sw_only stands
- **Median HPWL ratio 1.0106, mean 1.0149, over ALL 28 ISPD designs** (legal-vs-legal; GP from the
  2026-08-11 v3 run on `3c70b38`, `matrix_mult_a` substituted post-#27). 21/28 within ±2%, better
  than XPlace on 4. **Nothing is unscored any more** — #26 closed the 9-design fence-region hole,
  and they held no surprise: median 1.0154, 7 of 9 within ±2%.

  | | 19 designs (pre-#26) | **28 designs (now)** |
  |---|---|---|
  | **ISPD2005 (8)** | 1.0053 / 1.0052 | 1.0053 / 1.0052 |
  | **ISPD2015** | 1.0171 / 1.0223 (11) | **1.0163 / 1.0189 (20)** |
  | **all ISPD** | 1.0095 / 1.0151 (19) | **1.0106 / 1.0149 (28)** |

  *(median / mean.)* **The mean is quotable** as of #27's fix. ⚠️ **The 9 fence designs are scored on
  the fence-STRIPPED variant on both sides** (as the XPlace paper's †-marked table is) — a fair
  tool-vs-tool comparison, but **not** legal ISPD2015 solutions: we place 59–94% of their constrained
  cells outside their fence. ⚠️ The v3/v4 TSVs deliberately still hold the broken `matrix_mult_a`
  row — see #27.
  → [[_NEW_REPORT_26_fence_regions_20260811.md]], [[REPORT_26_precond_always_on_20260811.md]],
  [[REPORT_27_matrix_mult_a_stray_space_20260811.md]]
  <details><summary>Superseded: "median 1.0113, mean 1.1218" (2026-08-10, pre-`3c70b38`)</summary>

  > **Median HPWL ratio 1.0113 vs XPlace** over **19 scored ISPD designs** (legal-vs-legal, re-run
  > 2026-08-10 on the post-#23 binary). 12/19 within ±2%, better than XPlace on 4. **Quote the
  > median** — the mean (1.1218) is one broken design, `mgc_matrix_mult_a` at 3.03× (GP dies at
  > iteration 290); excluding it the mean is 1.0159. 9 designs unscored (#22 fence regions).
  > → [[_NEW_REPORT_performance_snapshot_20260810.md]]

  Not withdrawn — **directly comparable**, same 19 designs, same two-stage method, same references.
  The delta is exactly one commit. That report's §2 method section still governs.
  </details>
  <details><summary>Superseded: "1.0090 over 33 scored designs" (2026-08-07)</summary>

  > **Median HPWL ratio 1.0090 vs XPlace** over 33 scored designs (44-design suite, legal-vs-legal,
  > 2026-08-07). 25/33 within ±2%, better than XPlace on 7. **Quote the median** — the mean (1.087)
  > is one broken design. → `[[_NEW_REPORT_performance_snapshot_20260807.md]]`

  **Withdrawn, not corrected — the two are not comparable.** That figure spanned all three tiers
  (33 of 44, including 16 MMS); the new one is ISPD-only (19 of 28). Two independent reasons it
  could not stand: its cited report **never existed** in `1_REVIEW/reports/`, so which designs were
  scored and which inflated its mean were unrecoverable; and its stage-1 GP inputs predated the #23
  fix, with a third of the ISPD2015 tier frozen (`mgc_superblue12` carried a 7.05e+09 post-DP HPWL
  — XPlace's legalizer fed cells stacked at die centre). **Do not average the old and new numbers.**
  The MMS side still rests on `lgdp_suite_results.tsv`, valid but scored under a different harness.
  </details>
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
- **#24 — the `Restored … from iteration N` log line names a placement that is not the one
  shipped** (opened 2026-08-10). One snapshot buffer, two trackers writing it. Headline numbers are
  unaffected (recomputed on the restored geometry); the provenance line is false and it already
  caused one wrong diagnosis. → [[HANDOFF_24_best_solution_buffer_20260810.md]]
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

## Where pl_algo stands
- All datapath modules written, HLS C-synthesis clean, each verified against the sw_only golden.
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

## Closed 2026-08-10
- **#24 code DONE** — best-solution tracking rebuilt to match XPlace: three trackers
  (`best_primary`/`best_aux`/`best_rollback`), **each with its own geometry buffer**, one shared
  `selectBestSolution()` (`get_best_solution`, param_scheduler.py:540-577). Two defects, only one
  of which was known: the shared buffer (17 of 29 runs shipped a placement the log did not name),
  and a **torn restore** — density deposits at `probe_pos`, which the restore never touched, so
  every reported overflow described the *last iteration* rather than the shipped placement. That
  second one produced #24's original evidence, so the ticket's stated proof was misattributed.
  All three suites green; 3 baselines regenerated with `--reason`.
  ⚠️ **The rule does not always ship the spread-out placement** — aux 8 / primary 11 over 29
  traces, and the *bug* shipped spread nearly always. A/B on the 0.5% budget says **keep XPlace's
  1.005**: 1.010 buys ~35% less overflow for ~0.5–0.7% GP HPWL and DP recovers only 41–74% of it.
  **Two decisions still open** (fix (B)'s scope → possible MMS re-run; n=2 on the A/B).
  → [[_NEW_REPORT_24_best_solution_trackers_20260810.md]]
- **#23 the fix** — `init_step_seed` scaled by site width (above). Regress baselines for
  `mgc_fft_a` / `mgc_pci_bridge32_b` regenerated with `--reason`; `mms_adaptec1` untouched
  (⚠️ superseded — #24's fix (B) changed `mms_adaptec1`, see above).
- **#23 bullet 2** — `estimateInitialStep()` now hard-errors on a zero/NaN BB step instead of
  no-opping to max_iterations. `make test-regress` bit-identical; error path exercised directly.

## Closed 2026-08-09 (three low-risk items, both test suites green)
- **#19** — pl_algo's `dff`/`dff_coef` renamed to `kappa`/`kappa_coef`; `make test` numbers
  byte-identical, so it is a pure rename. **New:** `host/src/pl_algo/` still gates on the *real* dff —
  the pre-#19 bug, still live there, now tracked under #19.
- **#11** — the self-contradicting `macro_td_expand_ratio` entry resolved from the code: the toggle is
  gone, the faithful branch is unconditional, and the "re-test unblocked by #19" note is moot as
  written (re-testing means re-adding the branch). Whether that is worth doing is Mark's call.
- **#17** — `readDEF()` names the file it wanted instead of printing an empty path. Diagnosis only;
  the `floorplan.def` hardcoding stands.

## Newly open
- **#28 — `dse.py` refactor, plus two live defects found while triaging `tools/`.**
  ⚠️ **`dse.py`'s sweep summary is stale against the exe's CSV schema and degrades silently** — it
  filters result columns with a hardcoded denylist that predates `Best GP HPWL` / `Phase1 *`, so
  `results.md` has a **blank `Best HPWL` column on every row**, no HPWL-range footer, and result
  columns listed as swept parameters. Visible in `results/DSE_20260810_173906/results.md`.
  **Until it is fixed, read a sweep with `tools/analyze_dse.py`, not `results.md`** — the older file
  is the correct reader here. Second defect: `_full_suite()` duplicates `benchmarks.py`'s grids
  exactly and omits `target_density` (latent, would bite on MMS). Refactor design agreed with Mark
  (CLI flags + JSON runsets, ~250 lines); not started.
- **#25 — we and XPlace use different `target_density` on ISPD2015.** On byte-identical `.def`s our
  exact overflow vs XPlace's: `adaptec1` **0.9991** (both 1.0), `mgc_fft_b` 1.119 (ours 0.6),
  `mgc_des_perf_1` **8.79** (ours 0.906). We read the DEF's `placement.constraints`; XPlace leaves
  `args.target_density` at 1.0 for every `mgc_*` design and feeds the constraint only to its DP
  engine. adaptec1 is the control proving our metric is right. **Not just reporting** —
  `target_density` also drives convergence, filler area and the macro density weight, so on
  ISPD2015 we optimize to a tighter target. Possible contributor to the ISPD2015 HPWL gap.
  **Do not quote our ISPD2015 overflow against XPlace's until this is settled.**

## Also open
- **#21 — repo restructure** (host to top level, one host binary). Proposal only, nothing started.
  **Merge `origin/geert` before anything else** — one `.gitignore` conflict today, 25 hand-moved
  files after.

## Verify anything
```bash
cd vck5000 && make test          # pl_algo tier-1, seconds, no Vitis
cd vck5000 && make test-regress   # sw_only vs committed baselines, ~12 s
```

## Deeper
`.claude/tasks.md` (open items)
`.claude/1_REVIEW/reports/` (evidence) 
`vck5000/pl/src/pl_algo/DATAFLOW.md` (authoritative for pl_algo)
`.claude/2_ARTIFACTS/papers/xplace/` (XPlace paper: text per section, **all 24 equations
  hand-transcribed** in `eqs/transcriptions.md`, figure/table crops. README indexes which
  section holds what — §II objective, §IV-B filler/overflow split, §V ω and the γ/λ throttle.
  Numbers stay in `2_ARTIFACTS/xplace_results/`.)
`.claude/skills/paper-extract/` (does the above for any paper PDF; tracked)
