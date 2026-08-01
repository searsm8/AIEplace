# TODO

Cross-session task parking. New workflow (2026-07-25): park work here, not only in loose reports.
Report deliverables are named `NEW_<name>.md` until Mark has read them (then the prefix is dropped).

---

## #1 — Clean house: repo / notes / code cleanup  (planned Mon 2026-07-27)

Fast iteration left breadcrumbs sprawling and we started tripping over them. Goal: one tidy,
committed, self-consistent state. Checklist:

**Already done 2026-07-25:** established the numbered workflow dirs (`0_TODO`, `0_OVERNIGHT_WORK`,
`1_REVIEW`, `2_ARTIFACTS`), all git-ignored local-only via one root pattern `[0-9]_*/` (see
memory `workflow-dirs-convention`). Merged the root `AIEplace/REVIEW` (incl. 160M `archive/`)
into `1_REVIEW`. Net effect: the review/handoff dirs are now OUTSIDE the tracked repo.

### git working tree
- [x] DONE 2026-07-27 (commit 2a1a561): committed the 8 `vck5000/REVIEW/*` deletions +
      the new `[0-9]_*/` .gitignore. Verified all 8 files preserved locally in `1_REVIEW/`
      (2 renamed to `NEW_*` are byte-identical to the tracked versions) — nothing lost from history.
- [~] `tools/eval_overflow_xplace.sh`: LEFT UNTRACKED per Mark (2026-07-27). No stray source
      deletions found. Revisit if it should be committed into `tools/`.

### 1_REVIEW/ reports (now local-only)
- [ ] Consolidate handoffs/reports; apply the `NEW_` convention consistently (un-prefix the ones
      Mark has read, keep `NEW_` on the rest).
- [ ] Fold the still-relevant findings into TODO.md / memory so old reports can be archived.

### results/ + scratch
- [ ] Prune the `results/DSE_*` and `results/morris_*` pile. KEEP the ones reports cite:
      baseline `morris_20260723_173411`, new-code `morris_20260725_104908` (+ its
      `morris_20260725_104513` analysis). Delete stale/aborted sweeps.
- [~] `~/aieplace_tmp/` (1.1 G scratch): rescued the one authoritative artifact —
      `xplace_mms_reference.md` (16-design XPlace golden HPWL) → committed to `docs/` (fc89bcb).
      The rest (`swonly_*.pl` placements, A/B jsons/logs, run scripts, BEFORE/AFTER/viz dirs) is
      reproducible scratch — safe for Mark to nuke the whole dir whenever.

### code / comments audit
- [x] DONE 2026-07-27 (commit bb7904b): fixed the misleading `AIEplace.h` defaults AND the root cause.
      The defaults were duplicated (header initializer + `loadConfig` `.value(key,<literal>)` fallback)
      and had drifted (dct_normalize_inverse/precond_raw_area/dff_force_ratio header inits were the stale
      LEGACY values). Fix = single source of truth: `loadConfig` now falls back to the member's own value
      (`.value(key, member)`), and every configurable member carries its default as a header initializer
      (added inits to 8 previously-bare members). Behavior-preserving (run_config supplies every key);
      builds clean + smoke-verified.
- [~] Reconciled the stale run_config.json "NEEDS a full-suite re-baseline" comment → "validated by the
      MMS A/B (2026-07-26)". UNCOMMITTED — bundled with the #2 run_config comment pruning + the
      `enable_pin_offsets` breadcrumb decision (see #2).

### external (XPlace) repo
- [~] CHECKED 2026-07-27 — actual state differs from the note above (no `run_placement_nesterov.py`
      change present). `~/phd/Xplace` on `main` has 3 uncommitted local edits, all tied to the precond
      basis study and all look intentional (Claude did NOT touch them — Mark's call to keep/commit):
        1. `src/calculator.py` — BUG FIX: `apply_precond` returned None under `--use_precond False`
           (crashed upstream); now returns `mov_node_pos.grad`.
        2. `src/param_scheduler.py` — `PRECOND_TRACE` env→CSV dump of the a1/a2 addend norms per iter;
           `update_precond_weight` now always computes `weighted_weight` even with precond off (isolates
           just the preconditioning); `precond_a1_norm`/`precond_a2_norm` instrumentation.
      Decide: commit these into XPlace (they're real infra + a genuine bug fix) or keep local-only.

---

## #2 — Consolidate the preconditioner / field-faithful code (validated 2026-07-26)

The full MMS A/B (`1_REVIEW/NEW_mms_dct_ab_20260726.md`) definitively validated the
faithful-field + preconditioner set (`dct_normalize_inverse=false`, `precond_raw_area=true`,
`dff_force_ratio=true`): faithful beats legacy **16/16 on MMS, mean −9.6% HPWL**, confirming the
preconditioner works as intended. Now clean up the experiment scaffolding it was developed behind:
- [x] DONE 2026-07-27 (commit 1f84d8d): retired ALL 5 settled legacy toggles (Mark decided "delete all").
      Deleted the flags, config keys, dead branches + oversized comments: `dct_normalize_inverse` (the
      double-normalized inverse was an *error*, not a frame — inverse transforms now unconditionally
      unnormalized), `precond_raw_area=false`, `dff_force_ratio=false`, `precond_density_scale` (no-op
      EXPERIMENT), and `enable_pin_offsets` (per-pin offsets always on; dropped DataBase ctor param +
      member + 2 collapse-to-origin guards). Behavior-preserving (run_config already used every kept
      value); builds clean, smoke-verified within RNG noise. Net −94/+29 lines.
- [x] precond_density_scale: retired (part of the above).
- [x] Misleading defaults + comment blocks: DONE (bb7904b single-source dedup + 1f84d8d comment pruning).
      run_config.json fully committed (the `enable_pin_offsets` breadcrumb is gone with the key).
- [x] `updatePrecondWeights` simplified (dead ternaries + dff if/else removed). Kept `precond_a1_norm`/
      `precond_a2_norm` (still used by the trace) and the auto-enable/escalation logic (unchanged — not cruft).
- [ ] STILL TODO: mirror the faithful-only simplification into pl_algo (`Placement.hpp`/`Driver.cpp`).
      pl_algo already hardcodes the faithful path (avg_area=1 ⇒ raw-area, dff_force_ratio form), but its
      COMMENTS still name the retired sw_only flags — tidy for parity. Low urgency (pl_algo is stub-stage).

---

## #3 — Tooling & evaluation workflow

- [ ] **Build resumability into `dse.py`** — the overnight MMS runner proved the pattern: skip any run
      already recorded (keyed by label/config) so an interrupted sweep *resumes* instead of restarting.
      Generalize it (auto-detect completed rows in `results.csv`, or a `--resume <DSE_dir>` flag) so every
      long sweep gets it for free. Reference implementation: `2_ARTIFACTS/run_mms_ab.sh` (`grep`-on-TSV
      skip guard + phased concurrency).
- [ ] **Formalize full-pipeline evaluation (GP → LG → DP)** — make legalization + detailed placement a
      standard step on our GP results and report **post-DP HPWL** (+ XPlace-metric overflow) as the
      headline quality metric, not GP-only. GP-vs-GP is fair but incomplete: legalization normally raises
      HPWL (XPlace adaptec1 GP 6.238e7 → Legal 6.814e7, **+9.2%**), and an under-spread GP blows up in LG,
      so post-DP HPWL is the honest final number — and it *also* resolves the macro-heavy under-spread
      ambiguity from the MMS A/B. Reuse the 07-17 XPlace-legalizer flow (`def_to_bookshelf_pl.py` +
      `--global_placement False --given_solution`) and wire the result into the scorecard.
- [ ] **`verify_swonly.sh` cannot be diffed the way its own docstring says.** It instructs you to
      compare two runs with `diff -r A/artifacts B/artifacts`, but it collects
      `function_statistics.md`, which contains **wall-clock timings** — so `diff -r` reports a
      difference on every design, every run, regardless of correctness, and the equality test it
      advertises silently never passes. Fix: drop the timing file from the artifact set, or have
      the script do the comparison itself over `iterations.dat` + `RowBasedPlacement.def` only.
      (Hit 2026-07-31 during the P1 filler verification; worked around by diffing those two
      files directly.)
- [ ] **`2_ARTIFACTS/gen_footprint_ab_configs.py` is partly STALE.** It still writes
      `params["xplace_die_projection"]`, a key deleted in `caa8f2b` when #11a was adopted
      unconditionally. toml++ ignores unknown keys, so its `base` and `proj` arms are now
      **silently identical** — anyone reusing it for a sweep burns double the wall clock proving
      that. Strip the #11a arm machinery, keep the useful part (per-design grid /
      `maximum_utilization` / `random_seed` from `tools/benchmarks.py`).

---

## #4 — Fix convergence to include filler density (ELEVATED, ROOT CAUSE FOUND — likely one-line)

> ### ⚠️ REFRAMED 2026-07-31 — the premise below was measured against the wrong XPlace number
>
> **XPlace's mixed-size flow has TWO GP phases; sw_only implements only the first.** From
> `~/phd/Xplace/result/2026-07-17-23:03:11_adaptec5/log/test.log`: Mixed-GP (macros movable) →
> `Start running Macro Legalization` (LP/cbc) → `Reset optimizer` → `Re-run std cell placement
> with fixed macros`, a second full GP pass. The familiar `GP Stop!` line is the end of **phase 2**.
> Every "vs XPlace" MMS number we have ever quoted — including the −22%/−25% under-spread figures
> below — compared our phase-1 result against XPlace's phase-2 result.
>
> Phase-1 reference now recorded in `tools/benchmarks.py::_XPLACE_MMS_MIXED_GP`. Two things change:
> - **XPlace's Mixed-GP ends at 0.10–0.18 exact overflow on 15 of 16 MMS designs** (newblue3 is the
>   lone outlier at 0.040). It never reaches the 0.07 stop threshold either. "We don't spread to
>   0.07 with macros movable" is therefore NOT by itself a defect.
> - Against the phase-1 reference the footprint-A/B base arm is **mean +3.5% HPWL over 16 designs**,
>   within +0.7…+4% on 11 of them. The bad ones are adaptec5 (+7.0), newblue4 (+8.4), newblue7
>   (+6.7), newblue5 (+16.4), and newblue3 (−13.4, a *fake* win — it stopped at iter 375).
>
> **The real defect is the stop criterion, and it splits 8/8 by target_density** (stop reasons
> grepped from the A/B base-arm logs in `/tmp/fp_ab/logs`):
> - all 8 `target_density = 1.0` designs → clean overflow countdown
> - newblue1–6 → `divergence guard exhausted` (the plateau branch, `Schedule.cpp`
>   `checkFineDivergenceGuard`, `life -= MAX_LIFE` = instant kill) — an ad-hoc heuristic XPlace
>   does not have, firing while overflow is still descending
> - adaptec5, newblue7 → genuine 2× HPWL blowup
>
> Not one `td < 1.0` design reaches the threshold, so the guard is the *de facto* stop rule on
> exactly the designs we care about. XPlace runs 1300–2280 iterations; we die at 375–907 under a
> `convergence_max_iterations = 1200` ceiling.
>
> **Re-validate the "premature stop → under-spread" premise against the phase-1 reference before
> spending another sweep on it.** The filler fix is still right on faithfulness grounds (below),
> but its *predicted benefit* was derived from the phase-2 comparison.

### Step 1 — comparability, DONE 2026-07-31 (reporting only; no algorithm change)
- [x] **Headline exact overflow now includes fillers** — `computeFinalMetrics` uses
      `computeOverflow(false, nullptr, true)`, matching XPlace's `evaluate_placement` convention;
      row relabelled "Final Overflow (exact, +fillers)". The filler-excluded number read ~2× low
      and was never comparable to anything XPlace prints.
- [x] **Smoothed overflow follows the convergence flag** (`convergenceIncludesFillers()`, shared
      with `recordIterationResults`), so the reported number is the signal that actually stopped
      the run; label says which policy is in force.
- [x] **Machine-readable stop reason** — `Placer::StopReason` + one `[STOP] reason=<token>
      iteration=<n>` line, plus a "Stop reason" row in run_summary. Tokens: `converged`,
      `max_iterations`, `divergence_guard`, `diverged_hpwl`, `nan_metrics`, `nan_partials`.
      Sweeps can now group by termination mode instead of regexing prose. (NOT plumbed into
      results.csv — that schema is the DSE path, left alone deliberately.)
- [x] **XPlace Mixed-GP reference in `tools/benchmarks.py`** + surfaced in `BENCHMARKS.md`.
- Note: these change the *reported* overflow numbers, so **any golden captured before this no
  longer matches**. HPWL, iteration counts and the trajectory are untouched.

### Step 3 — OUTCOME: only (c) shipped. (a)+(b) are an inseparable pair that needs TODO #13 first.
**Final state 2026-07-31, verified against `2_ARTIFACTS/mms_baseline_20260731.tsv`:**
newblue1 6.156e7 @ 695 it, newblue4 2.426e8 @ 670 it — **baseline stopping fully restored**,
plus the safer divergence test. ispd2005/adaptec1 bit-identical throughout.

- **(a) doubled stop overflow — NOT applied.** Reverted; `applyMixedSizeStopPolicy` documents why
  and `mixed_size_mode` stays wired for #13.
- **(b) plateau kill off in mixed-size — NOT applied.** Reverted; documented deliberate divergence
  in `checkFineDivergenceGuard`.
- **(c) coarse-divergence overflow conjunct — APPLIED, and it is very nearly a NO-OP.**
  Validated 2026-07-31 on 6 designs against the corrected baseline, single binary
  (md5 `d4462bd6…`, hashed before and after the sweep — no mid-sweep rebuild this time):
  newblue1 / newblue4 / adaptec5 / newblue5 / newblue3 are **bit-identical** (same total
  iterations, same sharp/+filler overflow, same HPWL). Only newblue7 moved: 716 -> 718 iterations,
  overflow 0.420 -> 0.415, HPWL unchanged. Keep it — it costs nothing and is faithful — but do not
  attribute any result to it.
  > **Earlier claim in this file was WRONG:** "(c) made adaptec5 run 649 -> 1163 iterations."
  > adaptec5's baseline total was ALREADY 1163. 649 was the RESTORED-BEST iteration, read from
  > `footprint_ab_results.tsv`'s `iter` column, which is `iters_best`, not `iters_total`. The two
  > differ by hundreds of iterations. `2_ARTIFACTS/mms_baseline_20260731.tsv` now carries BOTH
  > columns explicitly to stop this recurring.

**Net effect of step 3 on results: zero.** That is the honest headline, and it is consistent with
the session's conclusion — phase-1 stop tuning cannot move MMS quality, because phase 1's std-cell
placement is discarded anyway. The gap is TODO #13.

**Why (a) and (b) cannot be separated.** Doubling `stop_overflow` also doubles the guard-arm band
(`overflow < stop_overflow*5`, 0.35 -> 0.70). XPlace can afford that ONLY because it simultaneously
disables the plateau kill in this phase. Applying (a) alone arms the guard during the early plateau
every design has before the density weight ramps: **newblue1 died at iteration 258, overflow still
0.70, HPWL 2.39e7 vs 6.15e7** — an unspread placement. Applying (b) alone removes our only phase-1
exit and grinds to `max_iterations`. Re-enable BOTH with #13.

### Step 3 — the analysis: THREE faithfulness gaps, all in XPlace's `include_macros` phase
Found by reading `Xplace/src/param_scheduler.py` (`need_to_early_stop`, `set_mixsize_init_param`)
rather than tuning our heuristics. XPlace runs its mixed-size phase under `include_macros = True`
and **loosens the stop rules while it holds**; sw_only applied the std-cell-phase rules throughout.
That is the whole explanation for the 8/8 split — it was never a per-design pathology.

- [x] **(a) Stop overflow is DOUBLED in mixed-size.** XPlace: `self.stop_overflow =
      args.stop_overflow * 2.0` when `enable_mixed_size and not zero_macro_grad`. So its Mixed-GP
      target is **0.14**, not 0.07 — which is exactly why its Mixed-GP ends at 0.10–0.18. We were
      chasing 0.07 with macros in the field, a target XPlace never asks for and that is likely
      unreachable in phase 1. Implemented in `Setup.cpp::applyMixedSizeStopPolicy` by doubling the
      `overflow_threshold` member once (mirroring XPlace, which mutates `stop_overflow` in one
      place), so the countdown, the guard-arm band and best-solution tracking all move together.
- [x] **(b) The overflow-plateau kill is DISABLED in mixed-size.** XPlace gates it on
      `not self.include_macros` (`param_scheduler.py:467`); we had no equivalent. Mixed-size
      overflow descends slowly enough to look flat over the 50-iteration window, so the guard read
      a healthy run as converged and killed it — this is what ended newblue1–6.
- [x] **(c) Coarse divergence was missing a conjunct.** XPlace requires
      `overflow[ptr] > overflow[ptr-1] AND hpwl[ptr] > best*2`; we tested the HPWL term alone.
      HPWL legitimately climbs past 2× best while cells spread, so this fired on healthy
      macro-heavy runs (adaptec5, newblue7) that were still descending in overflow.

`mixed_size_mode = (num_movable_macros > 0)` is the `include_macros` equivalent — sw_only only ever
runs phase 1 (no macro legalization + fixed-macro second pass), so the flag is simply "has movable
macros". **Non-mixed-size designs are unaffected: ispd2005/adaptec1 verified bit-identical**
(HPWL 60353676.000000, overflow 0.826129, seed 42).

**MEASURED 2026-07-31** (seed 42, XPlace grid + td, vs `2_ARTIFACTS/mms_baseline_20260731.tsv`):
| design | baseline | after step 3 | XPlace Mixed-GP |
|---|---|---|---|
| newblue1 | 6.156e7 @ 0.189, 673 it, `divergence_guard` | 6.156e7 @ 0.190, 661 it, **`converged`** | 5.946e7 @ 0.136 |
| newblue4 | 2.426e8 @ 0.355, 670 it, `divergence_guard` | 2.447e8 @ 0.337, 1200 it, **`max_iterations`** | 2.238e8 @ 0.182 |

Step 3 fixes *why* runs stop, not yet *how well* they place. newblue1's guard was firing about
where it would have converged anyway. newblue4 now runs to the ceiling and is still descending —
too slowly to reach 0.182.

- [x] **`convergence_max_iterations`: DECIDED 2026-07-31 — leave at 1200, do NOT raise.**
      XPlace's cap is `args.inner_iter`, **default 10000**, and `for iteration in range(args.inner_iter)`
      spans BOTH phases — observed totals 1344–2280, so it never binds. It is a runaway backstop,
      not a schedule; 1200 vs 2000 is the wrong axis. Raising ours would be actively harmful:
      newblue4's trajectory shows it is **stuck, not slow**. After ~iter 750 overflow is pinned at
      0.289 while `density_weight` runs away 60,000x (0.55 -> 3.3e4), `step_length` 6e2 -> 3.6e5,
      and HPWL inflates 45% (2.42e8 -> 3.52e8). More iterations = more waste. (The reported
      2.447e8 survives only because best-solution tracking restores an earlier placement.)
- [ ] **Density-weight runaway** — separate defect exposed by the above: `updateDensityWeight` has
      no upper clamp, so when overflow stops responding lambda ramps without bound and destroys
      HPWL. XPlace's mu is bounded the same way ours is, but XPlace exits phase 1 long before this
      matters. Worth its own look independent of #13.

> ### ⚠️ CORRECTION 2026-07-31 — the guard firing in phase 1 is XPlace's PHASE TRANSITION
> `run_placement_nesterov.py:167-172`: `need_to_early_stop()` sets `terminate_signal`, and when
> mixed-size is still in phase 1 that signal **triggers macro legalization and the optimizer reset**,
> then does `terminate_signal = False` and CONTINUES. It does not end the run. XPlace's own newblue4
> phase 1 ended at 0.182 — above its own 0.14 target — i.e. it too left phase 1 on the guard, not by
> converging.
>
> So sw_only's original `divergence_guard` stop on newblue1-6 was closer to CORRECT than step 3
> assumed: it was finding the right phase-1 exit point. What is missing is the phase 2 that should
> follow (**TODO #13**), not a better guard.
>
> **This puts step 3 change (b) in question.** Disabling the plateau kill is faithful to XPlace's
> gating (`not include_macros`), but without phase 2 there is nowhere for the run to go, so it
> grinds to the iteration cap — newblue4 and newblue5 both now stop at `max_iterations` 1200
> instead of exiting at ~670. Changes (a) doubled stop overflow and (c) coarse-divergence conjunct
> are unaffected and still right. Options: revert (b) until #13 lands (runs stop at the phase-1
> exit, as before, and that becomes the transition trigger later), or keep it and accept wasted
> iterations. NOT yet decided.
- [x] **Filler-area faithfulness gap — CONFIRMED AND FIXED 2026-07-31.** It was not "fewer fillers
      than XPlace"; on the macro-heavy low-td designs it was **ZERO fillers**.
      XPlace: `total_filler_area = target_density * stdcell_placeable_area - mov_stdcell_area`
      where BOTH terms exclude movable-macro area (`database.py` compute_filler_without_fence).
      sw_only had `available*target_util - m_total_movable_area`, macro area in both terms — a
      shortfall of exactly `M*(1-td)`. Measured: **adaptec5 0 → 310,073 fillers, newblue4 0 →
      205,682, newblue1 100,375 → 181,724.** adaptec5 now matches XPlace's own log
      (`#Fillers: 310073 Filler size: (1.0795e+01, 1.2000e+01)`) exactly on count and size.
      Landed together with five other divergences in the same function (macro exclusion from the
      size sample; filler height = ROW height, not a mean; the `target_density` raise to
      `stdcell_util`; a warning when zero fillers result). ISPD2005 verified bit-identical.
      **Quality impact UNMEASURED** — needs the MMS re-baseline. Full writeup + sweep instructions:
      `1_REVIEW/NEW_HANDOFF_filler_faithfulness_20260731.md`.

Measured 2026-07-26 (report `1_REVIEW/NEW_mms_overflow_faithfulness_20260726.md`, data
`2_ARTIFACTS/mms_xplace_overflow.tsv`). **Root cause = FILLERS, not grid, not deposit formula.** sw_only's
own `[OVFW-DIAG]` line (newblue2): `sharp/+filler=0.143` ≈ **XPlace 0.145** — the overflow formula is
faithful; my earlier "gap" compared sw's *no-filler* (0.060) to XPlace's *with-filler* number. XPlace
counts filler density in BOTH its convergence and its "exact" report. sw_only converges on the
**filler-EXCLUDED** smoothed overflow (`convergence_include_fillers` defaults **false**, Output.cpp:697),
so it stops at no-filler 0.044<0.07 while the real (with-filler) overflow is still 0.102 → premature stop
→ cells+fillers clumped → under-spread → artificially low HPWL on the 9 macro-heavy designs.
**VALIDATED 2026-07-27** (flag on, 9 designs; data `2_ARTIFACTS/mms_fillconv_results.tsv`): correct fix but
PARTIAL — not the one-line silver bullet. newblue3 cleanly fixed (−12.7%→−1.7% vs XPlace); newblue6/2,
adaptec2, newblue7 within ~±3%. BUT adaptec5 (−22%) & newblue4 (−25%) ran longer yet stayed under-spread
(sharp/+filler 0.14–0.18); newblue5 DIVERGED (sharp 0.61). Residual: convergence is SMOOTHED (√2 inflation,
XPlace does this too), so "converged" ≠ fully spread on the hardest designs.
- [→] **MOVED TO #13 (2026-07-31): adopt `convergence_include_fillers=true`.** Still the right
      change, but it is a *phase-2* change, not a phase-1 one — see "Convergence metric" under
      TODO #13. Do not do it standalone.
- [ ] **adaptec5 + newblue4 spreading** — they don't spread even running to ~950 iters; investigate (density
      force / precond / macro handling on these). Separate from the metric fix.
      **RULED OUT 2026-07-31: TODO #11b (macro deposit weight = target_density) is not the fix.**
      The MMS A/B (`1_REVIEW/NEW_REPORT_footprint_ab_20260731.md`) hit exactly these three
      designs — adaptec5, newblue4, and newblue5 below — as its three worst results (+17.9%,
      +6.1%, +11.2% HPWL). It does marginally improve physical spread on all three (letting
      macros deposit less density), but the wirelength cost is far too large to call it a fix.
      Rejected; `macro_td_expand_ratio` defaults false. Next attempt should look elsewhere
      (density force / precond / macro handling, as originally scoped).
- [ ] **newblue5 divergence** under the stricter metric (sharp/+filler 0.61) — own investigation.
      Same ruled-out note as above applies.
- [→] **MOVED TO #13: always include fillers in the CONVERGENCE signal.** (The *reporting* half of
      this is done — see Step 1.) Verified XPlace counts fillers in its GP-stop metric `overflow_fn`.
- [x] **Report the EXACT (sharp) + filler overflow as the headline number** — DONE in Step 1.
- [ ] **Rename `clamp` → `smooth`** in `computeOverflow` (signature `bool clamp` + call sites + comments).
      "smoothed" is the intuitive term (clamp=true = the √2·bin footprint smoothing).
- [ ] (No grid or deposit-formula change needed — both already match XPlace.)

---

## #5 — Logger cleanup (COMPLETED 2026-07-30 for sw_only)

**Status:** sw_only logging refactor completed and verified. See `history.md` for full details.

**Remaining open follow-ups:**
- [ ] `host/src/pl_algo/{src/Logger.cpp,include/Logger.h}` still has the OLD singleton/tabulate
      Logger. The two copies have now diverged; fold this rewrite in when the hosts merge (#9).
- [ ] The two summary tables still render with a **double border** (`| +---+---+ |`) because the
      callers nest a `Table` inside a title-only outer `Table` (`DataBase::printInfo`,
      `Placer::exportSummaryReports`). Caller-side tabulate idiom, not the Logger. Flattening to one
      bordered table with a title row would drop a level of box-drawing noise.
- [ ] The welcome banner still goes straight to `cout` via `banner.print(cout)`, bypassing the
      Logger — it is the only remaining source of trailing whitespace (11 lines). It is terminal-only
      decoration now, so this is cosmetic.
- [ ] `run.log` is written for **every** run including DSE sweeps (quiet only silences the console).
      ~250 KB/run at 1200 iterations; a 500-run sweep adds ~125 MB. Gate on `quiet` if that bites —
      see TODO #1's results/ pruning.
- [ ] "Algorithm time (s) | 0.000" in the Run Statistics table looks wrong (`algo_time` never
      accumulated?). Noticed in passing, unrelated to logging — not investigated.

---

## #6 — Port Xplace's operator-level optimizations (opened 2026-07-29)

Xplace (Liu et al., TCAD 2023) gets ~2x over DREAMPlace almost entirely from operator-level
restructuring of the same ePlace math we already implement — not a new algorithm. Four distinct
techniques, worth evaluating separately since our C++/HLS setting differs from Xplace's
PyTorch/autograd setting (see conversation on 2026-07-29 for the paper summary):

- [ ] **Operator combination** — Xplace merges WA wirelength, WA gradient, and HPWL into one pass
      since all three need the same per-net min/max (`x+`, `x-`). Check `Partials.cpp`
      (`computeHpwlPartials_CPU`) for redundant min/max recomputation across these three quantities;
      merge into a single pass if found.
- [ ] **Operator extraction** — Xplace factors out the shared cell-density-map computation used by
      both the electrostatic-system objective and the overflow-ratio metric, computing it once.
      Check `Density.cpp` (`compute_eField_DCT`) and the overflow computation (`Output.cpp`,
      `computeOverflow`) for duplicate density-map builds; share one if found.
- [ ] **Operator reduction** — Xplace's version of this is "skip PyTorch autograd, hand-derive
      gradients" — not directly portable since sw_only has no autograd layer. The analogous risk in
      our setting is redundant kernel launches / synchronization in `pl_algo`'s dataflow modules
      (`vck5000/pl/src/pl_algo/src/modules/*.hpp`) — worth a pass once those modules are filled in
      (post Gate 1) to check for avoidable per-iteration launch/sync overhead, but low priority until
      the modules exist.
- [ ] **Operator skipping** — Xplace skips the density-gradient operator most iterations early in
      placement, when `|density_grad|/|wirelength_grad| < 0.01` and `iteration < 100`. Check whether
      sw_only's early iterations already have a near-zero density term (γ/λ schedule) and would
      benefit from literally skipping the density gradient computation on those iterations rather
      than computing and discarding a near-zero result — potential real iteration-time win on sw_only,
      and worth carrying into `pl_algo`'s `density_manager`/`iteration_update` modules from the start.

---

## #7 — Investigate two more Xplace contributions (opened 2026-07-29)

From the same Xplace summary (2026-07-29 conversation), items 2 and 4 of the contributions list —
distinct from the operator-level optimizations in #6, worth a look independently:

- [ ] **Placement-stage-aware parameter scheduling** — Xplace defines a "precondition weighted
      ratio" κ(η) = |H_D| / |H_W + H_D| from the existing preconditioner terms (net-degree term H_W,
      cell-area term H_D) to classify each iteration into wirelength-dominated / spreading /
      final-overlap phases, then deliberately slows the γ/λ parameter update during the spreading
      phase (0.5 < κ < 0.95) to trade some runtime for better final quality. sw_only already has a
      preconditioner (`dff_force_ratio`/`precond_raw_area`, see memory
      `mms-faithful-field-ab-result`) and a γ/λ schedule (kept host-side per `AIEplace/CLAUDE.md`) —
      check whether κ(η) is cheap to compute from what we already track, and whether slowing the
      schedule during spreading helps our known-hard designs (adaptec5/newblue4/newblue5, see memory
      `mms-hard-spreading-three-diseases`) without hurting runtime elsewhere.
- [ ] **Xplace-Route (detailed-routability-driven placement)** — Xplace extends beyond global-router
      validation to actual detailed-routability: PG-rail/I/O-pin density penalties, a GPU pattern
      router for a live congestion map, congestion-driven cell inflation with historical inflation
      ratio, and a pin-accessibility post-refinement pass. This is a much bigger scope addition than
      #6/the scheduling item — AIEplace has no router or DP-stage routability handling today. Treat
      as a research question first: is detailed-routability in scope for this project's placement
      goal at all, or is HPWL/overflow-vs-XPlace (the current success metric) the right target to stay
      focused on? Needs a scoping conversation before any implementation work.

---

## #8 — Investigate zero-area interior `terminal` nodes in newblue5 (opened 2026-07-29)

> ### ✅ ANSWERED 2026-07-31 — the terminals are INERT. Report:
> ### `1_MARK_TO_REVIEW/NEW_REPORT_newblue5_todo8_20260731.md`
>
> **The terminal-node theory is dead, structurally — not merely "no evidence found".** All 4790 are
> 0×0, all FIXED, and the zero-area set and the `terminal` set **coincide exactly** (0 zero-area
> movable nodes anywhere). Every terminal pin offset is exactly **(0,0)** — so the "scrubbed macro
> left its pin geometry behind" theory is falsified (the 4 real movable macros, by contrast, carry
> 795/796/413/413 pins spread across their full footprint). Decisively: newblue5's total fixed area
> is **exactly 0.0** (confirmed by XPlace's own log, `FixArea: 0.000E+00`), so a terminal contributes
> zero to the density map, zero to the overflow numerator and zero to the filler budget. **A
> zero-area node cannot form a density hotspot**, so the planned per-region probe was unnecessary
> and deliberately not built.
>
> **What is actually wrong (canonical config: grid 1024, td 0.5, seed 42):**
> 1. **newblue5 places with ZERO fillers.** 51.4% of its movable area is movable macros (172.0M of
>    334.3M; 26.4% of the die). HEAD's `addFillers` counts macro area in *both* terms, driving the
>    budget to −9.2M. XPlace's std-cell frame gives **632,490** fillers — my independent arithmetic
>    from the bookshelf files matched XPlace's log digit-for-digit (`#Fillers: 632490`, size
>    `(1.0112e+01, 1.2000e+01)`).
> 2. **There is no "explosion".** HPWL rises smoothly (ordinary spreading). The real failure is an
>    overflow **floor** plus λ runaway: iterations 674→1044 move overflow 0.3544→0.3500 (flat) while
>    λ goes 0.147→102.5 (**×700**) and HPWL inflates **+17%**. That is TODO #4's unowned
>    "density-weight runaway"; newblue5 is a clean witness for it.
> 3. **The floor is structural** and XPlace hits it too — its Mixed-GP ends at **0.1697**, above its
>    own doubled 0.14 target, reaching 0.0452 only *after* macro legalization (phase 2).
>
> **2×2 measured** (both binaries verified bit-identical on adaptec1):
> | arm | fillers | #11b | HPWL | vs XPlace | sharp/+filler | stop |
> |---|---|---|---|---|---|---|
> | A baseline | 0 | off | 4.399e8 | +16.0% | 0.350 | `divergence_guard`, fallback |
> | B fillers | 632k | off | **3.994e8** | **+5.3%** | 0.429 | `divergence_guard`, fallback |
> | C #11b | 0 | on | 4.892e8 | +29.0% | 0.338 | **converged** |
> | D both | 632k | on | **4.003e8** | **+5.6%** | 0.394 | **converged** |
> | XPlace | 632,490 | on | 3.792e8 | — | **0.1697** | phase-1 handoff |
>
> Fillers buy the **HPWL** (−9.2%); `macro_td_expand_ratio` buys the **convergence**. Arm C
> reproduces the footprint A/B's newblue5 figure exactly (+11.2%), confirming the setup matches the
> record.
>
> ### ⚠️ CORRECTION 2026-08-01 — "2–2.5× under-spread vs XPlace" was WRONG (my error)
> It compared **our macro-INCLUDED** overflow to **XPlace's macro-EXCLUDED** one.
> `run_placement_nesterov.py:173` sets `ps.zero_macro_grad = True` **before** the Mixed-GP
> `evaluate_placement` at line 182, and `evaluator.py:30` then drops `is_mov_macro`. **So the 0.1697
> in `tools/benchmarks.py::_XPLACE_MMS_MIXED_GP` EXCLUDES movable macros** — this affects every MMS
> overflow comparison, not just newblue5. Confirmed three ways: code order; arithmetic (newblue5's
> macros alone contribute ≈0.257, so 0.1697 cannot include them); and XPlace's own
> `--global_placement False` run, where `zero_macro_grad` is never set, reporting **0.4836**.
>
> Recomputed like-for-like from the DEFs (`/tmp/t8/overflow_variants.py`, validated against
> sw_only's own `sharp/no-filler`): **A 0.0964 · B 0.1244 · D 0.0940 vs XPlace 0.1697.**
> **sw_only's std-cell spreading matches or beats XPlace.** The whole "under-spread" signal was the
> movable macros — exactly what phase 2 legalizes. Strengthens the case for **#13**.
>
> **Unresolved:** the filler axis. Code says XPlace's exact overflow also excludes fillers
> (`get_mov_node_info` appends them after `mov_rhs`; `get_obj_overflow` slices `[mov_lhs:mov_rhs]`),
> but the newblue2 calibration in `overflow-metric-grid-faithfulness` says included. Those
> contradict — resolve before treating the numbers above as final.
>
> **Follow-up actions this creates (not done):**
> - [ ] **Relabel the overflow column of `tools/benchmarks.py::_XPLACE_MMS_MIXED_GP`** (and its
>       note in `BENCHMARKS.md`) as **macro-EXCLUDED**. As written it invites exactly the
>       apples-to-oranges comparison made above. The HPWL column is unaffected (`get_obj_hpwl`
>       has no such exclusion).
> - [ ] **Re-read TODO #4's reframing in this light.** Its "XPlace's Mixed-GP ends at 0.10–0.18 on
>       15 of 16 MMS designs" figures are macro-EXCLUDED, so they describe XPlace's *std-cell*
>       spread. Ours on the same basis is ~0.09–0.12 — i.e. comparable or better, which changes
>       what "we don't spread" means across the whole suite.
> - [ ] **Add a macro-excluded variant to `computeOverflow`** so this is a first-class number
>       rather than a post-hoc DEF script. This is the config-gated diagnostic originally scoped
>       here — now justified by evidence rather than speculation.
>
> **`sharp/+filler` IS XPlace's "exact Overflow"** — verified in `src/evaluator.py`:
> `get_obj_overflow` uses exact node size with `node_weight = ones` (no `expand_ratio`, no macro
> fill) over `total_mov_area_without_filler`. So confining #11b to the clamp branch in `Grid.cpp` is
> *faithful*, and the comparison holds in every arm.
>
> ### ⚠️ CORRECTION 2026-08-01 (Mark) — "#11b blinds the stop metric" was WRONG
> I originally wrote that #11b makes macros "contribute zero to the convergence signal", framing it
> as a symptomatic fix. **Wrong.** A macro depositing at exactly `target_density` fills a covered
> bin to **exactly capacity** (`bin_area × td`), excess 0 — which is *correct*: a bin taken up
> solely by one large macro is full at the target density, not overflowed. The macro has not
> vanished; it has consumed the bin's entire budget, so **anything overlapping it overflows
> immediately** (`density = cap + cell_area ⇒ excess = cell_area`). Macros generate overflow
> exactly when other nodes overlap them — the desired behaviour.
>
> The real defect is the reverse: with #11b **off**, every movable macro emits `area × (1 − td)` of
> overflow **permanently, whether or not anything overlaps it** — irreducible by any movement, on a
> design where macros are 26.4% of the die. **#11b is a correctness fix, not a symptomatic one.**
>
> What survives: the headline `Final Overflow (exact, +fillers)` deposits macros at weight 1 (the
> #11b branch is clamp-only), so it still carries that spurious term on mixed-size designs — arm D
> reads sharp 0.394 vs clamp 0.0676. **The REPORTED number is the misleading one, not the
> convergence signal.** XPlace avoids this by excluding macros from its phase-1 eval
> (`zero_macro_grad`) — same conclusion as the macro-exclusion correction above, reached
> independently.
>
> ### ✅ DECIDED (Mark, 2026-08-01): `macro_td_expand_ratio` → expected to be LOCKED `true`
> Keep it a toggle **for now**, while testing continues; the expectation is that it becomes
> unconditional and the legacy branch is deleted. It is XPlace-faithful *and* correct on the
> density accounting. The only thing holding it back is the 16-design A/B's −5.2% mean HPWL — and
> that A/B ran on **zero-filler arms**, so it must be redone after the filler change lands.
> - [ ] Re-run the #11b A/B with correct fillers.
> - [ ] Then remove the toggle + legacy branch (as #11a was), per TODO #2's retire-settled-toggles
>       pattern.
>
> The real fix for the residual gap is **TODO #13 phase 2** — newblue5 is the suite's strongest
> case for it.
>
> ⚠️ **Corrections to the record (see report §5):**
> - `NEW_HANDOFF_filler_faithfulness_20260731.md` §4 is **wrong** that the memories
>   `mms-hard-spreading-three-diseases` / `overflow-metric-grid-faithfulness` were derived on a
>   *zero-filler* newblue4/adaptec5. Those runs were at **td = 1.0**, where the macro term cancels:
>   measured **adaptec5 1,509,741 · newblue4 1,104,300 · newblue5 2,602,944** fillers. (Mark caught
>   this from the GIFs.) The memories stand; the handoff's inference about them does not.
> - **Three newblue5 configurations are conflated across the notes** — td 1.0/auto (converged,
>   3.246e8 @ 0.053), td 1.0/2048 + `convergence_include_fillers` (the GIF that opened this TODO),
>   and canonical td 0.5/1024. **The GIF was not the canonical config.** State td and grid on any
>   future newblue5 claim.
> - **TODO #11b's rejection is confounded twice**, not once: with the stop criterion (already noted)
>   *and* with zero fillers. #11b alone is +11.2% HPWL; #11b with correct fillers is −9.0%.

Triggered by `2_ARTIFACTS/newblue5_placement.gif`: placement looks fine, then diverges/explodes
starting around iter ~400. Visually there are large *movable* macros (correctly parsed — verified,
not a fixed/movable bug, see below) plus a scatter of small dark-red squares in the die interior
that turned out to be the benchmark's zero-area `terminal` nodes, not misclassified macros.

**Established so far:**
- `newblue5.nodes`/`.pl`: 4790 nodes carry the literal `terminal` keyword, **all exactly 0×0** in
  size, and are marked `FIXED` in `.pl`. Confirmed via the parser (`DataBase.cpp` in
  `host/src/sw_only/src/`) that fixed-vs-movable classification is purely keyword/`.pl`-driven —
  no size-based logic anywhere flips a movable node to fixed (`isLarge()`/`m_is_large` is
  bookkeeping-only, never touches `m_status`). The 4 genuinely huge macros (e.g. `o1228259`–
  `o1228262`, up to 10260×3036) are correctly movable. Visualizer (`Visualizer.cpp`) draws FIXED
  as dark red w/ border, movable macros as bright red w/o border, and floors tiny sizes up to a
  min render size — so the 4790 zero-area FIXED terminals are what render as the scattered small
  dark-red squares. **Not a classification bug in our code.**
- XPlace (`~/phd/Xplace`) handles these identically: `terminal` alone doesn't set fixed (only the
  `.pl` `/FIXED` suffix does — `file_bkshf_db.cpp:711-788,1078-1111`), and it has zero position/
  size-based special-casing in `GPDatabase::setupNodes`. XPlace *does* have a real placement-
  blockage/keepout concept (`Database::placeBlockages`, distinct `"Blkg"` node type,
  `GPDatabase.cpp:75-91`), but it's only populated from DEF `PLACEMENT` blockage sections or a
  DAC/ICCAD-2012-only irregular-shape bookshelf extension (`file_bkshf_db.cpp:611-618`, explicitly
  commented as ICCAD/DAC2012-only) — **neither applies to newblue5** (plain ISPD05 bookshelf,
  `.aux` lists only `nodes/nets/wts/pl/scl`, no shapes file). So XPlace creates **zero** keepout
  geometry for these nodes either — same blind spot in both tools, not a divergence between them.
- Working theory (Mark's, plausible but unconfirmed): these are pin stubs left behind after IBM
  scrubbed proprietary hard macros out of the original design for the ISPD05 release — real
  connectivity, zero remaining footprint. Supporting evidence pulled from `newblue5.nets`: 4790
  terminals, avg net-degree 17.2, several groups of terminals sharing an *exact* degree (8 terminals
  at degree 313, 2 at 298, 2 at 88, 2 at 74/64, etc. — plausible repeated/mirrored macro instances),
  many as 2–3-pin nets fanning out to nearby small cells (i.e. plausible former intra-macro nets
  redirected to the macro's boundary pin). Terminal y-coordinates span the *whole* die height
  (p10=5635 to p90=19675 out of a ~67–25615 core range) — not clustered at the periphery like
  classic IO pads — which favors the removed-macro theory over "these are just boundary IO pads"
  for most of the 4790, though the highest-degree cluster (313-degree group) sits at a fairly
  narrow y-band (~1400–1700) which could equally be a pad row or a cluster of related macro pins.
- Checked `results/mms_suite_precondON/newblue5/newblue5/20260717_234925_203_cpu_cpu/schedule_trace.csv`
  around iter 400: `grad_norm_sq` and `pos_norm_sq` are already climbing *smoothly* well before 400
  (e.g. iter 395: grad_norm_sq=4.58e4 → iter 410: 1.21e5, steady ~exponential-ish growth, no sudden
  kink at exactly 400) — consistent with the existing `mms-hard-spreading-three-diseases` memory's
  characterization of newblue5 as a genuine λ-starved/under-damped-macro divergence, not a single
  discrete trigger event. Doesn't yet prove or rule out the terminal hub theory as a contributor.

**Original next steps — ALL THREE RESOLVED 2026-07-31 (see the banner above):**
- [x] ~~Per-node / per-region density data around the hub clusters.~~ **Not needed and deliberately
      not built.** The terminals are 0×0 and newblue5's total fixed area is *exactly 0.0*, so they
      deposit nothing — a zero-area node cannot produce a density hotspot. Building the probe would
      have been speculative (CLAUDE.md rule 2).
- [x] **Gradient/preconditioner path audited against a degree-313 zero-area node — clean.**
      `addNet` is called once per *pin* (`DataBase.cpp:626`), so `getNets().size()` really is the pin
      count and matches the per-pin gradient accumulation; `updatePrecondWeights` matches XPlace
      exactly (`alpha_1 = mov_node_to_num_pins`, `param_scheduler.py:372`); the WA gradient's
      max-shift is algebraically exact; nothing divides by node width/height. No hub-node or
      zero-area assumption anywhere.
      *Incidental (benign, unfixed):* `computeHpwlPartials` clears `probe_grad` only for movable +
      IOPad nodes, but the accumulation loop writes it for **every** pin including FIXED components,
      so a fixed node's `probe_grad` accrues across iterations. Never read for stepping (only
      `getMovableNodes()` steps), magnitudes stay O(1e5) — wasted work, not a bug. Worth a one-line
      skip if anyone is in there anyway.
- [x] **XPlace corroboration done from existing logs — no new GPU run needed.**
      `~/phd/Xplace/result/2026-07-17-23:11:24_newblue5` is the canonical Mixed-GP reference
      (3.792e8 @ 0.1697 → macro legalization → phase 2 `GP Stop! #Iters 2010 … overflow: 0.0452`).
      Confirmed newblue5's td=0.5 / grid=1024 originate in `utils/setup_dataset.py` (so
      `args.target_density < 1.0` holds and **#11b is active in the reference**), and that XPlace
      produces exactly 632,490 fillers. **Not a shared blind spot on the terminals** — the question
      is moot, since neither tool deposits anything for a zero-area node.

---

## #9 — User Friendliness (opened 2026-07-30)

- [ ] **Merge the `sw_only` / `pl_algo` host forks into one host** (deferred until pl_algo is fully
      brought up — Mark, 2026-07-30). Target: a single host that runs the placement iteration on the
      CPU, or offloads to the VCK5000 when a card/xclbin is available. Keeping them separate is a
      deliberate choice *for now* so pl_algo bring-up can move without destabilizing the tuned golden.

      **Why it needs doing:** the two trees are a silent fork, not a shared base. 15 files exist in
      both `host/src/sw_only/` and `host/src/pl_algo/` under the same class names and namespace, with
      the diffs already large:
      | file | diff lines |
      |---|---|
      | `src/DataBase.cpp` | 886 |
      | `src/Grid.cpp` / `src/Net.cpp` / `src/Logger.cpp` | 114 / 94 / 80 |
      | `include/DataBase.h` / `include/Bin.h` | 111 / 101 |
      | `include/Logger.h` / `include/Net.h` / `include/Common.h` | 44 / 41 / 40 |
      | `include/Node.h` / `include/MacroClass.h` / `include/Grid.h` / `include/IOPad.h` / `include/Component.h` | 33 / 19 / 17 / 15 / 13 |
      | `src/Common.cpp` | 8 |
      A parser or geometry fix landed in one tree does not exist in the other, and nothing catches it.
      Every cleanup applied to sw_only (see the 2026-07-30 review) has to be re-applied by hand to
      pl_algo, or the divergence grows.

      **Shape of the merge:** extract `host/src/common/` (DataBase, Grid, Node/Component/IOPad, Net,
      Bin, Logger, Common, MacroClass) and leave the variant dirs holding only what actually differs
      — the Placer/Driver and their backends. Reconcile `DataBase.cpp` first; it carries most of the
      drift. Then collapse the compute-method dispatch (`partials_method` / `density_method`) into the
      CPU-vs-accelerated selector the merged host needs anyway.

- [ ] **Dependencies** — `vck5000/requirements.txt` was just added (JSON→TOML config migration
      session) covering `tomlkit`, `numpy`, `matplotlib`, `SALib`, `Pillow`, `pyunpack`, `patool`.
      `pyunpack`/`patool` (used only by `host/benchmarks/ispd2005_2015.py`/`ispd2019.py`, the
      benchmark-download scripts) were NOT installed in this environment and so were never actually
      exercised against the new file — verify `pip install -r requirements.txt` succeeds clean and
      a benchmark download still works (patool may also need a system `unrar`/`7z` on PATH for some
      archive formats). Keep the file in sync as new scripts/imports are added.

---

## #10 — pl_algo cleanup & clarity (opened 2026-07-30, deferred)

From the 2026-07-30 fresh-eyes codebase review. **Deliberately deferred** — pl_algo is mid-bring-up
and these are clarity/hygiene items, not blockers. Do them when pl_algo settles, or opportunistically
when already editing the file in question. The equivalent sw_only items were fixed 2026-07-30.

### Stale docs that actively mislead (cheapest, highest value)
- [ ] **`pl/src/pl_algo/README.md` Status is wrong.** Says "module internals are stubs" and "the host
      `pl_algo` variant, the AIE `pl_algo` variant (FFT pool + HPWL graph), and IDXST are not yet
      written." All of those exist and are verified; `DATAFLOW.md` (which IS current) says Stage 5c is
      built and C-synthesizing. The Layout list also names modules that don't exist — `hpwl_manager`
      (the real file is `src/modules/hpwl_gradient.hpp`) and `density_manager` (a dead stub, below).
- [ ] **Root `CLAUDE.md` "pl_algo current state (2026-06)" is stale** in the same way: "Module
      internals are stubs" and "Next step = Gate 1: synthesize". Gate 1 passed. Refresh from
      `DATAFLOW.md`, and consider pointing CLAUDE.md *at* `DATAFLOW.md` rather than restating it, so
      there is one place to keep current.
- [ ] **`host/src/pl_algo/src/Driver.cpp` file header documents an obsolete kernel signature**
      (5 args, `float* result` as arg 3). `top()` has taken ~25 args for a long time. Either update it
      or delete it and point at `top.cpp` / `host_interface.hpp`.
- [ ] **`pl/src/pl_algo/CHECKPOINT.md` is misplaced and describes reverted code.** Its content is
      entirely sw_only numerics history (BB clamp, λ magnitude, DCT normalization), yet it lives in
      the pl_algo source dir where it reads as current guidance. Two of its headline items no longer
      exist in any source file: the `precond_weight_mean` BB clamp (removed — `Step.cpp:56` now says
      "No magnitude clamp — mirrors XPlace") and the `dct_normalize_inverse` flag (gone; the inverse
      is unconditionally unnormalized). **Keep the history** — it explains *why* the current defaults
      are what they are — but move it to an archive location with a "HISTORICAL — see
      `placer/Step.cpp` and `placer/Density.cpp` for current behavior" banner at the top.

### Dead / unwired code
- [ ] **Delete `src/modules/density_manager.hpp`.** 54 lines that TODO-stub the whole density solve and
      just zero their outputs. Its job was split across `density_bin` + `dct_transpose` + `spectral` +
      `force_gather`. Nothing includes it (only a comment in `density_bin.hpp` and the `model/`
      programs mention the name), but the README lists it as a real module — so a reader opens it
      first and concludes nothing is implemented.
- [ ] **Document that `bb_reduce.hpp` + `param_scheduler.hpp` are built-but-not-wired.** 320 lines,
      verified against the golden, C-synthesizing via `model/synth_check.tcl`, but not included by
      `top.cpp`. This is the correct in-progress state; it just needs one line in `DATAFLOW.md`'s
      Status so nobody re-derives them. (`DATAFLOW.md` half-says this already — make it explicit.)

### Structural
- [ ] **Port aliasing in `top.cpp` is a silent-wrong-answer risk.** 11 modes reinterpret the same 12
      `m_axi` ports per mode — `g_density` arrives on `dct_in`, `alpha` on `inv_lut_step`, `die_ymax`
      on `target_density`. Defensible for bring-up (one xclbin, many tests) and it *is* documented,
      but only in a 40-line prose block in `host_interface.hpp` that has to be cross-referenced by
      hand; host/kernel drift produces wrong numbers, not a compile error. Stage 5's unified datapath
      is meant to retire this — until then, consider a per-mode `struct` of named references, or at
      minimum `static_assert`-able aliases so the mapping lives in code rather than a comment.
- [ ] **`Driver.cpp` is 18× the same XRT boilerplate** (~1188 lines): open device → load xclbin →
      alloc bo per arg → memcpy → sync-to-device → run → sync-back, once per `run*()`. A small
      `KernelSession` helper (device/uuid/kernel + a `bind(idx, ptr, bytes)`) would cut it hard and
      make the port-aliasing above visible in one place.
- [ ] **20+ copy-pasted `run-*` targets in `vck5000/Makefile`.** Each is the same 6 lines of
      `emconfigutil` + `LD_LIBRARY_PATH` + `$(HOST_EXE) --flag`. A `STAGE4_RUN` define already exists
      and is used by 6 of them; ~14 others are verbatim duplicates. Fold them all into the define.

### Repo hygiene (pl_algo-scoped)
- [ ] **Build artifacts are tracked in git.** `git ls-files` shows `pl/src/pl_algo/_x/` (v++ logs,
      guidance HTML, `.pb3`) and the compiled ELFs `pl/src/pl_algo/model/density_model` and
      `model/density_bin_model`. Untrack and extend `.gitignore` (`_x/`, and the `model/` binaries —
      `model/.gitignore` exists but isn't catching them).
- [ ] **`common.mk` defaults point at the dead variant:** `AIE ?= markv1`, `PL ?= markv1`. A bare
      `make` builds the legacy partial-offload design, not `pl_algo`. Flip once pl_algo is the
      primary target (`HOST ?= sw_only` should probably stay until the #9 merge lands).

---

## #11 — XPlace density-footprint faithfulness gaps (opened 2026-07-30)

> ### CLEANUP DONE 2026-07-31 — asymmetric, on purpose (Mark's call)
>
> **#11a `xplace_die_projection` — toggle DELETED, projection now unconditional.** Its toggle
> guarded the *legacy* path, which is the one that is NOT XPlace-faithful, so there was no
> faithfulness argument for keeping it, and the A/B measured it neutral (±0.7%). Removed:
> the member + config key, the `if (xplace_die_projection)` guards in `Step.cpp`
> (`enforceDieBoundaries`) and `Setup.cpp` (`initializePlacement`), the deposit-time shift in
> `Grid.cpp::computeNodeFootprint`, and `FootprintConfig::shift_in_die` /
> `Grid::setShiftFootprintInDie` / `m_shift_footprint_in_die`. The in-die correction now lives
> in exactly one place (the position) instead of two.
> *Safety condition verified before deleting the shift:* fillers ARE in `mv_movable_nodes`
> (`DataBase.cpp:342`) and `enforceDieBoundaries` clamps BOTH `node_pos` and `probe_pos`
> (`Step.cpp`) — and `computeNodeFootprint` reads the probe — so every non-FIXED node's footprint
> is legal by construction. Fixed nodes are geometrically clipped by the caller as before.
>
> **#11b `macro_td_expand_ratio` — toggle KEPT, default false.** Reverses the report's
> "delete the losing branch" next-step, and resolves its contradiction with the note below
> (which already said the toggle "stays in the code as a documented, deliberate divergence").
> Two reasons:
> 1. It is XPlace-FAITHFUL (`database.py:921-923`). Per CLAUDE.md a divergence must be deliberate
>    and documented; the toggle *is* that documentation, and it keeps the divergence reversible.
> 2. **Its rejection is CONFOUNDED with the stop criterion.** The A/B report itself records that
>    every `true` arm "halted 25–65 iterations early on a deflated signal" — macros depositing at
>    `target_density` push less mass into the smoothed overflow that drives convergence. So the
>    +5.2% HPWL is partly "this arm stopped sooner", not purely "this arm places worse".
>    **Re-test #11b once the stop criterion is fixed (TODO #4).** Deleting it now would mean
>    re-implementing it to run that test.
> `tagMovableMacros()` / `Node::m_is_movable_macro` therefore STAY — they exist to serve #11b.
>
> **VERIFIED bit-identical.** adaptec1, 60 iters, `random_seed = 42`: HEAD (`56d8a16`, pre-removal)
> and this tree both give restored-best HPWL `60353676.000000`, overflow `0.826129`. Three
> consecutive runs of each are also identical to themselves, so `deterministic = true` holds.
>
> ⚠️ **Gotcha that cost time here — `run_config.toml` does NOT set `random_seed`.** It defaults to
> `-1`, which `Setup.cpp::initializePlacement` turns into `std::srand(std::time(nullptr))` — a
> time-based seed, so a bare template run randomizes the initial placement and looks
> nondeterministic. Sweeps are unaffected (they pin `random_seed` explicitly, e.g.
> `gen_footprint_ab_configs.py`). **Any manual A/B must pin `random_seed`** or it measures
> seed-to-seed variation, not the change under test.
>
> **DEFAULTS APPLIED 2026-07-31.** `xplace_die_projection = true`, `macro_td_expand_ratio = false`
> is now the default in both `run_config.toml` and the `AIEplace.h` member-initializer fallback (so
> the two agree whether or not a config file names the keys). Rebuilt clean; confirmed the new
> default changes the trajectory vs. the pre-#11 golden (as expected, since #11a is not a no-op) via
> the header-fallback path — `ef1c8f29…` vs. the old legacy `8019a989…`. **Any goldens captured
> before today no longer match** — that's intentional per the A/B, not a regression; recapture
> deliberately rather than diffing against old references. #11b confirmed faithful to XPlace too
> (its `database.py:921-923` / `density_map_cuda_kernel.cu:28` overwrite `expand_ratio` with
> `target_density` for movable macros, breaking area conservation on XPlace's own side) — rejected
> on results, not on faithfulness grounds; the toggle stays in the code as a documented, deliberate
> divergence rather than an oversight. Toggles NOT yet deleted (see cleanup steps below).
>
> **RESULT 2026-07-31 — A/B DONE (46/48 runs). Report:
> `1_REVIEW/NEW_REPORT_footprint_ab_20260731.md`.**
> **#11a ADOPT** — exactly neutral (mean +0.0% HPWL / 15 designs, worst 0.7%); take it for
> faithfulness + deleting the deposit-time-shift branch. **#11b REJECT** — mean +5.2% HPWL worse
> / 7 designs, and worst (+17.9%, +11.2%, +6.1%) on adaptec5/newblue5/newblue4, the exact designs
> it was meant to help. Caveat: a rebuild landed mid-sweep (00:05 Jul 31, the TODO #12 threading
> work); newblue7's two arms ran on the new binary and are excluded — no conclusion mixes binaries.
> Cleanup steps listed at the end of the report.
>
> **STATUS 2026-07-30: both implemented behind runtime toggles; A/B prepped, NOT yet launched.**
> `xplace_die_projection` (#11a) and `macro_td_expand_ratio` (#11b) in `run_config.toml`, both
> default `false` = legacy. Legacy path verified bit-identical to the pre-change golden. Harness:
> `2_ARTIFACTS/gen_footprint_ab_configs.py` (48 configs), `run_footprint_ab.sh`,
> `analyze_footprint_ab.py`. Launch with:
> ```
> python3 2_ARTIFACTS/gen_footprint_ab_configs.py
> nohup bash 2_ARTIFACTS/run_footprint_ab.sh > /tmp/fp_ab/runner.log 2>&1 &
> ```
> Decide from the scorecard, then **delete the losing branch** — these toggles are temporary.

Found by verifying `computeNodeFootprint()` line-by-line against XPlace source (not comments).
**Most of the footprint is faithful** — √2 clamp, area-conserving weight, centering, macros
unaffected, fixed nodes deposited at exact size and geometrically clipped, fixed density capped at
`target_density`. Two genuine divergences fell out, neither previously documented. Per CLAUDE.md,
divergence must be *deliberate and documented* — right now these are accidental.

### (a) The in-die shift is applied to the wrong thing, with the wrong size
**XPlace** (`run_placement_nesterov.py:5-11`, applied in `calculator.py:27` on **every** gradient
evaluation) constrains the **position**:
```python
node_pos_lb = mov_node_size / 2 + data.die_ll + 1e-4
node_pos_ub = data.die_ur - mov_node_size / 2 + data.die_ll - 1e-4
x.data.clamp_(min=node_pos_lb, max=node_pos_ub)
```
`mov_node_size` here is the **EXPANDED (√2-clamped)** size (`get_mov_node_info` returns
`mov_node_size = clamp_mov_node_size`), and `node_pos` is the node **CENTER** (confirmed:
`GPDatabase::getNodeCPosTensor` = `Lx + Width/2`; the CUDA kernel forms `node_pos ± node_size/2`).
So XPlace projects the optimization variable so the *expanded* footprint is always fully in-die.
The cell itself moves.

**sw_only** does it in two disconnected places, and neither matches:
- `enforceDieBoundaries()` (`Step.cpp`) clamps `node_pos`/`probe_pos` using the **RAW** size
  (`max_x = die_w - getXsize()`), so the expanded footprint can still hang off the edge.
- `computeNodeFootprint()` then applies a **second, non-persisted** shift of the expanded footprint
  at deposit time.

Net effect: both keep the deposit fully in-die (no lost mass), but for any cell within
`(cw-w)/2` of the edge XPlace moves the *cell*, while sw_only leaves the cell and displaces only
the phantom footprint — so the deposited mass sits off-centre from the cell it belongs to. The force
gather is self-consistent (it reads the same shifted `BinOverlaps`), so this isn't a bug, but it is
not XPlace's projected-gradient formulation and it biases edge cells differently.

- [ ] Decide: adopt XPlace's form (clamp position with the expanded size in `enforceDieBoundaries`,
      drop the shift from `computeNodeFootprint`), or keep ours and document why. Adopting it makes
      `computeNodeFootprint`'s movable branch disappear entirely, which is a simplification.
      **Not behavior-preserving — needs a suite re-baseline either way.**

### (b) Movable macros: XPlace overrides expand_ratio with target_density
`database.py:921-923`, applied *after* the area-conserving ratio, so it **overwrites** it:
```python
if args.target_density < 1.0:
    expand_ratio[mov_lhs:mov_rhs].masked_fill_(self.is_mov_macro[mov_lhs:mov_rhs], args.target_density)
```
A movable macro's deposited density is forced to `target_density` rather than `real/clamped` area.
sw_only has **no equivalent** — movable macros always get `weight = 1` (they exceed the √2 clamp).

Only fires when `target_density < 1.0`, so ISPD2005 (all target_density 1.0) is unaffected — but it
fires on exactly the ISPD2015/MMS designs with movable macros, target_density 0.42–0.9. That is the
same population as the known MMS under-spreading problem (#4, and auto-memories
`mms-hard-spreading-three-diseases` / `overflow-metric-grid-faithfulness`).

- [ ] Test whether adding this closes any of the residual MMS under-spread. Cheap to try: one
      `masked_fill`-equivalent in `computeNodeFootprint` gated on `target_density < 1.0` and a
      movable-macro flag (`num_movable_macros` detection already exists in `analyzeDesignArea`).

---

## #12 — Multithread sw_only (opened 2026-07-30) — **DONE 2026-07-31**

sw_only was single-threaded: one placement run used one core of the 8-core box, so interactive
turnaround on a big MMS design stayed at tens of minutes no matter what. Now threaded with OpenMP.
Commits: `e2f039c` (profile + DCT tables), `372861d` (threading). Report:
`1_REVIEW/NEW_multithread_swonly_20260731.md`.

### Step 0 — profile: the premise of this task was WRONG, and that changed the plan

The original table here (`computeElectricFields` 74%, DCT 87% of that, HPWL partials only 8%) came
from `mgc_fft_1` **forced** to a 512 grid — a 32k-cell design on 262k bins, 0.12 cells/bin. Real
designs auto-size the grid so cells/bin stays near 1, and then the picture inverts. Share of
`performIteration`, 20 iterations (`tools/profile_swonly.sh`):

| | adaptec1@512 | superblue11@1024 | newblue3@2048 | mgc_fft_1@512 |
|---|---|---|---|---|
| iteration time | 7.0 s | 36.9 s | 78.8 s | 2.8 s |
| **density DCT** | **20%** | **18%** | **43%** | **65%** |
| computeOverlaps | 22% | 25% | 24% | 10% |
| combineGradients | 12% | 15% | 16% | 5% |
| computeHpwlPartials | 15% | 14% | 4% | 8% |
| recordIterationResults | 12% | 10% | 3% | 5% |
| step/reset/BB/precond | 16% | 15% | 10% | 7% |

**No single function exceeds 25% on a real design**, so "parallelize the transforms" would have
capped at ~1.25x. The whole iteration had to be threaded, which is what was done.

### Step 1 — data layout: the targeted half was worth it, the full refactor is NOT
- [x] `DCT_fft`/`IDCT_fft` evaluated one `std::polar` (a libm sin+cos) **per output element** —
      ~6N² transcendental calls per iteration, 12.6M at a 1024 grid, the dominant term inside
      `dct_rowpass`. Now cached per N with the FFT stage roots and the bit-reversal permutation.
      Bit-exact (same argument, same `std::polar` call, just computed once). ~1.8x on that block
      alone, single-threaded.
- [x] The FFT forms take a caller-owned output buffer instead of returning a vector (two heap
      blocks per call, ~25k allocations/iteration at 2048 — the allocator traffic that would have
      stopped the row loops from scaling).
- [~] **Flat row-major matrices: NOT DONE, and measured as not justified.** After the above,
      `dct_transpose` is 1–4% of the iteration. Revisit only if a bigger grid changes that.

### Step 2/3 — threading (OpenMP; TBB stayed closed)
- [x] Transform row passes, transposes, spectral scaling, grid I/O, all per-node loops, per-net
      loops, `computeOverlaps`, `computeHpwlPartials`, `combineGradients`, `computeOverflow`,
      `computeTotalWirelength`.
- [x] `DataBase::buildNodeIndex()` — flat index-addressable views of the component/net maps
      (a `std::map` cannot drive an `omp parallel for`, and walking its tree ~10x per iteration
      pointer-chased 200k–1M entries for nothing).

### Reproducibility — solved, not traded away
`params.deterministic` (default **true**). Only the two *scatter* reductions (cell area into
shared bins, net gradients onto shared nodes) are switched by it; disjoint-write loops and scalar
reductions are threaded unconditionally and bit-exact by construction (`OrderedReduce` in
`Common.h` sums in index order). Under `true` the per-item work still runs threaded and only the
shared add is replayed in the original order.

**Verified bit-identical to the pre-threading serial binary** — `iterations.dat` and all 211447
adaptec1 cell positions — at `OMP_NUM_THREADS` = 1, 3, 8. So sw_only is still a valid golden.

### Thread count — one CPU is deliberately left free
No config key (Mark's call: "always all cores"). OpenMP default **minus one CPU**;
`OMP_NUM_THREADS` overrides.
- [x] **Re-measured on an idle box 2026-07-31 — my first explanation was WRONG and is corrected
      in the code.** There is no intrinsic cliff at the CPU count. Loop of small parallel regions
      separated by serial work, threads 1/2/4/6/7/8: idle = 0.68/0.68/0.69/0.71/0.72/**0.73** s;
      with one other job running = -/-/0.70/-/0.73/**5.89** s. The 8x collapse I first measured was
      contention with the footprint sweep, not "team size == CPU count". Reserving one CPU is
      still right — it costs nothing idle and avoids the collapse when the box is shared, which
      here is the normal case — but for that reason, not the one first written down.
      `OMP_WAIT_POLICY=passive` also avoids the collapse but is ~20% *slower* idle (0.88 vs
      0.72 s) and cannot be set from inside the program anyway (libgomp reads its environment in
      a library constructor — a `setenv()` in `main()` is verifiably too late).

### Measured speedup (idle box, `performIteration` over 40 iterations, `tools/bench_swonly.sh`)

| design | pre-#12 | deterministic | atomics | det | fast |
|---|---|---|---|---|---|
| adaptec1 @512 | 11.03 s | 5.44 s | 4.16 s | **2.03x** | **2.65x** |
| adaptec1 @1024 | 19.58 s | 8.31 s | 7.28 s | **2.36x** | **2.69x** |
| mgc_matrix_mult_1 @128 | 5.45 s | 2.67 s | 1.93 s | **2.04x** | **2.83x** |
| superblue11_a @1024 | 53.48 s | 28.31 s | 22.54 s | **1.89x** | **2.37x** |
| newblue3 @2048 | 111.44 s | 54.32 s | 46.73 s | **2.05x** | **2.38x** |

Deterministic ~2x, atomics ~2.4–2.8x, consistent across design size and grid. Roughly half the
deterministic win is single-threaded (twiddle tables + flat node index + no per-call allocation):
1 -> 7 threads alone is 1.67x on adaptec1, and 1.67 x ~1.2 ≈ 2.03.

Bit-exactness re-confirmed on the **full** set (adds superblue11_a and newblue3).

End to end, adaptec1 run to convergence (**total wall clock**, incl. the serial LEF/DEF read):
pre-#12 136.6 s / 688 iters / HPWL 7.057e+07 / ovfl 1.335e-01; deterministic **69.7 s (1.96x)**,
identical iteration count and identical result to every printed digit; atomics **50.5 s (2.71x)**,
696 iters, HPWL 7.056e+07, ovfl 1.323e-01 — the FP noise stays well inside seed-to-seed variation
and does not cost quality.

### Where the remaining headroom is (measured, not guessed)
Thread scaling on adaptec1, 1 -> 7 threads. Atomics mode: `dct_rowpass` **5.2x**,
`computeHpwlPartials` **3.9x** (both compute-bound) — but `computeOverlaps` **2.4x**,
`combineGradients` **2.0x**, `recordIterationResults` **1.3x**, all flat by 4 threads. Those are
**memory-bound over pointer-chased `Node`/`Bin` objects**, not contention-bound. Deterministic
mode on an idle box, same 1 -> 7: whole iteration 1.67x, `dct_rowpass` 4.6x, `computeHpwlPartials`
2.1x, `combineGradients` 1.9x, `computeOverlaps` 1.24x (its deposit half is serial by design),
`recordIterationResults` 1.17x.
- [ ] The next real win is an SoA layout for the hot per-node/per-bin fields, not more threads.
      `Bin` is ~64+ bytes (including a `std::vector<Node*> overlapping_nodes` that only the dead
      `Bin::computeOverlap` ever fills) but the density scatter touches only its 4-byte
      `total_overlap`. Big change, own task — do not fold into #12.

### Sweep concurrency — SETTLED 2026-07-31: sequential, one design at a time
Each run now takes ~7 threads, so the old phase-tiered concurrency (4/2/1) would have been 28
threads on 8 CPUs. Rather than cap it with `OMP_NUM_THREADS` math per phase, measured which
policy actually gives better sweep throughput (Mark's ask — direct A/B, not more modeling):
8-design mix (ISPD2005 + ISPD2015, `2_ARTIFACTS/run_thread_throughput_ab.sh`), one run each:
- **A** — sequential, one design at a time, placer default threads (all-but-one core): **873 s**
- **B** — all 8 concurrent, 1 thread each: **886 s**

**A tie (1.5% apart).** Per-run thread scaling is sublinear (~1.67x at 7 threads, measured
earlier), and running 8 single-threaded jobs at once gives some of that back to L3/memory-
bandwidth contention across the concurrent processes — the two effects roughly cancel. Every
one of the 8 designs also produced bit-identical HPWL and iteration count between A and B,
confirming `deterministic=true` holds under real concurrent scheduling, not just the isolated
thread-count microbenchmark.

Decision: **sequential everywhere, `OMP_NUM_THREADS` left unset (placer default).** Same
throughput as tiered concurrency, none of the memory-budget bookkeeping or oversubscription
risk. Applied to `tools/dse.py` (`MAX_PARALLEL = 1`) and `2_ARTIFACTS/run_footprint_ab.sh`
(phases collapsed to one sequential loop). `2_ARTIFACTS/run_mms_ab.sh` has the same
phase-tiered pattern and needs the identical simplification next time it's touched — not done
yet, just noted.

---

## #13 — Implement mixed-size PHASE 2: macro legalization + fixed-macro std-cell GP (opened 2026-07-31)

> ### ✅ IMPLEMENTED 2026-08-01 — newblue5 CONVERGES. Report:
> ### `1_REVIEW/NEW_REPORT_phase2_implemented_20260801.md`
> Built in worktree `/home/msears/phd/AIEplace_t8` on `caa8f2b` **+ the uncommitted filler work**
> (copied in; verified 632,490 fillers = XPlace exactly). **Mark's tree untouched.**
>
> | newblue5 (grid 1024, td 0.5, seed 42) | phase-1 only | **phase 2** | **+ LP legalization** | XPlace |
> |---|---|---|---|---|
> | iterations | 1070 | 1926 | 1912 | 2010 |
> | HPWL | 4.891e8 | **3.984e8** | **3.987e8** | 3.833e8 |
> | stop | `divergence_guard`, fallback | **`converged`**, primary | **`converged`**, primary | GP Stop |
> | clamp/+filler | 0.350 | **0.0844** | **0.0842** | — |
> | sharp/+filler | 0.429 | **0.229** | **0.229** | — |
> | macro-excluded (XPlace-comparable) | 0.1257 | **0.0756** | **0.0756** | **0.0452** |
>
> **The divergence TODO #8 chased is gone** — `converged` with a *primary* best. −9.4% HPWL and
> −35% exact overflow vs the original baseline; **+3.9% HPWL** from XPlace's final.
>
> **Hypothesis confirmed: freezing the macros, not legalizing them, is the mechanism.** Legalization
> moved HPWL 0.08%. newblue5's phase 1 already leaves the macros nearly legal — **1 overlapping pair
> of 91, displacement 8.85** (XPlace's own moves 2484). Stage 2 buys legality, not quality, which
> vindicates doing stage 3 first.
>
> **Built:** P3 phase-relative counter (`phaseIteration()`, XPlace `init_iter`) · P2 macro-definition
> unification · S3 `placer/Phase2.cpp` (freeze + rebuild fillers + re-seed + reset) ·
> S2 `placer/MacroLegalize.cpp` (constraint graph + LP solved by the **bundled CBC**, longest-path
> fallback). New keys `enable_phase2` / `macro_legalization` / `macro_lp_solver`;
> `enable_phase2 = false` restores the old single-phase run exactly.
> **P3 and P2 are verified NO-OPS** — adaptec1 *and* newblue5 bit-identical on `iterations.dat`
> *and* the placement DEF.
>
> **Bugs caught in review before they produced a wrong number:** (1) frozen macros kept phase 1's
> stale `probe_pos`, and `computeNodeFootprint` deposits at the PROBE position — every macro's
> density would have landed where it no longer was; (2) the best-solution restore was unguarded
> against a phase 1 that never recorded one. Plus one caught by the run: `legalizeMacros()` ran
> before the freeze and saw an empty set.
>
> ### ✅ `convergence_max_iterations` 1200 -> 10000 (DECIDED, Mark 2026-08-01)
> The first phase-2 run was **starved**: phase 1 ended at 1069, phase 2 got 131 iterations and
> stopped at `max_iterations` with overflow 0.756 (completely unspread). Now set to **10000**,
> matching XPlace's `args.inner_iter` default — which likewise spans both phases and never binds
> (its observed MMS totals are 1344-2280).
>
> This **supersedes TODO #4's "leave at 1200, do NOT raise"** for the two-phase flow. That
> decision remains correct about what it was actually reasoning over: a single-phase run that is
> *stuck* does not benefit from more iterations, and newblue4's λ-runaway trajectory proved it.
> What changed is that the cap must now cover BOTH phases to be the runaway backstop it is
> documented as, rather than a de-facto schedule. The density-weight-runaway defect #4 identified
> is unaffected and still open — a bigger cap makes bounding λ *more* important, not less.
>
> ### Not done
> - **Longest-path refinement not ported** (the TNS/WNS edge-migration loop that repairs an
>   infeasible direction assignment). We detect, warn, fall back. newblue5 never hit it.
> - `macro_legalization_xy` / `_ilp` variants and the retry driver; **site/row alignment** after
>   legalization.
> - **Only newblue5 run.** The other 7 macro-heavy MMS designs are untested.
> - Phase-1 numbers are logged (`[PHASE]` line) but not yet in run_summary / results.csv.

**sw_only implements only phase 1 of XPlace's mixed-size flow.** XPlace runs three stages
(`run_placement_nesterov.py:167-230`, log evidence in
`~/phd/Xplace/result/2026-07-17-23:03:11_adaptec5/log/test.log`):

1. **Mixed-GP** — macros movable, `include_macros = True`, `stop_overflow = args.stop_overflow*2`.
   Ends at `After Mixed-GP, best solution eval`. **This is all sw_only does.**
2. **Macro legalization** — `macro_legalization_main` (`detail_placement.py:347`): an LP solve
   (logs `Use cbc to solve LP`, variants `macro_legalization_mix` then `macro_legalization_xy`,
   picks the lower-displacement result), then site/row alignment + an overlap check via the
   compiled `gpudp.macroLegalization` op, with a retry at a longer time limit if macros still
   overlap. adaptec5: 76 macros, total displacement 10255, 0.56 s.
3. **Reset optimizer → re-run std-cell placement with macros FIXED** — `include_macros = False`
   (so `stop_overflow` returns to 0.07 and the plateau kill re-enables), density map re-built with
   the legalized macros as fixed obstacles, filler area recomputed from
   `__total_mov_area_without_filler__` (macro area excluded), std cells re-initialised
   `randn_center` while legalized macro positions are kept, macro gradients zeroed via
   `cache_node_weight[macros] = -1.0`, initial LR re-estimated, best-solution tracker reset.

### Why it matters — this is where XPlace's quality actually comes from
Phase 2 buys a **2–4× overflow improvement for ~0–3% HPWL** (from the local XPlace MMS logs;
phase-1 numbers are `tools/benchmarks.py::_XPLACE_MMS_MIXED_GP`):

| design | Mixed-GP (phase 1) | final (after phase 2) | Δ |
|---|---|---|---|
| adaptec1 | 6.238e7 @ 0.131 | 6.453e7 @ 0.042 | +3.4% HPWL, 3.1× overflow |
| adaptec5 | 3.035e8 @ 0.148 | 3.093e8 @ 0.047 | +1.9% HPWL, 3.1× overflow |
| newblue1 | 5.946e7 @ 0.136 | 5.832e7 @ 0.045 | **−1.9% HPWL**, 3.0× overflow |
| newblue4 | 2.238e8 @ 0.182 | 2.299e8 @ 0.094 | +2.7% HPWL, 1.9× overflow |
| newblue5 | 3.792e8 @ 0.170 | 3.833e8 @ 0.045 | +1.1% HPWL, 3.8× overflow |

Consequences of not having it:
- **Our GP output has illegal macro positions** (overlapping, off-site), so any downstream
  LG/DP has to absorb that. Ties directly to TODO #3's post-DP-HPWL item.
- **We can never match XPlace's headline number**, only its phase-1 endpoint. Every published
  XPlace MMS figure is post-phase-2.
- Phase 1 cannot spread past ~0.14 by design, so chasing lower overflow *in phase 1* is chasing
  the wrong target — see the newblue4 evidence in TODO #4 step 3 (1200 iterations, still 0.337).

### ⚠️ Phase 1's only durable output is MACRO POSITIONS
`get_mov_node_info(init_method="randn_center")` (`database.py:889`) **re-randomizes every movable
cell** to a Gaussian at the die centre (scale = 0.1% of die span); the legalized macro positions are
then copied back over them. The std-cell placement phase 1 worked so hard on is **thrown away**.
Log confirms it: overflow jumps 0.1485 -> 0.8604 across the restart.

This recontextualises every MMS comparison we have made:
- **Phase-1 HPWL is not the deliverable.** Our "+3.5% mean HPWL vs XPlace Mixed-GP" measures a
  placement XPlace discards. It is a sanity signal, not a quality target.
- **What phase 1 must get right is where the macros end up.** That is the input to legalization and
  the fixed obstacle set for phase 2. We currently have no metric for macro-placement quality at
  all — worth adding (macro displacement vs XPlace's legalized positions, macro overlap area).
- It also explains why chasing low phase-1 overflow is misdirected effort (TODO #4 step 3).

### Convergence metric — `convergence_include_fillers` (absorbed from TODO #4 "step 4", 2026-07-31)
Adopting `convergence_include_fillers = true` is XPlace-faithful (`overflow_fn`, its GP-stop signal,
counts filler density) and is still the right change — but it belongs HERE, not as a standalone
phase-1 fix. Three reasons, all measured today:

1. **Phase 1 is not where fillers matter.** Phase 2 re-randomizes std cells and rebuilds the filler
   set from `__total_mov_area_without_filler__` (macro area excluded) — so the filler population
   the convergence signal should count is the *phase-2* one, and it is a different population.
2. ~~**It is a NO-OP on exactly the designs we care about.**~~ **INVALIDATED 2026-07-31 — this
   premise was a BUG, now fixed.** newblue4 read `clamp/no-filler = clamp/+filler = 0.289` because
   at td=0.5 it had **literally zero fillers** — not a property of the design, but the filler-area
   gap in `addFillers` (see #4, now fixed). newblue4 now has 205,682 fillers and adaptec5 310,073,
   so the filler-inclusive convergence signal is no longer degenerate on them. **Re-evaluate
   whether `convergence_include_fillers` still belongs in phase 2 rather than phase 1** once the
   MMS re-baseline lands — this reason for deferring it is gone, and reasons 1 and 3 below should
   be re-read in that light.
3. **Where it DOES bite, the stop target also changes.** adaptec4 stopped on
   `clamp/no-filler = 0.046` while `clamp/+filler = 0.244` — a signal reading 5x low. But in phase 2
   `include_macros = False`, so `stop_overflow` returns to 0.07 (no doubling) and the plateau kill
   re-enables. Flipping fillers on under phase-1 rules changes both the signal AND which rules apply.
   Evaluating it before phase 2 exists would measure a configuration we will never ship.

- [ ] Set `convergence_include_fillers = true` as part of the phase-2 landing, and re-verify the
      filler count per design first (`clamp/no-filler` vs `clamp/+filler` in `[OVFW-DIAG]`) before
      attributing any result to it.

### Prerequisites (agreed 2026-07-31: land these BEFORE stage 3)
Three faithfulness/structure gaps that phase 2 would otherwise inherit. Ordered by blast radius.

- [x] **P1 — filler sizing in the standard-cell frame. DONE 2026-07-31 (code), sweep pending.**
      Six divergences from `compute_filler_without_fence` fixed in one landing; adaptec5 went from
      0 to 310,073 fillers, matching XPlace exactly. Details in #4 above and in
      `1_REVIEW/NEW_HANDOFF_filler_faithfulness_20260731.md`. **MMS re-baseline still owed
      — the quality effect is unmeasured.** Divergence D (overlapping fixed macros double-counted
      in placeable area) deliberately NOT implemented: no test design exercises it.
- [ ] **P2 — unify the two macro definitions.** `num_movable_macros` (die-area 0.02% heuristic,
      `Setup.cpp::analyzeDesignArea`, drives the auto-preconditioner + `mixed_size_mode` + the grid
      divisor) vs `Node::m_is_movable_macro` (XPlace `is_mov_macro` rule,
      `Setup.cpp::tagMovableMacros`). Phase 2 forces the choice: the set that gets legalized and
      fixed must be XPlace's. P1 already moved `tagMovableMacros()` ahead of the filler math, so
      the tag exists early; what remains is pointing `analyzeDesignArea` at it, which **changes
      grid sizing** and needs its own re-baseline. Measure where the two rules disagree first —
      if the answer is "nowhere", this is free.
- [ ] **P3 — phase-relative iteration counter.** XPlace's `set_init_param` resets
      `init_iter = iter`, and every schedule term is `iter - init_iter`: skip_update (`%3`, `<50`),
      the μ decay `0.9999^(iter-init_iter)`, precond escalation (`%20`), `need_to_early_stop`'s
      `<100` arming, `check_plateau`. sw_only uses raw `iteration` in the six equivalents
      (`Schedule.cpp` warmup/`%3`, μ decay, jolt warmup, precond `%20`, guard arming;
      `Output.cpp` `BEST_SOL_MIN_ITER`). Add a `phaseIteration()` offset. `reachedMaxIterations`
      stays ABSOLUTE — XPlace's `args.inner_iter` spans both phases. **Provably a no-op while
      there is one phase**, so it is verifiable with `tools/verify_swonly.sh` before phase 2
      exists — do it first.

### Scope / how hard
- **Stage 3 is nearly free**: it is the GP loop we already have, with macros treated as fixed and
  a re-initialised solver. The pieces (fixed-node density deposit, filler sizing, LR estimate,
  best-solution tracking) all exist; this is control flow + a re-init path.
- **Stage 2 is the real work**: an LP-based macro legalizer. No LP solver is currently linked into
  sw_only. Options: (a) port XPlace's formulation and link an LP solver, (b) a simpler
  displacement-minimising legalizer (macros are few — 25–959 per MMS design), (c) short term,
  call XPlace's legalizer externally to unblock measurement, reusing the 07-17 flow
  (`tools/def_to_bookshelf_pl.py` + `--global_placement False --given_solution`).
- **Hardware relevance (pl_algo):** stage 3 needs NO new PL/AIE blocks — it is the same iteration
  with a different fixed/movable partition. Stage 2 is an LP solve, i.e. host/CPU work, not PL.
  So this grows the host control flow, not the accelerator scope.

- [x] **Stage-2 approach DECIDED 2026-07-31 (Mark): port XPlace's LP formulation and link an LP
      solver.** i.e. option (a) — constraint-graph construction + longest-path refinement + the
      min-total-displacement LP, per `Xplace/src/core/macro_legalization.py`. Rejected: the
      simpler bespoke legalizer (b) and the permanent external XPlace call (c). Note XPlace's own
      file warns the LP gets slow above ~500 macros (newblue7 has 959) and that pulp/CBC is the
      bottleneck, so a C++ port is expected to help rather than merely match.
- [ ] Implement stage 3 (the fixed-macro restart) — likely worth doing FIRST behind an external
      or stub legalizer, since it is the cheap half and unblocks an end-to-end phase-1+2 number.
- [ ] Report post-phase-2 HPWL/overflow as the headline MMS result once both land.

---

## #14 — Zoomable Visualizer window (opened 2026-07-31)

The Visualizer only ever renders the whole die. At MMS scale that is 200k–1M cells in a few
thousand pixels, so everything below the macro scale is a grey wash — we have never actually
*looked* at the placement at the resolution the algorithm operates on.

Motivating observation (from the TODO #11/#13 filler work): global placement is easy to reason
about as a continuous density field and forget that the target is a row-based standard-cell
layout. sw_only sized fillers from a trimmed mean of cell heights rather than the row height for
exactly that reason. A zoomed view would make the row structure, the filler distribution, and the
macro edges visible, which is the kind of thing that produces insight rather than another metric.

- [ ] Add a configurable zoom window to `host/src/sw_only/include/Visualizer.h` — a
      centre + span (or a bounding box) in die coordinates, rendered at full canvas resolution.
      Config keys alongside the existing `output.visualize` / `iterations_per_export`.
- [ ] Decide what to draw at zoom that the full view can't afford: per-cell outlines, fillers
      vs. real cells in distinct colours, bin boundaries, row lines.
- [ ] Works with the existing GIF export path (`tools/make_viz_gifs.py`, `gif_builder.py`) so a
      zoomed region can be animated over the run the way the full-die GIFs already are.

Not urgent, not on the #13 critical path. Cheap to try and the payoff is diagnostic.

---

## Parked (not cleanup) — open technical follow-ups

- [x] **MMS faithful-field re-baseline** — DONE 2026-07-26 (full 16-design `false`-vs-`true` A/B). See
      report + `2_ARTIFACTS/mms_dct_ab_*`.
- [x] **XPlace-metric overflow re-measurement** — DONE 2026-07-26 (all 16, GPU). Confirmed the under-read
      → folded into **#4** above. The vs-XPlace "wins" on the 9 macro-heavy designs are under-spread
      artifacts; the 7 faithful designs' A/B numbers stand.
- [ ] **pl_algo initial-step mirror** — implemented in `host/src/pl_algo/src/Driver.cpp`
      (`estimate_initial_step`), compiles, but UNVERIFIED — needs Geert's card or sw_emu to check
      against the sw_only golden.
- [ ] **(optional) init_step_seed narrow-range Morris** — [0.005, 0.05] screen to positively
      confirm μ* collapses; mechanism already understood, so low priority.

---

# Improvements

Algorithmic ideas (beyond faithfulness cleanup) — hypotheses to try, not yet scoped.

- [ ] **Smoothing schedule (density footprint √2 inflation ramped down over the run).** The convergence
      metric is smoothed (each cell's footprint inflated to ≥√2·bin), which lets GP stop at smoothed
      overflow ~0.07 while the *exact* physical overflow is still 0.12–0.28 (hotspots) on the hard
      macro-heavy designs (adaptec5, newblue4, newblue5). Idea: start with heavy smoothing (helps early
      spreading / smooth gradients) and gradually reduce the inflation toward 1·bin (sharp) as GP
      progresses, so late convergence tracks the true physical density and the placer keeps spreading
      out the sub-bin hotspots instead of declaring victory early. Candidate remedy for the residual
      under-spread the `convergence_include_fillers` fix did NOT close (see #4). Don't implement yet —
      first diagnose *why* those designs won't spread (see the handoff / GIFs).
