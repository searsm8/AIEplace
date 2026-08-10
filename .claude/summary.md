# Summary — project status at a glance
*Updated 2026-08-10 15:41 EDT. Branch `pl_algo`. If this file and the code disagree, the code wins — say so.*

## Two threads
- **sw_only** — CPU golden reference; goal is to match XPlace. The active thread.
- **pl_algo** — move the whole placement iteration onto the PL. Blocked behind #20, deliberately.

## Where sw_only stands
- **Median HPWL ratio 1.0090 vs XPlace** over 33 scored designs (44-design suite, legal-vs-legal,
  2026-08-07). 25/33 within ±2%, better than XPlace on 7. **Quote the median** — the mean (1.087)
  is one broken design. → [[_NEW_REPORT_performance_snapshot_20260807.md]]
- Landed: two-phase mixed-size flow + LP macro legalization; #19's two XPlace faithfulness fixes
  (overflow excludes fillers; the γ/λ throttle gates on preconditioner κ). Both toggles retired
  2026-08-07 — faithful behaviour is now unconditional.
- **#23 — FIXED 2026-08-10: `init_step_seed` is in SITE WIDTHS, not raw DBU.** The 5 dead ISPD2015
  designs place now — `mgc_des_perf_b` converges in 825 iters, `mgc_superblue11_a` in 849
  (HPWL −56%, overflow 0.972→0.047), where both never moved a cell. XPlace
  prescales all coordinates by site width, so its `args.lr = 0.01` always meant 0.01 *sites*; ours
  meant 0.01 DBU and underflowed. **Bookshelf `Sitewidth = 1` ⇒ the whole MMS suite is
  bit-unchanged**; only the two ISPD2015 regress baselines were regenerated.
  ⚠️ Units, **not** precision — scaling coordinates buys nothing in float32; shifts do (that is #15).
  → [[REPORT_23_site_width_seed_20260810.md]]. Left open: re-run the 4 `nan_metrics` designs
  and re-score the snapshot.
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
- **#23 the fix** — `init_step_seed` scaled by site width (above). Regress baselines for
  `mgc_fft_a` / `mgc_pci_bridge32_b` regenerated with `--reason`; `mms_adaptec1` untouched.
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
