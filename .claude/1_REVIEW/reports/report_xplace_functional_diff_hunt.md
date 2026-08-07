# Report: hunting functional differences vs XPlace (GP HPWL gap)

**Goal:** turn the remaining sw_only↔XPlace GP HPWL gap into an educated guess, comb the suspect
code against XPlace, fix functional differences to match XPlace, A/B, commit. All runs seeded
(42), stop overflow 0.04, on the renamed `sw_only` build.

## Educated guess (gap is grid-dependent: +1.4% @512 → +10.4% @1024)
Ranked: (1) density-weight λ schedule, (2) λ init, (3) WA wirelength gradient / gamma,
(4) Nesterov/backtracking. Combed all of these plus the density force and net masking.

## What was combed and the verdict
| component | XPlace ref | verdict |
|---|---|---|
| λ schedule — worsening branch | `param_scheduler.step_density_weight` | **functional diff, FIXED** (see below) |
| λ schedule — improving branch, 1.05, clamps | same | already matches (`dw_max_step=1.05`, `0.9999^iter`) |
| Nesterov momentum + backtracking | `nesterov_optimizer.step` | **matches** (`backtrack_epsilon=1.05` ⇒ continue-if `α_new<0.952·α`, ≈ XPlace `≤0.95·α`; retries from fixed `v_k`) |
| WA wirelength gradient | `merged_wl_loss_grad` | **matches** (standard DREAMPlace WA smooth-max/min gradient) |
| init density weight | `initializer.py:133` | **matches** (`‖wl_grad‖₁/‖den_grad‖₁·8e-5`; the "trial BB step" is for init *lr*, not λ) |
| density/electrostatic force | `density_map_cuda_backward` | **matches** (`density_weight·overlap_area·eField`, `local_density_weight=1`) |
| **net-degree masking** | `net_mask`, `ignore_net_degree=100` | **functional diff, FIXED** (see below) |

## Fix 1 — λ worsening-branch: relative → XPlace fixed-K (commit `98772d9`)
`updateDensityWeight` used `mu=1.05·clamp(1.05^(−Δhpwl/prev_hpwl·100),…)` (relative). XPlace uses a
**fixed** `−Δhpwl/350000`. Verified the constant ports directly: XPlace's `recorder.hpwl` is
`masked_hpwl = round(hpwl·die_scale/site_width)`, the same ~1e7–1e8 magnitude frame as sw_only's
raw-DBU HPWL (XPlace only `prescale_by_site_width`, no `normalize_by_die`). The relative form was
based on a mistaken premise that 350000 lived in a different unit scale.

**A/B: quality-NEUTRAL.** adaptec1@512 7.181e7→7.180e7 (wash); adaptec2@1024 8.568e7→8.579e7
(+0.13%). Reason: per-iter Δhpwl is small vs both K's during smooth spreading, so `mu` stays near
the 1.05 max in both forms — the worsening branch barely deviates. **Key conclusion:** with the λ
schedule now matching XPlace *exactly*, the +8% adaptec2 gap PERSISTS ⇒ **the gap is not in the λ
schedule.** (Task 3's `max_step=1.045` win was sw_only *deviating* from XPlace to compensate for a
different, real difference.) Committed as XPlace-faithful default (knob `density_weight_worsening_hpwl_norm`,
≤0 = legacy).

## Fix 2 — net-degree masking (commit `8025644`) — the real apples-to-oranges bug
XPlace's `net_mask` excludes nets with **>100 pins** (clock/reset/scan spanning the die) from
**both** the WA gradient AND every reported HPWL (`net_mask` in `merged_wl_loss_grad` *and*
`evaluator.get_hpwl`). sw_only optimized and measured over **all** nets (degree ≥ 2, no cap). So
every XPlace comparison counted extra nets XPlace ignores — apples-to-oranges.

**Fix:** `ignore_net_degree` (default 100 = XPlace), applied in `computeHpwlPartials_CPU`/`_simple`
(gradient) and `computeTotalWirelength(method, max_net_degree)` (metric — feeds reported HPWL, the
schedule `Δhpwl`, and convergence).

**A/B (seeded, 100 vs 1e8):**
| design | mask OFF | mask ON (XPlace) | gap vs XPlace GP |
|---|---|---|---|
| adaptec2@1024 | 8.579e7 | **8.499e7 (−0.9%)** | +8.6% → **+7.5%** (4 nets >100, up to 1935 pins) |
| adaptec1@512 | 7.180e7 | 7.203e7 (+0.3%) | true apples-to-apples **+2.0%** vs XPlace 7.064e7 (only 2 masked nets) |

Design-dependent (helps designs with big high-degree nets; ~neutral otherwise) but now HONEST —
sw_only and XPlace measure the same masked wirelength. **This corrects a wrong prior hunch** that
HPWL masking was a non-issue (the *overflow* metric was matched; the *HPWL* metric was not).

## Net effect on the target
adaptec2@1024 apples-to-apples gap vs XPlace GP: was reported ~+8.6% (mixed masked/unmasked), now a
clean **+7.5%** with both codes masking. The λ schedule is ruled out as the source.

## Fix 3 — field-solve normalization → XPlace-faithful frame (commit `492ebf3`) — THE grid-dependent lever
Combed the density field solve. sw_only's inverse DCT re-applied the forward `1/N`, so the
electrostatic field was ~`N²` too weak and λ ~`N²` inflated vs the naive DREAMPlace field — a
distortion that **grows with grid resolution**, i.e. exactly the reason the gap widened from
+1.4% @512 to ~+10% @1024. Three gated flags flipped to XPlace-faithful defaults (run_config.json
+ C++ fallbacks):
- `dct_normalize_inverse`: true→**false** (field-faithful inverse)
- `precond_raw_area`: false→**true** (α₂ = pcoef·λ·mov_node_area)
- `dff_force_ratio`: false→**true** (force-ratio dff, invariant to the field-norm constant; REQUIRED
  or the faithful inverse regresses adaptec2@1024)

**A/B (seeded, on top of Fix 1 + Fix 2):**
| design | legacy frame | XPlace-faithful | Δ | gap vs XPlace GP |
|---|---|---|---|---|
| adaptec1@512 | 7.203e7 | **7.132e7** | −1.0% | +2.0% → **+1.0%** |
| adaptec2@1024 | 8.499e7 | **8.244e7** | **−3.0%** | +7.5% → **+4.3%** |

Bigger win at the higher grid — this closes the grid-dependent component of the gap and matches
XPlace's field formulation (λ now within ~50× of XPlace, was ~1e4×). Gated → revert via config.

## Cumulative result (adaptec2@1024 vs XPlace GP 7.903e7)
Start ~+8.6% (mixed metric) → **+4.3%** after: net-mask (honest metric) + fixed-K schedule (matched,
neutral) + XPlace-faithful field frame (−3.0%). The λ schedule was ruled out; the field
normalization was the real grid-dependent lever.

## Also combed and ruled out (match XPlace — no change needed)
- **Best-solution selection:** sw_only records `best_primary` = min HPWL among placements with
  `overflow < stop_overflow`, skipping the first `BEST_SOL_MIN_ITER=50` iters — identical to XPlace's
  HPWL-driven `best_metric` (`overflow < stop_overflow && hpwl < best`, `iter-init_iter < 50` skip).
  The fallback/aux tie-breaking differs slightly but only fires on non-convergence.
- **Filler initial positions:** sw_only spreads fillers "uniformly at random across the die" (XPlace
  `get_filler_pos`); the `(0,0)` in `addFillers` is a placeholder overwritten at init. Matches.
- **`gamma_ref_grid`:** re-A/B'd under the faithful frame — gref512 8.244e7 vs gref1024 (XPlace
  bin-tied) 8.285e7 (+0.5%). gref512 stays; a **justified** deliberate divergence, not a frame artifact.

## Remaining residual (+1.0% @512, +4.3% @1024) — no single gateable culprit found
After matching the schedule, net-mask, field frame, best-selection, fillers, WA gradient, optimizer,
init, and density force, the residual is small and NOT attributable to one gateable difference.
**The coordinate frame is NOT the suspect** — [[preconditioner_bb_fix]] already measured sw_only and
XPlace as sharing effectively the same frame (the precond root cause was the BB step-length clamp,
not the frame), and everything scales consistently with the coord unit (gamma, BB step, gradients).
A coordinate-frame refactor would not close this residual — do not pursue it. More likely the residual
is accumulated small numerical differences and design-level items:
- **Movable-macro designs:** the one place the filler logic still differs (XPlace excludes movable
  macros from filler sizing/area, uses `site_height`, `round`). Worth gating if such a design enters
  the test set (adaptec/bigblue macros are FIXED, so it doesn't bite there).
- Better spent: the **full-suite re-baseline** of the three committed default changes than chasing the
  last ~1–4% on two designs.

## Caveats (unchanged, now more important)
Three defaults changed this investigation (HPWL metric definition, λ schedule, field frame). None
has had a **full-suite re-baseline** — the adaptec1/adaptec2 A/B is a leading indicator. Run
`DSE_RUN_SET=full_suite` and the OpenDP legal-vs-legal check ([[opendp_legal_vs_legal]]) before the
new ratios are treated as final. All three are config-gated and individually revertible.

## Caveats
Both fixes change semantics that feed `results.csv`/scorecard (HPWL metric definition; schedule).
Neither has had a **full-suite re-baseline** — the single-design A/B (adaptec1/adaptec2) is a
leading indicator, not the verdict. Run `DSE_RUN_SET=full_suite` before treating the new ratios as
final, and legalize with the OpenDP flow ([[opendp_legal_vs_legal]]) for the legal-vs-legal check.
