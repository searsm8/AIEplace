# Tasks

Open work, one section per task. **Status lives here; evidence lives in a
report.** Don't reuse task numbers, find the highest number and add one.

**Compacted 2026-08-07** from 1621 lines. Nothing was closed by that edit — the full prior text
of every task below is in [[history.md]] § *2026-08-07 TODO.md compaction* (its historical name). Go there for
measurements, provenance, and retraction trails.

---

## #1 — Clean house: repo / notes / code cleanup (opened 2026-07-27)

Fast iteration left breadcrumbs and we started tripping over them. Workflow dirs established, git
tree committed, **67 GB freed** from `results/` (helper: `tools/prune_run_artifacts.sh`),
`vck5000/` top level tidied, harnesses moved to `vck5000/test/`. What is left is notes hygiene.

- [ ] Consolidate `.claude/1_REVIEW/` handoffs and reports; apply the `_NEW_` convention consistently —
      un-prefix the ones Mark has read, keep it on the rest.
- [ ] Fold the still-relevant findings out of old reports into tasks.md / memory so those reports
      can be archived.
- [ ] **Per-run `viz/` dumps are reproducible output** (~96 MB per adaptec1 run, ~480 MB per
      bigblue4). Already swept by the default slim. Given the size, consider making viz output
      **opt-in for sweeps** rather than default.
- [~] `tools/eval_overflow_xplace.sh` left untracked per Mark (2026-07-27). Revisit whether it
      belongs in `tools/`.
- [~] The stale `run_config.json` re-baseline comment is fixed but **uncommitted** — bundled with
      #2's comment pruning.
- [~] `~/phd/Xplace` carries **3 uncommitted local edits** (a real `calculator.py` bug fix under
      `--use_precond False`, plus `param_scheduler.py` PRECOND_TRACE instrumentation). Mark's call:
      commit them into XPlace or keep them local-only.

---

## #3 — Tooling & evaluation workflow

Full-pipeline GP→LG→DP evaluation is built and measured; `run-benchmark` + `viz-gif` skills landed
(a third, `xplace-compare`, was built, benchmarked at a **dead tie** against no skill, and
scrapped — the lesson is in [[history.md]]); `dse.py --resume` works. adaptec3's segfault inside
XPlace's legalizer was root-caused to **our own harness** — `gen_lgdp_inputs.py` hardcoded the
`.pl` template instead of reading the one the design's `.aux` names — and fixed for all 16.

⚠️ **[[history.md]] holds the XPlace-reference traps, which are written down nowhere else.** A
result dir is not a reference run until you check its argv *and* that it reached `After DP`; the
log header is the argument dump, not what ran; `gp_ovfl_in` is macro-INCLUDED; **newblue4 is
build-sensitive at ~1%**.

- [ ] **sw_only has no per-row site model.** `enforceDieBoundaries` clamps to the die *rectangle*,
      but 11 of 16 MMS designs have a ragged (staircase) core. `tools/check_row_spans.py`: adaptec3
      **315 cells outside their row's span** (worst overhang 4122), newblue4 25, adaptec5 23.
      Harmless so far and **not** the adaptec3 crash — but it is an unmodelled constraint a
      legalizer has to absorb. XPlace's GP makes the same rectangular assumption, so check what its
      output does here before calling this a divergence.
- [~] **Reconcile XPlace's overflow on our given solution** (`gp_ovfl_in`) with our
      `computeOverflow` — a consistent ~2-4×, direction unexplained. Fillers explain part of it
      (#19a), but a residual ~2.2× remains on adaptec1/adaptec4 and **newblue1 has the opposite
      sign**. The discriminator is a macro-INCLUDED variant of our own metric. Until then that
      column is a diagnostic, not a metric.

---

## #6 — Port XPlace's operator-level optimizations (opened 2026-07-29)

XPlace gets ~2× over DREAMPlace almost entirely from operator-level restructuring of the same
ePlace math we already implement — not a new algorithm. Four techniques; the fourth was measured
and folded into #20 (the bottleneck is **~76 MB/iter of host DMA, ~8× the launch overhead**, so the
payoff is keeping matrices device-side — exactly what Stage 5 does).

- [ ] **Operator combination** — merge WA wirelength, WA gradient and HPWL into one pass; all three
      need the same per-net min/max. Check `Partials.cpp::computeHpwlPartials_CPU` for redundant
      recomputation.
- [ ] **Operator extraction** — share one cell-density-map build between the objective and the
      overflow metric. Check `Density.cpp::compute_eField_DCT` against `computeOverflow`.
- [ ] **Operator skipping** — XPlace skips the density gradient while
      `|density_grad|/|wirelength_grad| < 0.01` and `iteration < 100`. Check whether sw_only's early
      iterations already qualify, and carry it into pl_algo's modules from the start.

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

## #14 — Zoomable visualizer window (opened 2026-07-31)

Done: a configurable `ViewWindow` (centre/span as **fractions of the die**, so one setting means the
same magnification on every benchmark), four zoom-only detail layers (row pitch, density bins, cell
outlines, filler/cell separation), and the GIF path. Verified by rendering on `mgc_pci_bridge32_a`.

⚠️ **The y axis was mirrored until 2026-08-05.** Every PNG/GIF produced before that date is
vertically flipped relative to every one produced after — including `.claude/2_ARTIFACTS/newblue5_placement.gif`
and the whole `GIFS_*` pile. **Do not compare an old frame against a new one and conclude the
placement moved.**

- [ ] **Run the zoom path once on a full MMS design** —
      `python3 tools/make_viz_gifs.py --designs newblue1 --zoom --every 50`. Watch whether `MIN_SIZE`
      (0.001 of canvas) still floors anything at zoom, where it should not.
- [ ] **Zoom window locked to a NODE, not a fixed region** — give the window a target (a node name,
      a macro, or "the node that moved most this iteration") and re-centre every frame, so the
      animation tracks that cell instead of watching cells drift through a static box. This is the
      version that answers *what is the optimizer doing to this cell*. Needs per-frame recomputation,
      so it belongs in the offline tool. → [[_NEW_HANDOFF_viz_offline_tool_20260805.md]]
- [ ] **Multiple zoom levels / regions per run** — today `output.zoom*` is one window fixed at setup
      and changing it means re-running the placement. Same conclusion: offline tool.

---

## #15 — Net-local coordinate frames for the wirelength gradient (opened 2026-08-03)

**Parked at Mark's request 2026-08-03 — analysis done, no implementation.**

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

## #17 — sw_only regression tripwire (opened 2026-08-05, BUILT 2026-08-05)

`vck5000/test/regress/`, run with `make test-regress`. Per design it asserts two things, both
**exact, no tolerance**: `iterations.dat` matches the committed baseline row for row, and the
`sha256` of the output `.def` matches. `vck5000/test/regress/README.md` is authoritative — read it
before touching a baseline. The frozen configs are **snapshots, not live copies** of
`run_config.toml`, and `random_seed = 42` is pinned and load-bearing.

- [ ] **Nothing checks quality, only stability.** A reproducibly *wrong* sw_only passes. Guarding the
      XPlace ratio would need committed reference numbers and a tolerance — a separate job.
- [x] **DONE 2026-08-09 — `readDEF()` now says what it wanted.** `DataBase.cpp:212` returns false with
      *"No 'floorplan.def' in \<dir\>; that is the only .def name readDEF() accepts. Found: \<names\>"*
      instead of handing the parser an empty path. **The hardcoding itself is unchanged** — this fixes
      the diagnosis, not the constraint; a dir whose DEF is named anything else still cannot be read.
      Control flow is identical (both paths returned false, and `readDesignFiles()` still falls back to
      Bookshelf), so `make test-regress` is bit-identical before and after. Error path exercised
      directly on a probe dir with `renamed_top.def`, not just reasoned about.

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
- [ ] **Regenerate the fixture trace from the post-#19 sw_only** — blocked on #20 step 1
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
- [ ] **`host/src/pl_algo/` still gates on the real dff — the pre-#19 bug, live.** Found doing the
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

- [ ] **1. Restore `dumpScheduleTrace()` in sw_only** with today's columns (`precond_kappa`,
      `precond_coef`, phase, `phaseIteration`, stop reason, `backtrack_steps`) and regenerate the
      adaptec1 fixture + its `config_used.toml`. Verify `make test-regress` is bit-identical before
      and after — the dump is config-gated, so it MUST be a no-op. **Do this before touching pl_algo:
      it is the instrument every later step is measured with.**
- [ ] **2. Re-verify `param_scheduler` against the new trace**, feeding **κ**, not dff. Fix what falls
      out: escalating `dff_coef`, the missing `overflow rising` conjunct on the coarse divergence
      test, phase-relative counters, jolt params read from config instead of hardcoded.
- [ ] **3. Tier-1 harnesses for the uncovered modules** — `node_footprint`, `density_bin` (include the
      real header; delete `density_bin_model`'s own stale copy), `iteration_update`, `bb_reduce`,
      `metrics`, `force_gather` — each against its named sw_only golden. **This is what makes 4–6 safe.**
- [ ] **4. Close the datapath divergences** under that coverage: `node_footprint.hpp` still does the
      in-die shift #11a deleted; it lacks #11b's movable-macro weight; `iteration_update.hpp` clamps to
      `[0, die−w]` where sw_only clamps to the √2-expanded box; **pl_algo has no fillers at all**.
- [ ] **5. Fillers** — packer, uniform-random initial placement (not centre-clustered), the λ-init
      balance that counts them, and the movable/filler split the two density maps need. Largest single
      change; largest quality lever.
- [ ] **6. Compose the resident loop (Stage 5 proper)** with the second movable-only density map, the
      best-position DDR buffer, and phase-2 re-entrancy designed in from the start.

**Open questions for Mark** (report §10): does "the same algorithm" include phase 2 and backtracking,
or is v1 "phase-1 GP, device-resident, bit-comparable"? Pin pl_algo to a named sw_only commit rather
than chasing HEAD? Pin sw_only to grid 1024 for the A/B, or build pl_algo per-design with `-DPL_GRID`?

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
- [ ] **pl_algo's mirror is compile-verified only.** `Driver.cpp::estimate_initial_step` now scales
      by `cfg.site_width` (set in `main.cpp` from `db.getSiteWidth()`), and `make host HOST=pl_algo`
      builds — but it has never been run against the golden. Needs Geert's card or sw_emu.
- [x] **Re-run the 4 excluded designs** — DONE, all 5 previously-`nan_metrics` designs re-run:
      `mgc_superblue11_a/12/14/16_a` converge; `mgc_des_perf_b` places but stops on
      `divergence_guard`. ⚠️ Surfaced a *new* defect: `mgc_matrix_mult_a` at **3.03×** (GP dies at
      iteration 290) — the single worst design in the tier, and what destroys the mean.
      → [[_NEW_REPORT_performance_snapshot_20260810.md]]

**Do NOT "fix" the snapshot by re-running these with a hand-tuned seed** — that is a per-design
hyperparameter, not comparable to the other 40, and it hides the defect.

---

## Parked — open technical follow-ups

- [ ] **pl_algo initial-step mirror** — implemented in `Driver.cpp::estimate_initial_step`, compiles,
      **unverified**. Needs Geert's card or sw_emu to check against the sw_only golden.
- [ ] **SoA layout for the hot per-node/per-bin fields** (from #12). The next real threading win is
      layout, not more threads: `computeOverlaps`/`combineGradients`/`recordIterationResults` are
      memory-bound over pointer-chased objects and go flat by 4 threads. Big change, own task.
- [ ] **Logger cosmetics** (from #5) — double-bordered summary tables (nested `Table`); the welcome
      banner still goes straight to `cout`, the last source of trailing whitespace; `run.log` written
      for **every** run including DSE sweeps (~125 MB per 500-run sweep); and "Algorithm time (s) |
      0.000" in Run Statistics, noticed in passing and never investigated.
- [ ] **(optional) `init_step_seed` narrow-range Morris** [0.005, 0.05] — low priority, mechanism
      already understood. ⚠️ #23 changes the premise: read it first.

---

## #24 — best-solution trackers: shared buffer + torn restore (opened 2026-08-10, CODE DONE 2026-08-10)

**FIXED — two defects, only one of which was known when this was opened.** Three trackers
(`best_primary`/`best_aux`/`best_rollback`) each with their own geometry buffer, plus XPlace's
`get_best_solution` selection; and `restoreBestPlacement()` now also sets `probe_pos = node_pos`.
`make test`, `make test-regress`, `make test-regress-slow` green; 3 baselines regenerated with
reasons. **Two decisions still open — see below.**
→ [[_NEW_REPORT_24_best_solution_trackers_20260810.md]]

- **Defect 1 (as opened): one buffer, two writers.** Confirmed on 17 of 29 `full44_v2` runs — and
  those 17 are exactly the *converged* designs, the ones that get scored.
- **Defect 2 (NOT known when this was opened): a placement is two variables and the snapshot copied
  one.** HPWL reads `node_pos`; density/overflow deposits at `probe_pos` (`Grid.cpp:36`), which the
  restore never touched. After a restore the node held committed position from one iteration and
  lookahead from another — a state that existed at no point in the run.
  **This is what produced the evidence quoted below**, so that evidence does not prove what it says.
  The real proof of defect 1 is the *HPWL* mismatch: adaptec1 logged iter 728 (HPWL 7.035e+07) while
  `Final HPWL` read 7.051e+07 = iteration **751**. `freezeMovableMacros` (`DataBase.cpp:396`) had
  already diagnosed and locally patched defect 2 — it was never generalised.

**⚠️ The rule does NOT always ship the spread-out solution** — it prefers it only when
`aux_hpwl < best_hpwl*1.005` and `aux_ovfl*1.1 < best_ovfl`. Over 29 traces: aux 8, primary 11,
none 10. The *bug* shipped the spread one nearly always, so this makes 11 designs less spread,
deliberately. A/B on that 0.5% budget (`best_aux_max_hpwl_ratio`, `DSE_RUN_SET=best_sol_ab`):
1.010 buys ~35% less overflow for ~0.5–0.7% GP HPWL, DP recovers 41–74% of it but never all —
**keep XPlace's 1.005** (n=2 designs with usable DP data; see report §5).

<details><summary>Superseded framing as opened (2026-08-10) — kept for the retraction trail</summary>

> **The "Restored … from iteration N" log line names a placement that is not the one shipped.**
> Headline HPWL/overflow are still trustworthy; the *provenance* line is not.
>
> There is exactly one snapshot buffer, `Node::best_solution_pos` (`AIEplace.cpp:107`), and **both**
> `best_primary` and `best_fallback` write it through the same `snapshotBestPlacement()`
> (`Output.cpp:670-685`). Last writer wins. `restoreBestSolution()` (`Output.cpp:414`) then selects by
> *metadata* priority (primary > fallback), logs that metadata, and calls `restoreBestPlacement()`,
> which loads whatever geometry happens to be in the buffer. When the fallback updated after the
> primary last did — the common case, since overflow keeps falling after the threshold crossing — the
> log names the primary's iteration while the restored cells are the fallback's.

**Two corrections.** "Headline HPWL/overflow are still trustworthy" was wrong for **overflow**:
defect 2 meant every reported overflow described the last iteration, not the shipped placement.
And the adaptec1 overflow evidence below is defect 2's signature, not defect 1's.
</details>

**Evidence (adaptec1, `full44_v2` run 2026-08-10):**
```
log:            Restored primary (converged) best placement from iteration 728
                (HPWL: 70346752, overflow: 0.069424)
reported:       Final Overflow (smoothed, no fillers) = 3.746e-02
iterations.dat: iter 757 OVFW = 3.746e-02   <- exact match
                iter 728 OVFW = 0.0694
```
`m.final_smoothed_overflow` is recomputed on the restored positions (`Output.cpp:455`), so the
geometry is demonstrably iteration **757** while the log says **728**. Reproduced on
`mgc_matrix_mult_c` (log it863 / 0.0692; reported 4.317e-02 = iter 892).

Two defects, ranked:

- [x] **1. Selection has no control over the geometry.** DONE — three trackers, three buffers, one
      shared `selectBestSolution()`. Verified on **both** branches of the rule: adaptec1 ships aux
      iter 757, `mgc_pci_bridge32_b` ships primary iter 723, and in each `Final HPWL` equals the
      selected solution's HPWL.
      ⚠️ **The falsifier as written is unusable** — it assumes `Final Overflow` describes the
      restored placement, which was defect 2. It can only be applied on a design whose selected
      solution is *not* the last iteration.
- [x] **2. The log line is false, and it misleads.** DONE — the slot now travels in the same struct
      as the metadata, so they cannot disagree.

- [x] **The `best_sol_aux` faithfulness gap** (the ⚠️ note below, folded in once defect 1 stopped
      confounding it). `best_fallback` was renamed `best_aux`, gated on convergence, and given
      XPlace's accept rule; the missing `best_sol_rollback` was added with its
      free-on-first-convergence lifetime. The inverted `OVFW_EPSILON = 0.005` rule is gone.

**Still open:**
- [x] **Fix (B)'s scope — RESOLVED 2026-08-11 as (b).** `syncProbeToCommitted()` is a separate step
      called only from `restoreBestSolution()`; `restoreBestPlacement()` restores `node_pos` alone,
      so the phase-2 macro freeze is untouched.
      ⚠️ **The premise for choosing (b) was wrong, and the correction matters more than the choice.**
      (b) does **not** leave MMS bit-identical: it produces sha `e9cc52242ad0`, byte-identical to
      the (a) build. Fix (B) never affected MMS at all. The MMS change is **#24's selection fix** —
      `beginFixedMacroPhase` (`Phase2.cpp:72`) picks the placement to freeze macros at via
      `selectBestSolution()`. So **MMS results move under #24 either way**, and the `full44_v2` MMS
      exclusion (valid for #23, which provably could not touch bookshelf designs) does **not**
      carry over. Cause of the error: `test-regress-slow` was never run between the tracker port
      and (B), so the divergence was pinned on the most recent change. See report §6a.
- [ ] **Re-run the MMS suite (16 designs).** Newly required by the above — #24 changes what phase 2
      freezes its macros at. Not needed for #23; is needed for #24.
- [ ] **Widen the A/B (n=2).** Only `bigblue2` and `mgc_superblue19` both flipped and produced DP
      numbers. Needs ~8–10 more designs run blind — the trace projection **cannot** identify
      flippers near the budget (4-sig-fig HPWL; `adaptec3`'s ratio straddles 1.005), so do not
      re-derive them offline.
- [x] **Our overflow metric vs XPlace's disagree on direction for `mgc_superblue19`** — ROOT-CAUSED
      2026-08-11, and it is not an overflow-metric bug: the two sides use **different
      `target_density`** on ISPD2015. Promoted to **#25**, which is the real issue.
- [ ] **Best-solution tracking still diverges from XPlace in two places** (found while answering
      "is it faithful?", 2026-08-11 — the *rule* is faithful, these are not):
      - **XPlace snapshots the LOOKAHEAD, we snapshot the COMMITTED position.** `mov_node_pos` IS
        `v_k` (`nesterov_optimizer.py:71`, *"directly use p as v_k to save memory"*), so
        `update_best_sol(mov_node_pos)` stores v_k and `evaluator_fn(mov_node_pos)` measures HPWL
        **and** overflow at v_k — one position, self-consistent. We snapshot `node_pos` (u), measure
        HPWL at u and overflow at v. Fix (B) made *our* side self-consistent at u; XPlace is
        self-consistent at v. Deciding u-vs-v is a separate call — it changes the shipped `.def`.
      - **`BEST_SOL_MIN_ITER` is absolute, XPlace's is phase-relative.** Ours: `iteration < 50`.
        XPlace: `self.iter - self.init_iter < 50` (`param_scheduler.py:393`). After the phase-2
        restart ours tracks immediately; XPlace waits 50 iterations. Affects mixed-size only.
- [ ] **Move the A/B data out of `/tmp`** into `.claude/2_ARTIFACTS/` if it is ever to be cited:
      `/tmp/lgdp_ab/results_{1005,101}.tsv`.

<details><summary>Superseded: the ⚠️ "Related but SEPARATE" note — now folded in and done</summary>

> ⚠️ **Related but SEPARATE — do not conflate.** sw_only has no equivalent of XPlace's `best_sol_aux`,
> and `best_fallback`'s accept rule is *inverted* against XPlace's: ours tolerates overflow degrading
> by `OVFW_EPSILON = 0.005` to gain HPWL, XPlace's requires overflow to strictly improve and tolerates
> 0.5% HPWL loss (`param_scheduler.py:432-441`), then prefers it over the HPWL-driven pick when
> `aux_hpwl < best_hpwl*1.005 and aux_ovfl*1.1 < best_ovfl` (`get_best_solution`, :563-577). That is a
> real faithfulness gap, **but defect 1 confounds any measurement of it** — fix 1 first, then re-measure.

Correct as written, and the sequencing advice was right — defect 1 *was* confounding it. Both were
fixed in one change once defect 1 landed, since the rename and the gate touch the same lines.
</details>

→ [[_NEW_REPORT_24_best_solution_trackers_20260810.md]],
  [[HANDOFF_24_best_solution_buffer_20260810.md]]

---

## #25 — We and XPlace use DIFFERENT `target_density` on ISPD2015 (opened 2026-08-11)

**Our ISPD2015 overflow numbers are not comparable to XPlace's, and it is not only a reporting
problem — we are optimizing to a different density target on 20 of 28 ISPD designs.**

Measured on identical `.def` files (byte-identical, verified), our `Final Overflow (exact, no
fillers)` vs XPlace's `get_obj_overflow` on the same placement:

| design | ours | XPlace | ratio | target_density (ours / XPlace) |
|---|---|---|---|---|
| `adaptec1` | 1.093e-01 | 0.1094 | **0.9991** | 1.0 / 1.0 |
| `mgc_fft_b` | 1.395e-01 | 0.1247 | 1.119 | **0.6** / 1.0 |
| `mgc_des_perf_1` | 9.932e-02 | 0.0113 | **8.79** | **0.906** / 1.0 |

**Root cause.** XPlace's overflow is `((density_map - args.target_density) * bin_area).clamp(min=0)
/ total_mov_area_without_filler` (`evaluator.py:48`). `args.target_density` is set **per design** by
`setup_design_args` (`utils/setup_dataset.py:54-85`) — whose branches cover only ISPD2005 and the
*classic* superblue names. **No branch matches `mgc_*`, so ISPD2015 keeps the default 1.0.** The
only other write is `database.py:679-682`, which can only raise it. `placement.constraints` is
passed to the external **DP engine** (`detail_placement.py:670,707`), never into the objective.

We instead take it from the DEF: `maximum_utilization ... Overridden by benchmark's
placement.constraints if present` (`default_config.toml`). Hence 0.6 / 0.906 above, and hence a
smaller per-bin capacity and a higher overflow. adaptec1 (bookshelf, no constraint) agrees to 0.1%,
which is the control proving the metric itself is right.

**Why this is bigger than a metric mismatch** — `target_density` also feeds:
- the **convergence signal** (smoothed overflow drives the stop), so we stop on a different criterion;
- **filler area** (`initializer.py:72` ↔ our `rebuildFillers`);
- the **movable-macro density weight** (`database.py:921-923`, the #11b `target_density` override).

So on ISPD2015 we spread to a tighter target than XPlace does. **Hypothesis worth testing: this is
part of the ISPD2015 HPWL gap** — a tighter density target buys spread with wirelength.

- [ ] **Decide which is right, and document it as deliberate either way.** Ours is arguably the more
      physical reading (the DEF states a utilization the design must respect, and XPlace *does*
      honour it at DP time). XPlace's is what our numbers must match to be comparable. These can be
      reconciled — e.g. optimize at 1.0 to match, and report the DEF-constrained number separately.
- [ ] **A/B it on ISPD2015**: `maximum_utilization` forced to 1.0 vs DEF-derived, GP HPWL + post-DP.
      Cheap via `DSE_RUN_SET`; `mgc_des_perf_1` (0.906) and `mgc_fft_b` (0.6) bracket the range.
- [ ] **Until then, do not quote our ISPD2015 exact overflow against XPlace's.** The smoothed
      overflow has the same defect. `adaptec1`-style bookshelf designs are unaffected.

→ [[_NEW_REPORT_24_best_solution_trackers_20260810.md]] §8 (measurement + XPlace source trail)

---

## #26 — Fence regions (opened 2026-08-11; steps 1,2,3,5 DONE 2026-08-11 — ONE DECISION LEFT)

**All 9 designs are scored; the ISPD suite is 28 of 28.** Their ratios are unremarkable — median
**1.0154**, 7 of 9 within 2%, `mgc_pci_bridge32_b` better than XPlace. ISPD median 1.0095 → **1.0106**.
→ [[_NEW_REPORT_26_fence_regions_20260811.md]]

**Step 2 was never an acquisition problem.** `ispd2015_fix` is *generated*, not downloaded:
`cd ~/phd/Xplace/data && python3 fix_ispd2015_route.py` builds all 20 designs from the same raw
files (which are a symlink to our own benchmarks). The regenerated `mgc_pci_bridge32_b` DEF is
**byte-identical** to the 2026-07-13 copy, so #22's "a hand-built _fix cannot be trusted" caveat is
retired — this is XPlace's own script.

**Ignoring the fence is a real gap and it is now measured: 59–94% of fence-constrained cells sit
outside their region** in our placements (`vck5000/tools/fence_check.py`). Controlled against the
contest's own legalized solutions, which report **0 of 190,010** outside. Against those legal
solutions we are 2.6% better on the 11 unfenced designs and 12.5% better on the fenced 9 —
**a 9.9 pp difference-in-differences**, i.e. ~10% of our margin there is the constraint, not the placer.

⚠️ **None of this touches the XPlace comparison** — XPlace strips fences too, and the 9 are scored
on the stripped variant on both sides. What it invalidates is any claim these are legal ISPD2015
solutions.

- [x] **1. Score `mgc_pci_bridge32_b`** — done, and the other 8 with it. Both harnesses now split on
      `^REGIONS` and record the data variant in a `variant` column; `analyze_full44.py` marks
      fence-stripped designs **†**, the paper's own convention.
- [x] **2. Obtain `ispd2015_fix` for the other 8** — regenerated (20 designs, 702 MB, ~90 s).
      8 new XPlace references added to `tools/benchmarks.py`, cross-checked against TCAD Table III:
      within 0.2% on 5 of 9, worst 5.4%, all on the better side (the paper's HPWL is NTUplace4dr's
      measurement of XPlace's placement, not XPlace's own).
- [x] **3. Measure what ignoring the fence costs** — above. Note the ticket's literal form of this
      step has a **null answer, proved**: placing the fence-carrying vs fence-stripped DEF with
      sw_only gives a **bit-identical** `iterations.dat` and output DEF, because the constraint is
      discarded either way.
- [ ] **4. Decide whether to implement fence regions.** **Mark's call — this is the only item left.**
      Report §7 recommends **no**: XPlace has no formulation to copy (it raises; the paper removes
      the constraint), implementing it cannot improve any number we report, and the gap is now
      visible rather than silent. The counter-argument is in the same section and is not weak — the
      contest scored these designs *with* the constraint, and §4b prices it at ~10%.
- [x] **5. Stop reporting fence-carrying designs as if unconstrained** — `readDEF()` warns on every
      run ("…the result is NOT a legal ISPD2015 solution…"), the TSVs carry `variant`, the scorecard
      daggers the 9, and `benchmarks.py` documents which reference came from which variant.

<details><summary>Original entry, as opened 2026-08-11 (diagnosis, still accurate)</summary>

**Supersedes #22's "SKIP for now".** Same root cause, but the diagnosis is now exact and one design
is scoreable today with a one-line harness change. #22 stays for its retraction trail; work here.

### The problem, precisely

`mgc_pci_bridge32_b`'s `floorplan.def` carries `REGIONS 3` / `GROUPS 3` — a *fence region*, i.e. a
set of cells constrained to a sub-rectangle of the die rather than the whole core. Two independent
consequences, and they are usually conflated:

**(a) XPlace crashes on them, so our stage-2 legal-vs-legal scoring dies.** Not a DP failure — it
never reaches DP:

```
run_placement_nesterov.py:442  data.init_filler()
database.py:863                self.compute_filler(...)
database.py:655                return self.compute_filler_with_fence(...)      # enable_fence = len(regions) > 1
database.py:660                raise NotImplementedError("We haven't yet supported fence region.")
```

`run_lgdp44.sh` records this as `exit1_nodp` (exit code 1, no `After DP` line) — an accurate status
that reads like a DP bug and is not one.

**(b) sw_only ignores fence regions entirely.** `DataBase::add_def_region()` and
`add_def_group()` are **empty stubs** (`common/src/DataBase.cpp:662-665`) — Limbo hands us the
parsed regions and we discard them. So on these 9 designs we solve an *unconstrained* problem and
report an HPWL that is optimistically low against any tool that honours the constraint. Nobody has
measured how much. This is a correctness question, not a scoring inconvenience, and it is the part
that has never been written down.

### The affected set is exactly the paper's †

Our 9 `exit1_nodp` designs and Table III's 9 †-marked designs are the **same set**, no exceptions:
`des_perf_a`, `des_perf_b`, `edit_dist_a`, `matrix_mult_b`, `matrix_mult_c`, `pci_bridge32_a`,
`pci_bridge32_b`, `superblue11_a`, `superblue16_a`. † means "fence region constraints removed", so
**the paper never reports a fence-carrying number either** — every published figure for these 9 is
on the stripped variant. Any comparison we make must be against the stripped variant too.

### Why one of the 9 already works

`Xplace/main.py:95` rewrites `--dataset ispd2015` → `ispd2015_fix` (their released fence-stripped
data). `run_lgdp44.sh` uses `--custom_path ... benchmark:ispd2015` for the ISPD2015 tier, which is
dispatched *before* that rewrite and hands XPlace the fence-carrying DEF — so our harness walks
straight into the crash XPlace works around. `data/raw/ispd2015_fix/` exists locally but holds
**only `mgc_pci_bridge32_b`** (fetched 2026-07-13), which is why that one design has a reference.

Verified 2026-08-11: patching a sw_only placement into
`ispd2015_fix/mgc_pci_bridge32_b/mgc_pci_bridge32_b.def` and running
`--dataset ispd2015_fix --global_placement False --given_solution <patched>` **completes LG+DP
cleanly**. Die area, `COMPONENTS` count and `UNITS` are byte-identical between the two variants, so
the patch is a straight coordinate substitution. Post-DP 3.310820e+06 site units = 6.62e+08 DBU,
against XPlace's own 3.477053e+06 — we are ~5% *ahead* on this design.

### What to do, in order

- [ ] **1. Score `mgc_pci_bridge32_b` now (cheap, unblocks 1 of 9).** Teach `run_lgdp44.sh` to use
      `--dataset ispd2015_fix --design_name <d>` when `Xplace/data/raw/ispd2015_fix/<d>/` exists,
      falling back to the current `--custom_path`. Template for the patch becomes that dir's
      `<design>.def`. Verify against the 6.62e+08 DBU above before trusting it.
- [ ] **2. Obtain the official `ispd2015_fix` for the other 8** (`Xplace/tree/main/data`, per the
      paper's footnote 4). No code change needed beyond step 1. **Preferred over constructing it** —
      and if constructed instead, `mgc_pci_bridge32_b` is a free validation: a hand-built `_fix` must
      reproduce its known numbers or none of the 8 can be trusted (this check is inherited from #22
      and still stands).
- [ ] **3. Measure what ignoring the fence costs us.** With both variants of `mgc_pci_bridge32_b` in
      hand, place *both* and compare: if our fence-carrying GP HPWL is materially below our
      fence-stripped GP HPWL, we are buying wirelength with illegal placements and every ISPD2015
      number on those 9 is inflated in our favour. This is the measurement that decides whether #26
      is a scoring chore or a correctness bug.
- [ ] **4. Decide whether to implement fence regions at all.** XPlace doesn't (it raises), the paper
      sidesteps them, and the ISPD2015 contest scored them. Implementing = per-region filler areas
      and a per-region density objective. **Do not start this before step 3** — if step 3 says the
      cost is negligible, the honest fix is to document that we place the stripped variant and stop
      pretending otherwise. If it is large, it is a real gap in the placer.
- [ ] **5. Either way, stop reporting fence-carrying designs as if unconstrained.** Whatever step 4
      decides, the run log should say which variant was placed.
</details>

Related: [[#22]] (same root cause, superseded decision), [[#25]] (the other ISPD2015 comparability
defect — different `target_density`, and it applies to all 20 ISPD2015 designs including these 9).

---

---

# Improvements

Algorithmic ideas beyond faithfulness cleanup — hypotheses, not yet scoped.

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
