# sw_only performance snapshot — MMS mixed-size suite, legal-vs-legal

**Date:** 2026-08-07 · **Commit:** `11d8901` (branch `pl_algo`) · **Variant:** `HOST=sw_only`
**Data:** `2_ARTIFACTS/faithful_suite_results.tsv` (GP), `2_ARTIFACTS/lgdp_faithful_results.tsv` (LG/DP)
**Reference:** `tools/benchmarks.py::_XPLACE_MMS_FINAL`, from the local `2026-07-17-23:0x_*` XPlace
runs (`--dataset mms --mixed_size True --seed 42`). No new GPU runs were needed.

**Status: sw_only is declared "close enough" to XPlace and closed for now.**

---

## 1. What is being compared — read this before quoting a number

This snapshot is **post-DP HPWL vs XPlace's post-DP HPWL** — legal placement against legal
placement, both produced by *the same* legalizer and detailed placer (XPlace's own, reached via
`--global_placement False --given_solution`). Both sides are the **exact, unmasked** HPWL
(`get_obj_hpwl` on XPlace's side, `Final HPWL (exact, all nets)` on ours).

Per-design target density and grid come from `tools/benchmarks.py::_ROWS` (XPlace's own
`utils/setup_dataset.py` values). `random_seed = 42`, `deterministic = true` throughout.

The suite is **MMS** — the ISPD-2005 netlists with their macros **movable**. This is a harder
regime than plain ISPD-2005 and is tracked separately from it.

## 2. Results — 16 of 16 designs

| design | td | grid | iters | stop | our post-GP | our post-DP | XPlace post-DP | ratio |
|---|---:|---:|---:|---|---:|---:|---:|---:|
| adaptec1 | 1.0 | 512 | 1325 | `converged` | 6.368e+07 | 6.782e+07 | 6.814e+07 | **0.995** |
| adaptec2 | 1.0 | 1024 | 1377 | `converged` | 7.259e+07 | 7.628e+07 | 7.618e+07 | 1.001 |
| adaptec3 | 1.0 | 1024 | 1374 | `converged` | 1.511e+08 | 1.548e+08 | 1.591e+08 | **0.973** |
| adaptec4 | 1.0 | 1024 | 1409 | `converged` | 1.397e+08 | 1.426e+08 | 1.414e+08 | 1.009 |
| adaptec5 | 0.5 | 1024 | 1434 | `converged` | 3.210e+08 | 3.230e+08 | 3.131e+08 | 1.032 |
| bigblue1 | 1.0 | 512 | 1387 | `converged` | 8.374e+07 | 8.571e+07 | 8.567e+07 | 1.000 |
| bigblue2 | 1.0 | 1024 | 1593 | `converged` | 1.219e+08 | 1.252e+08 | 1.257e+08 | **0.996** |
| bigblue3 | 1.0 | 2048 | 1096 | `converged` | 2.713e+08 | 2.833e+08 | 2.767e+08 | 1.024 |
| bigblue4 | 1.0 | 2048 | 1454 | `converged` | 6.451e+08 | 6.570e+08 | 6.464e+08 | 1.016 |
| newblue1 | 0.8 | 512 | 1427 | `converged` | 5.905e+07 | 6.071e+07 | 6.005e+07 | 1.011 |
| newblue2 | 0.9 | 1024 | 1001 | `converged` | 1.498e+08 | 1.526e+08 | 1.524e+08 | 1.001 |
| newblue3 | 0.8 | 2048 | 782 | `converged` | 2.736e+08 | 2.753e+08 | 2.727e+08 | 1.010 |
| newblue4 | 0.5 | 1024 | 1604 | `divergence_guard` | 2.326e+08 | 2.329e+08 | 2.298e+08 | 1.013 |
| newblue5 | 0.5 | 1024 | 1497 | `converged` | 3.913e+08 | 3.950e+08 | 3.899e+08 | 1.013 |
| newblue6 | 0.8 | 2048 | 1561 | `converged` | 4.124e+08 | 4.137e+08 | 4.083e+08 | 1.013 |
| newblue7 | 0.8 | 2048 | 1499 | `converged` | 8.793e+08 | 8.882e+08 | 8.803e+08 | 1.009 |

**Mean ratio 1.0074 · median 1.0093 · min 0.973 · max 1.032.**
Within ±2%: **13/16**. Within ±5%: **16/16**. Better than XPlace on **3** (adaptec3, adaptec1,
bigblue2); bigblue1 is a dead heat at 1.0004.

**Convergence: 15/16 stop on `converged`.** newblue4 is the sole `divergence_guard` exit; its
post-DP is +1.3% and its density is at parity, so it was closed as good enough rather than chased.

**Post-DP density: parity or better on all 8 designs where it is measurable.** At `td = 1.0`
post-DP overflow is identically zero by construction (legalization caps occupancy at 1.0 and the
capacity *is* 1.0), so the metric only discriminates on the 8 designs below 1.0:

| design | td | our overflow | XPlace | Δ |
|---|---:|---:|---:|---:|
| adaptec5 | 0.5 | 0.3829 | 0.3828 | +0.0% |
| newblue1 | 0.8 | 0.1440 | 0.1883 | **−23.5%** |
| newblue2 | 0.9 | 0.0876 | 0.0909 | −3.6% |
| newblue3 | 0.8 | 0.1913 | 0.1965 | −2.6% |
| newblue4 | 0.5 | 0.3355 | 0.3332 | +0.7% |
| newblue5 | 0.5 | 0.3258 | 0.3246 | +0.4% |
| newblue6 | 0.8 | 0.1410 | 0.1424 | −0.9% |
| newblue7 | 0.8 | 0.1563 | 0.1565 | −0.1% |

`max_util` is 1.000 on both sides everywhere — i.e. the legality check passing. **The wirelength
was not bought with density.**

## 3. Comparison to `PERFORMANCE_SNAPSHOT_July8.md`

### ⚠️ The two snapshots do not measure the same thing. Do not subtract the numbers.

| | July 8 | today |
|---|---|---|
| suite | ISPD-2005 + ISPD-2015, 28 designs | **MMS** (mixed-size), 16 designs |
| macros | fixed | **movable**, then legalized |
| our side | **global placement only** | **post-GP → LG → DP**, legal |
| their side | XPlace **published** post-LG+DP | XPlace's **own local run**, post-DP |
| basis | **GP vs legal — apples-to-oranges** | **legal vs legal** |
| headline | mean ratio 1.014 | mean ratio **1.0074** |

The July 8 snapshot says so itself, in its own §"Reading the ratio": our raw GP was being compared
against a number carrying a legalization penalty we had not yet paid, so *"a ratio of ~1.0 means
markv1's GP is at or ahead of XPlace's own GP"* and *"adding LG+DP would raise its HPWL (worsen
these ratios)"*. That prediction was correct and it is exactly what closing the gap cost.

**So the honest reading is not "1.014 → 1.0074".** It is: the July 8 figure was structurally
flattering by an unpaid legalization penalty, on an easier (fixed-macro) suite; today's figure has
paid that penalty, on the harder suite, and still lands at 1.0074. A like-for-like re-measurement
of the 28-design ISPD-2005/2015 suite on today's code **has not been run** — it would take a few
hours and is the obvious thing to do if this comparison ever needs to be exact.

### What changed in between — 86 commits

*(No line count is quoted here on purpose: `git diff --shortstat <jul8>..HEAD -- host/src/sw_only`
reports "22,741 insertions, 0 deletions", which is not a measure of work — the path did not exist
on July 8, because `markv1` was renamed to `sw_only` on July 10. A rename-detecting diff over
`host/` reports 25,757/36,423, which is mostly the same rename plus the `src/common` extraction.
Neither number means anything; the list below does.)*

Roughly in order of how much they moved the number:

1. **Mixed-size phase 2** (TODO #13) — macro legalization by LP (constraint graph + CBC, plus the
   longest-path refinement) and a fixed-macro std-cell restart. XPlace's headline number is
   post-phase-2; before this we could only ever match its *phase-1* endpoint. This is where most
   of the quality came from, and it is what makes a legal-vs-legal comparison possible at all.
2. **XPlace-faithful filler sizing** (TODO #13 P1) — six divergences in `addFillers`. On the
   macro-heavy low-td designs we had been placing with **zero fillers** (adaptec5 0 → 310,073,
   matching XPlace's log exactly).
3. **Movable macros deposit at `target_density`** (TODO #11b) — removes a permanent, irreducible
   overflow term that starved λ's feedback loop. Turned adaptec5's phase-1 divergence into a clean
   converged run.
4. **Two faithfulness fixes in the metric and the schedule** (TODO #19, this week) — every XPlace
   overflow metric excludes fillers, and the γ/λ throttle gates on the preconditioner ratio κ, not
   a gradient-norm ratio. Together: post-DP +1.15% → **+0.74%** and 6/16 → **15/16** converging.
5. **Faithful field + preconditioner** (TODO #2) — validated 16/16 on MMS, mean −9.6% HPWL.
6. **Infrastructure that made the above measurable:** the two-phase reference tables in
   `benchmarks.py`, `tools/post_dp_density.py`, the LG/DP harness, OpenMP threading (TODO #12),
   and `make test-regress` (TODO #17).

### Corrections to the record that came out of this period
Worth carrying forward, because each was believed and cited for days:
- **XPlace's MMS flow has two GP phases**, and phase 1's std-cell placement is *discarded*. Every
  "vs XPlace" MMS number before 2026-07-31 compared our phase 1 to its phase 2.
- **`GP Stop!` prints MASKED numbers**, for both HPWL and overflow. The `After GP … exact` line is
  the one to quote; the gap is 2–3×.
- **Every XPlace overflow metric excludes fillers** — the opposite of what was recorded for a week.
- The **July 8 GP-vs-legal caveat** above, which was written down correctly at the time and then
  quietly dropped from later citations.

## 4. What sw_only does NOT do

Stated so the number above is not over-read:

- **No routability.** HPWL and density only; no congestion map, no cell inflation, no
  detailed-routability handling (XPlace-Route, TODO #7 — deliberately out of scope, unscoped).
- **No legalizer or detailed placer of its own.** Every legal number here is produced by *XPlace's*
  LG+DP on our GP output. That is the right apples-to-apples call, but it means we do not own the
  back end.
- **No per-row site model.** `enforceDieBoundaries` clamps to the die *rectangle*; 11 of 16 MMS
  designs have a ragged core. Measured out-of-row-span cells: adaptec3 315, newblue4 25, adaptec5
  23, the rest ≤10. Harmless so far — every design legalizes — but it is an unmodelled constraint
  the downstream legalizer absorbs (TODO #3).
- **`macro_legalization_xy` / `_ilp` variants and site/row alignment are not ported** (TODO #13).
- **ISPD-2005 / ISPD-2015 have no committed reference table** — only MMS does. Any claim about
  those suites needs the logs read directly.

## 5. Open, and deliberately not chased

| item | why it is parked |
|---|---|
| **newblue4** stops on `divergence_guard` | post-DP +1.3% at density parity; no quality problem |
| **`gp_ovfl_in` reconciliation** (TODO #3) | XPlace's overflow of *our* placement disagrees with our own by ~2.2× on two designs and the *opposite* way on newblue1. Fillers explain part; macro inclusion is the likely rest. **Unresolved — treat that column as a diagnostic, not a metric** |
| **newblue2 0.143 ≈ 0.145 calibration** | contradicts the filler code reading and a fresh measurement disagrees in the opposite direction; recorded rather than dropped |
| **XPlace `legalizeBin` is UB for any zero-height movable non-macro** | real upstream bug found via TODO #3; not ours to patch |
| smoothing ramp, float-vs-double, #11a die projection | small expected value (see *Improvements* in TODO.md) |

## 6. How to reproduce

```bash
cd vck5000 && make test-regress          # tripwire, ~12 s
cd vck5000 && make test-regress-slow     # + the mixed-size design, ~3 min
```

Full suite (~5 h GP + ~10 min LG/DP), configs in `/tmp/faithful/configs` — **regenerate them into
a durable location before relying on this**, `/tmp` does not survive:

```bash
bash /tmp/faithful/run.sh
LGDP_PL=/tmp/lgdp3/pl python3 2_ARTIFACTS/gen_lgdp_inputs.py /tmp/faithful/results
LGDP_PL=/tmp/lgdp3/pl LGDP_RES=2_ARTIFACTS/lgdp_faithful_results.tsv bash 2_ARTIFACTS/run_lgdp_suite.sh
LGDP_OURS_GLOB='2026-08-06-23*' python3 2_ARTIFACTS/analyze_lgdp_suite.py 2_ARTIFACTS/lgdp_faithful_results.tsv --density
```

⚠️ Run `gen_lgdp_inputs.py` to **completion in the foreground** — backgrounding the whole
`gen && run` chain kills generation partway and silently produces a subset.

Before quoting any number from this document against XPlace, read
`.claude/skills/xplace-compare` — there are four independent ways to pick the wrong reference and
each has produced a published figure that later needed retracting.
