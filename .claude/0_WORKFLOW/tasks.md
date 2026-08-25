# Tasks

Open work, one section per task. **Status lives here; evidence lives in a
report.** Don't reuse task numbers, find the highest number and add one.

## ⚠️ sw_only functionality is FROZEN — called 2026-08-17, EFFECTIVE 2026-08-18 (Mark)

sw_only is at parity with XPlace — **median 1.0094 / mean 1.0112 legal-vs-legal over all 28 ISPD
designs**, 24/28 within ±2%, better on 5. Golden:
`.claude/2_ARTIFACTS/results/GOLDEN_sw_only_frozen_20260821/` at commit `02464d0`. The active
thread is now **pl_algo** (#20). Freezing is not a pause: pl_algo's algorithm is pinned to the
2026-07-14 sw_only, so **every further sw_only change is another port**, and a stable sw_only is
what makes #20 a bounded job.
⚠️ **MMS is NOT covered by that number** — it is ~1.9 pp worse than its last known-good run and
unresolved, see **#35**.

**Changes admitted under the freeze, in order.** It was called 2026-08-17 against the 08-15
numbers (1.0096 / 1.0113). Three faithfulness fixes already in flight were then allowed to land —
**#32's 7a+7b** and **#3's cap→scale** — which cost +0.13 pp of ISPD mean and were kept per
`CLAUDE.md`'s prefer-XPlace rule. **#34** (2026-08-21, Mark-authorized) then found `#3` had been
applied to only one of **four** places computing the same quantity, and made them agree; that
recovered the loss and more on ISPD (mean 1.0126 → **1.0112**, within-2% 22 → 24).
⚠️ **The frozen sw_only is still deliberately not the best-scoring sw_only we ever measured on
every tier** — MMS remains worse than pre-`#3` (#35). Anyone tempted to "improve" a number by
reverting one of these is re-opening a closed decision; take it to #35 instead.

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

- [ ] Consolidate `.claude/1_REVIEW/` handoffs and reports; apply the `_NEW_` convention consistently —
      un-prefix the ones Mark has read, keep it on the rest.
- [ ] Fold the still-relevant findings out of old reports into tasks.md / memory so those reports
      can be archived.
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
- [~] The stale `run_config.json` re-baseline comment is fixed but **uncommitted** — bundled with
      #2's comment pruning.
- [x] **DONE 2026-08-17 — `~/phd/Xplace`'s 3 local edits are committed**, on a new branch
      `local-fixes` (the clone was sitting on `main`). Split into three commits so the two genuine
      fixes are cherry-pickable and the instrumentation is not: `5ecf97e` apply_precond returns
      None, `9b0851d` weighted_weight never assigned, `88ae004` the PRECOND_TRACE dump (marked
      LOCAL ONLY in both its comment and its commit message). Working tree clean.
      → whether to send the two fixes upstream is now an **Improvements** item, see below.

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

- [x] **DONE 2026-08-17 — fixed-density is now a SCALE, matching XPlace.** Mark's call: bundle it
      with #32's 7a/7b and pay for one suite re-run instead of two. `Grid::clampFixedDensity`
      (`Grid.cpp`) now computes `min(overlap, bin_area) * td` — XPlace's
      `init_density_map.clamp_(min=0.0, max=1.0).mul_(args.target_density)` (`initializer.py:82`),
      i.e. `min(ρ,1)·td` — where it previously capped at `min(ρ,td)`. The two differ wherever a bin
      is partially occupied: at td=0.65, ρ=0.5 XPlace gives 0.325 and the cap gave 0.50, so we read
      HIGH in macro-perimeter bins.
      **Identical at td=1, and that is verifiable rather than asserted:** `mms_adaptec1` (td=1.0)
      re-baselined **bit-identical** — same 1274 iterations, same `.def` sha `91cbbdee0d59`. The two
      ISPD2015 regress designs (td<1) both moved: `mgc_fft_a` 620 → 632 iterations,
      `mgc_pci_bridge32_b` 693 → 713.
      ⚠️ **Net effect on the headline was slightly NEGATIVE** — see summary.md and #32. All three
      2026-08-17 fixes are faithfulness fixes and together they cost +0.13 pp of mean. Kept anyway,
      per `CLAUDE.md`'s rule to prefer XPlace's formulation over an ad-hoc win.
- [ ] **sw_only has no per-row site model.** `enforceDieBoundaries` clamps to the die *rectangle*,
      but 11 of 16 MMS designs have a ragged (staircase) core. `tools/check_row_spans.py`: adaptec3
      **315 cells outside their row's span** (worst overhang 4122), newblue4 25, adaptec5 23.
      Harmless so far and **not** the adaptec3 crash — but it is an unmodelled constraint a
      legalizer has to absorb. XPlace's GP makes the same rectangular assumption, so check what its
      output does here before calling this a divergence.
- [x] **SOLVED 2026-08-15 — it was the GRID, not a metric bug; `computeOverflow` is correct** (via
      #31). `XPlace In OVFW` (= `gp_ovfl_in`) and `Our Exact OVFW` are now standing `results.csv`
      columns. An **independent naive rectangular-overlap reference** on `fft_2`'s shipped `.def`
      reproduces OUR overflow exactly at every grid (512→0.161, 256→0.063, **128→0.020**), so our
      metric is physically correct. The 7× gap was that **XPlace evaluates the overflow at its
      row-capped grid** — `fft_2` has 171 rows so XPlace caps to **128** (its eval log:
      *"num_bin_y 512 is larger than num_rows 171. Use 128"*) — while **we ran the raw 512**. Naive at
      128 = 0.020 ≈ XPlace's 0.0215; at 512 = 0.161 = ours. Same metric, different grid.
      **Fixed:** sw_only now caps an explicit grid at `num_rows` too (`Setup.cpp`, the `row_cap`
      branch), matching XPlace, so both evaluate at the same grid. This also completed #31's grid fix
      (8 more low-row designs were still running at 512). `make test-regress` bit-identical (auto path
      unchanged). → [[_NEW_REPORT_31_overflow_stall_grid_20260815.md]]
      **Reporting follow-up 2026-08-18:** `results.csv`'s `grid` column was still printing the
      *requested* grid, so all 21 `mgc_*` rows read 512 while 12 of them had actually run at the
      capped 128/256 — the fix was in, the artifact hid it. `dse.py::summarize` now scrapes the
      effective `bins_per_row` from each run's `run_summary.md` and writes THAT into the single
      `grid` column (identity joins still use the requested value gp_only.csv/sweep.json share;
      unreadable run dirs now warn instead of silently showing the request).
      `results/DSE_20260817_223934/results.csv` regenerated: every other column bit-identical.
      Capped at 128: `mgc_fft_1/fft_2/pci_bridge32_a/des_perf_1`. At 256: `des_perf_a/des_perf_b/`
      `edit_dist_a/fft_a/fft_b/matrix_mult_1/matrix_mult_2/pci_bridge32_b`.
      ⚠️ One real *macro-design* difference remains, separate and minor: XPlace **scales** the
      fixed/blockage density by td (`initializer.py:82`) while we **cap** it at td (`Grid.cpp:139`);
      equal at td=1, ours slightly higher at td<1 in macro-perimeter bins. `fft_2` has 0 fixed cells
      so it was NOT this. Worth a faithfulness fix but does not affect the fillerless std-cell designs.
      <details><summary>RETRACTED twice on 2026-08-15: "metric diverges at td<1", then "grid+td config gap"</summary>

      > - [~] **NARROWED — NOT a config gap; the metric itself diverges at td<1** … on `mgc_*` it is
      >       2–7× high even when td AND grid match (`fft_2`: both grid 512). Root not yet localized.

      Wrong: I had read the *requested* 512 from XPlace's eval-log header and MISSED the cap warning
      below it — XPlace actually evaluated fft_2 at 128. Grid did NOT match; that was the whole cause.
      The naive reference (matching ours at 512, XPlace at 128) settled it. Kept as the trail.
      <details><summary>RETRACTED earlier same day: "EXPLAINED — entirely grid + target_density"</summary>

      > - [x] **EXPLAINED 2026-08-15 — the metric is correct; the "~2-4×" was the config gap.** … The
      >       `mgc_*` discrepancy is **entirely** two config divergences: (1) grid; (2) `target_density`
      >       — we use the DEF constraint, XPlace uses 1.0 (#25).

      Retracted the SAME day: the `target_density` half is FALSE — XPlace uses per-design td matching
      ours exactly (corrected #25), and at matched td+grid the overflow still diverges 7× on fft_2.
      The ISPD2005 (<0.3%) agreement holds, but it only validates the metric at td=1; the td<1 gap is
      the real, still-open reconciliation. Kept verbatim as the retraction trail.
      </details>
      </details>
      <details><summary>Superseded (original): "consistent ~2-4×, direction unexplained; diagnostic"</summary>

      > - [~] **Reconcile XPlace's overflow on our given solution** (`gp_ovfl_in`) with our
      >       `computeOverflow` — a consistent ~2-4×, direction unexplained. Fillers explain part of it
      >       (#19a), but a residual ~2.2× remains on adaptec1/adaptec4 and **newblue1 has the opposite
      >       sign**. The discriminator is a macro-INCLUDED variant of our own metric. Until then that
      >       column is a diagnostic, not a metric.

      Still the live question. The "opposite sign" (newblue1) was `Best OVFW` being *smoothed* (it
      under-reads exact by ~3×); the exact-vs-exact gap is the density-metric divergence above.
      </details>

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

## #17 — sw_only regression tripwire (opened 2026-08-05, BUILT 2026-08-05)

`vck5000/test/regress/`, run with `make test-regress`. Per design it asserts two things, both
**exact, no tolerance**: `iterations.dat` matches the committed baseline row for row, and the
`sha256` of the output `.def` matches. `vck5000/test/regress/README.md` is authoritative — read it
before touching a baseline. The frozen configs are **snapshots, not live copies** of
`run_config.toml`, and `random_seed = 42` is pinned and load-bearing.

- [x] **CLOSED 2026-08-17 by the sw_only freeze — "nothing checks quality, only stability" is no
      longer the right complaint.** The entry existed to catch *quality drift during active
      development*: a reproducibly wrong sw_only passes a stability check. Freezing inverts the
      requirement — bit-identical trajectory + position hash is now a **stronger** guard than any
      XPlace-ratio tolerance would be, because it admits no drift at all, and the ratio is pinned
      instead by the committed 28-design headline (median 1.0096 / mean 1.0113).
      Not building the reference-number-plus-tolerance harness that was scoped here.
      ⚠️ **Reopen this if the freeze lifts.** The gap it names is real and returns the moment
      sw_only's behaviour is allowed to move again. The blind spot that *does* still matter under
      the freeze is coverage, not quality — see #23's "add one large design", which is open.
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
      ⚠️ **The best-position DDR buffer must hold the LOOKAHEAD `v_k`, not the committed `u`** — see
      the sw_only-parity note below. Getting this wrong is invisible: both are float32 positions of
      the right shape, and a wrong choice costs a fraction of a percent of HPWL, not a crash.

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

## #34 — MMS regressed 1.0161 → 1.0351 on the frozen binary, and #3 carries it (opened 2026-08-19)

**The tier-3 run the freeze was waiting on has landed** — `vck5000/results/DSE_20260819_152124`,
16/16 scored, and it is **worse than the pre-fix 2026-08-14 run**:

| | 2026-08-14 (pre-fix) | **2026-08-19 (frozen binary)** |
|---|---|---|
| median | 1.0137 | **1.0192** |
| mean | 1.0161 | **1.0351** |
| within ±2% | — | 9 / 16 |
| better than XPlace | — | 4 |

**The split by `target_density` is the whole story, and it is clean.** #3's cap→scale is
*provably algebraically identical at td=1* (`min(ρ,1)·td` == `min(ρ,td)` when td=1), so the td=1
half isolates #32's 7a/7b and the td<1 half carries 7a/7b **plus** #3:

| group | designs | mean Δ |
|---|---|---|
| **td < 1** (#3 active) | adaptec5, newblue1-7 | **+4.99 pp — all 8 worse** |
| **td = 1** (#3 a no-op) | adaptec1-4, bigblue1-4 | **−1.19 pp — 5 of 8 better** |

So **7a/7b is a net win on MMS**, and **#3 costs roughly +6 pp** on the designs it touches. The
magnitude tracks (1−td) as the formula predicts: td=0.5 → `adaptec5` **+15.21 pp**,
`newblue4` +5.12, `newblue5` +5.05; td=0.8 → +0.56…+9.72; td=0.9 → `newblue2` +0.72.
(Biggest improvement anywhere: `bigblue3` **−8.15 pp**, td=1.0, i.e. 7a/7b.)

⚠️ **On ISPD, #3 cost about +0.10 pp; here it costs ~+6 pp — roughly 60×.** (Careful with the
basis: ISPD's headline +0.13 pp is the WHOLE three-fix bundle, not #3. Split it — ISPD2005 is all
td=1, so its +0.05 pp is 7a/7b alone; ISPD2015 moved +0.15 pp carrying both, leaving ~+0.10 pp for
#3.) **That is consistent, not contradictory:**
ISPD is std-cell, so the fixed-density path barely fires. Measured — the our/XPlace exact-overflow
ratio on ISPD is **unchanged** pre-vs-post #3 at every td band (td=0.65: 0.791→0.834; td=1.00:
0.998→0.999). MMS is where fixed blockage area actually matters, and it is the tier that was never
re-run before the freeze was called.

- [ ] **DEFECT — `computeOverflow` still uses the PRE-#3 cap, and its comment claims otherwise.**
      Found reading both functions, **not** inferred from the numbers. #3 changed the solver's
      field but not the metric:
      - `Grid::clampFixedDensity` (`Grid.cpp:143-152`) — **new**: `min(overlap, bin_area) * td`
      - `Placer::computeOverflow` (`Density.cpp:324-326`) — **old**: `min(density, bin_area*td)`,
        under the comment *"Fixed baseline at exact size, capped per bin (**mirrors
        clampFixedDensity**)"*. **It no longer mirrors it.**
      This is exactly the failure `CLAUDE.md` names: *a comment that names an upstream function is
      a claim; check the code below it computes that function.*
      ⚠️ **I have NOT established that this defect causes the MMS regression** — the sign of its
      effect on the reported metric is opposite to what is observed against XPlace, so something
      else is also in play. Treat it as a real inconsistency that must be fixed regardless, and as
      the first suspect, not as the diagnosis.
- [x] **FIX LANDED 2026-08-21 (`02464d0`) — all copies now compute `min(ρ,1)·td`.** Mark
      authorized it under the freeze. **A census found FOUR code copies, not three**, plus two doc
      comments stating the contract:

      | where | was | now |
      |---|---|---|
      | `Grid::clampFixedDensity` | `min(ρ,1)·td` | canonical, unchanged |
      | `Placer::computeOverflow` (`Density.cpp`) | `min(ρ,td)` | **fixed** |
      | `density_bin.hpp` (pl_algo HLS) | `min(ρ,td)` | **fixed** |
      | `test/density_bin_model.cpp` **×2** | `min(ρ,td)` | **fixed** (both impls) |

      **Regress-tier delta — both changed designs got BETTER, and it scales inversely with td
      exactly as the formula predicts:**

      | design | td | HPWL | iters | exact overflow |
      |---|---|---|---|---|
      | `mgc_fft_a` | 0.50 | **−1.05%** | 632 → 611 | 0.0530 → 0.0448 |
      | `mgc_pci_bridge32_b` | 0.14 | **−20.67%** | 713 → 651 | 0.0638 → 0.0529 |
      | `mms_adaptec1` | 1.00 | **bit-identical** | 1274 | sha `91cbbdee0d59` |

      ⚠️ **`mms_adaptec1` is the CONTROL, not a datapoint.** The formulas are algebraically equal
      at td=1, so its unchanged sha is what proves only the intended thing moved; its baseline diff
      is 4 header lines over a bit-identical body. A change here would have meant a mistake.
      ⚠️ **These are regress-tier GP numbers on 2 small designs — NOT suite numbers.** Do not quote
      −20.67% as a result. The suites are running (`DSE_20260821_112603` = ISPD 28, MMS follows).
      Also added a pointer comment at `Grid::clampFixedDensity` naming all three reproductions,
      since drift between them is what cost this task.
- [x] **BOTH SUITES RE-RUN 2026-08-21 — ISPD confirms the fix; MMS DISPROVES my hypothesis.**
      New goldens: `.claude/2_ARTIFACTS/results/GOLDEN_sw_only_frozen_20260821/` (ISPD, supersedes
      the 08-17 one, renamed `SUPERSEDED_sw_only_20260817_pre34/`) and
      `.claude/2_ARTIFACTS/results/MMS_sw_only_frozen_20260821/` (MMS, **not** golden).

      **ISPD: median 1.0097→1.0094, mean 1.0126→1.0112, within±2% 22→24.** `pci_bridge32_a` and
      `_b` (the two lowest-td ISPD2015 designs) carry essentially the whole move — GP HPWL −4.78%
      and −8.67%, and `_a`'s overflow gap against XPlace's own report of the same placement closed
      from 0.0151 to 0.0001. Independent confirmation the fix is correct.

      **MMS: mean 1.0161 (08-14, pre-#3) → 1.0351 (08-19, broken) → 1.0347 (08-21, fixed).**
      Fixing the metric moved MMS's mean by **0.04 pp — noise.** ⚠️ **This falsifies the working
      theory that a stale `computeOverflow` was driving the MMS regression** by feeding the
      schedule a wrong stop decision. `Grid::clampFixedDensity` — the solver's actual density
      field — was correct *before* this fix and is unchanged *by* it, so ruling out the metric
      leaves the field itself as the cause: **`#3`'s `min(ρ,1)·td` formulation, applied correctly,
      makes MMS worse on macro-heavy / mixed-size designs specifically**, even though it is more
      faithful to XPlace and it *helped* std-cell ISPD designs at the same td range.
      Per-design (unchanged from the 08-19 breakdown, confirming td<1 is still the carrier):
      `adaptec5` (td=0.5) +15.02 pp, `newblue1` (td=0.8) +9.68 pp vs the 08-14 baseline.

      **#34 is PARTIALLY closed.** The four-copy consistency fix stands — it is correct regardless
      of the MMS outcome (three copies silently disagreeing is a bug on its own terms, and the
      td=1 control proves it changed only the intended thing) and it demonstrably helped ISPD.
      **The MMS regression itself is NOT resolved and is NOT explained.** Continued under **#35**
      rather than left open here, since the diagnosis has changed: it is no longer a metric
      question, it is a "why does XPlace's own fixed-density formula hurt phase-2/mixed-size
      convergence" question, and that needs its own investigation.

- [ ] **STILL OPEN — `make test` does not actually verify the shipped HLS header.**
      `test/density_bin_model.cpp` **reproduces** `density_bin.hpp` rather than including it, so
      its (genuine, bit-exact) agreement between two reference implementations says nothing about
      the module that ships. That is #20 step 3's *"include the real header; delete
      `density_bin_model`'s own stale copy"* — and this task is exactly why it matters: the
      duplicate is how the module silently kept the old formula for four days.
- [ ] **⚠️ ESCALATED 2026-08-19 — `computeOverflow` is the CONVERGENCE SIGNAL, not just a report.**
      Verified by enumerating its callers: `Output.cpp:568` (*"convergence signal"*, the smoothed
      call that drives the γ/λ schedule and the stop test) and `Phase2.cpp:216-217` (the phase-1→2
      transition). The other call sites are reporting. **So the metric/field inconsistency above is
      live inside the optimizer**, not cosmetic — sw_only is currently optimizing one density map
      while deciding when to stop from a different one, on every td<1 design.
      **Two consequences that change the shape of this task:**
      - **Fixing it re-baselines ISPD as well as MMS**, so it invalidates
        `GOLDEN_sw_only_frozen_20260817`. The golden is not safe to build on until #34 closes.
      - It strengthens the mechanism: post-#3 the *field* says a partly-blocked bin has headroom
        (weaker density force there) while the *metric* still says it is full (overflow stays high,
        GP keeps running). Mismatched signals on exactly the designs that regressed.
      ⚠️ **Still not proof.** The direction of the raw overflow comparison does not line up — with
      the old cap our metric should read HIGHER than XPlace's and it reads LOWER on MMS td<1
      (`adaptec5` 0.1986 vs 0.3958). Something else is also in play. The mechanism is plausible and
      the inconsistency must be fixed either way; the proof is running the fix.
- [ ] **⚠️ THIS IS WHAT MAKES #34 BLOCK pl_algo — there is a THIRD copy, and it is also stale.**
      `pl/src/pl_algo/src/modules/density_bin.hpp:75` computes
      `cap = target_density * bin_area` and clamps `if (acc_URAM[i][y] > cap) ... = cap` — the
      **pre-#3** `min(ρ,td)`. Three copies of one quantity, disagreeing two ways:

      | where | formula | state |
      |---|---|---|
      | `Grid::clampFixedDensity` (sw_only solver field) | `min(ρ,1)·td` | **new (#3)** |
      | `Placer::computeOverflow` (sw_only convergence signal) | `min(ρ,td)` | **stale** |
      | `density_bin.hpp:75` (pl_algo) | `min(ρ,td)` | **stale** |

      #20 step 3 names `density_bin` as one of the six modules needing a tier-1 harness, and its
      golden is the sw_only field that changed. **Writing that harness before #34 closes means
      verifying against a golden that is about to move** — the exact waste `rules.md` exists to
      prevent. Same for `node_footprint`, `force_gather`, `metrics`.
- [ ] **DECISION FOR MARK — do not freeze MMS at 1.0351 without settling this.** Three options:
      **(a)** fix the metric side of #3 and re-run tier3 (~the length of one MMS suite);
      **(b)** revert #3 entirely — the td=1 evidence says 7a/7b alone would leave MMS *better* than
      2026-08-14 — and accept the ISPD golden loses its #3 component too;
      **(c)** accept 1.0351 as the faithful cost, per `CLAUDE.md`'s prefer-XPlace rule.
      ⚠️ **(c) is the standing default and I am flagging against applying it blind here.** The rule
      exists for a faithfulness change that costs a fraction of a percent, which is what #3 looked
      like when it was judged on ISPD alone. +15 pp on one design and +5 pp on a tier is a
      different question, and the metric/field inconsistency above means we have not yet measured
      #3 cleanly.
- [ ] **Reading of XPlace, for whoever takes this.** `init_density_map` is normalized to (0,1)
      (`database.py:671` comment), `.clamp_(0,1).mul_(target_density)` (`initializer.py:82`), and
      overflow thresholds at `(density_map - target_density) * bin_area` (`evaluator.py:48`). On a
      half-blocked bin at td=0.5 that leaves 0.25 of headroom for movable cells; the pre-#3 cap
      left **zero**. So **#3's direction is right and the old code was wrong** — which is what
      makes the regression worth explaining rather than simply reverting.
- [x] **#30's tier-3 spot-check PASSES** — the long-open "MMS via `make dse --designs tier3` has
      never been validated" bullet. All **16/16** `XPlace DP HPWL` values in `results.csv` match
      `benchmarks._XPLACE_MMS_FINAL` (tolerance 1e-4; `results.csv` stores ~5 significant figures,
      so a tighter bound reports false mismatches). `--designs tier3` needs no special config:
      `mixed_size_mode` is auto-detected from `num_movable_macros > 0` (`Setup.cpp:393`) and
      `enable_phase2` defaults true.

---

## #35 — MMS regression is intrinsic to `#3`'s field, not the metric — CLOSED 2026-08-25 (landed D)

**CLOSED 2026-08-25.** Root-caused to `#3`'s faithful scale, and **experiment D landed**: the
fixed-density formula reverted to the cap `min(rho,td)` in all four sites — a deliberate,
Mark-authorized divergence from XPlace, registered in `CLAUDE.md` under "Deliberate divergences
from XPlace". Worth **+2.38 pp of MMS mean** (D vs HEAD, isolated clean; D 1.0110 beats even
pre-`#3` A 1.0161). Regress baselines regenerated (`mgc_fft_a`, `mgc_pci_bridge32_b`; td=1
`mms_adaptec1` bit-identical, confirming the no-op-at-td=1 claim). The keep-scale-and-fix-the-
fixed-node-field alternative was recorded and **not taken** — see the ◐ note below if MMS ever
returns to focus. Full record below stays for the trail; a task-indexed copy goes to history.md.

→ [[_NEW_HANDOFF_35_mms_density_regression_20260821.md]] — the original handoff. Carries the
three-way numbers, the per-design td split, what was ruled out, and the XPlace-side reading.

**Spun off #34 at partial close.** #34 hypothesized the MMS regression (mean 1.0161 → 1.0351,
2026-08-14 → 2026-08-19) was caused by `Placer::computeOverflow` lagging `Grid::
clampFixedDensity`'s formula change. That hypothesis is **falsified**: fixing the metric
(`02464d0`) moved MMS's mean by 0.04 pp. See `.claude/2_ARTIFACTS/results/
MMS_sw_only_frozen_20260821/README.md` for the three-way comparison.

**What is established:**
- The regression is carried entirely by the **8 td<1 designs** (`adaptec5`, `newblue1-7`); the 8
  td=1 designs are flat once #32's 7a/7b is subtracted out (`bigblue3` −8.13 pp is 7a/7b).
- Magnitude tracks (1−td): `adaptec5` (td=0.5) +15.02 pp, `newblue1` (td=0.8) +9.68 pp,
  `newblue2` (td=0.9) +0.73 pp.
- The same field change **helped** ISPD2015's lowest-td designs (`pci_bridge32_a/b`, td=0.384/
  0.143) by 4-9% GP HPWL, and its overflow now matches XPlace's own report to within 0.0001.
- So the formula is not simply wrong — it is a correct, more-faithful formula that helps one
  design class and hurts another, and the discriminator is not td alone (ISPD's lowest-td design
  improved; MMS's did not) so something else about MMS specifically is the missing variable.

**RESOLVED 2026-08-25 — experiment D isolated `#3`, and it IS the whole MMS regression.**
`DSE_20260824_161319` (throwaway; formula reverted to `min(ρ,td)` in all four sites, everything
else at HEAD; source reverted after, regress bit-identical). D vs C isolates `#3` alone:

| | td=1 (8 designs) | td<1 (8 designs) | mean |
|---|---|---|---|
| C (HEAD, faithful `min(ρ,1)·td`) | flat | regressed | 1.0347 |
| **D (revert `#3`, else HEAD)** | flat (±0.04) | recovered | **1.0110** |
| A (pre-`#3`, 2026-08-14) | — | — | 1.0161 |

`#3` alone moves the MMS mean **−2.38 pp** (D−C), td-split perfectly clean: every td=1 flat,
every td<1 recovers, magnitude tracking (1−td). adaptec5 −14.57 pp (recovers the +15.02 it
regressed), newblue1 −8.47, newblue4/5 −5.2. **D (1.0110) beats even pre-`#3` A (1.0161)** by
0.52 pp, because D keeps `#32`'s 7a/7b (bigblue3 −8.12) and the rest of HEAD that A lacked. So
`#3` is the entire regression; reverting it while holding everything else at HEAD is the best MMS
result on record. Per-design table + scripts: `/tmp/cmp3.py`, `results/DSE_20260824_161319/`.

**Mechanism, from static reads (2026-08-25):**
- ❌ **Fillers are NOT the divergence.** `rebuildFillers` at the phase boundary is idempotent:
  `freezeMovableMacros` → `computeAreaBreakdown` moves macro area from movable→fixed, so
  `addFillers`'s two inputs (`stdcell_placeable_area`, `stdcell_area`) are invariant and the
  `stdcell_util>td` raise never fires. Confirmed in the D logs: adaptec1/newblue1/newblue2 all
  show identical filler count and unchanged td across the transition. (Lead 2 of the handoff, dead.)
- ❌ **Frozen-macro deposit weight is NOT the divergence.** The area-conserving weight is exactly
  1.0 for any node ≥ √2 bins (i.e. every macro), and XPlace zeroes the frozen macro out of the
  movable field (`cache_node_weight = -1.0`, `run_placement_nesterov.py:203`) — same as ours.
  (Lead 1 of the handoff, narrowed to dead for macro-sized nodes.)
- ◐ **One live faithful-vs-us divergence remains, untested:** our density *field*
  (`computeNodeOverlaps`, `m_clamp_density=true`) runs FIXED nodes through the √2 inflation;
  XPlace's `init_density_map` deposits fixed/frozen nodes at raw size, weight 1.0, no inflation.
  Zero effect above √2 bins; below it we smear a fixed node wider/lower than XPlace. This is the
  candidate mechanism by which the faithful scale hurts, and the lever for a keep-`#3`-and-fix path.

**The standing decision — DECIDED 2026-08-25: land D (arm a).**
- [x] **Accept D or fix macro handling? → LANDED D.** Mark's call 2026-08-25: take the measured
      −2.38 pp MMS win; MMS isn't the main focus but 2.38 pp is noticeable. The cap `min(ρ,td)` is
      *un-faithful* to XPlace, so this deliberately overrides `CLAUDE.md`'s prefer-XPlace rule —
      registered there under "Deliberate divergences from XPlace" and stamped in all four code
      comments so it isn't reverted-to-faithful by accident. Regress baselines regenerated with the
      reason recorded in-file; td=1 `mms_adaptec1` bit-identical.
      The **not-taken** alternative, kept for the record: (b) **keep the faithful scale and fix the
      fixed-node field divergence above** (the ◐ note), then re-measure. Untested. The mechanism it
      would test: the scale is faithful, ISPD has no frozen-macro/phase-2 path, so the MMS-specific
      cost most likely enters through fixed-node field smearing. Revisit only if MMS returns to
      focus AND matching XPlace on the field becomes worth a re-measure; the `◐` note is the lead.

---

## #30 — Legalization + detailed placement inside `dse.py` (opened 2026-08-12)

Mark, 2026-08-12: *"As a future goal, we also need to incorporate legalization and Detailed
placement, which Xplace handles for us."*

**Why it matters, not just tidiness:** GP-vs-GP is the *flattering* comparison — legalization costs
1–8% HPWL and an under-spread GP pays more of it (TODO #3). Post-DP is the number the XPlace paper
reports and the only one that settles whether we are actually better or worse. Today it takes a
separate multi-step pipeline that a sweep does not touch, so most sweeps are scored on the metric
that hides the defect.

**Everything needed already exists — this is wiring, not new capability:**

| piece | where | note |
|---|---|---|
| XPlace post-LG / post-DP reference, all 28 + 16 | `benchmarks.py::_XPLACE_ISPD_FINAL[2:4]`, `_XPLACE_MMS_FINAL[2:4]` | already in the manifest |
| LG+DP on **our** placement | `tools/run_lgdp44.sh` (ispd2005+2015), `run_lgdp_suite.sh` + `gen_lgdp_inputs.py` (mms) | runs XPlace's own legalizer, `main.py --global_placement False` |
| our DEF/`.pl` → legalizer input | `def_patch_placement.py` (LEF/DEF), `def_to_bookshelf_pl.py` (bookshelf) | frame is bit-perfect |
| scorecard | `analyze_full44.py`, `analyze_lgdp_suite.py` | the tables we quote |

- [x] **LG+DP in `dse.py` — done 2026-08-13, and ON BY DEFAULT** (Mark's call, overriding the
      opened-as "off by default" plan: *"I'd like dse to call the legalization and DP to run on our
      GP solution by default. Add a param --gp-only as a way to NOT run LG+DP."*). Each GP result is
      legalized + detailed-placed through XPlace; `--gp-only` stops after GP. Summary gains
      `Our LG HPWL` / `Our DP HPWL` / `XPlace DP HPWL` / `DP Ratio`, with a median/mean/within-2%
      footer. The per-design core is a new `tools/lgdp.py` (`legalize(bench_path, gp_def, work_dir)
      -> {lg, dp, variant, status}`), porting the `run_one()` of both bash runners — all three
      format paths (bookshelf, ispd2015 `--custom_path`, ispd2015_fix fence). Each run→DEF is mapped
      via results.csv's `Output Dir` column (robust across A/B sweeps), and the `.def` basename is
      globbed (`RowBasedPlacement.def` vs `fft.def` vs …). LG is decoupled from GP so `--resume`
      backfills a run whose GP finished but LG did not; results persist in `lgdp.json`.
      **Verified** end-to-end on all three paths against known refs: adaptec1 **1.001** (the
      anchor), mgc_fft_2 1.028, fenced mgc_des_perf_a 1.018. **Frame handled correctly:** post-DP
      HPWL is scraped from XPlace's own log so it is already in XPlace's frame — the ratio against
      `xplace_dp_hpwl` needs **no** ×site_width (the opposite of #29's raw-DBU GP number; this is
      analyze_full44.py's frame rule, and it is why LG/DP was the easy half).
- [x] **DP results in the SAME results.csv — done 2026-08-14** (`edd268f`). The exe can't know DP
      (it finishes before legalization), so dse.py enriches results.csv after LG+DP with
      `Our LG HPWL` / `Our DP HPWL` / `XPlace DP HPWL` / `DP Ratio`, alongside the GP comparison
      (#29). One file, one reference table (`benchmarks.py`).
- [x] **Full-suite cross-check — PASSED 2026-08-14.** A single `make dse` (GP+LG+DP+pipeline overlap)
      over all 28, `results/DSE_20260814_133037`, reproduces the standing pipeline's committed
      numbers **exactly** (to 4 decimals), per tier: ispd2005 1.0053/1.0052, ispd2015 1.0163/1.0189,
      **all 28 median 1.0106 / mean 1.0149, 21/28 within 2%, better on 4** — identical to
      `summary.md`. Expected: GP is deterministic (seed 42), the Output.cpp changes were schema-only
      (test-regress bit-identical), XPlace's legalizer is unchanged. The falsification criterion
      ("dse.py's per-design DP ratio == analyze_full44.py's within rounding") is met.
- [ ] **Collapse the two suite runners — now UNBLOCKED.** With the cross-check passed, retire
      `gen_suite_configs.py` + `run_suite.sh` + `run_lgdp44.sh` + `run_lgdp_suite.sh` + the two
      `analyze_*` scorecards (folding the last bits — smallest-first ordering, the MMS `--mixed_size`
      arm — into `dse.py`/`lgdp.py`; `lgdp.py` already subsumes their `run_one()`). Needs Mark's
      go-ahead since these produced every headline number until today. MMS/tier-3 via `make dse
      --designs tier3` should be spot-checked against `_XPLACE_MMS_FINAL` first (this run was
      tier1+tier2 only).

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
