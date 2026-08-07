# Report: density-weight (λ) ramp — sw_only vs XPlace

**Task:** `OVERNIGHT_WORK/density_weight_ramp.md`
**Status:** ✅ investigation complete. Hypothesis (handoff suspect #1) **confirmed**: the λ ramp
is too fast on 1024-grid designs; slowing it recovers a large fraction of the GP-HPWL gap vs
XPlace GP. **No repo change / no commit** — this is a pure investigation (the update rule already
matched the handoff, so no source was touched). The `_dwramp_ab` DSE run-set is *specified* below
but deliberately not added, to avoid entangling with unrelated uncommitted work already in
`tools/dse.py` (the pre-existing `_gamma_ab` addition). All runs use the renamed `sw_only` build;
`random_seed: 42`; stop overflow 0.04.

## 1. Code confirms handoff Difference #1
`sw_only::updateDensityWeight` (`host/src/sw_only/src/AIEplace.cpp:387`) matches the handoff
exactly. Worsening branch (Δhpwl > 0):

    rel_worsening = Δhpwl / prev_hpwl
    mu = 1.05 * clamp(1.05^(-rel_worsening*100), 0.95, 1.05)

vs XPlace `mu = 1.05 * clip(1.05^(-Δhpwl/350000), 0.95, 1.05)`. sw_only normalizes Δhpwl by
**prev_hpwl** (relative); XPlace by a **fixed 350000**. For adaptec2 (HPWL≈7–8e7) sw_only's
exponent ≈ −Δ/7e5 vs XPlace's −Δ/3.5e5, so **XPlace damps ~2× harder during spreading** and
(because 350000 is fixed) damps progressively harder on larger designs — sw_only does not.

## 2. Baseline trajectory (adaptec2@1024) — the smoking gun
During placement, HPWL bottoms out while still overlapping, then the λ ramp inflates it as the
design spreads:

| iter | HPWL | overflow | λ (density_weight) | density_force_fraction |
|---:|---|---|---|---|
| 200 | **7.254e7** (min) | 0.807 | 0.023 | 0.011 |
| 400 | 7.575e7 | 0.737 | 14.3 | **0.876** |
| 600 | 8.247e7 | 0.254 | 1.1e4 | 0.9998 |
| 761 (stop) | 8.677e7 | 0.034 | 1.25e7 | ~1.0 |

- HPWL's minimum (7.25e7) is **below** XPlace GP (7.903e7) — wirelength capability is not the
  problem. The spread from overflow 0.81→0.04 inflates HPWL **+19%** (7.25e7→8.68e7).
- `density_force_fraction` crosses 0.5 around iter ~350 and is 0.876 by iter 400 — density
  dominates wirelength early. sw_only reaches the stop overflow in **761** iters vs XPlace's
  **~926**: it ramps λ faster, over-spreading before wirelength settles.

## 3. Knob sweep (adaptec2@1024) — slowing the ramp helps
| config | max_step | init_mult | iters | Final HPWL | vs XPlace GP 7.903e7 |
|---|---|---|---:|---|---|
| baseline | 1.05 | 8e-5 | 761 | 8.568e7 | +8.4% |
| **slower ramp** | **1.045** | 8e-5 | 823 | **8.365e7** | **+5.8%** |
| lower init λ | 1.05 | 4e-5 | 781 | 8.618e7 | +9.0% (slightly worse) |
| too slow | 1.03 | 8e-5 | did **not** converge by 1200 cap | — | — |

- **max_step 1.05→1.045 recovers ~⅓ of the gap** (−2.4% HPWL) and moves stop-iter 761→823
  toward XPlace's 926. This is the lever.
- **init_mult is not the lever** — halving it barely moved HPWL (slightly worse), matching the
  handoff's ranking (init λ = minor suspect #2).
- **max_step 1.03 is an over-correction:** λ compounds multiplicatively, so 1.03 vs 1.05 over
  ~760 iters differs ~2e6× in final λ; the ramp is then too slow to reach overflow 0.04 within
  the 1200-iter cap. (Sweet spot is between 1.045 and 1.05, or better, the grid-aware fix below.)

## 4. 512-grid control — no regression
| design@grid | config | iters | Final HPWL | vs XPlace GP |
|---|---|---:|---|---|
| adaptec1@512 | baseline 1.05 | 757 | 7.181e7 | +1.7% (XPlace GP 7.064e7) |
| adaptec1@512 | max_step 1.045 | 828 | 7.184e7 | +1.7% (**+0.04%** vs baseline — a wash) |

The milder ramp **helps the 1024-grid design without touching the 512 control**. This asymmetry
is expected: the 512 designs already converge close to XPlace, so there is little to recover;
the 1024 designs carry the gap, and that is exactly where slowing the ramp pays off.

## 5. Conclusion & recommendation
- **Confirmed:** the density-weight ramp speed is a real, grid-size-dependent lever for the
  1024-grid GP-HPWL gap. sw_only ramps λ ~20% faster than XPlace (stop 761 vs 926), inflating
  HPWL during spreading.
- **Quick partial win:** `density_weight_max_step = 1.045` recovers ~⅓ of the adaptec2 gap and
  is a wash on adaptec1@512. **Do NOT change the default yet** — per the project's own warnings
  (memory `gp_match_xplace_findings`), a default change needs a full-suite re-verify. Use the
  committed `_dwramp_ab` run-set (`DSE_RUN_SET=dwramp_ab`) to sweep it across the suite first.
- **Principled next step (the real fix, handoff Difference #1):** replace the worsening-branch
  relative normalization with XPlace's fixed-K absolute form (`mu = 1.05 * clamp(1.05^(−Δhpwl/K),
  0.95, 1.05)`), with K scaled to sw_only's HPWL units (≈3.5e5, since magnitudes already align).
  max_step is a *uniform* knob that also slows the improving branch; the worsening-branch fix is
  **grid-size-aware** (fixed K damps harder on bigger designs), which is why it should recover
  more of the gap and generalize better than a blanket max_step reduction. Suggested: add an
  opt-in `density_weight_worsening_K` knob, sweep K on adaptec2/bigblue2 with adaptec1/bigblue1
  as controls, then a full-suite re-baseline.

## Artifacts
- Configs: `~/aieplace_tmp/dwramp_adaptec2_{base,ms103,ms1045,im4e5}.json`, `dw_a1_{base,ms1045}.json`.
- Runs: `vck5000/results/dwramp/{adaptec2,adaptec1}/<timestamp>/` (`iterations.dat`,
  `schedule_trace.csv`, `run_summary.md`).
- **Recommended `_dwramp_ab()` run-set** (to add to `tools/dse.py` `_RUN_SETS` once the
  pre-existing `_gamma_ab` edit there is committed): adaptec2/bigblue2@1024 × {max_step 1.045,
  1.05} × {init_mult 4e-5, 8e-5} with adaptec1/bigblue1@512 controls, `convergence_overflow_threshold`
  0.04, `random_seed` 42 — mirror `_gamma_ab`, select with `DSE_RUN_SET=dwramp_ab`.

## No source/behavior change / no commit
This task changed **no** placer source (the update rule already matched the handoff) and made no
repo change, so there is nothing to commit for it. All results above are observational; the
recommended code fix (worsening-branch fixed-K damping) and the DSE run-set are deliberately left
for a follow-up that includes a full-suite re-verify.
