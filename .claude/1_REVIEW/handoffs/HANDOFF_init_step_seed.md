# Handoff: implement XPlace's initial-step (learning-rate) estimation in sw_only

**Goal:** Replace sw_only's hard-coded first optimizer step with XPlace's Barzilai-Borwein (BB)
estimate, and rename the config param `init_step_length` → `init_step_seed` to reflect its new role
(a *seed* for the estimate, not the step itself). This makes sw_only XPlace-faithful on the one knob
a sensitivity analysis flagged as the most important and most finicky.

---

## Why (context — you can act without reading the prior chat)

A Morris/Sobol sensitivity analysis of the sw_only hyperparameters (tools/morris*.py, sobol*.py;
see auto-memory `morris_sobol_sensitivity_tooling` and `init_step_length_xplace_gap`) found that
`init_step_length` is the ONE factor important in every design regime, is stall-prone when too small
(a run with init_step_length=0.001 diverged immediately), and is heavily interaction-dependent. That
finickiness is the classic symptom of a **fixed constant where the reference (XPlace) self-calibrates
per design**. Fixing it should (a) match XPlace, and (b) collapse this param's SA importance/variance,
turning a sensitive user knob into a robust internal default — directly serving the config-cleanup goal.

**Environment reminder:** the Bash tool runs on Windows; wrap everything in
`wsl -e bash -c "cd /home/msears/phd/AIEplace/vck5000 && <cmd>"` with real Linux paths. Build with
`make host` (HOST defaults to sw_only). The `freopen /dev/tty` parser hang is already fixed (commit
9dceb24) so headless runs no longer wedge.

---

## What XPlace does (the formula to copy)

`Xplace/src/initializer.py:171` — `estimate_initial_learning_rate(obj_and_grad_fn, constraint_fn, x_k, lr)`:

```python
x_k   = constraint_fn(x_k)                 # current movable positions
_, g_k   = obj_and_grad_fn(x_k)            # TOTAL gradient at x_k (wl + density_weight*density)
x_k_1 = constraint_fn(x_k - lr * g_k)      # ONE trial step; lr is a SEED (args.lr, default 0.01)
_, g_k_1 = obj_and_grad_fn(x_k_1)          # total gradient at the trial point
init_lr = (x_k - x_k_1).norm(p=2) / (g_k - g_k_1).norm(p=2)   # BB inverse-Lipschitz estimate
```

Call ordering (`Xplace/src/run_placement_nesterov.py:104-113`): **`init_params` (sets the density
weight λ via the grad-norm ratio) runs FIRST, THEN `estimate_initial_learning_rate`**, so the trial
step uses the full gradient with λ already applied. `args.lr` default = 0.01 (`main.py:22`). XPlace
also re-estimates at restarts (lines 233, 320) — out of scope here; do the init only.

Key details to match:
- **L2 norms** (`p=2`) for the BB ratio — distinct from the L1 norms sw_only uses for the density-weight ratio.
- Trial step is over **movable nodes only** (in sw_only: movable Components + fillers, skip FIXED).
- Uses the **total/combined** gradient (wl + λ·density), not the HPWL-only probe_grad.

---

## What sw_only does now (what to change)

- `host/src/sw_only/src/AIEplace.cpp:163` — `step_length = cfg["params"]["init_step_length"];`
  The config constant (0.1) is used as the LITERAL first step. BB/Lipschitz + backtracking only take
  over from iteration 2, so the FIRST step is uncalibrated.
- `initializeDensityWeight()` (AIEplace.cpp:783) already matches XPlace for the density weight λ
  (`density_weight = HPWL_L1 / density_L1 * multiplier`). **Only the step length is the gap.**
- `combineGradients()` (AIEplace.cpp:947) forms the total gradient in `node->next.probe_grad`
  (probe_grad −= electro, electro already λ-weighted) — this is sw_only's `g(x)`.
- Iteration-1 flow (`performIteration`, AIEplace.cpp:49-67): computeHpwlPartials → computeElectricFields
  → initializeDensityWeight → recordInitialHPWL → updatePrecondWeights → performNextStep. The real
  first step happens inside `performNextStep` (AIEplace.cpp:1038) using `step_length`.

---

## Implementation plan

**1. Rename `init_step_length` → `init_step_seed` (and change default 0.1 → 0.01 to match XPlace's args.lr).**
   - `host/src/sw_only/run_config.json`: rename key, set default `0.01`, update the comment to explain
     it is the BB trial-step seed (not the step). Grep the repo for every reader:
     `wsl -e bash -c "cd /home/msears/phd/AIEplace && grep -rn init_step_length vck5000/host vck5000/pl vck5000/tools"`.
     Known sites: AIEplace.cpp:163; tools/morris_factors.py (FACTORS + it's a screened factor);
     tools/dse.py sweep examples; possibly pl_algo host params + report configs. Update all.
   - Keep backward-compat optional: read `init_step_seed`, falling back to `init_step_length` if present,
     so old configs don't break silently (log a deprecation note). Author's call.

**2. Implement the BB estimate at iteration 1** (new helper `estimateInitialStep()` in AIEplace.cpp):
   - Insert AFTER `initializeDensityWeight()` and AFTER the total gradient at x_0 is available
     (i.e. after `combineGradients()` for iteration 1), BEFORE the first real step in `performNextStep`.
     You will need the combined gradient at x_0, then evaluate it again at a trial point.
   - Algorithm (movable Components + fillers, skip FIXED):
     1. g0 = combined gradient at current positions x0 (already computed).
     2. Save x0. Take a trial step: x' = x0 − seed · g0  (seed = init_step_seed). Match the sign
        convention of the REAL step in performNextStep (whatever direction it moves for a given grad).
     3. Recompute the full pipeline at x': computeHpwlPartials + computeElectricFields + combineGradients
        → g'. (Density weight λ stays fixed at its iteration-1 value.)
     4. `step_length = ||x0 − x'||_2 / ||g0 − g'||_2`  (L2 over all movable node coords; guard the
        denominator with a small epsilon).
     5. Restore positions to x0 and proceed with the normal first step using the new step_length.
   - Watch: this adds one extra gradient evaluation at iteration 1 (cheap, one-time). Make sure probe
     positions / Nesterov state (v_1=u_1) are restored so the real step is unaffected apart from
     step_length. Cross-check against how performNextStep's backtracking already re-evaluates gradients
     at trial positions (AIEplace.cpp ~1050) — you may be able to reuse that machinery.

---

## Verification (success criteria)

1. **Builds clean:** `wsl -e bash -c "cd /home/msears/phd/AIEplace/vck5000 && source /tools/Xilinx/Vitis/2022.2/settings64.sh >/dev/null 2>&1; make host"`.
2. **Numeric match vs XPlace:** run adaptec1 (frame is bit-identical to XPlace — see memory
   `xplace_coordinate_frame`) and log sw_only's estimated first `step_length`; run XPlace on adaptec1
   (`xplace_build_and_run` memory) and compare its `init_lr`. They should be the same order of
   magnitude / close once frames are aligned.
3. **SA confirmation (the payoff):** re-run the Morris screen on mgc_fft_a
   (`python3 tools/morris.py --benchmark ispd2015/mgc_fft_a -r 30` → DSE_RUN_SET=morris ... → analyze).
   PREDICTION: `init_step_seed`'s μ*/σ should drop sharply vs the old `init_step_length` (it becomes a
   robust seed). If it does NOT drop, the estimate isn't working — debug before proceeding.
4. **No regression:** the tuned benchmarks (adaptec1, bigblue1, fft_a) must still converge (overflow
   0.07) with HPWL no worse than before. Seeded (random_seed=42) A/B old-vs-new.

---

## Key references
- XPlace: `Xplace/src/initializer.py:171` (formula), `run_placement_nesterov.py:104-113` (ordering),
  `main.py:22` (seed default 0.01).
- sw_only: `AIEplace.cpp:163` (current constant), `:783` initializeDensityWeight, `:947` combineGradients,
  `:1038` performNextStep, `:49-67` iteration-1 flow.
- Auto-memories: `init_step_length_xplace_gap`, `morris_sobol_sensitivity_tooling`,
  `sensitivity_regime_dependence`, `xplace_coordinate_frame`, `xplace_build_and_run`.
- This is part of a larger config-refactor effort (SA-driven): fix nuisance params at defaults,
  characterize the few real knobs, document "change these if not converging". init_step_seed is the
  first param to be refactored from a knob into a self-calibrating default.
