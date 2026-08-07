# TODO #8 — newblue5: the zero-area `terminal` nodes are NOT the cause. Here is what is.

> ## ⚠️ SELF-CORRECTION 2026-08-01 — §3/§4's "2–2.5× under-spread vs XPlace" was WRONG
>
> It compared **our macro-INCLUDED** overflow against **XPlace's macro-EXCLUDED** one.
> `run_placement_nesterov.py:173` sets `ps.zero_macro_grad = True` **before** the Mixed-GP
> `evaluate_placement` call at line 182, and `evaluator.py:30` then drops `is_mov_macro` nodes.
> So the **0.1697 phase-1 reference in `tools/benchmarks.py::_XPLACE_MMS_MIXED_GP` excludes the
> movable macros.** Two independent confirmations: (a) the code order above; (b) arithmetic —
> newblue5's macros alone would contribute ≈0.257 (0.5 × 172.0M / 334.3M), so a macro-inclusive
> total could not be 0.1697. A third, empirical: XPlace's `--global_placement False` run
> (`2026-07-26-10:45:04_newblue5`), where `zero_macro_grad` is never set, reports
> **exact Overflow 0.4836** — macro-inclusive, and squarely in our 0.35–0.43 range.
>
> **Recomputed like-for-like** from the exported DEFs (`/tmp/t8/overflow_variants.py`; validated
> against sw_only's own reported `sharp/no-filler`: 0.3490 vs 0.350, 0.3505 vs 0.348):
>
> | arm | macros IN | **macros OUT** |
> |---|---|---|
> | A baseline | 0.3490 | **0.0964** |
> | B fillers | 0.3767 | **0.1244** |
> | D both | 0.3505 | **0.0940** |
> | **XPlace phase 1** | — | **0.1697** |
>
> **sw_only's standard-cell spreading is as good as XPlace's or better.** The entire
> "under-spread" signal was the movable macros — which is exactly what phase 2 legalizes.
> This *strengthens* recommendation §7.3 considerably: what we lack is not spreading quality,
> it is macro handling.
>
> **Open, deliberately not asserted:** the *filler* axis. Code reading says XPlace's exact
> overflow also excludes fillers (`get_mov_node_info` appends fillers **after** `mov_rhs`, and
> `get_obj_overflow` slices `[mov_lhs:mov_rhs]`), but the newblue2 calibration in
> `overflow-metric-grid-faithfulness` (sw `sharp/+filler` 0.143 ≈ XPlace 0.145, vs
> `sharp/no-filler` 0.0599) says included. Those contradict. The DEF carries no fillers, so the
> table above is filler-excluded on our side; including them would raise our numbers. **Resolve
> before quoting any of this as final.** §4's claim that `sharp/+filler` *is* XPlace's exact
> overflow holds for the non-mixed-size eval path only — not for the "After Mixed-GP" number.

**Date:** 2026-07-31 · **Base:** `caa8f2b` (pl_algo), run in an isolated git worktree
`/home/msears/phd/AIEplace_t8` so Mark's uncommitted filler work was never touched.
**Config for every run below:** newblue5, grid 1024, `maximum_utilization = 0.5`, `random_seed = 42`
— i.e. `tools/benchmarks.py::_ROWS` ("XPlace grid + td"), which is what
`2_ARTIFACTS/mms_baseline_20260731.tsv` uses.

---

## 1. Verdict on the original question: the terminals are inert

TODO #8 asked whether newblue5's 4790 zero-area `terminal` nodes drive the divergence.
**They do not, and the reason is stronger than "no evidence found".**

Verified directly from the bookshelf files (`/tmp/t8/nb5_terminals.py`, `nb5_pinoffsets.py`):

| claim | result |
|---|---|
| 4790 nodes carry the `terminal` keyword | confirmed |
| all exactly 0×0 | confirmed (0 exceptions) |
| all FIXED in `.pl` | confirmed (0 exceptions) |
| zero-area nodes that are *not* terminals | **0** — the two sets coincide exactly |
| zero-area **movable** nodes | **0** — no misclassification anywhere |
| max node degree | 313 (4 nodes at 313, 4 at 311, 2 at 227) |
| degree after the `ignore_net_degree = 100` net mask | **313 → 313** — the mask does *not* remove the hubs |
| **pin offsets on terminal pins** | **all exactly (0,0)** — every one of the 4790, every pin |

Two of these kill the leading theories outright:

- **The "scrubbed macro left its pin offsets behind" theory is dead.** Every terminal presents a
  single distinct offset, (0,0). For contrast, the four genuine movable macros carry 795 / 796 /
  413 / 413 pins spread across their *full* footprint (`o1228262`: dx span 10080 of a 10260-wide
  macro). If the terminals were scrubbed macros with residual pin geometry, they would look like
  that. They do not — they are clean single points.
- **The terminals cannot form a density hotspot at all.** They are 0×0, so they deposit nothing.
  Measured: newblue5's total fixed area is **exactly 0.0**, independently confirmed by XPlace's own
  log (`FixArea: 0.000E+00 (0.0%)`). A node with zero area contributes zero to the density map, zero
  to the overflow numerator, and zero to the filler budget. It only anchors nets — which XPlace does
  identically.

So no per-region density probe was needed to rule them out, and I did not build one (see §6).

---

## 2. What actually goes wrong

### 2a. newblue5 places with **zero filler cells**

At the canonical config, HEAD's `DataBase::addFillers` produces **`Total fillers added: 0`**
(confirmed in my own run's `run.log`, not inferred). The arithmetic:

```
HEAD : available = die − fixed                     = 650.2M − 0        = 650.2M
       unfilled  = available × 0.5 − movable_area  = 325.1M − 334.3M   = −9.2M  → 0 fillers
XPlace: placeable = die − fixed − mov_macro_area   = 650.2M − 172.0M   = 478.3M
       whitespace = 0.5 × placeable − stdcell_area = 239.1M − 162.4M   = +76.7M → 632,490 fillers
```

The driver is that **51.4% of newblue5's movable area is movable macros** (172.0M of 334.3M,
26.4% of the whole die). HEAD counts that macro area in *both* terms, which drives the budget
negative. The gap is `mov_macro_area × (1 − td)` — identically zero at td = 1.0, which is why this
never showed on ISPD2005.

**Independently validated against XPlace.** My arithmetic, written from the bookshelf files with no
sw_only or XPlace code involved, predicted 632,490 fillers at (10.112, 12.0). XPlace's log reads:

```
#Fillers: 632490 Filler size: (1.0112e+01, 1.2000e+01)
DieArea: 6.502E+08  FixArea: 0.000E+00  MovArea: 3.343E+08 (51.4%)
FillerArea: 7.675E+07 (11.8%)  MovMacroArea: 1.720E+08 (26.4%)  MovStdCellArea: 1.624E+08
```

Every figure matches to the printed precision.

### 2b. The failure is a density **floor** plus λ runaway — not an explosion

TODO #8 described "diverges/explodes around iter ~400". At the canonical config there is **no
explosion**. HPWL rises smoothly from 2.20e8 (iter 4) to ~4.4e8, which is ordinary spreading.
What actually happens (from `iterations.dat`):

| | iter 674 | iter 1044 | change |
|---|---|---|---|
| overflow | 0.3544 | 0.3500 | **flat over 370 iterations** |
| density weight λ | 0.147 | 102.5 | **×700** |
| HPWL | 3.994e8 | 4.658e8 | **+17%** |

Overflow hits a floor, λ ramps without bound trying to push through it, HPWL inflates, and the
divergence guard eventually fires with a *fallback* best. That is exactly the **density-weight
runaway** TODO #4 already lists as an unowned defect — newblue5 is a clean, reproducible witness
for it. A bounded λ would have preserved the iter-706 HPWL instead of inflating it 17%.

The floor is **structural**: 91 movable macros occupying 26.4% of the die as incompressible lumps
at local density 1.0 against a 0.5 target. XPlace hits the same wall — its Mixed-GP ends at
overflow **0.1697**, above its own doubled 0.14 target, and only reaches 0.0452 *after* macro
legalization fixes the macros (phase 2, 2010 iterations).

---

## 3. The 2×2 experiment

Two variables, one binary each, both verified behaviour-preserving on adaptec1 (160,067 fillers,
**bit-identical** `iterations.dat` and placement DEF between the two binaries).

| arm | fillers | `macro_td_expand_ratio` | iters | HPWL | vs XPlace | clamp/+filler | **sharp/+filler** | stop |
|---|---|---|---|---|---|---|---|---|
| **A** baseline (HEAD) | 0 | off | 674 | 4.399e8 | +16.0% | 0.307 | 0.350 | `divergence_guard`, *fallback* |
| **B** fillers | 632,401 | off | 1044 | **3.994e8** | **+5.3%** | 0.402 | 0.429 | `divergence_guard`, *fallback* |
| **C** #11b only | 0 | on | 672 | 4.892e8 | +29.0% | 0.0354 | 0.338 | **`converged`**, primary |
| **D** both | 632,401 | on | 683 | **4.003e8** | **+5.6%** | 0.0676 | 0.394 | **`converged`**, primary |
| XPlace Mixed-GP | 632,490 | on | — | 3.792e8 | — | — | **0.1697** | phase-1 handoff |

Three separable findings:

1. **Fillers buy the HPWL.** Arm B alone closes the XPlace gap from +16.0% to +5.3% (−9.2% HPWL).
   This is the single biggest quality effect measured.
2. **`macro_td_expand_ratio` buys the convergence.** Both #11b arms stop with `converged` and a
   *primary* best instead of `divergence_guard` and a *fallback*. The λ runaway is gone.
3. **Neither fixes the physical spread.** On the metric that is genuinely comparable to XPlace
   (§4), every arm sits at 0.34–0.43 against XPlace's 0.1697 — still 2–2.5× under-spread.

**Cross-validation:** arm C reproduces the footprint A/B's newblue5 number exactly. A→C is
4.399e8 → 4.892e8 = **+11.2%**, and `_NEW_REPORT_footprint_ab_20260731.md` recorded newblue5 at
**+11.2%** for the `macro` arm. My setup matches the record.

---

## 4. Which overflow number is comparable to XPlace (this matters)

With #11b on, `clamp` collapses (0.307 → 0.0354) while `sharp` barely moves (0.350 → 0.338),
because `computeNodeFootprint` applies the macro target-density weight only inside the clamp
branch. **That is faithful, not a divergence** — I checked XPlace's evaluator:

```python
# src/evaluator.py :: get_obj_overflow
node_size   = data.node_size[mov_lhs:mov_rhs]          # EXACT size, not clamped
node_weight = node_size.new_ones(node_pos.shape[0])    # weight = 1.0 — no expand_ratio,
                                                       # no macro target-density fill
```

XPlace's reported "exact Overflow" ignores `expand_ratio` entirely, and its denominator is
`total_mov_area_without_filler` — structurally identical to `computeOverflow`'s
`db.getTotalMovableArea()`. So **`sharp/+filler` is XPlace's exact overflow**, in every arm,
#11b on or off. (Corroborated empirically by the newblue2 0.143 ≈ 0.145 match in
`overflow-metric-grid-faithfulness`.) Grid.cpp's comment justifying the placement of the #11b
branch is correct; it just never states that this is what keeps the metric comparable.

> **⚠️ CORRECTED 2026-08-01 (Mark's catch).** This paragraph originally claimed #11b makes macros
> "contribute zero overflow to the convergence signal" and "blinds the stop metric". **That was
> wrong.**
>
> A macro depositing at exactly `target_density` fills a covered bin to `bin_area × td` =
> **exactly the bin capacity**, so excess = 0. That is the *correct* accounting: a bin taken up
> solely by one large macro is **full at the target density, not overflowed**. And the macro has
> not vanished — it has consumed the bin's entire budget, so **anything else overlapping it
> overflows immediately**: `density = cap + cell_area ⇒ excess = cell_area`. Macros generate
> overflow exactly when other nodes overlap them, which is the desired behaviour.
>
> The correct conclusion is the reverse of what I wrote. With #11b **off**, a fully-covered bin
> receives `bin_area` (density 1.0) against a `0.5 × bin_area` capacity, so every movable macro
> emits `area × (1 − td)` of overflow **permanently, whether or not anything overlaps it** — an
> irreducible term no amount of moving can clear, on a design where macros are 26.4% of the die.
> *That* is the spurious signal, and #11b removes it. This agrees with the correction at the top
> of this report, reached independently: our macro-inclusive `sharp` number was never meaningful.
>
> **What does survive:** sw_only's headline `Final Overflow (exact, +fillers)` deposits macros at
> weight 1 (the #11b branch is clamp-only), so on a mixed-size design with `target_density < 1` it
> still carries that spurious macro term — arm D reads sharp 0.394 against clamp 0.0676. **The
> reported number is the misleading one, not the convergence signal.** XPlace avoids this by
> excluding macros from its phase-1 eval entirely (`zero_macro_grad`). Action captured in TODO #8.
>
> **Consequence for §7.2:** the recommendation "do not flip `macro_td_expand_ratio` on this
> evidence" rested partly on the blinding claim and is weakened accordingly. It is now a
> correctness argument *for* the flag; the remaining reason to keep it a toggle is only that the
> 16-design A/B measured −5.2% mean HPWL, and that A/B ran on zero-filler arms.

---

## 5. Corrections to the record

1. **`_NEW_HANDOFF_filler_faithfulness_20260731.md` §4 is wrong** where it says the memories
   `mms-hard-spreading-three-diseases` and `overflow-metric-grid-faithfulness` "were derived on a
   **zero-filler** newblue4/adaptec5". Those runs used `maximum_utilization = 1.0`, where the
   macro-area term cancels identically. Measured filler counts in those exact runs
   (`/tmp/mms_viz/logs/*.log`): **adaptec5 1,509,741 · newblue4 1,104,300 · newblue5 2,602,944**.
   Mark spotted this from the GIFs. Those memories' observations stand; only the handoff's
   inference about them needs retracting. (Its Trap #1 already states the td=1.0 behaviour
   correctly — §4 just contradicts it.)
2. **Three different newblue5 configurations are conflated across the notes**, which explains most
   of the apparently contradictory history:
   - `mms_suite_precondON` 2026-07-17 — td **1.0**, auto grid → *converged*, 3.246e8 @ 0.053
   - fillconv / the GIF 2026-07-27 — td **1.0**, auto grid (2048), `convergence_include_fillers=true`
     → the "diverges at ~400" observation that opened TODO #8
   - canonical MMS — td **0.5**, grid **1024** → the arm above, and the only one comparable to
     `mms_baseline_20260731.tsv` and to XPlace
   **The GIF that motivated TODO #8 was not the canonical configuration.** Any future newblue5
   claim should state td and grid.
3. **TODO #11b's rejection is confounded a second way.** TODO #11 records the confound with the
   stop criterion. It was *also* measured on a newblue5/adaptec5/newblue4 that had **zero fillers**
   — i.e. no whitespace representation at all. Arms C vs D show the two interact: #11b alone is
   +11.2% HPWL, #11b with correct fillers is −9.0% against baseline.

---

## 6. Deliberate non-changes

- **No per-region density/gradient trace was built.** It was scoped for the terminal-hotspot
  question, and §1 answers that question structurally — a zero-area node cannot produce a hotspot.
  The existing `[OVFW-DIAG]` line, `iterations.dat` (which already carries λ), and the XPlace logs
  covered everything else. Building it would have been speculative (CLAUDE.md rule 2).
- **No default was changed, and no fix was landed.** The filler correction is Mark's in-flight
  work; my worktree copy exists only to isolate the variable and is not proposed as a competing
  change. `macro_td_expand_ratio` stays default-false — see §7.

---

## 7. Recommendations

1. **The filler correction is confirmed to improve quality, not just faithfulness.** −9.2% HPWL on
   newblue5, +16.0% → +5.3% against XPlace. This is the answer to the open question in
   `_NEW_HANDOFF_filler_faithfulness_20260731.md` §4 for at least one design, and it is the
   macro-heavy design that was hardest. Worth stating in that handoff's re-baseline.
2. **Do not flip `macro_td_expand_ratio` globally on this evidence.** It fixes newblue5's stop
   behaviour, but partly by deflating the convergence signal (§4), and the 16-design A/B measured
   it −5.2% mean. The honest reading: it is a *symptomatic* fix.
3. **The real fix is TODO #13 phase 2.** XPlace gets 0.1697 → 0.0452 by legalizing the macros and
   re-running std cells with them fixed. newblue5 is the strongest case for #13 in the suite: 26.4%
   of its die is movable macro, so phase 1 provably cannot spread it, and neither tool does.
4. **Give `updateDensityWeight` an upper bound** (TODO #4's unowned "density-weight runaway").
   newblue5 arm B is a clean witness: λ ×700 across 370 flat iterations bought 0.004 of overflow
   and cost 17% HPWL.
5. **Re-test #11b after the filler change lands**, as TODO #11 already asked — but note the
   rejection was confounded twice, not once.

---

## Artifacts

- Scripts: `/tmp/t8/nb5_terminals.py`, `nb5_pinoffsets.py`, `nb5_fillers.py`, `traj.py`
- Run dirs: `/tmp/t8/results/{baseline,fillerfix,tdexp_only,both}/newblue5/*/`
- Binaries: `/tmp/t8/swonly_head.exe` (`2a98193127cc…`), `/tmp/t8/swonly_fillerfix.exe` (`a43b070ea605…`)
- Worktree: `/home/msears/phd/AIEplace_t8` — detached at `caa8f2b`, one experimental edit to
  `DataBase.cpp`. Remove with `git worktree remove --force /home/msears/phd/AIEplace_t8`.
