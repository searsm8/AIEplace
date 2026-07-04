# Checkpoint — pl_algo Stage 5c COMPLETE (functional iteration loop on the PL); next = Stage 6 (real-HW + hardware optimization)

## Where we are
The **entire ePlace iteration** now runs on the PL, sw_emu-verified end to end. From node
positions the PL+AIE produces `g_hpwl` and `g_density` (Stages 1–5b), and Stage 5c added the
loop that turns them into placement: combine → precondition → Barzilai-Borwein/Nesterov step →
die clamp → write-back, plus the {HPWL, overflow} metrics reduce and the host λ/α/γ/precond/
convergence policy. The functional hardware *draft* (per the math→golden→draft→optimization
workflow) is done; what remains is real-hardware convergence and the hardware-optimization stage.

## What landed this session (branch `pl_algo`, all sw_emu-verified)
- **5c algorithm audit** (memory `pl_algo_5c_algo_audit`): markv1's Nesterov/metrics found faithful
  to Xplace/DREAMPlace. The one real PL gap was the **preconditioner** (built into iteration_update
  from the start). Confirmed the combine sign is `−` (eField bakes Xplace's `+=`) and that Xplace
  has **no per-bin local_density_weight** (markv1's is a self-cancelling constant → dropped).
- `f81212b` **Stage 5c.1–5c.4** — `iteration_update.hpp` (one Nesterov step: `g_total = g_hpwl −
  λ·g_density`, precondition by host weight, `u=v−α·P·g`, `v=u+coeff·(u−u_k)` on unclamped u, clamp
  both), `memory_writer.hpp` (single coords writer, DATAFLOW pair via `iteration_step_df`),
  `metrics.hpp` (HPWL bbox reduce + overflow_sum, double accumulation). `MODE_ITERATION_UPDATE` /
  `MODE_METRICS` reuse the existing 12 gmem bundles (port aliasing in host_interface.hpp).
  Verify: `--iter-update` rel_rms 3.28e-08 (clamp path exercised); `--metrics` HPWL/overflow
  rel_err ~4e-09.
- `b1bfd92` **Stage 5c.5** — `runPlacement` (Driver.cpp): the full loop in ONE device/graph session
  (sw_emu can't reopen the device/AIE-sim in a process), ~12 passes/iter (hpwl_CU, density_bin,
  8-pass field, force_gather, iteration_update), intermediate matrices crossing via host. Host
  policy in `Placement.hpp` (POD/ABI-neutral): initDensityWeight, bbStepLength (BB α on host),
  updateGammaValue, momentumCoeff, updatePrecondWeights, host HPWL/overflow. `--place` flag.
- `b5c2e75` **Stage 5c.6** — `--place mgc_pci_bridge32_b` 2-iter sw_emu run (EXIT=0): loop closes,
  iter-1 HPWL matches the metrics golden exactly, Nesterov coeff matches the recurrence, positions
  finite/in-bounds. (A 6-iter run for movement/stability is in the build report.)

## Verified sw_emu state (per `--flag` on the current xclbin)
Prior: `--hpwl-grad --density --dct --dct-rowpass --transpose --dct-transpose --auv
--idct-transpose --idxst-transpose --spectral --field --force-gather --density-grad`.
New: `--iter-update --metrics --place`. All PASS.

## ⚑ sw_emu limit on 5c.6
A full solve is 50–200 iters; each iter is one `--density-grad`-class evaluation (8 field passes,
~15 min in sw_emu). Full convergence in sw_emu is infeasible — it is a **real-HW task (Geert's
card)**. The sw_emu deliverable is the short-trajectory loop-closure check (done). See
`vck5000/build_reports/stage5c.md` for the trajectory analysis and the flagged-but-expected early
behavior (tiny seed step, BB α saturating to the 4000 clamp, λ_init ≈ 3e-19 from the 1024²-grid
field scale).

## Stage 6 plan (real-HW + hardware optimization), priority order
1. **Real-HW convergence (Geert's card)** — the true 5c.6 completion; compare final HPWL/overflow
   vs markv1 DSE. The one remaining correctness gate.
2. **Re-tune λ/γ schedule for 1024²** — field magnitudes differ from markv1's 64² by orders of
   magnitude; the ratio-based init is scale-robust but the growth cadence/γ constants need a look.
3. **Re-enable fillers** (excluded in v1; affects density/quality — needed for a fair comparison).
4. **Fuse the density solve** — 8 passes currently round-trip via host each iter; fuse to on-chip
   dataflow + widen ports to 128-bit beats (DATAFLOW.md). Biggest throughput lever.
5. **Single-kernel iteration** — collapse the per-iter `MODE_*` dispatch into one fused datapath.
6. **BB α on the PL** (currently a host reduction).
7. **Backtracking/stability** — v1 has no backtracking; confirm BB-α saturation doesn't overshoot on
   long real-HW runs (die clamp bounds it); port markv1 Algorithm-2 if needed.
8. **Convergence + best-solution tracking** on host (overflow-countdown + snapshot/restore).

## Key references
- **PL modules:** `vck5000/pl/src/pl_algo/src/modules/{iteration_update,memory_writer,metrics,
  force_gather,density_bin,dct_transpose,spectral,dct_1d,hpwl_gradient}.hpp`; `top.cpp`;
  `host_interface.hpp` (`top_mode` incl. MODE_ITERATION_UPDATE/MODE_METRICS, port aliasing);
  `formats.hpp`; `DATAFLOW.md`.
- **Host:** `vck5000/host/src/pl_algo/src/{Driver,Placement,IterVerify,MetricsVerify,main}.*`.
- **markv1 golden:** `AIEplace.cpp` (performNextStep, stepAllNodes, combineGradients,
  updatePrecondWeights, updateDensityWeight, updateGamma, checkConvergence), `Node.h` (step()),
  `Density.cpp` (computeElectrostaticForce), `Partials.cpp`.
- **Xplace/DREAMPlace** source of truth: `~/phd/Xplace/src/{nesterov_optimizer,param_scheduler,
  calculator,initializer}.py`, `~/phd/DREAMPlace/dreamplace/NesterovAcceleratedGradientOptimizer.py`.
- **Auto-memory:** `pl_algo_5c_algo_audit`, `pl_algo_force_gather`, `pl_algo_density_manager`,
  `architecture`, `pin_architecture`.

Stage 5c done, clean tree. Paused here.
