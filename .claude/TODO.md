# TODO

Open work, one section per task. **Status lives here; evidence lives in a
report.** Don't reuse numbers, find the highest number and add one.

**Compacted 2026-08-07** from 1621 lines. Nothing was closed by that edit — the full prior text
of every task below is in [[history.md]] § *2026-08-07 TODO.md compaction*. Go there for
measurements, provenance, and retraction trails.

---

## #1 — Clean house: repo / notes / code cleanup (opened 2026-07-27)

Fast iteration left breadcrumbs and we started tripping over them. Workflow dirs established, git
tree committed, **67 GB freed** from `results/` (helper: `tools/prune_run_artifacts.sh`),
`vck5000/` top level tidied, harnesses moved to `vck5000/test/`. What is left is notes hygiene.

- [ ] Consolidate `.claude/1_REVIEW/` handoffs and reports; apply the `_NEW_` convention consistently —
      un-prefix the ones Mark has read, keep it on the rest.
- [ ] Fold the still-relevant findings out of old reports into TODO.md / memory so those reports
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
- [ ] **❓FOR MARK — this entry contradicts itself and I did not resolve it.** The 2026-07-31 header
      says #11b's `macro_td_expand_ratio` toggle was **KEPT** (default false, rejected on results but
      faithful, re-test once the stop criterion is fixed). A later 2026-08-02 line says the toggle was
      **DELETED** the same day it landed and the faithful branch is unconditional. A third note
      (2026-08-07) says the re-test is now **unblocked by #19** and that `tagMovableMacros()` stays to
      serve it. Those cannot all be true. Check the code, then fix the entry — and if the re-test is
      real, run it now that #19 has fixed the stop criterion.

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
- [ ] **`readDEF()`'s `floorplan.def` hardcoding is a latent bug**, not just an ISPD-2019
      inconvenience: a benchmark dir holding `.def` files but none named `floorplan.def` fails with an
      empty path in the error message rather than saying what it wanted.

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

- [ ] **Make `sched_verify` assert `dff_coef` constancy** (within a `precond_coef` plateau; allow the
      ×2 steps). **The single highest-value test change in the repo right now** — an already-designed
      check that was left as a print, reading a **633,000× spread** and passing anyway. Asserting it
      would have caught #19b on day one from the pl_algo side.
- [ ] **Regenerate the fixture trace from the post-#19 sw_only** — blocked on #20 step 1
      (`dumpScheduleTrace()` must be restored first). Until then the fixture is a 2026-08-05 capture
      of the OLD gate quantity.
- [ ] **Rename pl_algo's `dff`/`dff_coef` to `kappa`/`kappa_coef`** to match sw_only's `precond_kappa`
      and XPlace's `weighted_weight`. **The name is what hid this.**

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

## #22 — 8 ISPD2015 designs have no XPlace reference (opened 2026-08-07)

**Decided 2026-08-07 (Mark): SKIP for now.** The 44-design snapshot ships with 36 of 44 carrying a
reference; these 8 appear with our own numbers and no ratio. This entry exists so the analysis is not
re-derived.

`Xplace/main.py:94-96` silently rewrites `--dataset ispd2015` → `ispd2015_fix`, and that directory
holds exactly **one** design. `--custom_path` is checked *before* the dispatch and bypasses the
rewrite — that got us the other 11. The remaining 8 carry `REGIONS`/`GROUPS` that XPlace says it
cannot handle, so they are recorded `blocked_fence_region` rather than given a **silently wrong**
reference.

If revisited: **Option 1 (preferred)** obtain the official `ispd2015_fix` — no code change needed.
**Option 2** construct it (merge tech+cells LEF, strip REGIONS/GROUPS) — but it is *not* a plain
concatenation, and the validation is free and must come first: we hold both variants of
`mgc_pci_bridge32_b`, so a constructed `_fix` must reproduce its known numbers or none of the 8 can
be trusted.

⚠️ **Caveat that applies even today:** `mgc_pci_bridge32_b`'s reference came from the `_fix` data
(fence regions stripped) while sw_only places the region-bearing DEF. For that one design the two
tools solved slightly different problems.

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

- [ ] **Decide the fix**, in order of preference: (a) make the probe **relative** — scale the trial
      displacement by the die span or position magnitude so it can never round to zero; this removes
      the seed's design-sensitivity entirely; (b) detect `α == 0` and retry with a geometrically
      larger seed; (c) raise the default seed — cheapest, but it only moves the cliff and needs a full
      re-baseline.
- [ ] **Assert it, do not just fix it.** `α == 0` at iteration 1 should be a hard error with a clear
      message, not a silent 2133-iteration no-op that writes a plausible-looking HPWL. Same rule as
      `CLAUDE.md` § *A test asserts*.
- [ ] **Add one large design to `make test-regress`.** The two mgc designs are small enough that the
      probe never underflows, so the tripwire cannot see this class of bug.
- [ ] **Re-run the 4 excluded designs** once fixed; they are marked `nan_metrics` in the snapshot.

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
