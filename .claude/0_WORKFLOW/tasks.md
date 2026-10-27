# Tasks

**Active Task (2026-10-20): #45 step 6 / #48 — `bin_density_CU`.** One cell per cycle is in: 4×4 banks +
forwarding, II = 1, bit-exact to sw_flat on 44/44, forwarding proven in RTL co-sim; F per design from the
total-area bound ([[REPORT_48_bin_density_fixed_point_20261019.md]]). Next: lanes (row-band ownership,
8 lanes = no extra URAM) — Mark's call. XPlace's own scatter/gather is being read in
[[REPORT_48_xplace_density_scatter_gather_20261020.md]] (separate session). Done alongside (2026-10-20): #45 step 8's
`hpwl_gradient_CU` hardware baseline, **PASS bit-exact on the VCK5000 at 200 MHz** ([[REPORT_45_hpwl_gradient_hw_flow_20261019.md]]).
#46 closed 2026-10-19 (re-scored, new goldens archived; history.md).

Open work, one section per task. **Status lives here; evidence lives in a
report.** Don't reuse task numbers, find the highest number and add one.

## ⚠️ sw_only functionality is FROZEN — called 2026-08-17, EFFECTIVE 2026-08-18 (Mark)

**Current golden (2026-10-19, `077b4c4`, after the authorized #46 and #49): ISPD 1.0095 / 1.0116,
MMS 1.0136 / 1.0158 (mms/adaptec2 noise), in `GOLDEN_sw_only_frozen_20261019/` and
`GOLDEN_mms_sw_only_frozen_20261019/`.** The freeze-time numbers below stand as the record. Meow.

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
exist once, in `host/src/common/`. **Since 2026-10-22 a plain `git clone` builds the host with no
bootstrap:** tabulate's headers are vendored in `third_party/tabulate` (f932c98), and Limbo is gone
entirely — submodule, `bootstrap_third_party.sh`, `third_party/patches`. `make test-parser` now
checks the native reader against `test/parser/limbo_dump_hashes.txt`, Limbo's dumps for all 86
inputs frozen as sha256 the same day (last native == Limbo run: all 86 identical, log in
`.claude/2_ARTIFACTS/limbo_freeze_20261022/`). To diff a failure against Limbo, check out f932c98.
The only submodule left is `vck5000/aie/lib/Vitis_Libraries`, used by legacy `AIE=markv1` alone
(pl_algo takes the FFT from the Vitis install); Mark: decide when merging Geert's AIE code.

<details><summary>Superseded 2026-10-22</summary>

Limbo and tabulate are real submodules, zero `.a` tracked,
`tools/bootstrap_third_party.sh` added; Boost reconciled (two Boosts on this box — everything
actually compiles against 1.80; `-DBoost_NO_BOOST_CMAKE=ON` is required, not cosmetic).
</details>

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
`5867600` on `pl_algo` — see [[HANDOFF_39_readability_refactor_session_20260901.md]] for the
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

---

## #44 — Control loop on the host vs on the PL: build both, measure which is faster (opened 2026-10-05, Mark)

**Why (Mark):** learn from XPlace rather than assume the PL is the right home for the control
logic. XPlace keeps **all** data on the GPU (one upload, `database.py:463`) but runs the γ/λ
schedule, best-solution bookkeeping and stop test in **Python on the CPU**. The cost per GP iteration
is ~3–4 **scalar** readbacks (each one stalls the host until the GPU drains), not a bulk transfer:
HPWL + overflow `.item()` in `ParamScheduler.push` (`param_scheduler.py:20`), a `weighted_weight > 0.5`
gate (`:285`), plus one per backtrack try (`nesterov_optimizer.py:108`, up to 10). (Upstream count.
Our local `PRECOND_TRACE` instrumentation in that file adds 2 more.) #20 step 6 instead moved the
schedule onto the PL (`param_scheduler.hpp`, `bb_reduce.hpp`, `resident_place`) without measuring
that alternative.

**The two arms.** Same datapath and same device-resident matrices in both. The ~76 MB/iter
host-DMA finding (Improvements, ex-#6) still applies, so neither arm moves positions or gradients
per iteration.
- **A — resident (PL control):** `MODE_PLACE` / `resident_place` as composed in #20 step 6. One
  kernel launch for N iterations; the host only sees the final result.
- **B — host control (XPlace pattern):** one kernel launch per iteration. The kernel returns HPWL,
  overflow, the BB norms and κ through a few scalar registers; the host runs the γ/λ schedule and
  convergence test (as `host/src/pl_algo/src/Driver.cpp` does today) and writes back λ, γ and the step.
  It uses fewer PL resources (no `param_scheduler`/`SchedState`) and the policy stays editable
  without a rebuild.

**What decides it:** per-iteration time is `t_datapath + t_control`. Arm B pays an XRT
launch + wait round trip per iteration; arm A pays the PL area and latency of the control modules
(and they compete for the URAM that #41's resident loop needs). If the round trip is small next to
`t_datapath`, B costs almost nothing and is simpler. (No per-iteration `t_datapath` on real HW yet;
#40/#41 timing closure gates that.)
**Measure on real hardware** (build server). sw_emu timing means nothing here.

**Steps.**
1. Measure the bare XRT round-trip cost on the VCK5000: launch, wait, read a few scalars, with an
   empty kernel. This alone may decide the question before either arm is finished.
2. Arm B: per-iteration `MODE` with a scalar-out/scalar-in ABI. Same trajectory as A, bit-exact
   (both run the same schedule, so `sched_verify` + the #20 trace are the golden).
3. A/B on ≥3 designs (a small ISPD2005, a chunked design, an MMS one): wall-clock per iteration,
   end-to-end time, PL resources. Report → `.claude/1_REVIEW/reports/`.

**Falsifier:** if step 1's round trip is ≥10% of `t_datapath` on the smallest design, arm A
wins outright and B is shelved. Not started.

## #45 — Integration: one parse, slot order everywhere, `hpwl_gradient_CU`, in `/AIEplace/pl` (opened 2026-10-06, Mark)

**Why:** #41 (chunked gradient), #42 (partitioning) and #43 (native parser) each verify on their own
and none is connected to a placer. Two node orders (pl_algo's `Packer.cpp` vs beat_packer's
`node_slot`), two data models from two reads, and netless nodes / fillers with no slot keep them
apart. **Mark's decisions (2026-10-06):** slot order everywhere; one parse feeds the software and
hardware routes; one chunk-capable `hpwl_gradient_CU` (from `hpwl_gradient_computer_v2`);
`hpwl_computer*` retired as stepping stones; both partitioners behind `partition_fm_passes`; start
fresh in `/AIEplace/pl`, `/vck5000` deleted once moved; timing after integration.

**State:** steps 1–5b done (5 bit-identical to sw_only; 5b: sw_flat's `pl+Bprime` config computes the
gradient + HPWL bit-exact to `hpwl_gradient_CU`, drift within noise). **Next: step 6**, density path
in slot order, each module vs sw_flat `pl+Bprime`. **Step 8 brought forward (2026-10-19, Mark: (a) 250 MHz,
emulation first, a baseline to improve on):** `make -C pl xclbin TARGET=...` plus the XRT host
`make -C host/sw_flat hpwl-gradient-xrt`. **sw_emu 4/4 and hw_emu 3/3 (synthetic with 16 chunks, mgc_pci_bridge32_b,
mgc_fft_a) PASS bit-exact** vs the g++ CU. **hw: 250 MHz failed timing** (WNS −0.729 ns, the borrowed-copy
exchange's URAM read); **200 MHz met timing** (WNS +0.031 ns; URAM 256, LUT 122 K) and built an xclbin at `b3d0e87`.
**Card run 2026-10-20 on `vck` (booked): PASS bit-exact** on synthetic, mgc_fft_a, mms/adaptec1 and
bigblue4 (3 chunks; ~8.4 ms wall-clock per axis, WL gradient only). Baseline done; clock, profiling and DDR mapping
are the improvements. → [[REPORT_45_hpwl_gradient_hw_flow_20261019.md]].
**Clock (2026-10-27): 230 MHz, PASS bit-exact on the card.**
- **230 MHz met timing** (WNS +0.001 ns, WHS +0.010; 220 MHz +0.005) and **passed on the VCK5000** on synthetic,
  fft_a, mms/adaptec1 and bigblue4 (7.9 ms/axis, from 8.4). `KERNEL_FREQ_HZ` = 230 MHz. xclbin: `build`
  `~/AIEplace_timing/expF/pl/build/run230/` (md5 `f725da2a…`), also `vck` `~/AIEplace/pl/build/hw/`. F = HLS
  scheduled at 400 MHz (new `HLS_FREQ_HZ`) plus fold/merge/refresh each on its own adder.
- **On `vck` set `XRT_DEVICE_BDF=0000:c1:00.1`:** two U55Cs now enumerate first, so device 0 is not a VCK5000.
- **250 MHz is not closed:** best WNS −0.178 ns. What remains: `send_ops` (goes with B4, #47), URAM input
  fan-out from `fold_macros`/`load_slots`, and `wa_sums` DSP inputs. Mark's call: `pos_URAM` `latency=3`
  would need `fold_macros` at II=2.
→ [[REPORT_45_hpwl_gradient_timing_20261027.md]]
**Macro beats (2026-10-27, Mark): refresh and fold at 16 pins per cycle.** Golden-first; bit-exact golden vs CU
6/6, tier 1, packer MB1–MB3 on all 16 MMS designs, sw_emu 4/4, hw_emu 2/2 (synthetic with macros, fft_a). Macro-pin cycles ~11–14× fewer
on MMS (adaptec1 16,265 → 1,203 per pass); fold distance 8 took `fold_macros` from −1.80 to −0.01 ns estimated.
**The kernel interface changed** (`MacroBeat` + new `macro_offsets` argument): the 230 MHz xclbin above no
longer matches pl_algo's host. Next: a routed 230/250 MHz build, card run. → [[REPORT_45_macro_beats_20261027.md]]

Done 2026-10-06 as low-hanging fruit: gradient `max_rel` 1e-3
(`vck5000/test/wa_gradient_golden.hpp`; newblue3 / mms/bigblue4 / newblue7 now pass, rel_rms still
1e-5-bounded); `make test` 21/21 PASS.
- [x] **Decisions, second round (Mark, 2026-10-06):** D1 mailbox A now / C later / B fallback; D2
      `_CU` suffix (CLAUDE.md rule amended); D3 `sw_flat` built new, sw_only's high-level structure,
      likely the final host; D4 hardware in `pl/`, all software in `host/sw_flat/`; D5 #20 / #40
      closed; D6 v1 scope, control on the CPU (#44 arm B); D7 `origin/geert` stays separate. **No
      caching** of encoded designs. Netless movable nodes measured: 0 on 29/44 designs, max 0.58%
      (mgc_superblue14); they and the fillers form a density-only slot tail.
- [x] **Step 1: skeleton (2026-10-06).** `pl/` (README, Makefile, `src/format/pin_record.hpp` copied
      from beat_packer @ 41f2193, `test/` runner over one `HARNESSES` list) and `host/sw_flat/README.md`.
      `pin_record_test`: 13 checks pass; 4/4 header mutants make `make -C pl test` exit non-zero
      (`.claude/2_ARTIFACTS/scratch_45/pl_runner_mutants.sh`).
- [x] **Step 2: `hpwl_gradient_CU` (2026-10-06).** Also `reduce_window` → `balanced_tree`,
      `TreeOutputs` removed, calls annotated with their files (Mark). **Done 2026-10-19:** `PinBeat.net_max/net_min`
      de-duplicated to one bbox per net (8, not 16 lanes); stage B maps lane → net through a constexpr
      ROM (`LANE_NET`, wa_sums.hpp). A runtime `i / degree` there instead cost +34 K LUT and 7 pipeline
      stages. C-synth vs HEAD: A→B FIFO 2132 → 1620 bits, LUT 278,546 → 265,799, FF ≈ (300 K), II=1 and
      depth unchanged; `make -C pl test` and `check-record-stream` 6/6 bit-exact. Meow.
      Step 2 detail:
      - **2a restructure** (`8e94220`): 8 bring_up headers → `pl/src/modules/hpwl_gradient_CU.hpp` +
        `hpwl_gradient/` (7 files); all 44 definitions character-identical, legacy vs new outputs
        byte-identical on 5 designs (synthetic 6/38/1 chunks, adaptec1, mms/newblue3,
        mgc_superblue12, mms/bigblue4). Packer → `host/sw_flat/packer/`; `IGNORE_NET_DEGREE` = 96.
      - **2b C-synth**: identical to legacy at HEAD (3.617 ns est., LUT 265,076, every loop II=1).
      - **2c mailbox burst fix (option A)**: padding segments skipped in the outer loop.
        `send_parcel` / `collect_parcel` mailbox accesses now burst (depth 144 → 7 / 6), II=1,
        3.617 ns, LUT +49. Byte-identical outputs; 2/2 mutants caught. RTL co-sim
        (`pl/synth/hpwl_gradient_CU/cosim`, capacity 160): hazard 4 PASS with 3 same-slot pairs
        straddling skipped padding; hazard 1 negative control FAILS in RTL (passes in C).
        ⚠️ The legacy co-sim's capacity 256 has **no** padding, so that path was never co-simulated before.
      - **44-design sweep** (`hpwl_gradient_CU_test` real mode, on-chip capacity): 44/44 pass,
        rel_rms ≤ 8.2e-7, worst max_rel 4.18e-4 (mms/newblue3), HPWL bit-exact on all 88 axes; the
        8 chunked designs at K=2–3 (`.claude/2_ARTIFACTS/scratch_45/sweep44.txt`).
      - **Renames (Mark):** stage `pin_bbox` → `hpwl_CU`; `beat_count`/`span_(beat_)count` →
        `degree_group_end`/`span_group_end` (cumulative stream positions, not counts),
        `group_counts` → `group_ends`, `GROUP_COUNTS` → `NUM_GROUP_ENDS` — device and packer alike.
        Byte-identical on the 5 designs.
      - **Padding coverage strengthened:** `fixture::straddling_pairs()` (collect entries of one
        slot < HAZARD_DISTANCE apart once padding is removed) shared by tier 1 and the co-sim bench.
        Tier 1 now runs the bare cut at capacity 704 (23 padding entries, 16 pairs, need ≥ 8); was
        768 with 1. Both padding mutants caught, the lane-list one on both axes. `make -C pl test` 8 s.
      - Notes for later: (a) FM at tiny capacities is slow — 92 s to encode the 3 K-cell synthetic
        design at capacity 384 (bare cut 0.05 s); irrelevant at 1 M slots, but a pathology. (b) The
        bare cut at capacity ≤ 576 on that design reports "64 chunks, 50 external" — looks like a
        fallback, not a partition. (c) Capacities not a multiple of 32 fail `check_chunked` with "no
        chunk count fits". (d) Co-sim C-run numbers come from Vitis's compiler and differ from g++
        (axis 1 rel_rms 5.191e-7 vs 4.924e-7, same code): compare like with like.
- [x] **Step 3: `FlatDatabase` (2026-10-07; was `FlatSink`, renamed 2026-10-09, Mark).** Mark: COVER movable (DataBase's rule); packer macro rule =
      sw_only's `tagMovableMacros`. `host/sw_flat/`: `flat_design.hpp`, `flat_database.{hpp,cpp}`,
      `flat_netlist.hpp` (`tag_movable_macros`, `to_netlist`). `make -C host/sw_flat check-database`:
      **47/47** (44 + 3 fixtures): bit-exact vs DataBase, macro tags = sw_only's, legacy reader
      differences all accounted for; 16/16 mutants (`.claude/2_ARTIFACTS/scratch_45/sink_mutants.sh`).
      COVER / PLACED→FIXED / `.pl`-fixed occur on **0** of the 44 (fixtures only, new
      `test/fixtures/pl_fixed`). Packer offsets now DBU from the lower-left (DataBase's), not the
      legacy centre/micron convention. Parse parallelized (Mark, same day): 2–3× faster than
      DataBase (bigblue4 1.18 vs 2.52 s, superblue12 0.54 vs 1.44 s), still bit-exact.
      → [[REPORT_45_flat_sink_20261007.md]]
- [x] **Packer fed by FlatDatabase; its internal parser removed (2026-10-09, Mark).**
      `host/sw_flat/export_netlist` writes the packer's `Netlist` file (`write_netlist` /
      `read_netlist` in `beat_packer.hpp`, round-trip asserted); `make -C host/sw_flat netlists` writes
      all 44 in 21 s. `pl/test` (`--netlist FILE`) and the #47 models read them, still header-only.
      Re-measured on the new input: `make -C pl test` PASS; real-mode sweep **44/44**, HPWL bit-exact
      on 88/88 axes, worst max_rel 4.83e-4 (bound 1e-3), `.claude/2_ARTIFACTS/scratch_45/sweep44_netlists.txt`;
      **chunk counts identical on all 44**, externals change only on bigblue3 (ispd2005 51,456 →
      50,398; mms 56,031 → 54,687). #47 model (`chunk_model_before_after_netlist.md` there): bigblue3
      `today` −14–15% (981 K → 837 K cycles; mms 1.07 M → 921 K): the legacy "macros" are cells now, so
      their macro-pin fold goes. Other 6 designs within 60 cycles; #47's bigblue4 figures stand. The
      model's `make check` (vs legacy `chunk_profile`) is retired: different inputs by decision;
      last PASS 8/8 at 9905557.
- [x] **Step 4: whole-design slot layout (2026-10-09).** `host/sw_flat/slot_layout.hpp`: global slot
      = the chunks' slots back to back, then a filler-only tail. `fillers.hpp` ports `addFillers`.
      `make -C host/sw_flat check-layout` **44/44**: fillers (count, size, effective td) bit-equal
      to DataBase; node↔slot bijection; externals are copies in the tail rows; 5–7 self-test
      corruptions per design, all caught. Chunking unchanged (= HEAD's encode, post-C6).
      ⚠️ **Plan deviation:** netless movable nodes stay in their chunks, not the tail. The packer
      already slots every movable node (the plan's "NO_BANK" premise is stale); cost ≤ 0.58% URAM.
      For step 6: the filler tail ≈ the chunks in size (bigblue4 2.89 M vs 2.62 M slots).
      → [[REPORT_45_slot_layout_20261009.md]], `.claude/2_ARTIFACTS/scratch_45/layout44.txt`
- [x] **Step 5: `sw_flat_place`, CPU phase-1 GP (D6 scope) (2026-10-09).** `host/sw_flat/placer/`,
      one file per sw_only file. **Bit-identical to sw_only** (Mark's call over D3's tolerance A/B):
      the state is in slot order, but the sums run in sw_only's orders. `make -C host/sw_flat
      check-placer` diffs `schedule_trace.csv` as text plus the stop and final report: PASS on
      mgc_fft_a, mgc_pci_bridge32_b, adaptec1, mms/adaptec1 (phase 1, 62 movable macros), bigblue4
      (3 chunks, 759 iterations). 2–3× faster than sw_only (bigblue4 369 s vs 1102 s). A 2-ULP `init_gamma` change diverges at row 1.
      Not done: DEF output for `dse.py` (redundant while bit-identical), final-position hash.
      → [[REPORT_45_sw_flat_placer_20261009.md]]
- [x] **Step 5b part 2: the rest of the PL's arithmetic (2026-10-16).** `[sw_flat] wa_sums = "tree"`,
      `grad_merge = "stream"`, `hpwl = "fixed"` (`placer/record_stream.cpp`; need `ignore_net_degree =
      96`; off by default, check-placer PASS). With `wa_exp = "lut"` the gradient and both int64 HPWLs
      equal `hpwl_gradient_CU`'s **bit for bit on 6/6** (fft_a, pci_bridge32_b, adaptec1, mms/adaptec1,
      bigblue4 and mms/bigblue4 with 3 chunks, tail merges and folds): `make -C host/sw_flat
      check-record-stream`, ~1.5 min. 7 mutants caught (`scratch_45/record_stream_mutants.sh`). The
      combiner was already the CU's; what changed is the sum order and the merge order. Drift: 19/25
      inside noise. bigblue4's outliers are ≤ 0.012%; mms/adaptec1's `pl` −0.49% is a best-solution
      flip (primary @641 vs aux @657), with the trajectory within 1e-3. **Step 6's golden = `pl+Bprime`.**
      ⚠️ Found: sw_only's float HPWL metric reads **0.39% low on bigblue4** (float accumulation over
      2.2 M nets; 1e-6 on the small designs; int64 exact to 2e-8). It touches sw_only's schedule and
      printed GP HPWL, not the scored goldens. **Fixed the same day by #49** (double sum, Mark;
      history.md).
      → [[REPORT_45_pl_arithmetic_20261016.md]]
- [x] **Step 5b part 1: slot-order sums** — flip each order-sensitive sum to slot order and measure the drift
      against the bit-identical baseline (2026-10-14). `[sw_flat] slot_order =
      ["deposit", "reductions"]` and `wa_exp = "lut"` (the PL's exp table), all off by default
      (check-placer still PASS). `test/sum_order_drift.py`: **28/30 variant runs inside the 2-ULP
      noise band** on fft_a, pci_bridge32_b, adaptec1, mms/adaptec1, bigblue4 (5/5, ±0.011%); the 2
      outliers (mms/adaptec1) ≤ 0.1% HPWL. In-run drift 1e-7..1e-5 for 200-300 iterations, ~1e-4 in
      the endgame; larger final differences come from the stop firing a few iterations off.
      **Slot order hides no semantic difference.** The table saves 12-21% of the gradient but only ~2-3% of
      software run time (gradient = 15% of an iteration; deposit + DCT 43%, metrics 24%).
      → [[REPORT_45_slot_order_sums_20261014.md]]
- [ ] Steps 6–9 (density path in slot order → compose → hardware → retire `/vck5000`), each with its
      verification, in the plan.
- [ ] **sw_flat DEF / `.pl` writer, so `dse.py` can score it legal-vs-legal.** Step 5 called it
      redundant while sw_flat was bit-identical to sw_only; the `pl+Bprime` golden (5b) no longer is,
      so this is now needed before any quality claim for it, and before sw_flat replaces sw_only as
      the golden (D3). It also enables the final-position hash check step 5 skipped. Meow.
- [ ] FlatDesign has no PLACED flag (step 5 report): a no-op on all 44 (none is PLACED), a gap if
      such a design arrives. Meow.
- Carried in: #41's timing / NMU check / URAM-resident loop; #42's widening choice, now contested by
  #47 (producer-side duplication; touches steps 4 and 6).

→ [[PLAN_45_integration_20261006.md]]

## #47 — Mailbox redesign: exchange external slots where each chunk is already streamed (opened 2026-10-06, Mark)

**Why:** the external-slot sets are static, but the chunked gradient spends two whole passes on
them: a send pass (position reload + send) and a fold pass (gradient reload + collect + drain).
**Today** (FM 1 pass + burst fix, cycle model): the mailbox is 14–43% of a chunked evaluation, and
the passes' reloads another 19–32%. bigblue4: 2.49 M cycles per axis, mailbox 43%, beat loop 25%.
(The 27–68% quoted earlier on 2026-10-06 was the pre-FM baseline partition.)

**Idea:** `iteration_update` (positions are device-resident, #45 D6) streams every chunk's slots in
order. It emits shared slots straight into the consumers' parcels (send) and merge-adds the returned
gradient parcels, which are sorted by owner slot (collect). Optionally, a contiguous external tail per
chunk turns receive into part of load and return into part of drain. Estimate for bigblue4: 2.49 M →
1.48 M (receive/return kept) or 0.95 M (tail), against 1.49 M for D1-B widening.

**State (2026-10-26): B1-B3 + C8 built; the pass removal (B4 = A) is NOT built.** The CU still runs
both extra passes (`send_borrowed_pass`, `merge_borrowed_pass`); A, which deletes them, exists only as
the M2 golden and the M0 model (bigblue4 1.57 M → 0.98 M cycles per axis). Device side (send + merge in
`iteration_update`) waits on #45 step 6. **Buildable now:** M1 re-run on the as-built C7/C8 layout, A's
beat-level op format, a beat-level golden + standalone HLS send/merge blocks (early M5). "tail" was
renamed "borrowed" on this date (borrower side; "shared" stays the owner side).
→ [[PLAN_47_borrowed_copies_status_20261026.md]]. Briefing: [[EXPLAINER_47_tail_exchange_deck_20261009.md]].
<details><summary>superseded headline (2026-10-09)</summary>

**State (2026-10-09): B1-B3 built and verified; B4 needs a decision** (it needs `iteration_update`
in slot order, #45 step 6).
</details>
- [x] **M0 (2026-10-07):** `host/sw_flat/model/` (`make check`: TODAY equals the legacy model on 8/8).
      As a fraction of today's cycles, FUSED is 58–65%, FUSED_TAIL 28–56% (1 M: 8 designs; 512 K:
      17). bigblue4 at 1 M: 2.49 M → 1.52 M → 0.99 M. Merge: 2 adders/lane → 0 stalls. Open: shared
      macro pins (≤ 6.8 K entries) need a refresh in R1. The tail's alignment is not charged (M1).
- [x] **M1 (2026-10-07, measured; layout code not written):** `host/sw_flat/model/borrowed_layout.cpp` (was `tail_layout.cpp`).
      Per-bank tail rows with today's colouring: tail/E 1.002–1.029, every chunk within capacity
      (8 designs at 1 M, 17 at 512 K). The update needs an assembly buffer per consumer stream (≤ 162
      rows of 32 floats) taking 4 writes/cycle (stalls ≤ 0.34%); the merge needs the mirror gather.
      Colouring hints measured (`ed888f9`) and dropped: no buffer gain. Next: M5 compares that
      hardware to widening just receive/return.
- [x] **B1 (2026-10-09): rule C6, macro pins never external.** The macro is the external; its
      pins are local slots, refreshed after receive and folded in the compute pass. Packer, FM model,
      `check_chunked` (now also asserts model = builder) and `hpwl_gradient_CU`. 8/8 chunked designs
      pass tier 1 real mode (`.claude/2_ARTIFACTS/mailbox_47/c6_sweep_20261009.txt`), 4/4 mutants.
      Resolves the macro-fold decision and M0's shared-macro-pin gap.
- [x] **B2 (2026-10-09): tail rows (C7).** Externals in tail rows after the owned rows, each
      owner block in the owner's slot order; device-transparent. Chunk counts unchanged on all 44 at
      1 M and 512 K; single-chunk designs byte-identical; 4/4 mutants.
- [x] **B3 (2026-10-09): the tail exchange replaces the mailbox.** One tail block per owner on fresh
      rows; `TailStream` (renamed `TailBlockDesc` 2026-10-16) op lists replayed through a 32 x 256 row buffer (`borrowed_copies.hpp`, was `tail_exchange.hpp`);
      compute pass has no receive / return. `check_chunked` replays every block on node ids. Tier 1,
      8/8 real designs (blocks up to 3,107 rows: the buffer wraps), 7/7 mutants, C-synth II=1
      (3.482 ns est.), **RTL co-sim PASS** (hazard-1 control FAILS). bigblue4 at 1 M: 2.49 M ->
      1.93 M cycles per axis. Synthetic test capacities moved to 4096 / 2560 (per-owner block
      rounding).
      **Re-verified 2026-10-16 at `48fb9fa`** (after the owner/borrower refactor `83e3719`, which also
      moved `load_pos_zero_grad` and wrapped `send_tails_pass`, now `send_borrowed_pass`, in `if (chunked)`): on the build server,
      C-synth 24/24 loops II=1, Fmax est. 287.2 MHz (unchanged); RTL co-sim capacity 1536 hazard 4
      PASS, hazard 1 FAILS (rel_rms ~2e2). Logs: build:`~/AIEplace/pl/synth/hpwl_gradient_CU/`.
- [ ] **B4 = A, stream fusion (Mark, 2026-10-09).** **M2 DONE:** `host/sw_flat/borrowed_fusion.hpp`
      (`fused_send` / `fused_merge`: one ascending pass per owner, a cursor per consumer, merge in
      ascending consumer order). Bit-identical to B3's device exchange: `pl/test/borrowed_copies_test`
      (in `make -C pl test`), synthetic 3 + 30 chunks, and **8/8 chunked real designs at 1 M, 0
      differences**; consumer-order and dropped-entry mutants caught.
      `.claude/2_ARTIFACTS/mailbox_47/m2_tail_fusion_20261009.txt`. **Next: M3** (`hpwl_gradient_CU`
      loses send + fold; `iteration_update` gains them). Blocked on #45 step 6's slot-order
      `iteration_update`.
      <details><summary>the options as put to Mark</summary>

      A (stream fusion in `iteration_update`, 0.98 M, waits for #45 steps 5-6) or C (widen B3's loops
      now, ~1.5 M, interim). Recommended: wait and build A.
      </details>
- [x] **C8 shared-first owned rows (2026-10-16, Mark): an interim before B4.** Only 1.4–17% of an
      owner's slots are borrowed, but they were scattered over 13–69% of its owned beats. Rows inside a
      bank are free (`assign_node_slots`; colouring fixes only the bank), so each bank's shared nodes
      now take its lowest rows. `ChunkDesc.first_private_slot` marks the end of that prefix (≈ shared/16
      beats; banks balance within 0.4 pp), and the send reload and merge reload/drain cover only it.
      Cycle model, B3 → C8: bigblue4 1.93 M → 1.57 M (−18%), the other 7 −17..−31%; chunk counts,
      externals, max_slots unchanged. `check_chunked` asserts C8; borrowed_copies_test [5] mutant (prefix
      a row short) bites. Tier 1 PASS, 8/8 real chunked designs PASS. **C-synth at `a98b3fe` (build
      server, in a since-removed worktree):** every pipelined loop II=1, Fmax est.
      287.2 MHz, both unchanged; +222 FF / +48 LUT (ChunkDesc 40 → 44 bytes). The top-level −1.05 ns
      "Timing" flag is identical at B3's `48fb9fa`. **RTL co-sim (capacity 1536, 16 chunks): hazard 4
      PASS** (rel_rms 4.3e-7 / 3.4e-7), **hazard 1 FAILS as required** (rel_rms 2.5e2 / 1.9e2; its C-sim
      pre-run passes, as it should). `.claude/2_ARTIFACTS/borrowed_47/` (census,
      `m0_1M_c8_20261016.txt`, `c8_real_sweep_20261016.txt`, and `c8_synth_20261016/`: csynth.rpt +
      both co-sim logs).
- [x] **R = resident shared region: CLOSED 2026-10-19 (Mark), measured, not chosen; B4 stays the
      plan.** It does not scale to K=6, and neither does a sliding window (below); only per-chunk
      tails do. Kept: `Encoded::shared_bank`, `chunk_model` res/res+m, `window_model`.
      (Opened 2026-10-16 as Mark's alternative to B4.) Every shared node gets one slot, the same in every chunk, in rows that stay on chip
      all iteration: no tails, no send, no merge, and `iteration_update` unchanged. Census at 1 M, 8/8
      chunked designs, today's partition (`borrowed_census.cpp` + the new `Encoded::shared_bank`
      in `assign_banks`, a no-op when empty; tier 1 PASS):
      **global bank colouring costs nothing:** 0 collisions, beats within ±7 of today in every chunk
      (control: re-encode without shared banks = today, asserted). **No dedup:** copies/unique 1.000–1.021 (≤ 2%
      of shared nodes have 2 borrowers). **Slots:** K=2 +0.6–1.0%; K=3 +13–16.5% on the chunks
      with small tails today, since every chunk now holds the whole region. Worst bigblue4 chunk 0 at
      1,035,072 (98.7% of 1 M): fits, K unchanged, but only 13 K slots of headroom.
      **Cycle model (2026-10-16, `chunk_model` res / res+m):** layout per chunk = private | region | fixed
      (Mark); the region starts at P_max (the largest chunk's private rows) so it sits at the same rows
      in every chunk. res+m also puts every macro and macro pin in the region: refresh once, fold once.
      % of today (C8), fused (B4) / res / res+m: bigblue4 62.6 / 58.5 / 58.4; mms/bigblue4 64.5 / 60.5 /
      59.6; mms/newblue7 75.3 / 72.6 / 71.5; the other 5 1–3 pp better than fused. **R beats B4
      everywhere** (today/fused rows byte-identical to the C8 run). Fit: bigblue4 res 1,040,192 (99.2%);
      **mms/newblue7 res+m 1,068,352 DOES NOT FIT** (res fits at 1,034,848). Not done: a partition that
      budgets the region; add order becomes chunk-ascending (not bit-identical to B3).
      `.claude/2_ARTIFACTS/borrowed_47/{resident_1M_20261016.txt, m0_1M_resident_20261016.txt}`.
      **Direction (Mark, 2026-10-16):** static regions (shared, maybe macros, maybe fixed) are the
      strategy so long as chunks stay large. Next: (1) measure each static region's share of the 1 M
      slots per design; (2) packer must raise K until private + regions fit (region-aware FM target),
      so a fit is guaranteed, not lucky. Fixed pins stay per chunk (Mark: not a static region).
      **(1) measured (2026-10-16, `chunk_model` region breakdown; today's FM partition):** region as % of
      capacity. **1 M:** shared region 3–8% at K=2, 17.5–26.4% at K=3 (bigblue4 25.2%). Macro region
      adds +2.0–5.5 pp, almost all macro pins (21–58 K; macros themselves ≤ 200), and buys only
      0.1–1.1 pp of cycles. **512 K (stand-in for a 2x bigger design at 1 M):** K=2 2–16%, K=3 8–34%;
      K=6 bigblue4 **101.7%**, mms/bigblue4 103.8%, mms/newblue7 83.7% -- the region alone fills the
      capacity, and raising K only grows it, so (2) cannot rescue these. 5 of 17 chunked designs do not
      fit under res. The region is ~24% of the design at K=6 (bigblue4 527 K of 2.17 M): R's ceiling
      is near K=3-4. `.claude/2_ARTIFACTS/borrowed_47/m0_{1M,512K}_regions_20261016.txt`.
      **Sliding window measured (2026-10-19, `model/window_model`):** a shared node resident only from
      its first to its last user chunk; size = peak live set (node counts, no bank rounding). **K ≤ 3:
      window = global region exactly** (every interval covers the middle chunk). **K=6 (512 K):
      0.47–0.58x global but still 39–58% of capacity**, 1.1–1.2x what today's chunk holds (owned
      shared + tail); bigblue4, mms/bigblue4, mms/newblue7 do not fit (632–710 K vs 524 K). Today's
      chunk order is already the best of all 720. Verdict: the window does not scale either; only
      per-chunk tails (B3/C8 today, B4 fused) do. `window_{1M,512K}_20261019.txt` (same folder).
- [x] **Bring-up shortcut: YES, behind an option (Mark, 2026-10-09). Done:**
      `chunked_device_arrays(..., fill_borrowed = true)` writes real positions into the borrowed rows;
      NaN stays the default. `borrowed_copies_test` [2] holds it equal to the sent image.

→ [[PLAN_47_mailbox_redesign_20261006.md]]

---

# Improvements

Algorithmic ideas beyond faithfulness cleanup — hypotheses, not yet scoped.

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
- [ ] **Bin grids above 1024², by tiling the density map** (opened 2026-10-22, Mark: postponed,
      a generality improvement for huge industrial designs). `bin_density_CU`'s whole 1024² grid fills
      128 URAMs; 2048² does not fit on chip. A tiled pass (deposit one grid tile at a time, slots routed
      or re-streamed per tile) would lift the cap. Why it is worth having: the on-chip grid has no cache,
      so cost per node does not depend on grid size, while XPlace's GPU scatter/gather cost per node rises
      3-5× from 512² to 2048² ([[REPORT_48_xplace_scatter_gather_suite_20261020.md]] T2). Meow.

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

## #48 — On-chip density path: density map, field, force gather (opened 2026-10-14, Mark)

**Why:** XPlace on the GPU spends 54% of bigblue4's kernel time on density scatter + force gather,
3.6% on the DCTs ([[REPORT_xplace_gpu_iteration_breakdown_20261013.md]]). The VCK can keep the
grid on chip and run density alongside the WL gradient. Bar: the paper's 3090, bigblue4 10.0 ms/step
(Table IV), ~12 ms deterministic. v1 grid 1024; 2048 is a planned extension.

**State (2026-10-14):** plan written; `bin_density_CU` skeleton + tier-1 harness landed (`2dad68f`).
Phases: 1 URAM budget, 2 density CU (+ force gather), 2b dataflow, 3 Geert's AIE FFT, 4 evaluation.
- [x] **Phase 2, real-design harness + footprint count (2026-10-14).** `host/sw_flat/density_inputs.hpp`
      builds the module's inputs from a placer (footprint table by (w, h, macro) bits, per-slot index,
      fixed base map, now built once in setup: check-placer still bit-identical).
      `make -C host/sw_flat check-bin-density`: `bin_density_CU` vs sw_flat's slot-order deposit after
      30 iterations, **rho BIT-EQUAL on 44/44 designs** (grid capped at 1024; the 7 grid-2048 designs
      run at 1024); a dropped-slot mutant is caught on every design. Name order differs from slot order
      in hundreds to thousands of bins ([info]), so the check sees order. **Footprint table: 4-105
      entries** (max mms/bigblue4; MMS 40-105, ISPD 4-49), against an assumed 4096: a 128-entry table
      would do. `.claude/2_ARTIFACTS/scratch_48/bin_density_check_44_20261014.txt`
- [x] **Phase 2, overflow map: B' (Mark, 2026-10-14: "XPlace as gospel, URAM is precious").** The
      overflow is reduced from the solver's map after the cells, before the fillers, over an EXACT-size
      fixed base (XPlace's one deposit; no second accumulator). sw_flat `[sw_flat] overflow_map =
      "snapshot"` + `fixed_base = "exact"` (default off, check-placer still bit-identical).
      `make check-overflow-map`: B' == sw_only's metric bit-equal 38/44, ≤2.5e-4 rel (rounding) on 6;
      B alone reads 1.3-6.7% LOW at the stop on bigblue2/3/4, so the exact base is required.
      Trajectories (7 designs): stop ±2 it, HPWL ≤0.03%. Moves the solver away from frozen sw_only,
      toward XPlace (small fixed nodes no longer stretched; 6/44 designs affected).
      → [[REPORT_48_overflow_map_20261015.md]]
- [x] **Phase 2, device side (2026-10-15).** `bin_density_CU` deposits the cells (`num_cell_beats =
      first_filler_slot / 16`; beat-aligned by construction, every chunk's `num_slots` is a multiple
      of BANKS, and the check asserts it), reduces XPlace's overflow_fn over `rho_URAM` (out: one
      float), then deposits the fillers. The sum is a 16-lane tree into 8 rotating partials, NOT
      sw_flat's serial bin order (that would be bins² cycles), so it is held to a tolerance:
      `check-bin-density` [4] **44/44: CU within 3.9e-6 of a double sum of the same map** (bound 2e-5)
      and within 9.0e-4 of `snapshot_overflow` (bound 2e-3); on the 5 MMS designs above 1e-4 the
      double sum is exactly as far from sw_flat, so it is sw_flat's float accumulator that drifts.
      rho still bit-equal 44/44 (now with the exact base). pl tier-1 [5] OVERFLOW vs double, 2.6e-6.
      **C-synth passes** (new `pl/synth/bin_density_CU/`): `reduce` II=1, 65.5K cycles ≈ 0.22 ms at
      1024 (HLS infers rho_URAM cyclic-16 from it). `.claude/2_ARTIFACTS/scratch_48/bin_density_check_44_overflow_20261015.txt`
- [x] **Phase 2: turn B' on in the step-6 golden config.** Done 2026-10-16 in #45 step 5b part 2: the
      `pl+Bprime` variant (`host/sw_flat/test/sum_order_drift.py`) is step 6's golden, measured on 5
      designs, all converged ([[REPORT_45_pl_arithmetic_20261016.md]]). Box closed 2026-10-19.
- [ ] **Optional: bigblue3/4 trajectory at grid 1024** (the drift ran at 2048, where fewer fixed nodes
      are stretched). Before step 6 quotes numbers.
- [x] **Phase 2, fixed-point accumulator (Mark, 2026-10-19: rename to `bin_density_CU`, whole chip for
      now, 1024 only, ~half the URAMs, accumulate in fixed point; 10-20: keep F = 18 fixed).**
      **CU side DONE 2026-10-20:** `bin_density_CU` accumulates `ap_uint<36>` per bin, scales the table
      weight at load, takes the base as integers (`densrec::FixedBeat`), returns the uint64 excess (host:
      `Placer::overflow_from_excess`), drains `(float)acc * RHO_UNIT`; `to_fixed` and `RHO_FRAC_BITS` live
      in the contract. `make -C host/sw_flat check-bin-density`: **44/44 BIT-EXACT** — rho, excess and
      overflow, and again with the slots shuffled (`scratch_48/bin_density_check_44_fixed_20261020.txt`).
      pl tier-1 [6]: shuffled order bit-identical, while a float accumulator differs in 5,219 / 1,153 bins.
      C-synth: `reduce` and `drain` II=1, `rows` II=4 (the prototype's RMW; banking is next), DSP 58 → 24;
      URAM reads 37 in csynth, which cannot hold 1M × 36 b (8 per bank = 128 expected): confirm at link.
      **Step 6's golden is now `pl+Bprime+fixed`.** → [[REPORT_48_bin_density_fixed_point_20261019.md]]
      **sw_flat side (10-19):** `[sw_flat] deposit = "fixed"` (default off; check-placer bit-identical).
- [x] **Phase 2, total-area F + banked deposit + forwarding (Mark, 2026-10-20).** F per design:
      `densrec::rho_frac_bits(Σ)`, Σ = base + every footprint's area in bin areas, integer bits ⌊log2 Σ⌋ + 2,
      so no bin can overflow; F = 14 (7 large designs at 1024) .. 23. The CU: 16 URAM banks, bank =
      4·(col%4) + row%4, one cell per cycle (**`deposit_fast_loop` II = 1**, depth 117), footprints > 2.99 bins
      on a slow list (≤ 0.5% of slots), 6-deep per-bank forwarding (`BIN_FORWARD_DEPTH`). **44/44 bit-exact**
      (`check-bin-density`, incl. shuffled order); tier-1 PASS. **RTL co-sim** (`pl/synth/bin_density_CU/cosim/
      run_depths.sh`): depth 0 and 4 FAIL, 5 and 6 PASS — the scheduled read→write distance is 5. C-synth
      3.33 ns met, LUT 11%. Drift of per-design F: in noise on 3/4, mms/adaptec1 +0.12% (its two-outcome
      flip); bigblue4 (2048, F ≈ 12) −0.013%, 0.003 pp past its ±0.01% band.
- [ ] **Phase 2b, 8 lanes (Mark, 2026-10-20; modelled, not built).** Each lane owns grid rows with its own
      16 banks (8 lanes = 128 URAMs, no extra). Lane model on real positions (`make -C host/sw_flat
      lane-model`): **4-row strips + FIFO 128 + 4 read points + pairing** (two cells per cycle when their banks
      don't overlap) gives 4.6–7.2× over one cell per cycle on 5 designs (bigblue4 7.2×, ~2.6 ms); bands
      and Mark's centre-weighted maps lose (graded bands: 192 URAMs, ≤ bands). The first ~16 iterations stay
      1.5–3× on every map. **Round 3 (Mark's FIFO per bank, 64 banks, 8 producers, 4 pushes/FIFO/cycle):
      5.2–8.3× and better early too; 16 producers 5.5–9.7× (bigblue4 9.3×) — beats lanes everywhere.**
      **Producer synthesized (10-22):** 95 DSP / 17 K LUT each (csynth estimate), II = 1 at 3.33 ns.
      ⚠️ "Dividers are not the cost" is RETRACTED: csynth omits the fdiv cores (0 LUT/FF); Vivado synthesis
      on the build server (this box has no Vivado synth license for the VC1902), 10-22: **divide 21.7 K LUT /
      17.2 K FF / 90 DSP vs multiply 13.8 K / 14.5 K / 94 per producer: dividers +57% LUT, ~63 K across 8.**
      → change the golden's bin index to a multiply (sw_flat first, drift measured). **Whole banked deposit
      (`pl/synth/bin_density_banked`, bit-exact in C on 1 M bins): 1,136 DSP / 454 K LUT / 128 URAM; 64 of
      each producer's 142 DSP are bank-address multiplies (removable); drain −2.1 ns.** **P = 8 chosen** (760 DSP, fits
      beside hpwl_gradient_CU's 708); P = 16 needs the `(ow·w)·oh` reorder (−40% DSP, a golden change).
      **Next:** golden's bin index by multiply (no dividers; sw_flat first, drift
      measured), then build. Real-design fixtures for the tests: branch `bin-density-real-fixtures`
      (`0994f7a`), not merged. → [[PLAN_48_bin_density_lanes_20261020.md]],
      [[EXPLAINER_48_lane_dispatcher_20261020.md]] ~21 ms per evaluation on bigblue4 at 1 cell/cycle → lanes next.
      → [[REPORT_48_bin_density_fixed_point_20261019.md]] One 36-bit
      URAM word per bin (8K × 36): 128 URAMs. Peak ρ over 44 designs = 95,408 bins (mms/newblue4,
      iteration 4; every peak at iteration 0–7) → 18.18 leaves 2.7× headroom. At equal positions q18's
      overflow beats float everywhere (≤ 9.3e-7 vs up to 4.8e-5); ρ beats float on dense bins (bigblue4
      3.0e-7 vs 2.3e-5) and loses on small sparse designs (pci_bridge32_b 9.7e-6 vs 3.6e-7). Drift vs
      `pl+Bprime`: in noise on 5/5 (mms/adaptec1 read against `pl+Bprime`'s own flipped mode; bigblue4
      +0.002%). Order-independent: fft_a's trace is
      byte-identical in name and slot order. Per-iteration exponent: not taken (Mark, 10-20).
→ [[_NEW_PLAN_48_density_onchip_20261014.md]]
