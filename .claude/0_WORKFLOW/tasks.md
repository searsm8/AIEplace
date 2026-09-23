# Tasks

Open work, one section per task. **Status lives here; evidence lives in a
report.** Don't reuse task numbers, find the highest number and add one.

## ⚠️ sw_only functionality is FROZEN — called 2026-08-17, EFFECTIVE 2026-08-18 (Mark)

sw_only is at parity with XPlace on **both** tiers, at commit `271d024`:
- **ISPD — median 1.0096 / mean 1.0115** legal-vs-legal over all 28 designs, 22/28 within ±2%,
  better on 6. Golden: `.claude/2_ARTIFACTS/results/GOLDEN_sw_only_frozen_20260825/`.
- **MMS — median 1.0139 / mean 1.0110** over 16 designs. Golden:
  `.claude/2_ARTIFACTS/results/GOLDEN_mms_sw_only_frozen_20260825/`. **Now covered** — the #35
  regression is fixed (below).

The active thread is now **pl_algo** (#20). Freezing is not a pause: pl_algo's algorithm is pinned
to the 2026-07-14 sw_only, so **every further sw_only change is another port**, and a stable
sw_only is what makes #20 a bounded job.

**Changes admitted under the freeze, in order.** It was called 2026-08-17 against the 08-15 ISPD
numbers (1.0096 / 1.0113). Three faithfulness fixes already in flight were then allowed to land —
**#32's 7a+7b** and **#3's cap→scale** — kept per `CLAUDE.md`'s prefer-XPlace rule. **#34**
(2026-08-21) found `#3` applied to only one of **four** places computing the same quantity and made
them agree. **#35** (2026-08-25, Mark-authorized) then reverted `#3` outright — landing experiment
"D", the fixed-density **cap** `min(ρ,td)` — a **deliberate divergence from XPlace** (registered in
`CLAUDE.md`), worth **−2.38 pp of MMS mean** at a cost of only **+0.03 pp on ISPD** (net-neutral:
the formula is a no-op except on macro-bearing td<1 designs, where ISPD gains and losses cancel).
That is the current frozen state above. **#36** (2026-08-26) then collapsed the cap's two **host**
copies into a single `capFixedDensity` (`common/include/Grid.h`), so the #34 drift can no longer
recur on the host by hand — bit-identical, zero baseline changes; the two pl_algo HLS copies stay
hand-mirrored until **#20 step 3**.
⚠️ **The cap is a knowing divergence, not a bug.** Anyone tempted to "restore XPlace faithfulness"
by reverting it is re-opening a closed, measured decision — see #35 in history.md and the
divergence registry in `CLAUDE.md` first.

What the freeze means:
- **No further algorithm or behaviour changes to `host/src/sw_only/`** without an explicit
  decision from Mark. Bit-identical `make test-regress` is now the contract, not a convenience.
- **Cleanup, tooling, docs and tests are NOT frozen** — #1 covers most of that.
- Items that only ever mattered *because* sw_only was moving have been closed or demoted; items
  that were filed under sw_only but are really pl_algo work have been re-filed (marked
  **↪ pl_algo** below).

---

## #1 — Clean house: repo / notes / code cleanup (opened 2026-07-27)

Fast iteration left breadcrumbs and we started tripping over them. Workflow dirs established, git
tree committed, **67 GB freed** from `results/` (helper: `tools/prune_run_artifacts.sh`),
`vck5000/` top level tidied, harnesses moved to `vck5000/test/`. What is left is notes hygiene.

- [x] **Handoffs are now reports-in-progress (2026-08-31, Mark).** New policy in `CLAUDE.md`
      ("A handoff IS a report-in-progress"): a handoff exists only *between* sessions and, when the
      task closes, is `git mv`'d in place to `REPORT_...`. No standing `handoffs/` class anymore —
      all 16 accumulated handoffs deleted and the `handoffs/` dir removed (git history holds them).
      `_NEW_` un-prefixing stays Mark's alone; not part of this item.
- [ ] Fold the still-relevant findings out of old reports into tasks.md / memory so those reports
      can be archived.
- [x] **Placer class map + three structural refactors (2026-08-31, bit-identical).** Built a
      hierarchical map of the `Placer` class (all ~85 methods by responsibility along the dataflow,
      each tagged with its file, misplacements flagged) — artifact "Placer Class Map". Acting on it,
      with Mark, three readability moves (freeze is functionality-only), each verified
      `make test-regress` + `test-regress-slow` bit-identical (mms_adaptec1 exercises phase 2):
      (1) **new `placer/BestSolution.cpp`** gathers the 6 best-solution methods that had been split
      across AIEplace/Schedule/Output — this is why `BestPlacement` felt wrong at the top level;
      (2) **all γ code into `Schedule.cpp`** — `updateGamma` moved out of `Partials.cpp` beside
      `configureGammaSchedule`; (3) **`run()` reads plainly** — `beginFixedMacroPhase()` split into
      `readyForFixedMacroPhase()` (eligibility) + `void beginFixedMacroPhase()` (transition), gated
      in `run()` by `m_phase == Phase::MIXED_SIZE`, retiring the long explanatory comment. New file
      registered in `makeflags.mk`. Remaining flagged-but-open: `recordIterationResults` placement.
- [x] **sw_only readability refactor is real and ongoing — tracked in #39, not here.** The comment
      sweep below found the *comments* clean; it did not anticipate the *structural* readability
      pass (function order, naming, file placement) Mark opened right after, driven by thesis
      presentation needs. Do not read "found clean, no refactor taken" below as the current state.
      <details><summary>Original 2026-08-31 entry: "sw_only comment + readability audit — found
      clean, no refactor taken"</summary>

      > Freeze is functionality-only, so readability refactoring is admitted; but the audit found
      > little to pay. Comments: the masked→smoothed rename has zero stale survivors, the removed BB
      > step-clamp is correctly annotated as gone, deleted toggles are documented *as* deleted with
      > dates/TODOs. Structure: Output/Schedule/Setup are already 13–20 small single-purpose
      > functions; the one long function (`updateDensityWeight`, 107 lines) is ~80% load-bearing
      > history comments over ~30 lines of code. Refactoring clean code would only risk the
      > bit-identical contract — not done, per surgical-changes. Real refactor debt lives in
      > **pl_algo** (`Driver.cpp`, #10), which is device-gated. `make test-regress` baseline
      > confirmed green this session.
      </details>
- [ ] **Per-run `viz/` dumps are reproducible output** (~96 MB per adaptec1 run, ~480 MB per
      bigblue4). Already swept by the default slim. Given the size, consider making viz output
      **opt-in for sweeps** rather than default.
- [x] **`tools/` triaged and every survivor given a status (2026-08-12, `331f1df`).** 5 stale tools
      deleted (`xplace_gp_ref.py`, `collate_mms.py`, `make_scorecard.py`, `legalize_swonly_mms.sh`,
      `bench_swonly.sh`) — each verified to have zero references from code, Makefile or skill, and
      each superseded by a named replacement. 2.1 MB of `adaptec1_*.png` run output was **moved**
      (not destroyed) out of the code dir to `.claude/2_ARTIFACTS/legacy_density_heatmaps/`.
      `tools/README.md` now carries a **live / dormant** status row for all 38 survivors, so an
      unlisted tool is a visible defect rather than an unknown. The OpenROAD opendp island,
      `eval_overflow_xplace.sh` and `vcd_to_svg.py` were kept and marked **dormant** — they work
      and are independent of the XPlace path, they are just off it.
      <details><summary>Superseded: "`tools/eval_overflow_xplace.sh` left untracked per Mark (2026-07-27)"</summary>

      > - [~] `tools/eval_overflow_xplace.sh` left untracked per Mark (2026-07-27). Revisit whether it
      >       belongs in `tools/`.

      Stale on two counts as of 2026-08-12: the file **is** tracked (and has been since the
      2026-08-12 `5b52f50` scoring-pipeline move), and the question is now answered — it stays,
      marked dormant, because it evaluates overflow *without* invoking the fragile legalizer.
      </details>
- [x] **Moot 2026-08-31.** The "stale `run_config.json` re-baseline comment" item is dead: there is
      no `run_config.json` (the sw_only config is `default_config.toml`), the working tree has no
      uncommitted config change, and a 2026-08-31 sweep found the toml's comments already clean. #2
      (which it was "bundled with") is closed and in history.md.
- [x] **DONE 2026-08-17 — `~/phd/Xplace`'s 3 local edits are committed**, on a new branch
      `local-fixes` (the clone was sitting on `main`). Split into three commits so the two genuine
      fixes are cherry-pickable and the instrumentation is not: `5ecf97e` apply_precond returns
      None, `9b0851d` weighted_weight never assigned, `88ae004` the PRECOND_TRACE dump (marked
      LOCAL ONLY in both its comment and its commit message). Working tree clean.
      → whether to send the two fixes upstream is now an **Improvements** item, see below.

---

## #9 — User friendliness (opened 2026-07-30)

**Step 1 landed 2026-08-04: the silent fork is gone.** All 15 files that existed in both hosts now
exist once, in `host/src/common/`. Limbo and tabulate are real submodules, zero `.a` tracked,
`tools/bootstrap_third_party.sh` added; Boost reconciled (two Boosts on this box — everything
actually compiles against 1.80; `-DBoost_NO_BOOST_CMAKE=ON` is required, not cosmetic).

- [ ] **STEP 1b — re-run `make run-density` and record the number.** ⚠️ **Any PASS recorded between
      2026-07-05 and 2026-08-04 is void**: pl_algo's frozen `Grid.cpp` had no √2 density clamp while
      the PL gained one, so every `--density` sw_emu run compared a *clamped* device rho against an
      *unclamped* software rho. While there: the software `computeNodeFootprint` and the PL
      `node_footprint` still differ on the overhang shift (PL shifts back on-grid, the golden does
      not). Decide whether the golden should shift or the harness should project —
      **do not change synthesizable HLS to chase it blind.**
- [ ] **STEP 2 — collapse the two hosts into ONE binary.** An 18-site refactor of hardware-driving
      code; cannot be signed off without a real build + sw_emu re-verify. **This is #21's change B**
      and inherits all of #20's preconditions.
- [ ] **Dependencies** — verify `pip install -r vck5000/requirements.txt` succeeds clean and a
      benchmark download still works. `pyunpack`/`patool` were never actually exercised against the
      new file and may need a system `unrar`/`7z` on PATH.

---

## #10 — pl_algo cleanup & clarity (opened 2026-07-30, mostly done 2026-08-02)

Fresh-eyes cleanup: stale docs rewritten, dead code deleted, 16 `run-*` targets folded through one
define, build artifacts untracked. Done under a no-CPU constraint — inspection, `g++ -fsyntax-only`
and `make -n` only, **no build, no synthesis, no emulation**.

- [ ] **`Driver.cpp` is 18× the same XRT boilerplate** (~1188 lines): open device → load xclbin →
      alloc bo per arg → memcpy → sync → run → sync back, once per `run*()`. A small `KernelSession`
      helper (device/uuid/kernel + `bind(idx, ptr, bytes)`) would cut it hard. Needs a real build and
      an sw_emu re-verify, not a syntax check.
- [ ] **`common.mk` defaults point at the dead variant** — `AIE ?= markv1`, `PL ?= markv1`, so a bare
      `make` builds the legacy partial-offload design. Flipping it is Mark's call, and also means
      updating the defaults documented in `CLAUDE.md`.
- [~] **Port aliasing in `top.cpp`** — the scannable PORT-ALIAS TABLE landed in
      `host_interface.hpp`. The code-level fix (per-mode struct of named references) was not
      attempted: it changes a synthesizable kernel and needs HLS C-synthesis. Stage 5 supersedes it.

---

## #11 — XPlace density-footprint faithfulness gaps (opened 2026-07-30)

Found by verifying `computeNodeFootprint()` line-by-line against XPlace **source**, not comments.
Most of the footprint is faithful. #11a (in-die projection) was adopted and its toggle deleted;
#11b (movable macros deposit at `target_density`) landed as `macro_deposits_target_density` and is
a large part of the adaptec5 fix.

⚠️ **`run_config.toml` does not set `random_seed`** — it defaults to a time-based seed, so a bare
template run looks nondeterministic. **Pin it in any manual A/B.**

- [ ] **Decide the in-die shift form.** XPlace clamps the *position* using the **expanded** size on
      every gradient evaluation; sw_only clamps position with the **raw** size and then applies a
      second, non-persisted shift to the footprint at deposit time. Both keep the deposit in-die, but
      ours leaves the cell and displaces only the phantom footprint. Adopting XPlace's form makes
      `computeNodeFootprint`'s movable branch disappear entirely. **Not behavior-preserving — needs a
      suite re-baseline either way.**
- [x] **RESOLVED 2026-08-09 — the toggle is GONE; the faithful branch is unconditional.** The
      2026-08-02 line was the true one. `git grep macro_td_expand_ratio -- '*.cpp' '*.hpp'` returns
      **zero** hits. The name survives only as a rename note in
      `host/src/sw_only/default_config.toml:37`, which says it outright — *"locked unconditional here
      too as of 2026-08-02 … No config toggle any more"* — plus the three frozen regress configs that
      copy that comment. It was **renamed 2026-08-01 to `macro_deposits_target_density`**, which is
      part of why the old name reads as "deleted".
      So: the 2026-07-31 "KEPT, default false" header is **stale**, and the 2026-08-07 "re-test now
      unblocked by #19" is **moot as written** — there is no toggle left to A/B; a re-test means
      re-adding the branch, a code change rather than a run. `tagMovableMacros()` stays for an
      unrelated reason — `Setup.cpp:74` needs it to precede `createFillers` (the filler math is
      std-cell-only) — so its presence is **not** evidence of a pending re-test.
      **Still Mark's call:** is the post-#19 re-test worth re-adding the branch for? The A/B that
      rejected it (mean +0.6% HPWL over 8 macro-heavy designs, −0.4% excluding adaptec5) predates the
      stop-criterion fix, so its verdict is measured on the old criterion.
      <details><summary>superseded — the self-contradicting entry, verbatim</summary>

      - [ ] **❓FOR MARK — this entry contradicts itself and I did not resolve it.** The 2026-07-31 header
            says #11b's `macro_td_expand_ratio` toggle was **KEPT** (default false, rejected on results but
            faithful, re-test once the stop criterion is fixed). A later 2026-08-02 line says the toggle was
            **DELETED** the same day it landed and the faithful branch is unconditional. A third note
            (2026-08-07) says the re-test is now **unblocked by #19** and that `tagMovableMacros()` stays to
            serve it. Those cannot all be true. Check the code, then fix the entry — and if the re-test is
            real, run it now that #19 has fixed the stop criterion.
      </details>

---

## #15 — ↪ pl_algo — Net-local coordinate frames for the wirelength gradient (opened 2026-08-03)

**Parked at Mark's request 2026-08-03 — analysis done, no implementation.**

**↪ RE-FILED to pl_algo 2026-08-17** (sw_only freeze). This was never sw_only work and the entry
says so itself: the motivation is **PL precision** — it is what makes a narrow `ap_fixed` feasible —
and the third bullet expects **no HPWL movement** on sw_only. Nothing here changes the CPU golden.
Sequence it against #20; #23's close notes the specific trigger (*"if pl_algo ever narrows to fixed
point this stops being cosmetic"*).

Store each net's pin coordinates relative to that net's own min, so the absolute die offset never
enters the gradient arithmetic. The WA gradient is translation-invariant, so this is a **reframing,
not an approximation**. Motivation is PL precision (it is what makes a narrow `ap_fixed` feasible),
not sw_only quality. Measured: today's error tracks `x_max/γ` linearly, the net-local form sits at
machine epsilon regardless of coordinate magnitude — **301×** better on adaptec1 late.

⚠️ **The trap: `C` and the `(1 ± x_i/γ)` factor must move TOGETHER.** Shifting `C` alone leaves a
residue measured at **62× the signal**. It does not NaN — it silently points the gradient the wrong
way. Any implementation needs a test that catches a half-applied shift.

Related: **#23 is this same precision problem actually killing runs on the CPU golden.**

- [ ] **Layout cost** — a node sits on many nets, so a per-net frame means per-net-pin duplication
      of coordinates. Check what pl_algo's pin streaming already materializes; this may cost nothing
      or it may be the whole expense.
- [ ] **Scope boundary** — only the *wirelength* gradient is translation-invariant; the density/field
      path needs absolute die coordinates for bin indexing. Define exactly where the frame converts
      back, and confirm nothing downstream of `probe_grad` assumes a shared frame.
- [ ] **Does sw_only change too?** Expect **no HPWL movement** (error is ~1e-6 there, below what BB
      reacts to) — do not sell this as a quality fix. If PL shifts and sw_only does not, the sw_emu
      partials tolerance must not be set tighter than ~1e-6 or it will chase a phantom.

---

## #19 — Two XPlace faithfulness gaps: overflow metric and schedule gate (opened 2026-08-06)

**Landed, measured, and both toggles retired 2026-08-07 — faithful behaviour is unconditional.**

**(a)** Every XPlace overflow metric **excludes fillers** on all three code paths; we included them.
**(b)** The γ/λ throttle gated on `density_force_fraction`, a gradient-norm ratio, while its own
doc-comment claimed to compute XPlace's `weighted_weight`. κ carries λ linearly so it is monotone and
crosses the (0.5, 0.95) window once; the gradient ratio is not monotone, so it drifted *into* the
window late and held the 3× throttle on exactly when λ needed to ramp.

| | before | after |
|---|---|---|
| post-DP HPWL vs XPlace, all 16 MMS | +1.15% | **+0.74%** |
| runs that `converged` | 6/16 | **15/16** |
| post-DP density vs XPlace (8 measurable) | parity | parity, unchanged |

**pl_algo was right all along**: its `sched_dff` closed form `c·λ/(1+c·λ)` *is* κ algebraically. It
had the right function under the wrong name while sw_only had the wrong function under the right
name. newblue4 closed as good enough (Mark, 2026-08-07).

⚠️ **Any overflow number recorded between 2026-07-31 and 2026-08-06 is filler-INCLUDED** and reads
roughly 2× high against anything XPlace prints.

- [x] **DONE 2026-08-08 — `sched_verify` now asserts the coefficient's constancy.** It derives the
      constant from **κ** (`precond_a2_norm/(a1+a2)`, cols 14-16 of the trace, which the harness
      never parsed) instead of from the `density_force_fraction` column, grouped per `precond_coef`
      plateau. On the fixture: **κ gives 1.12% spread, dff gives 2136%** — the closed form was right
      and the quantity it was fitted against was wrong, exactly as #19b predicted. Two asserts added
      (plateau spread < 5%, closed form vs κ < 2e-2), both bounds taken from the observed values.
      The dff fit is still printed, tagged `[info]`, so the divergence stays visible.
      **Negative control run:** scaling `a2` by 1.5× over half the trace → 51.1% spread, both checks
      FAIL, exit 1 — while schedule and convergence still pass, which is the blind spot they had.
      Also corrected `test/fixtures/README.md`, which had explained the 1.608 closed-form error away
      as a preconditioning artifact. It was not an artifact.
- [ ] **↪ pl_algo — Regenerate the fixture trace from the post-#19 sw_only** — blocked on #20 step 1
      (`dumpScheduleTrace()` must be restored first). Until then the fixture is a 2026-08-05 capture
      of the OLD gate quantity.
- [x] **DONE 2026-08-09 — renamed pl_algo's `dff`/`dff_coef` to `kappa`/`kappa_coef`** (matching
      sw_only's `precond_kappa` and XPlace's `weighted_weight`). `sched_dff`→`sched_kappa`,
      `SchedParams::dff_coef`→`kappa_coef`, `param_scheduler`'s `dff` arg→`kappa`, plus the comments
      that made the false claim. Four files: `param_scheduler.hpp`, `test/sched_verify.cpp`,
      `test/synth_check.cpp` (its s_axilite port names change with it), `DATAFLOW.md`.
      `make test` prints **byte-identical numbers** before and after (kappa_coef median 70.4116,
      spread 1.12%; closed form 5.327e-03) — a pure rename, as intended. The module is not yet
      instantiated in `top.cpp`, so those two harnesses were the only callers.
      Remaining `dff` mentions are deliberate: the trace **column** is genuinely
      density_force_fraction, and the `[info]` line that prints its 2136% spread must keep its name.
- [ ] **↪ pl_algo — `host/src/pl_algo/` still gates on the real dff — the pre-#19 bug, live.** Found doing the
      rename above and deliberately NOT bundled with it. `Placement.hpp`'s throttle uses
      `densityForceFraction()` (gradient L1 norms, `Driver.cpp`), i.e. exactly the non-monotone
      quantity #19b replaced in sw_only. It was left alone because fixing it is a behaviour change to
      the pl_algo host schedule, not a rename. Fold into #20 step 2, or fix standalone — but it
      should not silently outlive #19.

---

## #20 — pl_algo Stage 5: wire the full device design (opened 2026-08-06)

→ [[_NEW_REPORT_pl_algo_stage5_assessment_20260806.md]] (UNREAD). `DATAFLOW.md` stays authoritative
for the dataflow itself; this item is the work plan.

**The problem is not "compose the resident loop".** pl_algo's algorithm is pinned to the
**2026-07-14** sw_only, 20 commits + #19 ago. Composing Stage 5 on top hardens a three-week-old
algorithm. `Placer::dumpScheduleTrace()` — the only producer of `sched_verify`'s golden — was deleted
from sw_only as dead code on 2026-07-28, and its consumer lives in another variant and names it by
*filename*, so **nothing in the build could see the coupling**. `make test`'s green `sched_verify`
validates the device scheduler against sw_only as of 2026-07-18 and will keep passing forever.
**Tier-1 covers 3 modules of 17.**

Steps — cheap and load-bearing first; 1–4 need no Vitis and no free CPU:

- [x] **1. Restore `dumpScheduleTrace()` in sw_only — DONE 2026-08-28.** Config-gated
      (`output.dump_schedule_trace`, default false), hooked at the end of `performIteration` after
      `updateSchedule()`; emits 21 columns (the original 16 + `precond_kappa`, `phase`,
      `phase_iteration`, `stop_reason`, `backtrack_steps`). `computeLipschitzEstimate` now records
      its two BB norms into members so the dump can emit them (verbatim capture, no behaviour change).
      `make test-regress` **bit-identical** before/after on both designs (proven no-op). Fixture
      regenerated: adaptec1 grid 512 / td 1.0 / seed 42, **converges 652 iters**, replacing the
      2026-07-18 trace; `config_used.toml` committed (config is TOML now, `.config.json` deleted).
- [~] **2. Re-verify `param_scheduler` against the new trace — CORE DONE 2026-08-28.** `sched_verify`
      now feeds **κ** (`precond_kappa` column), not the dff hack. Schedule scalars verify
      **bit-exact** (inv_gamma/alpha/coeff/lambda all 0.0), convergence fires exactly at 652.
      **Escalating `precond_coef` (the "escalating dff_coef" item) handled and now EXERCISED
      (pc 1→256):** κ[i] is produced by `precond_coef[i-1]` (at an x2 boundary the row logs the new
      pc but its κ reflects the old one), so the c-derivation groups by the *producing* pc and the
      closed-form check uses a per-regime c; every plateau holds to ~1.1% and c scales cleanly with
      pc. The old fixture pinned pc=1.0 and never tested this. **Still open (need other fixtures, not
      adaptec1):** the `overflow rising` conjunct on the coarse divergence test (needs a *diverging*
      trace — adaptec1 converges), phase-relative counters (needs a *mixed-size/phase-2* trace —
      adaptec1 is phase-1 only), and jolt params read from config vs hardcoded (a param_scheduler
      cleanup). → [[REPORT_pl_algo_stage5_assessment_20260806.md]]
- [x] **3. Tier-1 harnesses for the uncovered modules — DONE 2026-08-28.** All the step-3 modules
      are covered; `make test` runs **11 harnesses**, coverage 10 of ~14 real modules. Each is vs an
      independent double golden per the `bb_reduce` template.
      - `hpwl_gradient` (`hpwl_grad_test.cpp`, 6 assertions + mutation table), `bb_reduce`
        (`bb_reduce_test.cpp`) — done earlier this session.
      - **The `formats.hpp` wall (the "decide once" blocker) is gone (`b4130e6`).** formats.hpp
        guards its HLS transport includes + `axis_t`/`beat_t` behind `#ifndef PL_TIER1_STUB`
        (no-op for the real HLS build; byte-identical preprocessed output). `test/tier1_stub.hpp`
        sets the macro and supplies a `std::deque`-backed `hls::stream<T>` — the only HLS surface a
        module signature exposes. This unblocked all four remaining modules at once (option (a)).
      - `node_footprint` (`node_footprint_test.cpp`): independent double spec golden + area/on-grid/
        macro-passthrough invariants, all four in-die-shift edges driven.
      - `density_bin` (`density_bin_model.cpp`, upgraded): now calls the **real** `density_bin()` and
        the **real** `node_footprint` — its stale hand-copies deleted (the duplication the wall
        forced). Naive full-grid golden, bit-exact. Cap is the shared `capFixedDensity` spec
        (`min(rho,td)`, #36/#35).
      - `force_gather` (`force_gather_test.cpp`): gather vs double reference (1.7e-6) + adjoint/area
        conservation with field==1 (7.4e-5).
      - `metrics` (`metrics_test.cpp`): HPWL CSR reduce (6e-9) + overflow_sum (2e-8) + masked-net
        invariance.
      - `iteration_update` + `memory_writer` (`iteration_update_test.cpp`): full combine+precond+BB+
        momentum+clamp chain for u and streamed v (2.5e-8/4.3e-8), four-edge clamp exact, writer
        bit-exact, coeff==0 warm-up v==u.
      → [[_NEW_HANDOFF_20_pl_algo_stage3_20260828.md]] carried the plan; this closes its "NEXT
      SESSION STARTS HERE" (the wall + the four modules).
- [ ] **3b. `hpwl_gradient` optimization — proposed, not implemented.** With coverage in place,
      the module was profiled against the last real `TARGET=hw` csynth. It is **gather-bound, not
      compute-bound**, at ~1% of the VC1902: three truly-random `num_pins` `node_pos` gathers per
      iteration (one of them in `metrics::hpwl_sweep`, which recomputes a bbox `sweep_bbox`
      already has), `sweep_bbox` II=2 / `sweep_sums` II=3, and pin streams running the 512-bit
      bus at 128 bits. Seven ranked proposals with costs and per-proposal verification —
      including Mark's absolute-position `NodePin` contract (**verdict: yes, and better than the
      24-byte variant**) — in [[_NEW_REPORT_20_hpwl_gradient_opt_20260828.md]]. Suggested order
      **P5a → P1b → P1 → P2 → P3 → P4 → P5b → P6**, then spatial replication. P1b's prerequisite
      HPWL assertion **landed 2026-08-28 as `[6]`** (`aa007a7`); it reduces the `bb_DDR` boxes
      phase 1 already writes and matches the double golden exactly, and two mutations (perturbed
      `maxy`, dropped last-net flush) are caught by `[6]` alone.
      **P5a implemented 2026-08-28** (per-port `num_read_outstanding` / `max_read_burst_length` on
      the HPWL bundles in `top.cpp`, each tuned to the pattern the burst log shows). Its proposed
      second half — `sp=` bank tags — **was withdrawn: this platform has ONE memory controller**
      (`platforminfo` reports only `BRAM` and `MC_NOC0`), so there are no banks to spread across
      and the absence of `sp=` tags in the build is correct, not an oversight. ⚠️ **P5a's speedup
      is UNVERIFIED and cannot be verified here** — C-synthesis shows cost, not latency, and the
      experiment needs Geert's card. It is the cheapest test that can *falsify* the gather-bound
      diagnosis, so run it before building P1/P2 on top of that diagnosis.
      **P1b implemented 2026-08-28** (`21adad6`): `hpwl_gradient` emits HPWL from a separate
      `hpwl_reduce` over `num_nets` (II 8, 221k cycles) — **not** an accumulator inside
      `sweep_bbox`, which was built first and cost II 2→7. `metrics::hpwl_sweep` has the same
      double-accumulator pathology (II≈7 over `num_pins`), so this is ~30× cheaper than the pass
      it replaces. Cost: LUT +2.4%, BRAM 0, timing unchanged. ⚠️ **New precondition — `bb_DDR`
      must be zeroed before first use** (`host_interface.hpp` NetBBox; both `Driver.cpp` sites do
      it). Host switched off `hostHPWL` 2026-09-09 (see the P2 follow-through item below; that also
      fixed a stale-position regression). Still TODO: delete `metrics::hpwl_sweep` — left in place on
      purpose, `metrics` is the only thing `runMetrics()` covers.
      ⚠️ **`pl/Makefile` did not track header dependencies until `21adad6`** — a module-header
      edit left the stale `.xo` and `make` said "Nothing to be done". Two measurements in this
      session were silently stale. Any pre-2026-08-28 synthesis claim that followed a
      header-only edit is unverified.
      **P1 (fuse phases 1+2) INVESTIGATED AND BLOCKED 2026-08-28 — do not just retry it.** Two
      measured blockers: (a) adaptec1's unmasked nets have **median degree 2** (53.3% are
      degree-2, mean 4.28), so the no-stream version's per-net inner drain loop pays pipeline
      fill/drain on a 166-deep datapath and comes out ~4.4M cycles against phase 2's current
      2.8M — slower than what it replaces; (b) the `hls::stream` DATAFLOW version puts
      `hls_stream.h` into the module, which compiles under g++ only with `-I$XILINX_HLS/include`
      (breaks the tier-1 no-Vitis contract) **and** the csim stream is unbounded, so the harness
      could not detect the undersized-FIFO/deadlock risk that is P1's whole danger.
      **Recommendation: skip to P2** — it delivers P1's gather saving without a FIFO, without a
      new buffer, and fixes phases 1+2 *and* `metrics` at once.
      **P2 LANDED 2026-08-28 (`ed25f1a`) — the main win of this thread.** `NodePin` now carries
      the ABSOLUTE pin position (`{x,y}` replacing `{off_x,off_y}`), with the static offsets in a
      new upload-once `PinOffset[]` and a new `modules/refresh_pin_pos.hpp` folding v_k in once
      per iteration (`MODE_REFRESH_PINS`, II=1). **All three random `node_pos` gathers left
      `hpwl_gradient`, and `metrics::hpwl_sweep`'s went too.** Burst log after: `sweep_bbox`,
      `sweep_sums`, `seg_reduce`, `metrics` all burst; **gmem0 no longer appears in the HPWL
      path**. `sweep_bbox` depth **146 → 73** (DDR latency out of the pipeline); IIs unchanged;
      LUT +2.4%, BRAM/DSP/timing unchanged. Tier-1 output **bit-identical** before/after.
      Note the proposal's "NodePin stays 16 B so it is free" was half right — the offsets still
      need a device home (24 B/pin either way); the win is the hot/cold split.
- [x] **↪ pl_algo — finish P2: the host issues `MODE_REFRESH_PINS` — DONE (verified 2026-09-09).**
      `Driver.cpp` `eval_gradients` runs `MODE_REFRESH_PINS` (with `r.wait()`) immediately before
      `MODE_HPWL_GRAD` (Driver.cpp ~1048-1052), so the device pin arrays carry `v_k` before any
      sweep reads them.
- [x] **↪ pl_algo — switch the host off `hostHPWL` (P1b follow-through) + FIX a stale-HPWL
      regression — DONE 2026-09-09.** The `--place` loop now takes HPWL from `MODE_HPWL_GRAD`'s
      by-product (`dct_out[0]`, captured in `eval_gradients` before the field passes reuse `b_dout`),
      deleting a full CPU pass over all pins per iteration. **This also fixed a real bug:** `ed25f1a`
      (P2) rewrote `hostHPWL` to read `r.x` from the *host* `pins` array, which P2 refreshes only on
      the device (`MODE_REFRESH_PINS`) — so since 2026-08-28 the loop computed HPWL at the **initial**
      positions every iteration and fed that stale value into `updateDensityWeight` (Driver.cpp:1209),
      corrupting the λ trend, not just the log. `hostHPWL` deleted from `Placement.hpp` (was its only
      caller). Verified offline: pl_algo host builds with XRT; `make test` green, assertion `[6]`
      proves the by-product == double golden at rel 2.1e-08. ⚠️ **Not yet run end-to-end** — the
      before/after λ-schedule + HPWL-history change needs a `sw_emu` `run-place` A/B (built xclbin).
      Next lever after that is **P3 (II fixes)** — re-read the P1b lesson first: HLS cannot see a
      rotating array index, partial accumulators need STATIC indices via unrolling.
      → [[_NEW_REPORT_20_hpwl_gradient_opt_20260828.md]]
- [x] **4. Close the datapath divergences** under that coverage — CLOSED for v1 scope 2026-08-29
      (geometry pair done; macro-weight deferred to step 5). **Geometry pair DONE 2026-08-29
      (`d095a9f`):** `node_footprint.hpp` no longer does the in-die shift #11a deleted (centered box,
      matches `computeNodeFootprint`), and `iteration_update.hpp` now clamps to the √2-**expanded** box
      `[0.5(cw−w), die−0.5(cw+w)]` matching `enforceDieBoundaries` (Step.cpp:131) — these are one
      contract (the expanded clamp is what makes the unshifted footprint legal), so they landed
      together. `bin_w` derives from `die/GRID`, no new top() scalar. `force_gather`/`density_bin` are
      faithful as a consequence (edge cells now CLIP like `computeNodeOverlaps`, not shift). Verified:
      `node_footprint_test` [3]→CENTERING, `iteration_update_test` NEW [5] on-grid coupling (0
      off-grid), `IterVerify.cpp` host golden updated; `make test` 12 harnesses green. ⚠️ **C-synth +
      sw_emu trajectory A/B vs sw_only still needed** to confirm the end-to-end match (tier-1 only
      proves the module math is now faithful to sw_only's formulation).
      **Movable-macro weight override (#11b) — TABLED for v1 (decision 2026-08-29, Mark).** sw_only
      sets weight = target_density for a movable macro when td < 1 (Grid.cpp:31; is_mov_macro rule =
      Setup.cpp:106 tagMovableMacros). It is a **no-op on every design pl_algo runs** (std-cell,
      num_movable_macros == 0 → override never fires) and only bites MMS, which needs phase 2 +
      fillers v1 lacks. It also needs the **same host→PL per-node "kind" flag that fillers need**, so
      it is **bundled into step 5**, not built alone as dead code. `node_footprint_test` [4] is the
      tripwire (pins the current no-override contract: macro weight == 1.0) — see its header for the
      exact split when the flag lands. So **step 4 is CLOSED for v1 scope** (geometry done; macro
      weight deferred to step 5).
- [~] **5. Fillers — MODEL + PACKER DONE 2026-08-29 (tier-1 + host pack-check); device run pending.**
      **Kind flag = NESTED INDEX RANGES (Mark's call), not a per-node field.** The movable prefix is
      `std [0,first_macro) | macro [first_macro,first_filler) | filler [first_filler,M)`; a per-node
      consumer classifies by index (`host_interface.hpp` `classifyNode` → `{is_std_cell,
      is_movable_macro, is_filler, is_fixed}`, exactly one true). "Real movable" (overflow map) is the
      prefix `[0,first_filler)`; "all movable" (force map) is `[0,M)` — the ideal nesting.
      Landed: `DesignHeader` gains `first_macro`/`first_filler`; `node_footprint` takes
      `(is_movable_macro, target_density)` and applies the **#11b override** (`weight=td`, Grid.cpp:31);
      `density_bin`/`force_gather` derive `is_macro` per node and stay adjoints (shared helper);
      **`tagMovableMacros` ported** into the pl_algo host (`Packer.cpp`), `db.addFillers()` called in
      `--place` (adopts effective td), packer buckets the four ranges + seeds filler positions
      uniform-random (deterministic seed, **not** matched to sw_only's `rand()` stream — documented).
      Verified: `make test` 12/12 (node_footprint [4] override split, force_gather [3] override,
      density_bin bit-exact w/ 100 movable macros); new host `--pack-check` mode PASS on
      **adaptec1 (filler=160067 = XPlace-exact), mms/adaptec5 (76 macros, 1.51M fillers, all in-die,
      HPWL rel=0)**. ⚠️ **Two things NOT done (deferred to step 6 / MMS):** (a) the movable-only
      overflow map — `density_bin` currently emits ONE filler-inclusive force map, so `--place`
      overflow reads filler-inclusive until step 6 adds the `[0,first_filler)` scatter; (b) the macro
      override is **latent on every design pl_algo runs today** (ISPD std-cell → no macros; bookshelf
      MMS → td defaults to 1.0 in `--place`, so `td<1` never fires) — implemented+tier-1-tested, ready
      for MMS+phase-2. Device `--place` run with fillers is tier-3 (Vitis/Geert's card). Initial
      placement parity (sw_only centre-clusters movable; pl_algo uses parsed positions) is a separate
      step-6 A/B concern. → [[_NEW_HANDOFF_20_pl_algo_stage3_20260828.md]] (step-5 section to append).
- [~] **6. Compose the resident loop — STRUCTURAL DRAFT DONE 2026-08-29 ("structure first", Mark).**
      `top.cpp` now reads like the DATAFLOW diagram: three functions — `density_gradient` (bin_scatter
      → AIE-FFT field solve → force_gather, mirroring the proven Driver.cpp:547-556 sequence),
      `resident_iteration` (refresh → hpwl_grad + density → bb_reduce → metrics → param_scheduler →
      iteration_update, on-chip `SchedState`, no host round-trip), and `resident_place` (the loop),
      driven by a new `MODE_PLACE` with a dedicated resident ABI (gmem14-27 + 8 scalars). **Verified
      to the extent possible here:** `g++ -fsyntax-only -I$XILINX_HLS/include` clean (both `!PL_ONLY`
      and `PL_ONLY`) — a real check that every module signature + the wiring is consistent — and tier-1
      stays 12/12. **NOT done (the next gates):** (a) **tier-2 C-synth** (`vitis_hls`) — syntax ≠
      C-synth; (b) **tier-3 sw_emu** trajectory A/B vs sw_only; (c) **host co-design** — `Driver.cpp`
      needs a `runResidentPlacement()` to bind the 14 resident buffers + 8 scalars for `MODE_PLACE`,
      AND dummy bindings for those args in the bring-up modes (XRT requires every kernel arg set, so
      the bring-up sw_emu modes error until then — the Driver still *compiles*, `xrt::kernel` is
      variadic); (d) the **movable-only overflow map** `[0,first_filler)` (deferred: `metrics` reduces
      the filler-inclusive force map — "2nd map next"); (e) the **best-position snapshot** (hook marked
      in `resident_iteration`) — ⚠️ **it must snapshot the LOOKAHEAD `v_k`, not committed `u`** (parity
      note below). Phase-2 re-entrancy left out per the v1 decision (leave room, don't build).

⚠️ **sw_only parity note — u vs v (added 2026-08-17, from #32/7a).** sw_only now does what XPlace
does: **best-solution tracking snapshots the lookahead `v_k` (`probe_pos`) and measures HPWL there
too**, so HPWL, overflow and the stored solution all describe one position. XPlace has only one
position variable — `p` IS `v_k` (`nesterov_optimizer.py:71`) — and `evaluator_fn` measures both
metrics at it (`run_placement_nesterov.py:142-145`). Three consequences for pl_algo, all in step 6:
- the resident loop's **snapshot** writes `v_k`;
- its **HPWL metric** (`metrics.hpp`) must evaluate at `v_k`, not at the committed position — sw_only
  threads this as the new `at_probe` argument on `computeTotalWirelength`/`computeWirelength_HPWL`;
- a **restore** writes BOTH position fields, because sw_only's `syncProbeToCommitted()` is gone —
  folded into `restoreBestPlacement()`, which now restores the whole pair (u == v afterwards, which
  is the state XPlace is permanently in).
pl_algo's density deposit is already at the probe (`node_footprint.hpp`), so the deposit side needs
no change — it is the snapshot and the HPWL metric that would otherwise inherit the old split.
This is exactly the class of divergence that `sched_verify` cannot catch (it checks the schedule,
not the geometry), so it needs a step-3 harness or it will not be noticed.

**Decisions (Mark, 2026-08-28)** — report §10's first two open questions are now settled:
- **v1 = phase-1 GP, device-resident, bit-comparable. NO phase 2, NO backtracking.** Both are
  deferred until a measured need appears ("worry about them later, if we need to"). This relaxes
  step 6: phase-2 re-entrancy is no longer a v1 requirement (leave room for it, don't build it).
  The no-backtracking call also makes P1b's by-product HPWL unconditionally safe (Report #20 P1b's
  "take HPWL from the accepted trial" caveat only bit if backtracking existed).
- **pl_algo pins to up-to-date sw_only** (the frozen golden HEAD). The freeze makes this cheap and
  it is what makes `sched_verify` meaningful again — restore `dumpScheduleTrace()` against it (step 1).

**Still open** (report §10, third question): pin sw_only to grid 1024 for the A/B, or build pl_algo
per-design with `-DPL_GRID`?

**2026-09-04 (Path B / tier-3 sw_emu, mid-task):** the degenerate resident trajectory found
2026-09-03 is root-caused to the α seed alone (a real BB trial-step estimate is ~88,700x larger than
the crude `init_step_seed*site_width` fallback; the density-weight scale anomaly is a red herring,
self-normalizing). A host-side fix (`Driver.cpp::runResidentPlacement`, mirrors `runPlacement`'s
`estimate_initial_step`) is implemented and syntax-clean but **blocked, unverified**: the bring-up-
mode dispatch it needs stalls indefinitely (40-60+ min, zero output, not grid-size-dependent) at
this design's real scale (31k movable / 29k nets) in sw_emu, for reasons not yet diagnosed (no
ptrace access this session to get a backtrace). → [[_NEW_HANDOFF_20_pl_only_resident_bringup_20260903.md]] §8.

**2026-09-09 (bring-up ladder landed on real silicon; the sw_emu dispatch stall was sidestepped,
not fixed in sw_emu).** Rather than keep fighting the 2026-09-04 sw_emu dispatch stall, bring-up
moved to **real hardware** via a ladder of standalone PL-only harnesses, now committed at
**`vck5000/bring_up/`** (`709a116`): `add1_pl` (load/run) → `fft_pl` (DCT/IDCT/IDXST) →
`field_solve_pl` (2D field solve) → `hpwl_pl` (WA HPWL gradient) → `iteration_pl` (the full
gradient-and-step). `iteration_pl` runs one complete Nesterov step — both gradient sources combined
into a position update — on the VCK5000 and verifies `u_{k+1}`/`v_{k+1}` against a from-scratch double
golden (per its README; `rel_rms < 3e-2`, bounded by `hpwl_CU`'s exp-LUT). Each harness `-I`-includes
the real `modules/`, not copies. Also landed `dct_fft_aie` (AIE FFT) carrying the **bring-up dispatch
fix** (Mark-confirmed): the graph/kernel path was throwing `open_graph_handle: Operation not
supported`; the fix **enumerates devices** (probe each card on a multi-card node and use the first
that accepts the xclbin, not `device(0)`) and creates the graph/kernel through an **`xrt::hw_context`**
instead of the legacy `device+uuid` constructors (`+ -luuid` on the host link). This work was built on
the build server and was untracked/stashed there — see `rules.md` "Build server" for how that box is
reached and why nothing pushed from it directly.
⚠️ This is **not** the device-resident Stage-5 loop (step 6): no on-chip schedule, convergence test, or
looping — `iteration_pl` is a single host-driven step. The 2026-09-04 `runResidentPlacement` sw_emu
stall above is therefore **still open for the resident loop**; what changed is that the datapath is now
proven on silicon one step at a time, so the resident loop composes hardware-verified blocks.

---

## #21 — Repo restructure: one host at the top level (opened 2026-08-07)

→ [[_NEW_HANDOFF_repo_restructure_20260807.md]] (UNREAD). Target: `AIEplace/host/` (one host, three
backends), `AIEplace/vck5000/{pl,aie}/`.

**Two changes, and only the first is cheap.** **A** = the move + build rewire, ~1 day, mechanical,
shippable alone. **B** = collapse the hosts into one — **B is #20 wearing a different hat** and
inherits all of its preconditions.

**Merge `origin/geert` BEFORE restructuring.** `git merge-tree` (2026-08-07) shows **one conflict,
`.gitignore`, two independent appends**. Since the fork Geert changed 42 files and Mark 4493, and the
intersection is exactly 2. After the move his 25 `host/src/v2/**` files become adds-into-a-deleted-
directory — a no-op turns into a manual relocation of 25 files. **Tell Geert before merging.**

⚠️ **The real risk is semantic.** `host/src/v2/` is Geert's own from-scratch host rewrite (Limbo
removed, hand-written LEF/DEF, FPGA-targeted, JSON config; his README calls the gradient functions
stubs). So the repo holds **two independent, mutually unaware consolidations of the same component**
— `host/src/common/` and `host/src/v2/` — and git merges them happily forever because they never
share a file. **"One host" cannot mean four hosts.**

- [ ] **1. Merge `origin/geert`**, resolving `.gitignore` by keeping both appends. → `make test` +
      `make test-regress` green, `make host HOST=v2` builds.
- [ ] **2. PURE-RENAME commit** — `git mv vck5000/host host`, **zero content edits**. The build is
      broken at this commit; that is intended. → `git show --stat` is 100% renames. *Git detects
      renames by similarity; a commit that moves and edits can drop below threshold, and then every
      change Geert made becomes a delete/modify conflict he resolves by hand.*
- [ ] **3. BUILD-REWIRE commit** — split `REPO_ROOT` from `PROJECT_ROOT` in `common.mk`. Keep the
      `HOST=` selector working throughout. → builds for `sw_only`, `pl_algo`, **and `v2`**.
- [ ] **4. SWEEP commit** — the **92** path references across ~20 dirs. → `make test` and
      `make test-regress` green **without regenerating any baseline**. *If a baseline needs
      regenerating, stop.* Recommend `host/benchmarks/` does NOT move: it is data, and re-baselining
      to accommodate a directory move destroys the tripwire for exactly the change it should catch.
- [ ] **5. Decide what `v2` is** (Mark + Geert). Blocks 7.
- [ ] **6. #20 steps 1–3.** **Non-negotiable prerequisite for 7.**
- [ ] **7. Collapse `Placement.hpp` into the sw_only schedule** behind a backend interface.

**Decide in step 3, not step 7:** compile-time / **link-time (recommended)** / run-time backend
selection — the answer changes `common.mk`.

---

## #23 — `init_step_seed = 0.01` underflows: 5 ISPD2015 designs are dead on arrival (opened 2026-08-07)

`mgc_superblue{11_a,12,14,16_a}` and `mgc_des_perf_b` **never move a cell**. `estimateInitialStep()`
takes one trial step of `init_step_seed`, then sets α = ‖Δpos‖/‖Δgrad‖; on these designs the
displacement is below one float32 ULP of their coordinates, so **Δpos is exactly 0 ⇒ α = 0**, and a
zero step is self-sustaining. λ ramps unbounded for 2133 iterations, then NaNs — and the run reports
the untouched initial placement as its HPWL. Confirmed by probe: seed 1.0 → step 189647, spreads
immediately.

⚠️ **NOT a size threshold.** `mgc_des_perf_b`'s HPWL is two orders of magnitude below superblue's and
it fails identically. The only reliable detector is `α == 0` itself. Same family as #15.

**Found by the 44-design snapshot — exactly the blind spot that suite was built to expose.** Nothing
else covers ISPD2015.

- [x] **DONE 2026-08-10 — the seed is now in SITE WIDTHS.** Chose (a), with the scale taken from
      XPlace rather than invented: `step_length = init_step_seed * site_width` in
      `estimateInitialStep()`. → [[REPORT_23_site_width_seed_20260810.md]]
      **Why site width and not die span:** XPlace's estimator (`initializer.py:171-177`) is
      character-for-character ours, guard included (it has none) — it never trips because
      `database.py:854` prescales every coordinate by site width first, so its `args.lr = 0.01` is
      0.01 *site widths*. Ours was 0.01 raw DBU. Scaling by site width restores XPlace's unit; a
      die-span fraction would have been our own invention.
      ⚠️ **This is a change of UNITS, not of precision.** float32's relative epsilon is
      scale-invariant, so normalizing coordinates buys **nothing** numerically — the ULP scales with
      them (superblue11_a: 0.25 DBU ours, 0.38 DBU-equivalent XPlace). **Shifts buy precision, scales
      do not**; that is why #15 (net-local frames, a shift) measures 301× and this measures 1×.
      **Bookshelf `Sitewidth = 1`** on every ISPD2005/MMS design ⇒ `seed·1 == seed` ⇒ the whole tuned
      MMS suite is bit-unchanged, confirmed not assumed (`mms_adaptec1` PASSes untouched). LEF/DEF
      sites are 100-200 DBU, which is exactly where the bug lived.
      **Verified:** `make test-regress` red before / green after with both ISPD2015 baselines
      regenerated via `--reason`; `mgc_fft_a` 5.903e8→5.906e8 (+0.05%), `mgc_pci_bridge32_b`
      7.248e8→**7.196e8 (−0.72%)**; `make test` unaffected; `make host HOST=pl_algo` builds.
      Both designs re-run end-to-end: `mgc_des_perf_b` **converges in 825 iters** (overflow
      0.996→0.046) and `mgc_superblue11_a` in **849** (overflow 0.972→0.047, HPWL 7.443e10→3.300e10,
      −56%), where both previously never moved a cell.
      **Global normalization was considered and rejected** — exactly ONE active config parameter
      carries coordinate units (`init_gamma` was the other and was already fixed the same targeted
      way via `gamma_bin_scaled`/`gamma_ref_grid`), against a refactor that re-baselines everything
      and desyncs pl_algo. **Exception: `ap_fixed` has an absolute resolution, so if pl_algo ever
      narrows to fixed point this stops being cosmetic** — tracked under #15.
- [x] **DONE 2026-08-10 — the no-op now aborts instead of reporting a number.** `Step.cpp:209`
      guards the BB estimate at the end of `estimateInitialStep()`: `if (!(step_length > 0.0f))` →
      `Logger::log_error(...)` + `exit(1)`, naming the iteration, the phase, and the offending
      `init_step_seed`. Written as `!(α > 0)` so a NaN α trips it too. **This is the detector, not
      the fix** — the four other bullets stand, and these 5 designs now fail loudly rather than
      silently. Fires at *any* `phaseIteration() == 1`, so a phase-2 α of 0 aborts too and discards
      the phase-1 result; that has never been observed, and it is the correct default (a zero step
      makes the rest of the phase a no-op either way), but it is the one behaviour worth revisiting
      if it ever trips there.
      **Verified:** `make test-regress` green and **bit-identical** before and after (mgc_fft_a 731
      iters, mgc_pci_bridge32_b 751). Error path **exercised directly**, not reasoned about — the
      frozen mgc_fft_a regress config with `init_step_seed = 1e-30` exits **1** with
      *"Initial BB step estimate is 0 at iteration 1 (mixed_size): a trial step of init_step_seed =
      1e-30 displaced no movable node, so no later step can either."*
- [ ] **Add one large design to `make test-regress`.** The two mgc designs are small enough that the
      probe never underflows, so the tripwire cannot see this class of bug. **Still true after the
      2026-08-10 fix, and now for two reasons:** the fast tier is small LEF/DEF, and the slow tier is
      bookshelf where `site_width = 1` makes the new scaling a no-op. Nothing in the suite exercises
      a large site width.
- [ ] **↪ pl_algo — pl_algo's mirror is compile-verified only.** `Driver.cpp::estimate_initial_step`
      now scales by `cfg.site_width` (set in `main.cpp` from `db.getSiteWidth()`), and
      `make host HOST=pl_algo` builds — but it has never been run against the golden. Needs Geert's
      card or sw_emu. **Re-filed 2026-08-17**: this is the only bullet left in #23 that is not
      sw_only work, and it is the same item as the "pl_algo initial-step mirror" that used to sit
      in **Parked** — the duplicate has been deleted, this is the one copy. Fold into #20.
- [x] **Re-run the 4 excluded designs** — DONE, all 5 previously-`nan_metrics` designs re-run:
      `mgc_superblue11_a/12/14/16_a` converge; `mgc_des_perf_b` places but stops on
      `divergence_guard`. ⚠️ Surfaced a *new* defect: `mgc_matrix_mult_a` at **3.03×** (GP dies at
      iteration 290) — the single worst design in the tier, and what destroys the mean.
      → [[_NEW_REPORT_performance_snapshot_20260810.md]]

**Do NOT "fix" the snapshot by re-running these with a hand-tuned seed** — that is a per-design
hyperparameter, not comparable to the other 40, and it hides the defect.

---

## Parked — open technical follow-ups

*(2026-08-17: the **pl_algo initial-step mirror** bullet was deleted from here — it duplicated #23's
last bullet word for word. #23 keeps the one copy, now marked ↪ pl_algo.)*

- [ ] **SoA layout for the hot per-node/per-bin fields** (from #12). The next real threading win is
      layout, not more threads: `computeOverlaps`/`combineGradients`/`recordIterationResults` are
      memory-bound over pointer-chased objects and go flat by 4 threads. Big change, own task.
      ⚠️ **Under the sw_only freeze this needs a decision, not just a schedule slot.** It is meant
      to be behaviour-preserving, but it rewrites the hot data model of a frozen reference — so it
      is exactly the class of change the freeze exists to stop. `make test-regress` bit-identical
      is the bar if it is ever attempted.
- [ ] **Logger cosmetics** (from #5) — double-bordered summary tables (nested `Table`); the welcome
      banner still goes straight to `cout`, the last source of trailing whitespace; `run.log` written
      for **every** run including DSE sweeps (~125 MB per 500-run sweep); and "Algorithm time (s) |
      0.000" in Run Statistics, noticed in passing and never investigated.
*(2026-08-17: the **`init_step_seed` narrow-range Morris** bullet was deleted — self-marked optional
and low-priority, and its own warning said #23 changed its premise. #23 found the mechanism
outright (the seed was in raw DBU, not site widths); a sensitivity sweep would now be measuring a
solved problem, on a frozen placer. → [[REPORT_23_site_width_seed_20260810.md]].)*

---

## #33 — The aux ACCEPT budget: named 2026-08-17, still unswept (opened 2026-08-17)

Spun off #32's A/B rather than holding that item open, because it is a different knob.

**Renamed 2026-08-17 (Mark).** The four tracker tolerances were one named parameter and three magic
numbers; the name that existed, `best_aux_max_hpwl_ratio`, omitted *what it was measured against* —
which is the only thing separating it from its twin, and exactly why the twin stayed hidden. All
four now carry the reference point in a `{tracker}_{moment}_{quantity}` slot, where the moment is
XPlace's own function name, so each name round-trips upstream:

| moment | compares | name | XPlace |
|---|---|---|---|
| aux update | new HPWL vs **aux's own** snapshot | `AUX_UPDATE_HPWL_RATIO` = 1.005 | `param_scheduler.py:436` |
| aux select | aux HPWL vs **primary** | `aux_select_hpwl_ratio` = 1.005 (config) | `:568` |
| aux select | aux overflow vs **primary** | `AUX_SELECT_OVFW_RATIO` = 1.1 | `:569` |
| rollback update | new HPWL vs **rollback's own** | `ROLLBACK_UPDATE_HPWL_RATIO` = 1.01 | `:425` |

`update` = `update_best_sol` (recording, every iteration, against the tracker's own previous value);
`select` = `get_best_solution` (choosing what to ship, once, against the other tracker). Rename is
behaviour-neutral — `make test-regress` bit-identical. XPlace writes all four as bare literals,
which is why the duplication was invisible there too.

- [ ] **The accept budget is still unswept.** #32 settled the *selection* budget (keep 1.005; binds
      on 1 design of 28). The accept budget governs something different — how often `best_aux` is
      refreshed during the run, and therefore which placement it holds by the end. A knob that
      rarely binds at selection time may still be shaping the candidate it selects from.
      Cheap diagnostic first: count how many times the accept rule fires per run and how far the aux
      snapshot moves, before spending another 2.7 h suite. If aux is refreshed a handful of times,
      close this as known-and-accepted.
- [ ] **If it is ever exposed to config, it needs a `_ratio` entry of its own**, not a shared one.
      ⚠️ **Do not "fix" this by pointing both at one config value.** They are separate constants in
      XPlace and merely happen to be equal; collapsing them asserts an equality upstream does not.

### Dead-config-key guard (landed 2026-08-17, Mark's call)

*"Any parameter that is set but not used should raise a flag. A single typo could silently cause
unintended behaviour."* The failure it prevents: `write_config()` (`dse.py`) writes **any** key into
the TOML with no validation, and the exe reads with `value_or(default)` — so
`--set aux_select_hpwl_rato=1.01` writes the misspelled key, every arm falls back to the default,
and the sweep reports a clean success with all arms secretly identical. That is a multi-hour run
producing a confident wrong answer.

`tools/config_keys.py` derives the readable key set **from the sw_only sources on every call**, so
it cannot drift the way a checked-in list would. Wired in at the two points that matter:
- `make test` → `--check-configs` (the live configs set nothing unread)
- `dse.py` → `--check` on every `--set`, **before launching**; refuses to start and suggests the
  closest real key.

Audit at landing: the tracked configs were clean apart from **`input.xclbin`**, genuinely dead —
sw_only is CPU-only and never reads it; it is a leftover of the era when one config served the
hardware variants. Listed in `_KNOWN_UNREAD` rather than deleted, because it also sits in the
FROZEN `test/regress` configs, which must stay byte-identical to the inputs that produced their
baselines.

- [ ] **Residual gap: a config passed straight to the exe is still unchecked.**
      `aieplace_sw_only.exe my_config.toml` (i.e. `make run`, the `run-benchmark` skill, hand runs)
      does not go through either guard, so a typo there is still silent. The complete fix is runtime
      read-tracking in C++ — or, cheaper, generate a key header from `config_keys.py --list` and
      validate at startup, with a test that regenerates it and asserts it is unchanged. **Not done:
      it adds build machinery to a frozen sw_only and the expensive failure mode is already
      covered.** Mark's call whether it is worth it.

---

## #37 — The "macro-excluded" overflow was never macro-excluded (opened+landed 2026-08-27)

Found in a `/code-review` pass over `host/src/sw_only`. **Reporting-only; lands under the freeze's
cleanup/docs carve-out.** `make test-regress` AND `test-regress-slow` bit-identical on all three
designs, mms_adaptec1 (which exercises phase 2) included.

**The bug.** `computeOverflow(..., exclude_macros=true)` skips macros only inside the
`getMovableComponents()` pass. `computeFinalMetrics()` calls it at the END of the run — by which
point `freezeMovableMacros()` has moved every movable macro into `getFixedComponents()`, so the
flag matches nothing and the number collapses onto the plain exact overflow. The run's own
diagnostic printed the proof for months: `sharp/no-filler=0.118  macro-excluded=0.118`.

**Why it mattered.** `tools/benchmarks.py` instructs comparing exactly that row against
`_XPLACE_MMS_MIXED_GP`, which XPlace measures at its phase-1 Mixed-GP checkpoint under
`zero_macro_grad=True`. So a post-phase-2, macro-INCLUDED number was being quoted against a
phase-1, macro-EXCLUDED reference.

**The fix.** The number is now computed in `reportPhaseSummary()`, which moved to **after**
`restoreBestPlacement()` and before `freezeMovableMacros()` — XPlace's exact checkpoint
(`ps.get_best_solution()` → copy → `evaluate_placement`, `run_placement_nesterov.py:172-179`).
Mark's call 2026-08-27, chosen over the minimal patch because the other three phase-1 numbers had
the same defect: they were measured at the last ITERATED placement, not the one phase 1 ships.
Also added `Phase 1 HPWL (exact, all nets)` — `_XPLACE_MMS_MIXED_GP` is a *pair* and its HPWL half
is XPlace's unmasked `get_obj_hpwl`, so fixing only the overflow half left the comparison half-done.
The post-phase-2 `Macro-Excluded Overflow` row is now suppressed rather than printed misleadingly.

⚠️ **Every phase-1 number quoted before 2026-08-27 is on the old basis.** The correction is not
cosmetic — mms/adaptec1 goes **0.118 → 0.0702** against XPlace's 0.1306. The old number read as
"slightly better spread than XPlace"; the true one says we hand off *substantially* more spread.
history.md's tier-3 flags (§ "Flag a tier-3 design only where OUR macro-excluded overflow
materially exceeds its Mixed-GP") were computed on the wrong column and should be re-read, not
trusted. Not re-run here — that is a suite job.

- [ ] **Re-derive the tier-3 flags** from the `[PHASE] ovfw_macro_excluded` column across the 16
      MMS designs. One `make dse` MMS pass; no code change. Until then treat the existing
      macro-excluded comparisons in history.md as retracted rather than merely stale.

## #38 — Is `MacroLegalize.cpp` redundant? (opened 2026-08-27, Mark's question)

**Established, not yet decided.** ~600 lines porting XPlace's `macro_legalization.py`.

**Yes for scoring.** `tools/lgdp.py` scores every GP through XPlace `main.py --global_placement
False --given_solution <def>`, and XPlace's LG path calls `macro_legalization_main`
**unconditionally** (`detail_placement.py:374`, inside `run_lg`, before greedy legalization). So
XPlace re-legalizes our macros from scratch on the way to every LG/DP number we quote — our port
contributes nothing to the scored result.

**No for GP.** `legalizeMacros()` is not only a legalization deliverable: it runs *inside*
`beginFixedMacroPhase()`, between the freeze and the std-cell re-seed, so phase 2 optimizes std
cells against that macro floorplan and deposits macro density from it. Deleting it changes the GP
result, not just the artifact. The disabled path already exists and says so: "macros frozen where
GP left them (overlapping; phase-2 overflow is still meaningful, the placement is not legal)".

**So it is an empirical question, and the switch already exists.**
- [ ] **A/B `macro_legalization = true|false` over the 16 MMS designs**, scoring post-DP HPWL via
      `make dse`. If post-DP is a wash, delete the file — XPlace does the job better on the way to
      the score, and its version has three things ours explicitly does not (the
      `macro_legalization_xy` variant, site/row alignment, retry-with-longer-CBC-time-limit).
- [ ] **Known gap, relevant either way** (same review): `runMacroLegalization()` hard-codes
      `m.fixed = false` and collects only `isMovableMacro()` components, so genuinely-fixed
      macros/blockages never enter the constraint graph or the LP — every `MacroBox::fixed` branch
      is dead code. XPlace includes them (`detail_placement.py:314-320`). Latent on MMS only
      because every `terminal` in `data/raw/mms/*.nodes` is zero-area; it would bite on any
      LEF/DEF mixed-size input with a sized fixed macro. **Don't fix this before the A/B** — if the
      file goes, the gap goes with it.

---

## #39 — sw_only readability refactor, for thesis presentation (opened 2026-08-31, Mark)

Mark is presenting this codebase for his thesis and needs to know what's in it and be able to
explain it — going through the high-level `Placer` files with a fine-tooth comb, comparing code
snippets against a hierarchical class map to check each is in the right file, right order, right
name. **Functionality is frozen; readability is explicitly in scope** (CLAUDE.md's freeze section).
Every code move is verified bit-identical (`make test-regress[-slow]`) before landing — skip that
per-change during an active session only when Mark says so; confirm it before calling a stretch of
readability work done. Session started from — and is guided by — the "Placer Class Map" artifact
built 2026-08-31 (all ~85 `Placer` methods grouped by responsibility along the dataflow, each
tagged with its file; misplacements flagged).

**Session 2026-08-31 → 2026-09-01 landed and is now committed** (3 commits, `1ed9ea7`/`652ba25`/
`5867600` on `pl_algo` — see [[_NEW_HANDOFF_39_readability_refactor_session_20260901.md]] for the
full narrative, including a real bit-identical regression hit and resolved mid-session):

- **New `placer/BestSolution.cpp`** — gathers 6 best-solution methods that were split across
  AIEplace/Schedule/Output (this is why `BestPlacement` felt wrong in the top-level file).
- **All γ code consolidated into `Schedule.cpp`** — `updateGamma` out of `Partials.cpp`.
- **`run()` reads plainly via `phase`** — `beginFixedMacroPhase()` split into `readyForPhase2()`
  (eligibility) + `beginPhase2()` (transition), gated in `run()` by `phase == Phase::MIXED_SIZE`.
- **`FixedMacroPhase` → `Phase2`** renamed throughout; `Phase2.cpp` now states explicitly how
  XPlace's 3 stages map onto our 2-state `Phase` enum.
- **`Step.cpp` reordered** — `performNextStep()` (Algorithm 2, the heart) and `estimateInitialStep()`
  lead; shared primitives follow in call order; `logStepDiagnostics()` last.
- **`Schedule.cpp` reordered** — `updateSchedule()` and `checkConvergence()` (both called directly
  from `run()`) lead, each followed by its dispatched callees in call order; the two cross-cutting
  predicates (`checkOverflowPlateau`, `checkDivergence`) moved to the end as shared primitives.
- **`checkForNaN()`** — the two scattered `nan_detected` checks consolidated into one Schedule.cpp
  function, called once from `run()`.
- **`dumpScheduleTrace()` self-gates** — the config check moved inside the function; the call site
  is now unconditional.
- **`iterationReset()` moved** from `AIEplace.cpp` to `Step.cpp`, beside `advanceIterationState()`.
- **`beginPhase2()` now calls `performIterationZero()`** instead of hand-duplicating its body —
  closes a silent-drift risk.
- **`m_` prefix dropped** from every `Placer` data member (landed in a parallel session, rolled into
  commit 1 here as a from-`HEAD` mechanical rename, independently re-verified bit-identical).

**A real bit-identical regression was hit and resolved.** `make test-regress-slow` failed
mid-session (`density_weight` off by ~0.3% from iteration 3 onward, on every design including ones
with no macros). Root cause, found by direct A/B bisection of the diff (not none of the readability
moves above — all individually re-verified clean): a **statement-order swap** in `performIteration()`
— `printIterationResults()` moved to run *after* `updateSchedule()` instead of *before*, part of an
unrelated "group by function type" edit. Confirmed by flipping the order alone, reproducibly, across
clean rebuilds. **The mechanism is still unknown** — none of `printIterationResults()`'s callees
write any state `updateSchedule()` reads, so this reads like a compiler/FPU-state side effect of the
iostream formatting in the print path, not a logic bug. Effect was negligible on the one benchmark
measured (final HPWL/overflow identical to 4 sig figs; only the internal `density_weight` scheduling
value drifted). **The original order is kept** (Mark's call) to leave the committed baselines
unchanged — see next section.

Open / queued for a future session:
- [ ] **Understand the print/updateSchedule order-sensitivity mechanism.** Not urgent (order is
      correct in committed code, `test-regress-slow` is green), but it's an undocumented,
      load-bearing statement-order dependency that looks like harmless reordering — exactly the
      kind of thing that bites again. Suspect: iostream formatting (`SCI`/`PREC`/`std::to_string`)
      perturbing FPU rounding-mode state ahead of `updateDensityWeight()`'s `std::pow()` call. If
      confirmed, the fix is probably a comment at the call site, not a code change.
- [x] **DONE 2026-09-02 — `Setup.cpp` reordered to match call order (`a2a2c1b`).**
      `setupDesign()`'s callees (`loadConfiguration`/`resolveGridResolution`/`loadDesignDatabase`/
      `tagMovableMacros`/`createFillers`) now follow it in call order, same convention as
      Step.cpp/Schedule.cpp; `setupGrid()` moved next to it (the constructor's next call after
      `setupDesign()`). Pure reordering of out-of-line member definitions — no logic touched.
      Verified: `make host` clean, `make test-regress` bit-identical on both designs.
- [x] **DONE 2026-09-02 — `configureThreadPool()` relocated + `setupGrid()` folded into
      `setupDesign()` (`df6a2c2`).** `configureThreadPool()` (a static helper with only one
      caller) moved down to its call-order position right after `loadConfiguration()`, behind a
      forward declaration, so `setupDesign()` is the first function in the file as Mark asked.
      `setupGrid()`'s call moved from the constructor into `setupDesign()` (as its last step) —
      safe because nothing ran between the two calls in the constructor, so no execution order
      changed; `setupGrid()` itself stays put, already at its call-order position. Verified:
      `make host` clean, `make test-regress-slow` bit-identical on all three baselines
      (mms_adaptec1 exercises phase 2).
- [x] **DONE 2026-09-02 — `recordIterationResults()` moved to `BestSolution.cpp` (`df6a2c2`).**
      It drives the same three trackers (primary/aux/rollback) the rest of that file manages, not
      output/reporting — placed right before its callee `snapshotBestPlacement()`. A one-line
      breadcrumb left in `Output.cpp`, matching the existing `restoreBestSolution()` breadcrumb.
      Verified: `make host` clean, `make test-regress-slow` bit-identical on all three baselines.
- [x] **DONE 2026-09-02 — @file doc comments for `AIEplace.h`/`.cpp` + `Density.cpp`, README
      refresh (`3ef1d61`).** Fresh-reader orientation gap closed: `AIEplace.h` (every file includes
      it, had zero doc) and `AIEplace.cpp` (the loop skeleton every sibling `.cpp` already pointed
      readers to) now carry brief `@file` blocks naming what's there and where to read next.
      `Density.cpp`'s plain comment header upgraded to the same `@file` style as its gradient-pair
      sibling `Partials.cpp`. `README.md`: fixed the stale ~110-line claim for `AIEplace.cpp` (now
      ~80), added `BestSolution.cpp` to the Placer file-split table (missing since it was created
      2026-08-31), marked which five files are the core algorithm (AIEplace/Partials/Density/Step/
      Schedule) vs. supporting machinery. Comment-only; verified `make host` + `make test-regress`
      bit-identical.
- [x] **DONE 2026-09-02 — `ConfigUtils::require` moved to the end of `AIEplace.h` (`5cf7491`).**
      It's a generic TOML helper with no `Placer` dependency, used from 5 different placer/*.cpp
      files (has to stay in a header, since it's a template) — moving it into `Setup.cpp` where
      config is actually read wasn't an option without breaking the other four TUs. `AIEplace.h`
      now opens directly with the `Placer` class the file exists to declare. Verified: `make host`
      clean (all 5 call sites), `make test-regress` bit-identical.
- [x] **DONE 2026-09-02 — swept `Density.cpp`/`Partials.cpp`/`Phase2.cpp`/`PositionDump.cpp`
      (`5fbd182`).** `PositionDump.cpp` checked clean, not touched — its four entry points are
      already lifecycle-ordered and each callee already immediately follows its sole caller. Three
      real mismatches found and fixed:
      - **`Density.cpp`**: `computeOverlaps()` is `computeElectricFields()`'s FIRST call, but sat
        ~200 lines below it, after the naive/DCT reference implementations. Moved up to lead the
        callee block. Left naive-before-DCT alone — that's the deliberate
        reference-implementation-first pattern (README: "kept alongside as the verification
        reference"), and naive is unreachable from config, so there's no live call order to
        violate there.
      - **`Partials.cpp`**: `computeHpwlPartials()` checks `"cpu"` before `"simple"`, and `cpu` is
        the default/golden path (README, `default_config.toml`, and the function's own doc comment
        all agree) — but the file presented the simple/LUT backend's implementation first. Swapped
        the two blocks so the golden path leads, matching both the dispatcher's branch order and
        the file's own doc-comment ("cpu / simple").
      - **`Phase2.cpp`**: `reportPhaseSummary()` is `beginPhase2()`'s first local callee (called
        right after the best-solution restore, before freeze/legalize/re-seed), but sat at the very
        end of the file. Moved up to lead the block.
      Pure reordering of out-of-line definitions, no logic changed. Verified: `make host` clean,
      `make test-regress-slow` bit-identical on all three baselines (mms_adaptec1 exercises
      `Phase2.cpp`).
- [ ] Fold this task into `#1`'s two 2026-08-31 sub-items once concluded — see the superseded note
      there; that "found clean, no refactor taken" verdict was true at the time but is now stale.

---

## #40 — `hpwl_gradient_dhar`: real-hardware failure is a TIMING CLOSURE bug, not a logic/readback bug (opened 2026-09-10)

↪ pl_algo bring-up. One of the `vck5000/bring_up/` standalone hardware harnesses (#20's 2026-09-09
ladder: `add1_pl`→`fft_pl`→`field_solve_pl`→`hpwl_pl`→`iteration_pl`). `hpwl_gradient_dhar` is a
separate fast-adder-tree HPWL-gradient kernel.

**ROOT-CAUSED 2026-09-10: the design does not meet timing on real silicon.** Read the routed
(post-place-and-route) `v++` timing report from the `hw` build already sitting on the build server
(`_x/reports/link/imp/impl_1_..._timing_summary_routed.rpt`) — never rebuilt, the report already
existed from the run that failed. `WNS = -0.710 ns`, `TNS = -11,741.808 ns`, **43,859 / 196,028
endpoints (22%) fail setup** on the kernel's own 300 MHz clock (`clkwiz_aclk_kernel_00_clk_out1`).
`v++` shipped the xclbin anyway — the default flow warns on unmet timing but doesn't block bitstream
generation, so nothing upstream would have caught this short of reading the report.

**Every one of the 10 worst violated paths lands inside `grp_hpwl_gradient_dhar_Pipeline_net_loop_fu_336`**
— the `net_loop:` pipeline region in `hpwl_gradient_dhar.hpp`, the fully-unrolled 16-lane
term-gen/adder-tree/combiner block. The worst path is **85% route delay, 15% logic** (only 5 logic
levels) — a congestion signature, not a too-deep datapath. Destination register names
(`lut_BRAM_load_127_reg`, `ce_reg_replica_15`) point at the cause: `term_gen` calls `hpwl_lut_exp`
4×/lane × 16 lanes (fully `UNROLL`ed) = 64 concurrent on-chip LUT reads per cycle, and a BRAM/LUTRAM
only has 1-2 read ports, so HLS replicated the LUT cache (and its control fan-out) many times over to
feed all 64 readers at once — long-distance routing to reach the replicas within one cycle. Kernel
resource utilization is only **6.36% LUT / 3.99% REG** of the reconfigurable region, so this is
*local* fan-out congestion, not a capacity problem.

**Functionality is verified correct — tier-1 (`make test`, pure g++) passes at ~1e-6, unchanged.**
This is a classic FPGA physical-design problem (too much parallel logic crammed into one pipeline
stage), not a bug in the math, the DDR readback, or the host/device transfer path.

⚠️ **hw_emu would not have caught this and is not worth running for this bug** — it is cycle-accurate
to the HLS-*scheduled* RTL, not to post-place-and-route timing, so it almost certainly passes. Chasing
it would burn a build cycle to learn nothing.

<details><summary>Superseded 2026-09-10: original working hypothesis (DDR-bank/host readback), written
before the timing report was read — kept for the record, now refuted</summary>

> **Tier-1 offline (`make test`, pure g++) PASSES** — same kernel, same golden, ~1e-6. **Real-hardware
> run FAILS**: `bo_grad` (the per-node gradient array, `krnl.group_id(6)`) comes back wrong by orders
> of magnitude (rel_rms ~18, max_rel ~296), while `bo_hpwl` (the single scalar at `group_id(7)`) is
> correct to rel=2e-8 — same order as the tier-1 run. HPWL being right means net/pin/LUT loading and
> the segmented-reduction passes that feed *both* outputs are executing correctly, which localizes the
> bug to the grad-specific readback path (`bo_pin_grad`/`bo_grad`, group_id(5)/(6)) rather than the
> kernel math itself (already proven correct offline).
>
> **Working hypothesis, not yet confirmed:** a DDR-bank/connectivity or host/device size-mismatch
> issue specific to real hardware (untested in tier-1 C-sim and presumably `sw_emu`) — not a
> timing/precision bug, the magnitude is too large for that.
>
> Refuted by the routed timing report: HPWL reads correct not because its path is clean but because
> (apparently) enough of its narrower accumulation path avoided the 22%-failing region, while the
> wide `net_loop` combinational block did not. No DDR-bank/size-mismatch evidence was ever found —
> the real cause was sitting in a report that already existed and had not been read.

</details>

**2026-09-11: three pragma-level experiments tried against the LUT/port-contention diagnosis above
— one confirmed net-negative, two confirmed no-ops.** All verified via a real `v++ -c` C-synthesis
re-run on the build server (not guessed), and the design's own compile log/guidance report read
directly rather than just the packaged summary:

- **`lut_BRAM` cyclic-partitioned (factor=16)** — DID what it targeted: `BRAM_18K` 14→0, the 64
  concurrent reads moved onto 16 LUTRAM banks (confirmed in the csynth resource report). **But a
  full P&R rebuild then FAILED to route at all** — 1126 unrouted signals, 771 illegal node overlaps,
  worse than the pre-fix "routes but misses timing" baseline. The +42% LUT cost (69k→98k) landed as
  LUTRAM in the *same* SLICEM fabric the surrounding `fmul`/`fmadd`/`faddfsub` DSP-adjacent logic
  already needed, worsening local congestion rather than fixing it — confirmed by the P&R log's
  top-10 congested-node list, which is dominated by that floating-point logic, not `lut_BRAM` itself.
  **REVERTED** (`hpwl_gradient_dhar.hpp`, at `lut_BRAM`'s declaration — the revert note is load-bearing,
  don't reapply without also cutting `net_loop`'s parallelism first).
- **`load_net` UNROLL→PIPELINE II=1** — no effect whatsoever; identical `Final II=16, Depth=411,
  Fmax=349.47MHz` and identical `gmem1` port-conflict warning, both before and after. Root cause:
  Vitis HLS automatically flattens/re-unrolls loops nested inside an already-`PIPELINE`d outer loop
  (`net_loop` itself), overriding the inner loop's own pragma. Left in place (harmless, doesn't hurt),
  but does not fix `gmem1`'s contention — that needs real restructuring (see below), not a pragma.
- **`hpwl_total` split into 8 named lane accumulators + `switch(n&7)`** (mirroring
  `hpwl_gradient.hpp`'s `HPWL_LANES` trick) — the underlying carried-dependence warning did NOT
  disappear, it just moved to `hpwl_part6`. The sibling's actual trick is a **statically-unrolled**
  outer-stride loop (`hpwl_part[k]` for compile-time-constant `k`, 8 *textually distinct*
  statements) — HLS's dependence checker doesn't reason about `n&7`'s modular arithmetic to prove
  a runtime `switch` never revisits the same lane on consecutive iterations, so it stayed
  conservative. Kept as-is (cheap, harmless), but not a validated fix.

**The real pattern across all three: `net_loop` fully unrolls 16 lanes of the ENTIRE
term-gen→adder-tree→combine chain (dozens of parallel float multiply/FMA/add DSP units) into ONE
HLS pipeline region, and that is simply too dense to legally route regardless of which piece you
relocate.** Fixing the LUT reads didn't help because the LUT was never the only over-subscribed
resource; it just moved the pain. **Next step: reduce `net_loop`'s parallelism WIDTH, not relocate
its resource usage** — partially unroll `term_gen`/the adder trees/`combine` (e.g. 4 or 8 lanes per
wave instead of 16), trading more cycles/net for materially less floating-point hardware
co-resident in one place. Not yet attempted.

- [ ] Design and implement the partial-unroll restructure (lane width TBD — start at 8, halving the
      per-cycle DSP/LUT count, and measure via C-synthesis before committing to a P&R run).
- [ ] Validate via the cheap loop first: tier-1 (`make test`, seconds) → C-synthesis-only `v++ -c`
      resource/II numbers (~15 min) → only THEN spend a full P&R run (~4 hrs) once the numbers look
      genuinely better. Do not go straight to P&R on a guess again — the 2026-09-11 `lut_BRAM`
      experiment cost a full P&R cycle to learn it regressed.
- [ ] `gmem1`/`gmem3` (`load_net`'s DDR-port contention, forcing `Final II=16` independent of
      everything else) still needs its own fix — likely duplicating `net_pins_DDR`/`pin_to_npin_DDR`
      across multiple `m_axi` bundles, or genuinely decoupling `load_net` into its own pipelined
      region separate from `net_loop` (mirroring how `hpwl_gradient.hpp` splits load/compute into
      distinct phases instead of fusing them per-net). Not yet scoped in detail.

**2026-09-18: the 16-pin cap itself is a quality bug — measured, not Dhar's "slight improvement".**
`ignore_net_degree=16` vs the frozen golden (100), 28 ISPD designs, post-DP legal-vs-legal: **28/28
worse, mean +12.44%** (ISPD2005 +30.6%, up to +51% bigblue4; ISPD2015 +5.2%). 17–100-pin nets hold
20–27% of ISPD2005 pins. Run `vck5000/results/DSE_20260918_124001/`. **Fix = exact chunking** (same WA
math in ⌈d/16⌉ blocks, net-level B/C sums accumulated across blocks): +6–9% blocks on ISPD2005, no
quality experiment needed, and it is the SAME restructure as the lane-narrowing above — do together.
- [ ] Fold arbitrary-degree support (chunked, ≤100 pins) into the partial-unroll restructure; switch
      `hpwl_dhar_test` to the uncapped golden and add 17/32/33/100-pin nets. Drop phase Z after.
→ [[_NEW_REPORT_40_net_degree_cap16_20260918.md]] (measurement) ·
[[_NEW_PLAN_40_dhar_large_net_chunking_20260918.md]] (design; NOT yet built, Mark 2026-09-18)

→ [[_NEW_HANDOFF_40_hpwl_gradient_dhar_hw_grad_bug_20260910.md]]

**2026-09-18: v1 is being REBUILT, not patched** — a new `bring_up/hpwl_gradient_dhar_v2/` (one axis per
call, structure-of-arrays data, loop structure ready for chunking; Mark places the pragmas and reviews
10–20-line chunks). The checklist items above are superseded by its lessons list and its D1–D9
design questions. → [[_NEW_HANDOFF_40_dhar_v2_rewrite_20260918.md]]

**2026-09-21: v2's bbox/HPWL milestone hit II=1 on `compute`, II=8 on `input_controller`** — the
remaining II=8 is `pin_x`'s single `m_axi` port serving 8 simultaneous reads/net, a DDR-bandwidth-
shape problem, not a logic one. Spawned a new module, **`hpwl_computer`**
(`bring_up/hpwl_computer/`): `LANES=16` (matches the 512-bit beat exactly), nets bucketed by degree
and packed `nets_per_beat = floor(16/degree)` to a beat (Dhar Method 1), so one beat/cycle feeds
compute directly — no separate beat-parsing controller needed (one was designed, then abandoned
once the padded/grouped layout made it unnecessary). Compute reuses **Dhar's Fig. 6/7 multi-output
tree + selector unchanged, with comparators in place of adders** (max/min are
associative/commutative exactly like sum, so the structure carries over).

**2026-09-22: `hpwl_computer` DONE — built, tier-1-verified, C-synthesizes at II=1.**
`resolve_beat` (beat-granularity degree resolution) → `dhar_tree<MaxOp>`/`dhar_tree<MinOp>` (the
34-output tree, now a template over the combining operator, ready for an `AddOp` instantiation in
#41) → `select_lane_hpwl` (the 16 per-lane muxes) → packed into one `OutBeat` per input beat.
Tier-1: bit-exact over 279 real nets (independent golden at every stage). C-synthesis journey
measured step by step (not guessed): `PIPELINE II=1` on `beat_loop` was first **rejected outright**
(a variable-trip-count subloop can't be unrolled) → fixed-width guarded loop → pipelines but II=12
(`TreeOutputs` arrays not partitioned) → `ARRAY_PARTITION` → II=8 (bottleneck moves to `lane_hpwl`)
→ `ARRAY_PARTITION` again → II=8 unchanged (bottleneck moves to scalar DDR writes, up to 8/beat on
one port) → **pack all real values into one wide `OutBeat`, one write/beat → II=1.** This last fix
is the output-side mirror of the module's own reason for existing (wide beats beat narrow
multi-request I/O) — same lesson, symmetric top to bottom. Output is beat-aligned (not tightly
packed), and total output DDR traffic is bounded at exactly 50% of input traffic regardless of
degree mix (`OutBeat` is half `InBeat`'s width, one of each per beat).
→ [[REPORT_40_hpwl_computer_20260921.md]] (full build + synthesis table) ·
[[DIAGRAM_hpwl_computer.md]] (`.claude/2_ARTIFACTS/diagrams/`). **Unblocks #41.**

---

## #41 — `hpwl_gradient_computer`: extend `hpwl_computer` to the full WA gradient (opened 2026-09-21)

**#40 landed 2026-09-22 — unblocked.** `hpwl_computer` (#40) is deliberately scoped to bbox/HPWL
only — two comparator trees (Dhar Fig. 6/7 reused with max/min instead of sum, now a
`dhar_tree<Op>` template built specifically so an `AddOp` instantiation drops in here), one beat of
16 same-degree pins in, one `hpwl_span` out per net, verified tier-1 + C-synthesis (II=1). This task
is the follow-on: add the remaining stages needed to get the actual wirelength-average **gradient**
per pin, on top of the same now-proven multi-net-per-beat pattern.

**Scope — the stages `hpwl_computer` explicitly skips, from Dhar's Method 1 (§III-B) and the v2
handoff's D1–D9:**
- **Term generation (Fig. 5):** for each of the 16 incoming pins, `e^x`, `e^-x`, `x*e^x`, `x*e^-x`
  (the packed-pair LUT design, D8, already decided: `(lut[i], lut[i+1])` in one 64-bit word).
- **Two more multi-output trees** (adder, not comparator — sum, not max/min) for the `e^x`/`e^-x`
  term sums, reusing the same Fig. 6/7 structure a third and fourth time (four trees total once
  bbox's two are counted, exactly matching Dhar's own count).
- **Adder result selector (Fig. 7)** — per-pin, not per-block like `hpwl_computer`'s degree-only
  selector: picks the correct one of the 34 sums for *each* of the 16 pin positions, since gradient
  (unlike HPWL) is a per-pin output, not a per-net one.
  - **Combiner (Fig. 8):** the actual gradient formula per pin from the 4 selected term-sums.
- **Same tail-partial-beat handling as #40** applies here too — inherit that solution rather than
  re-deriving it.

**Naming note:** `hpwl_computer` was named for what it does now (HPWL only); this task's module
adds "gradient" to both the computation and the name, hence `hpwl_gradient_computer`. Whether it
replaces `hpwl_computer` in place or is a new module that reuses/wraps it (the bbox trees are a
strict subset of what the gradient needs) is an open question for when this task starts — decide it
the same way #40 decided its own module boundary: discuss dataflow with Mark before any datapath
code.

- [x] **#40 (`hpwl_computer`) landed and verified 2026-09-22** — tier-1 + C-synthesis (II=1). This
      task now builds on its proven multi-net-per-beat pattern and its `dhar_tree<Op>` template.
- [x] **Pin→node summation strategy decided 2026-09-22 (Mark): on-chip scatter-add, not DDR.** A
      scatter or gather through DDR is ~10–50× short of the trees' 4.8 G pins/s per axis. Instead,
      `node_grad`/`node_pos` sit in banked URAM (8 MB budget, float accumulation), and the host
      precomputes a conflict-free beat packing. Prototype `vck5000/bring_up/beat_packer/`:
      **32 banks + pre-crossbar merge of same-node lanes = ≥99.69% of ideal beats on all 44
      designs**; 16 banks + merge reaches ≥94.3%. The dominant obstacle was a cell with 2+ pins on
      one net (~10% of ISPD2005 nets). → [[_NEW_REPORT_41_beat_packer_20260922.md]]
      URAM budget vs density's bin scatter deferred until the whole-iteration dataflow is clearer.
- [x] **Host→device record designed + prototyped 2026-09-22.** Positions go on chip too, so the
      refresh gather disappears. The DDR stream is static:
      `record = node_slot << offset_bits | offset_idx`, 32 bits × 16 lanes per beat, one stream
      per axis. The flags (EMPTY, fixed, repeated node) are comparisons on `node_slot`, so they
      cost no bits. Fixed pins get pre-resolved slots, which keeps the offset tables ≤127 entries
      on ISPD. **41/44 designs round-trip exactly** (a decode-based checker, 6/6 mutants caught).
      The only misses are MMS bigblue3/4 and newblue7, which are over the node count (chunking)
      and have movable-macro offsets. → same report, Part 2.
- [x] **Four record-stream modules built overnight 2026-09-22 (Mark's brief).**
      - `hpwl_computer_v2` (on-chip gather)
      - `hpwl_gradient_computer` (Dhar Method 1 + on-chip merge / scatter-add + macro fold)
      - `hpwl_computer_v3` and `hpwl_gradient_computer_v2` (chunking: ghosts through a
        consumer-major DDR exchange buffer)

      Each is tier-1 verified against a golden from the parsed netlist (HPWL bit-exact; gradient
      rel_rms ~4e-7, tol 1e-5), mutation-tested, and in `make test`. Also:
      - Packer is a library now, with movable-macro pin slots.
      - All 44 designs encode (≤29 bits, ≤131 offsets) and chunk at 1 M slots (8 need K=2–3,
        ghosts 4–19%).
      - C-synthesis: v2 / v3 are II=1 at full capacity. The gather needed a masked-readout
        rewrite: a dynamic index hung the HLS front end (bisected).

      → [[_NEW_REPORT_41_record_datapath_20260922.md]]. (The old "module boundary / port Dhar
      stages / tier-1 golden" boxes are what this delivered: new modules, not an in-place
      extension.)
- [ ] Take `HAZARD_DISTANCE` from the synthesized RMW depth (see the report); v1's count-based
      `resolve_beat` is superseded by EMPTY lanes from v2 on.
- [ ] **Nets of 17..100 pins:** still dropped; they are 20–29% of ISPD2005 pins (+12.4% HPWL when
      dropped). **Needs Mark's call before code.** The proposal is L3: exact, three II=1 passes
      (bbox / sums / combine) on the existing engine. A chunk beat is a degree-16 beat, plus a small
      net-state table. It costs about 2× gradient cycles on ISPD2005 and 1.04–1.5× on ISPD2015. L2
      (online rescale, about 1.7×) is a deliberate divergence from sw_only.
      → [[_NEW_PLAN_41_large_nets_on_records_20260923.md]]
- [ ] Optional: a min-cut partitioner if bigblue4 / newblue7 ghost cost (13–19%) matters.

---

# Improvements

Algorithmic ideas beyond faithfulness cleanup — hypotheses, not yet scoped.

- [ ] **Upstream the two XPlace `--use_precond False` fixes as a PR or issue** (opened 2026-08-18).
      Both are already committed locally on `~/phd/Xplace` branch `local-fixes`; this item is only
      about whether to send them to `github.com/cuhk-eda/Xplace`.
      **The bug:** `--use_precond False` is a documented flag that cannot run at all, breaking two
      independent ways. (1) `apply_precond()` (`calculator.py:5`) returns the preconditioned
      gradient on the normal path but falls off the end returning `None` when `use_precond` is
      false; its only caller assigns that to `grad` (`calculator.py:89`) and hands it to the
      optimizer. (2) `update_precond_weight()` returned early, but `self.weighted_weight` is
      **never initialised in `__init__`** — it appears there only as a *string* in the
      `self.metrics` list — while `step()` reads it unconditionally at `param_scheduler.py:284`
      to gate the every-3rd-iteration throttle.
      **Why it would be a good PR:** tiny, self-contained, a documented flag that is completely
      broken, and trivial for a maintainer to verify.
      ⚠️ **Three things to settle before sending, all real:**
      - **It is verified STATICALLY, not by running.** Nobody has executed XPlace with
        `--use_precond False` and captured the two tracebacks. That is the first thing a maintainer
        will ask for, and it is the one piece of evidence missing.
      - **Fix (2) is a judgement call, not mechanical.** The minimal fix is
        `self.weighted_weight = 0.0` in `__init__`, keeping the early return; ours computes it
        unconditionally, which *changes throttle behaviour* under the flag. Our argument is that
        `weighted_weight` is a **schedule** quantity and `use_precond` properly gates
        `apply_precond()`, where the division actually happens — defensible, but a maintainer may
        prefer the minimal form. **File as an issue showing both**, rather than a PR that assumes
        ours is the wanted one.
      - **Repo activity is unknown** — last upstream commit is "update download link". Worth
        checking issue/PR response times before spending effort.
      **Why we care beyond good citizenship:** we run XPlace as our reference, and fix (2) sits on
      the path that computes `weighted_weight` = our `precond_kappa` (see the naming rule in
      `CLAUDE.md`). If upstream ever adopts the minimal form instead, our `--use_precond False`
      diagnostic runs quietly stop being comparable to theirs.

- [ ] **Operator-level optimizations, ported from XPlace** (was **#6**, opened 2026-07-29, demoted
      here 2026-08-17 by the sw_only freeze). XPlace gets ~2× over DREAMPlace almost entirely from
      operator-level restructuring of the same ePlace math we already implement — not a new
      algorithm. These are **pure speed on sw_only and change no HPWL**, which is why the freeze
      demotes rather than closes them. Three remaining techniques (the fourth was measured and
      folded into #20 — the bottleneck is **~76 MB/iter of host DMA, ~8× the launch overhead**, so
      the payoff is keeping matrices device-side, exactly what Stage 5 does):
      **(a) operator combination** — merge WA wirelength, WA gradient and HPWL into one pass; all
      three need the same per-net min/max (check `Partials.cpp::computeHpwlPartials_CPU` for
      redundant recomputation). **(b) operator extraction** — share one cell-density-map build
      between the objective and the overflow metric (`Density.cpp::compute_eField_DCT` vs
      `computeOverflow`). **(c) operator skipping** — XPlace skips the density gradient while
      `|density_grad|/|wirelength_grad| < 0.01` and `iteration < 100`.
      ⚠️ **(c) is the one with a pl_algo deadline** — its own note says *carry it into pl_algo's
      modules from the start*, so it wants deciding during #20 step 6, not after.
- [ ] **Data type precision sweep (float vs double).** The codebase uses a mix (`float` for density
      grids to save bandwidth, `double` elsewhere). Sweep sw_only systematically — wall-clock, HPWL,
      convergence trajectory — under float-only, double-only and the current hybrid on adaptec1 /
      newblue3 / newblue5. Question: does precision limit solution *quality*, or only the speed to
      solution?
- [ ] **Smoothing schedule (√2 footprint inflation ramped down over the run).** The convergence metric
      is smoothed, which lets GP stop at smoothed overflow ~0.07 while *exact* overflow is still
      0.12–0.28 on the hard macro-heavy designs. Start with heavy smoothing (good early spreading) and
      reduce toward sharp as GP progresses, so late convergence tracks true physical density instead of
      declaring victory early. **Don't implement yet** — first diagnose *why* those designs won't
      spread.
- [ ] **Respect the row site model, minimize overhang, measure the difference** (opened 2026-08-24,
      tabled from #3's open "no per-row site model" item). sw_only builds `Grid` from one die
      rectangle and `enforceDieBoundaries` clamps every movable cell to `[0, die−w]` regardless of
      which row it lands in. But 11 of 16 MMS designs are ragged — the `.scl` gives each `CoreRow`
      its own `SubrowOrigin`/`NumSites`, so the core is a staircase and "inside the die bbox" is
      weaker than "inside a row". `tools/check_row_spans.py` measures the gap: adaptec3 **315** cells
      outside their row span (worst overhang 4122 DBU ≈ 343 row-heights), newblue4 25, adaptec5 23.
      **Idea:** carry the per-row legal span into GP — bound `enforceDieBoundaries` per row, or add a
      blockage/whitespace penalty over the notch — so cells stop parking where no site exists and the
      legalizer isn't handed 343-row-height forced moves.
      **Why this is cleanly measurable:** our LG+DP is XPlace's OWN pipeline (`main.py
      --global_placement False --given_solution`, then its greedy+abacus legalizer and the external
      NTUplace3 for bookshelf), identical for every scored placement. A row-aware GP arm vs the
      current arm, both fed through that same legalizer, isolates the effect on post-DP HPWL with
      nothing else moving. Report overhang counts (this script) AND the DP-frame HPWL ratio.
      ⚠️ **Not yet shown to be a divergence.** XPlace's GP makes the SAME rectangular assumption, so
      it likely produces the same notch cells — meaning a fix could *beat* XPlace rather than match
      it, or be absorbed entirely by the shared legalizer. **First** run XPlace on a ragged design and
      measure ITS overhang; if it's comparable, the win (if any) is ours to take, not a faithfulness
      gap. Also **not a lead for #35** — adaptec3, the worst offender (315), is td=1.0 and flat; the
      regression tracks (1−td), not raggedness.

---

# Topics for investigation

Open questions worth measuring, not yet scoped into a task.

- [x] **DECIDED 2026-08-17 by #32's 7a — and decided the OTHER WAY: everything now evaluates at
      the lookahead `v_k`, so the deposit stays where it was.** This entry proposed moving the
      density deposit *back* to the committed `node_pos` to match HPWL. 7a instead moved HPWL and
      the best-solution snapshot *forward* onto the probe, which is what XPlace does (`p` IS `v_k`,
      `nesterov_optimizer.py:71`; `evaluator_fn` measures both metrics there). The inconsistency the
      entry was written to fix — density at v, HPWL at u — is gone, resolved in the direction
      opposite to the one proposed here. **No A/B needed; do not re-open without re-reading #32.**
      <details><summary>original proposal, verbatim</summary>

      - [ ] **Deposit the density footprint at the committed `node_pos`, not the probe/lookahead
      `probe_pos`** (Mark, 2026-08-15). `computeNodeFootprint` (`Grid.cpp:38-39`) deposits at
      `getProbeX()/getProbeY()` — the Nesterov lookahead `v_k`, not the committed position `u`. XPlace
      snapshots and evaluates the LOOKAHEAD too (`mov_node_pos` IS `v_k`, `nesterov_optimizer.py:71`),
      so probe-deposit is arguably the faithful choice — but it means our density/overflow and our HPWL
      describe *different* positions (HPWL is at `node_pos`). This is the u-vs-v question flagged under
      #24 ("XPlace snapshots the LOOKAHEAD, we snapshot the COMMITTED"). Measure: deposit at `node_pos`
      instead and A/B the GP trajectory + post-DP HPWL. Discovered while cracking #31's overflow puzzle
      — the naive reference (committed `.def` positions) matched our overflow exactly, so on the
      *shipped* placement probe and committed have already converged; the interesting effect is
      mid-run, where they differ. Cheap to try: one-line change in `computeNodeFootprint`, then
      `make test-regress` (expect it to CHANGE — needs a deliberate A/B, not a pass/fail).
      </details>
