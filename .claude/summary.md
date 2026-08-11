# Summary — project status at a glance
*Updated 2026-08-10 23:04 EDT. Branch `pl_algo`. If this file and the code disagree, the code wins — say so.*

## Two threads
- **sw_only** — CPU golden reference; goal is to match XPlace. The active thread.
- **pl_algo** — move the whole placement iteration onto the PL. Blocked behind #20, deliberately.

## Where sw_only stands
- **Median HPWL ratio 1.0113 vs XPlace** over **19 scored ISPD designs** (legal-vs-legal, re-run
  2026-08-10 on the post-#23 binary). 12/19 within ±2%, better than XPlace on 4. **Quote the
  median** — the mean (1.1218) is one broken design, `mgc_matrix_mult_a` at 3.03× (GP dies at
  iteration 290); excluding it the mean is 1.0159. 9 designs unscored (#22 fence regions).
  → [[_NEW_REPORT_performance_snapshot_20260810.md]]
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

  > The 5 dead ISPD2015 designs place now — `mgc_des_perf_b` converges in 825 iters,
  > `mgc_superblue11_a` in 849 (HPWL −56%, overflow 0.972→0.047), where both never moved a cell.

  `mgc_des_perf_b` **does not reproduce** as converging: under the manifest's own config
  (`gen_suite_configs.py`, seed 42) it reaches `divergence_guard` at 889 iterations. Verified twice
  — standalone and in the 28-design suite. Which config produced the 825-iteration claim is
  unknown. `mgc_superblue11_a`'s iteration count also differs (842, not 849).
  </details>
- **#24 — the `Restored … from iteration N` log line names a placement that is not the one
  shipped** (opened 2026-08-10). One snapshot buffer, two trackers writing it. Headline numbers are
  unaffected (recomputed on the restored geometry); the provenance line is false and it already
  caused one wrong diagnosis. → [[_NEW_HANDOFF_24_best_solution_buffer_20260810.md]]
- **#22 — 8 ISPD2015 designs have no XPlace reference** (fence regions; `--dataset ispd2015` is
  silently rewritten to `ispd2015_fix`). Blocked on obtaining that dataset.

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
