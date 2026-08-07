# Phase 2 through the MMS suite — 15 of 16 designs, one new finding, one correction

**Date:** 2026-08-02 · Handoff item 1 (`1_REVIEW/_NEW_HANDOFF_phase2_breadth_20260801.md`). Ran the
15 MMS designs not covered by `NEW_REPORT_phase2_implemented_20260801.md` (newblue5). Same tree
as that report (branch `pl_algo`, commits through `451867b`) plus this session's changes: item 3
(longest-path legalizer repair), item 4 (macro-excluded overflow axis), item 5c (phase-2
`convergence_include_fillers` always on). **Config:** each design's own XPlace grid/target_density
(`tools/benchmarks.py`), seed 42, `macro_deposits_target_density=false` (unchanged from baseline —
that flag is item 2's axis, deliberately not touched here), `convergence_max_iterations=10000`
(template default). Sequential run, 00:53–05:38, all 15 exit 0. Generator/runner:
`2_ARTIFACTS/gen_phase2_suite_configs.py` + `2_ARTIFACTS/run_phase2_suite.sh`, raw data
`2_ARTIFACTS/phase2_suite_results.tsv`.

---

## 1. Correction to the handoff's premise: no design in this suite is phase-2-inert

The handoff's item 1 expected **8 macro-heavy (td<1.0) designs where phase 2 activates** and
**8 td=1.0 designs where it's a no-op** ("no movable macros"). That's wrong for this suite — MMS's
adaptec1–4 and bigblue1–4 (same names as ISPD2005, different files) **do carry movable macros
despite `target_density=1.0`**; td and macro presence are independent knobs. Measured macro counts
this run: adaptec1 62, adaptec2 127, adaptec3 58, adaptec4 69, bigblue1 31, bigblue2 **959**,
bigblue3 69, bigblue4 199, newblue1 53, newblue2 25, newblue3 51, newblue4 81, newblue6 74,
newblue7 161 (newblue5 91, from the earlier report). **14 of 15 designs here entered phase 2** —
every one of them, td=1.0 or not.

## 2. adaptec5 is the one exception, and it's correct that it stayed in phase 1

adaptec5 (td=0.5) never transitioned: phase 1 ended `diverged_hpwl` at iteration 1163, and
`beginFixedMacroPhase()`'s eligibility gate (`Phase2.cpp:56-58`) deliberately excludes
`diverged_hpwl`/NaN stops — "a genuinely diverged phase 1 has no macro placement worth freezing."
This matches the pre-existing baseline exactly: `mms_baseline_20260731.tsv` also shows adaptec5
stopping `diverged_hpwl` at 1163 iterations (single-phase code, before any of this session's
changes). adaptec5 is one of the three designs in memory `mms-hard-spreading-three-diseases`
(λ-starved/under-damped macro divergence) — not a new failure, and not something phase 2 was ever
going to fix, since there's no legal macro placement to freeze from a diverged run.

## 3. The legalizer repair loop (item 3) fired for real, on adaptec2

Every other design's constraint graph was already feasible after step 1 (direction assignment)
alone — `runLongestPathRefinement` never mutated an edge, matching newblue5's earlier result.
**adaptec2 is the first real trigger:** total negative slack **-3.53e3 → 0 ("repaired")**, and the
subsequent LP then resolved all **42 of 42** overlapping pairs to zero with the repaired graph. This
is the first non-synthetic validation that the repair loop's edge migration is not just
code-reviewed against the XPlace source but actually produces a feasible, LP-solvable graph on a
real design.

```
DETAIL  Macro legalization: longest-path refinement total negative slack -3.53e+03 -> 0  (repaired)
  INFO  Macro legalization: 127 macros, overlap pairs 42 -> 0, total displacement 1.02e+04  [LP/CBC]
```

## 4. Results

Baseline = `2_ARTIFACTS/mms_baseline_20260731.tsv` (single-phase, pre-filler-fix code, frozen
2026-07-31). **The delta below is NOT phase 2 in isolation** — the XPlace-faithful filler-sizing fix
(`7a0d030`) landed in the same window and changes phase 1's own filler population, same as it did
for the original newblue5 report. `ovfl_macro_excl` is the new item-4 axis (sharp, no filler,
movable macros dropped from the deposit — the number comparable to XPlace's phase-1 Mixed-GP
reference, `tools/benchmarks.py::_XPLACE_MMS_MIXED_GP`, itself also macro- and filler-excluded).

| design | td | macros | baseline HPWL (phase-1-only, old) | **new HPWL (phase 2)** | ΔHPWL | new stop reason | new ovfl (macro-excl) | XPlace phase-1 ref (macro-excl) |
|---|---|---|---|---|---|---|---|---|
| adaptec1 | 1.0 | 62 | 6.360e7 | 6.385e7 | +0.4% | converged | 0.109 | 0.131 |
| adaptec2 | 1.0 | 127 | 7.228e7 | 7.320e7 | +1.3% | divergence_guard | 0.086 | 0.096 |
| adaptec3 | 1.0 | 58 | 1.600e8 | 1.576e8 | −1.5% | divergence_guard | 0.071 | 0.125 |
| adaptec4 | 1.0 | 69 | 1.411e8 | 1.445e8 | +2.4% | divergence_guard | 0.102 | 0.135 |
| adaptec5 | 0.5 | 76 | 3.245e8 | 3.020e8 | −6.9% | **diverged_hpwl (phase 1 only)** | — | 0.149 |
| bigblue1 | 1.0 | 31 | 8.395e7 | 8.445e7 | +0.6% | converged | 0.093 | 0.174 |
| bigblue2 | 1.0 | 959 | 1.219e8 | 1.247e8 | +2.3% | divergence_guard | 0.079 | 0.105 |
| bigblue3 | 1.0 | 69 | 2.822e8 | 2.721e8 | −3.6% | divergence_guard | 0.083 | 0.124 |
| bigblue4 | 1.0 | 199 | 6.494e8 | 6.570e8 | +1.2% | divergence_guard | 0.083 | 0.130 |
| newblue1 | 0.8 | 53 | 6.156e7 | 5.894e7 | −4.3% | converged | 0.125 | 0.136 |
| newblue2 | 0.9 | 25 | 1.562e8 | 1.490e8 | −4.6% | converged | 0.088 | 0.143 |
| newblue3 | 0.8 | 51 | 2.449e8 | 2.638e8 | +7.7% | divergence_guard | 0.087 | 0.040 |
| newblue4 | 0.5 | 81 | 2.426e8 | 2.338e8 | −3.6% | divergence_guard | 0.179 | 0.182 |
| newblue6 | 0.8 | 74 | 4.159e8 | 4.216e8 | +1.4% | divergence_guard | 0.105 | 0.142 |
| newblue7 | 0.8 | 161 | 8.194e8 | 8.911e8 | +8.7% | divergence_guard | 0.093 | 0.152 |
| newblue5* | 0.5 | 91 | 4.412e8 | 3.987e8 | −9.6% | converged | 0.076 | 0.170 |

\* newblue5 from `NEW_REPORT_phase2_implemented_20260801.md`, **not re-run this session** — its
numbers predate item 5c (`convergence_include_fillers` now always on in phase 2), so are not
directly comparable to the other 15 rows' stop-reason column. Re-run before citing newblue5
alongside this table in anything quality-sensitive.

**Reading it:** HPWL moves by single-digit percent either way on 13 of 15 — no systematic
regression, no systematic win. newblue3 (+7.7%) and newblue7 (+8.7%) are the two outliers, both
already-known hard designs (`mms-hard-spreading-three-diseases`: newblue3's baseline row is itself
flagged in that TSV as a *fake* early-stop win, not a clean baseline to beat). The macro-excluded
overflow column undershoots the XPlace phase-1 reference on every row **except** newblue4 and
newblue5, which land close to or above it — but this is comparing our **phase-2 endpoint** against
XPlace's **phase-1 (Mixed-GP)** endpoint, since no local XPlace phase-2 log exists for these 15 (only
newblue5 has one, from Mark's 2026-07-17 run). That is not the intended final comparison, just the
best one available without new XPlace runs — treat the "vs XPlace" column as directional, not a
verdict.

## 5. Not done

> ⛔ **RETRACTED 2026-08-04 — the first bullet below is WRONG. Do not schedule those GPU runs.**
> The phase-2 / LG / DP reference already existed when this was written. Every 2026-07-17 log in
> `~/phd/Xplace/result/` carries the **whole** flow — `After Mixed-GP` → macro LG → `GP Stop!` →
> `After GP, best solution eval` → `Finish Legalization` → `After DP` — for all 16 designs, not
> just Mixed-GP. It is now curated as `_XPLACE_MMS_FINAL` in `vck5000/tools/benchmarks.py`.
> The error was assuming these logs stopped at Mixed-GP.
> Correction: `1_REVIEW/reports/_NEW_REPORT_lgdp_suite_20260804.md` §1 and `0_TODO/TODO.md` #3.
> Note also that `GP Stop!` is the wrong line to want — it prints **masked** HPWL *and* overflow;
> the `... best solution eval, exact ...` lines are the comparable ones.
>
> *(Pointer added 2026-08-06. This claim had resurfaced repeatedly because this report is
> un-prefixed — i.e. read — while its correction still carries `_NEW_`.)*

- ~~**No local XPlace phase-2 (`GP Stop!`) reference exists for these 15 designs** — only newblue5
  has one. Getting a real "our phase 2 vs XPlace phase 2" number needs 15 more local XPlace runs
  (`~/phd/Xplace`, `--dataset mms --mixed_size True --seed 42`, full run not just Mixed-GP), out of
  scope for this session.~~ **← see retraction above**
- **newblue5 not re-run** under item 5c's filler-inclusive phase-2 convergence — flagged above.
- This sweep used `macro_deposits_target_density=false` throughout (matching the baseline arm).
  Item 2 (the `#11b` A/B re-run) is the next item in the handoff and covers that axis.
