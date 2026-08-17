# journal.md — the dated narrative

Append-only, most-recent-first. This is the **story** of the project: superseded status
snapshots (the retraction trail `summary.md` sheds under its soft cap) and dated notes on how
the numbers and state evolved. Read it chronologically.

Boundary with its neighbours — don't let this become a second history.md:
- **[[summary.md]]** holds only CURRENT state, soft-capped. When a snapshot there is superseded,
  it moves **here** rather than growing a `<details>` inside summary forever.
- **[[history.md]]** is the **task-indexed archive** — full completed-task sections, looked up by
  `#n`. journal.md is **chronological and cross-cutting**; a closed task's formal record still
  goes to history.md, but the day's status-level narration (headline numbers, "we thought X, now
  Y") belongs here.

Nothing here is injected into sessions — it's the archive you consult, not the always-loaded
status. **Never rewrite an entry; annotate.** The retraction trail is the point.

---

## Superseded headline HPWL-ratio snapshots (sw_only vs XPlace)

### 2026-08-10 — "median 1.0113, mean 1.1218" (pre-`3c70b38`)
> **Median HPWL ratio 1.0113 vs XPlace** over **19 scored ISPD designs** (legal-vs-legal, re-run
> 2026-08-10 on the post-#23 binary). 12/19 within ±2%, better than XPlace on 4. **Quote the
> median** — the mean (1.1218) is one broken design, `mgc_matrix_mult_a` at 3.03× (GP dies at
> iteration 290); excluding it the mean is 1.0159. 9 designs unscored (#22 fence regions).
> → [[_NEW_REPORT_performance_snapshot_20260810.md]]

Not withdrawn — **directly comparable**, same 19 designs, same two-stage method, same references.
The delta is exactly one commit. That report's §2 method section still governs.

### 2026-08-07 — "1.0090 over 33 scored designs"
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

---

## Dated closed narration, evicted from summary.md's soft cap

### 2026-08-10 — #24 "code DONE", #23 baselines
> - **#24 code DONE** — best-solution tracking rebuilt to match XPlace: three trackers
>   (`best_primary`/`best_aux`/`best_rollback`), **each with its own geometry buffer**, one shared
>   `selectBestSolution()` (`get_best_solution`, param_scheduler.py:540-577). Two defects, only one
>   of which was known: the shared buffer (17 of 29 runs shipped a placement the log did not name),
>   and a **torn restore** — density deposits at `probe_pos`, which the restore never touched, so
>   every reported overflow described the *last iteration* rather than the shipped placement. That
>   second one produced #24's original evidence, so the ticket's stated proof was misattributed.
>   All three suites green; 3 baselines regenerated with `--reason`.
>   ⚠️ **The rule does not always ship the spread-out placement** — aux 8 / primary 11 over 29
>   traces, and the *bug* shipped spread nearly always. A/B on the 0.5% budget says **keep XPlace's
>   1.005**: 1.010 buys ~35% less overflow for ~0.5–0.7% GP HPWL and DP recovers only 41–74% of it.
>   **Two decisions still open** (fix (B)'s scope → possible MMS re-run; n=2 on the A/B).
>   → [[_NEW_REPORT_24_best_solution_trackers_20260810.md]]
> - **#23 the fix** — `init_step_seed` scaled by site width. Regress baselines for
>   `mgc_fft_a` / `mgc_pci_bridge32_b` regenerated with `--reason`; `mms_adaptec1` untouched
>   (⚠️ superseded — #24's fix (B) changed `mms_adaptec1`, see above).
> - **#23 bullet 2** — `estimateInitialStep()` now hard-errors on a zero/NaN BB step instead of
>   no-opping to max_iterations. `make test-regress` bit-identical; error path exercised directly.

**Superseded 2026-08-17 by #24's close.** Both "decisions still open" resolved: fix (B) was scoped
to the final restore only (`syncProbeToCommitted()`), and its premise ("scoping it leaves MMS
bit-identical") was itself wrong — MMS moves under #24's *selection* fix regardless of (B)'s scope,
via `beginFixedMacroPhase`. The MMS suite was re-run 2026-08-14 (16/16, DP ratio median 1.0138).
The A/B's n=2 and the two further faithfulness gaps found while auditing the close (u-vs-v
snapshot position, `BEST_SOL_MIN_ITER` absolute-vs-phase-relative) carry forward as **#32**. Full
record: `history.md` #24.
