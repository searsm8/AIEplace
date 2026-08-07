# MMS overnight session — tasks #1–#5 (2026-07-17)

Worked the five ranked next-steps from `HANDOFF_20260717.md`. Summary of outcomes; raw data in
`~/aieplace_tmp/xplace_mms_reference.md`.

## Headline
- **The honest MMS reference now exists.** XPlace run on all 16 MMS designs (mixed_size, legalized).
- **sw_only's GP is genuinely near-XPlace** (adaptec1 GP +2%), and **after the *same* legalizer
  sw_only BEATS XPlace** on adaptec1 (−2.0%) and newblue1 (−4.4%), ties bigblue1/newblue2.
- **`enable_preconditioning` now auto-enables for movable-macro designs** (committed 638b9a8, verified).
- One real defect surfaced: a **legalization blowup on adaptec2** traced to sw_only's overflow metric
  under-reading density on macro-heavy designs (coarse auto-grid).

## #1 — XPlace MMS reference (DONE)
`main.py --dataset mms --design_name <d> --load_from_raw True --mixed_size True --num_threads 8 --seed 42`
(default `--num_threads 20` crashes numba macro-LG; must be ≤8). Wired MMS via
`Xplace/data/raw/mms -> AIEplace/.../benchmarks/mms`. Full GP + legal table for all 16 designs saved.
adaptec1: GP 6.238e7, legal(DP) 6.814e7.

## #2 — Legalize sw_only through XPlace's legalizer (DONE, flow proven)
- `tools/def_to_bookshelf_pl.py`: sw_only DEF → bookshelf `.pl` (frame is bit-perfect; XPlace reads the
  input HPWL identical to sw_only's own).
- `tools/legalize_swonly_mms.sh <design> <def>`: runs XPlace with `--global_placement False
  --given_solution X.pl --mixed_size True`.
- **XPlace edit** (`src/run_placement_nesterov.py`): the skip-GP path skipped macro legalization (it
  lives inside `global_placement_main`); added a branch to run `macro_legalization_main` on the given
  solution for mixed_size. This is measurement infra in the XPlace repo, not AIEplace.

## #4 — precond-ON generalizes across MMS (STRONG, suite still finishing)
Full 15-design sw_only precond-ON suite running (`~/aieplace_tmp/run_swonly_suite.sh`, concurrency 6).
**GP-vs-GP (all-nets exact HPWL, the robust metric)** — precond-ON gives near-XPlace-or-better GP on
every finished design:

| Design | sw GP | XP GP | sw vs XP GP | sw ovfw |
|---|---|---|---|---|
| adaptec1 | 6.366e7 | 6.238e7 | +2.1% | 0.071 |
| adaptec2 | 7.302e7 | 7.270e7 | +0.4% | 0.060 |
| adaptec3 | 1.593e8 | 1.544e8 | +3.2% | 0.058 |
| adaptec4 | 1.411e8 | 1.370e8 | +3.0% | 0.075 |
| bigblue1 | 8.392e7 | 8.333e7 | +0.7% | 0.096 |
| bigblue2 | 1.219e8 | 1.217e8 | +0.2% | 0.091 |
| newblue1 | 5.455e7 | 5.837e7 | **−6.5%** | 0.081 |
| newblue2 | 1.490e8 | 1.487e8 | +0.2% | 0.060 |
| newblue3 | 2.336e8 | 2.692e8 | **−13.2%** | 0.061 |
| newblue4 | 1.744e8 | 2.299e8 | **−24.1%** | 0.102 |

**IMPORTANT honesty caveat (found late):** the `sw ovfw` column above is sw_only's *self-reported*
overflow. On macro-heavy designs the pre-fix coarse grid **under-reads** it badly — XPlace measures the
same placements at adaptec5 **0.50**, newblue5 **0.48**, newblue1/3 **0.24** (vs self 0.06–0.08). So the
large apparent "wins" (adaptec5 −18%, newblue3 −13%, newblue4 −24%, newblue5 −15%) are **under-spread,
not-really-converged placements with artificially low HPWL — NOT real quality wins.** The trustworthy
GP-vs-GP numbers are the **matched-overflow** designs (self ovfw ≈ XPlace exact, confirmed by clean
legalization): adaptec1 +2.1%, adaptec3 +3.2%, adaptec4 +3.0%, bigblue1 +0.7%, bigblue2 +0.2%,
newblue2 +0.2%, adaptec2 (grid-fixed) ≈ 0%. **Conclusion: precond-ON gives GP within +0.2–3.2% of XPlace
where the grid resolves density; the #3 grid fix is required for macro-heavy designs to converge honestly.**
The definitive follow-up is a full suite re-run with the grid-fixed exe (this suite used the pre-fix exe).

**Legal-vs-legal** (sw_only GP → XPlace LG+DP) works cleanly on adaptec1 (**−2.0%** vs XPlace legal) but
**XPlace's legalizer is fragile on non-XPlace inputs**: adaptec2/newblue3 std-cell-LG blow up, adaptec3
segfaults in greedy-LG. So legal-vs-legal is an imperfect tool here; GP-vs-GP above is the trustworthy
generalization evidence. Legal results where they ran:

| Design | sw legal | XP legal | sw vs XP |
|---|---|---|---|
| adaptec1 | 6.676e7 | 6.814e7 | **−2.0%** |
| adaptec4 | 1.441e8 | 1.414e8 | +1.9% |
| bigblue1 | 8.617e7 | 8.567e7 | +0.6% |
| bigblue2 | 1.253e8 | 1.257e8 | **−0.3%** |
| newblue1 | 5.740e7 | 6.005e7 | **−4.4%** |
| newblue2 | 1.537e8 | 1.524e8 | +0.9% |
| adaptec2 (512, old grid) | 8.954e7 | 7.618e7 | +17.5% (LG blowup) |
| **adaptec2 (grid fix)** | **7.598e7** | 7.618e7 | **−0.3%** (blowup GONE) |
| newblue3 | 3.415e8 | 2.727e8 | +25% (LG blowup; grid fix does NOT help — separate cause) |
| adaptec3 | (greedy-LG segfault) | 1.591e8 | — (XPlace legalizer robustness, not sw_only) |

Clean legalizations (adaptec1/2-with-fix/4, bigblue1/2, newblue1/2) mean **≈ −0.5% vs XPlace legal** —
sw_only is competitive-to-better wherever XPlace's legalizer behaves. The two blowups correlated exactly
with the sw/XP overflow-metric mismatch, and the **grid fix (#3) eliminates it** (adaptec2 +17.5% → −0.3%).

## #3 — MMS convergence-lever sweep (IN PROGRESS)
Root-caused the overflow-metric discrepancy behind the legalizer blowups: sw_only's own **exact overflow
disagrees with XPlace's exact overflow on macro-heavy designs**, correlating with movable-macro count:

| design | #mov macros | sw exact ovfw | XP exact ovfw | match? |
|---|---|---|---|---|
| adaptec1 | 62 | 0.071 | 0.074 | ✓ |
| adaptec3 | 58 | 0.058 | 0.062 | ✓ |
| adaptec2 | **127** | 0.060 | **0.087** | ✗ underreads |

Hypothesis: the ePlace auto-grid formula divides placeable area by avg cell area *including* macros, so
macro-heavy designs get a **coarser grid → density under-read → premature stop → denser real placement →
LG blows up**.

**CONFIRMED + FIXED.** Manual `bins_per_row=1024` on adaptec2: overflow now reads a true 0.057 (vs 0.087
under-read at 512), converges to GP 7.220e7, and **legalizes cleanly to 7.597e7 = −0.3% vs XPlace legal
7.618e7** (vs the +17.5% blowup at 512). Implemented the principled fix in `AIEplace.cpp`: the auto-grid
divisor now uses the average **std-cell** area (excludes movable macros), guarded to be **bit-identical
when there are no movable macros** (all fixed-macro designs unchanged). Verifying the auto-grid adaptec2
run reproduces the manual-1024 result. Sweep configs staged in `~/aieplace_tmp/cfg_s3_*.json` for the
remaining levers (λ-max-step, stop-threshold, precond-scale, init-mult) — not yet needed since the grid
fix resolves the one observed pathology.

## #5 — auto-enable preconditioning (DONE, committed 638b9a8)
`enable_preconditioning` unset → auto-ON iff design has movable macros (die-relative 0.02% area
threshold, same rule the Visualizer uses); explicit config value always wins. run_config.json ships
`auto_enable_preconditioning: true`. Verified via schedule_trace: MMS adaptec1 auto-ON, ispd2005 adaptec1
auto-OFF (bit-identical to explicit OFF), explicit override respected.

## Overflow underread has TWO distinct causes (diagnosed via explicit bins_per_row=1024 re-runs)
The sw_only self-overflow underreads vs XPlace-exact on the non-"reliable" designs. Forcing the grid to
1024 separates two mechanisms:

1. **Grid too coarse (grid-resolution underread).**
   - adaptec2 (127 macros): the macro-inflated avg-cell divisor. **FIXED (#3, commit 7aa22d9)** — auto
     now picks 1024, true overflow 0.057, legal **−0.3%** (was +17.5%). Confirmed win.
   - adaptec5: auto-grid also too coarse (self 0.041, but at explicit 1024 overflow reads **0.455**).
     But its coarseness is **NOT** the macro divisor (grid-fix HPWL identical) — it's the row-cap /
     base sqrt formula. So the #3 fix does not catch adaptec5; a broader grid-sizing fix would.
2. **Genuine metric/density difference (grid-independent).**
   - newblue3 at explicit 1024 converges to self-overflow **0.059** ≈ auto 0.056, HPWL identical — the
     finer grid does not change it, yet XPlace measures 0.24 on the same placement. So the newblue-family
     discrepancy is a real overflow-metric/density mismatch (candidates: density normalization /
     target_density, large fixed-blockage/whitespace deposit, filler exclusion vs newblue structure).
     newblue1 behaves the same. Their GP "wins" (−6.5/−13/−15/−24%) are under-spread, untrustworthy.

## Open items (ranked)
1. **Investigate the newblue-family overflow discrepancy** (self ~0.05 vs XPlace ~0.24, grid-fix-immune).
   This is the remaining honesty gap — it inflates apparent newblue GP wins and causes LG blowups.
2. Full suite re-run with the grid-fixed exe for honest macro-heavy numbers (this suite used pre-fix exe;
   huge designs bigblue4/newblue7 are slow at the finer grid).
3. adaptec3 greedy-LG segfault = XPlace-legalizer fragility on non-XPlace inputs (not sw_only).
