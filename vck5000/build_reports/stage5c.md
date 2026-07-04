# Stage 5c — the ePlace iteration loop on the PL

Running report for Mark to review. One section per milestone. Anything that looked
off is flagged **⚑**.

Context: Stage 5b left the full **gradient** (g_hpwl + g_density) built and sw_emu-verified.
Stage 5c adds the **iteration loop**: combine the gradients, take the Nesterov/BB step, write
coordinates back, reduce metrics, run the host λ/α/convergence policy, and verify the whole
placer against markv1. The 5c algorithm audit (memory `pl_algo_5c_algo_audit`) found markv1
faithful to Xplace/DREAMPlace; the only real gap to carry to the PL was the **preconditioner**
(built into `iteration_update` from the start), plus pinning the combine sign and dropping the
vestigial per-bin `local_density_weight` (Xplace has none).

---

## 5c.1 + 5c.2 — iteration_update (combine + precond + BB step + Nesterov + clamp)

**Status:** implemented; sw_emu verify in progress.

Implemented as one module, `pl/src/pl_algo/src/modules/iteration_update.hpp`, mirroring three
markv1 functions run back-to-back (`AIEplace.cpp`, `Node.h`):

| markv1 | iteration_update |
|---|---|
| `combineGradients` | `g_total = g_hpwl − λ·g_density` (per-node) |
| `Node::step` | `pg = g_total/precond`; `u_{k+1} = v_k − α·pg`; `v_{k+1} = u_{k+1} + coeff·(u_{k+1}−u_k)` |
| `enforceDieBoundaries` | clamp `u_{k+1}` and `v_{k+1}` independently into `[0, die−size]` |

Key fidelity points (all from the audit):
- **Sign is `−`**: the eField sign convention bakes Xplace's `+=` into the field. A wrong sign
  would flip the step direction and fail the verify.
- **No per-bin `local_density_weight`**: Xplace has none; markv1's is a constant `1` that
  self-cancels. Dropped. `g_density` from `force_gather` is the pure `Σ overlap·eField`.
- **Preconditioner built in**: `pg = g_total / precond_weight[n]`. The host supplies the per-node
  weight `max(1, num_pins + precond_coef·λ·area/avg_area)` (host owns the precond_coef/λ schedule
  for v1). This was *the* PL gap the audit flagged.
- **Momentum uses the UNCLAMPED u_{k+1}** then both are clamped — matches markv1's step/clamp order.
- **v is the anchor**: gradients are evaluated at the probe positions v (markv1 convention), so
  `node_box` carries v_k (its {x,y}) and the cell size (its {w,h}); u_k is a separate committed
  buffer. The step emits u_{k+1} and STREAMS v_{k+1} to the Memory Writer (5c.3).

**Structure:** `iteration_update` (producer) → `hls::stream<coord_t>` → `memory_writer` (consumer)
inside a dedicated `iteration_step_df()` dataflow function (kept out of top()'s mode if/else so
`#pragma HLS DATAFLOW` sits at a canonical function-body top level). Natural `coord_t` pointers,
matching force_gather's style. Ports are aliased onto the existing 12 gmem bundles (documented in
`host_interface.hpp` MODE_ITERATION_UPDATE) — no new kernel args, no new memory interfaces.

**Verify** (`IterVerify.cpp`, `--iter-update`, synthetic M=4000): a double-precision golden
replicates combine→precond→step→momentum→clamp and compares both u_{k+1} and v_{k+1}. Inputs are
randomized so a good fraction of nodes clamp at the die boundary (exercises the clamp path).
PASS threshold rel_rms < 1e-5 (float-vs-double).

**Result: PASS.** `AIE=none` sw_emu, M=4000: `max_abs=7.18e-05 rel_rms=3.28e-08`, 4/4000 nodes
clamped at the die boundary (clamp path exercised). Kernel synthesized clean (no HLS errors);
the DATAFLOW iteration_update→memory_writer region built without complaint.

---

## 5c.3 — memory_writer (single coords writer)

**Status:** implemented; verified as the consumer half of the 5c.2 dataflow (v_{k+1} is streamed
through it into the coords buffer, so the IterVerify PASS covers it end-to-end).

`memory_writer.hpp` drains the `hls::stream<coord_t>` and commits each v_{k+1} to the canonical
coords buffer (the buffer the next iteration's gradient pipeline reads). Keeping coords
single-writer is what lets the whole iteration fuse into one kernel later; for v1 it is one stage
of the MODE_ITERATION_UPDATE invocation.

---

## 5c.4 — metrics (HPWL + overflow reduce)

**Status:** implemented; sw_emu verify pending.

`metrics.hpp`:
- **HPWL** = Σ_nets (max_x−min_x)+(max_y−min_y), segmented bbox reduce over nets, pin position =
  `node_pos[node_idx] + {off_x,off_y}` (mirrors hpwl_CU phase A1 and DataBase HPWL).
- **overflow_sum** = Σ_bins max(0, rho−target). The host scales by `bin_area/total_movable_area`
  to form the ePlace overflow ratio (markv1 `Grid::computeTotalOverflow` — the bin_area and the
  movable-area normalization stay on the host, matching "host owns the schedule").
- Totals accumulate in **double** (a float sum over ~1e6 nets/bins is order-dependent to ~0.3%;
  the metric drives convergence, so it must be reproducible), narrowed to float on readback.

**Verify** (`MetricsVerify.cpp`, `--metrics <bench>`): HPWL checked against `hpwlFromPacked` (the
same double golden main.cpp uses), overflow_sum against a double reduce of a synthetic rho.
PASS threshold rel_err < 1e-4.

**Result: PASS.** `AIE=none` sw_emu on `mgc_pci_bridge32_b` (28914 movable / 29281 nodes /
29417 nets / 83944 pins, 1024² bins): HPWL `dev=8.80562e8 golden=8.80562e8 rel_err=3.98e-09`;
overflow_sum `dev=93449.617 golden=93449.618 rel_err=4.77e-09`. Double accumulation makes both
essentially exact.

---

## 5c.5 — host runPlacement orchestration

**Status:** implemented; host compiles clean. Device run is 5c.6.

**⚑ Single-session constraint (important).** The checkpoint notes sw_emu **cannot reopen the
device/AIE-sim within one process**. The per-function drivers (runHpwlGradCU, runDensityGradient,
…) each open the device, so the placement loop *cannot* call them iteratively. `runPlacement`
(Driver.cpp) therefore opens the device+graph **once** and runs every iteration's ~12 passes in
that single session — hpwl_CU → g_hpwl, density_bin → rho, 8-pass field solve → Ex/Ey,
force_gather → g_density, then iteration_update → new u,v. Intermediate matrices cross via host
(same pattern as runField/runDensityGradient). This is the right structure for v1's
host-orchestrated multi-invocation design anyway.

**Host policy** (`Placement.hpp`, pure POD/vector math, ABI-neutral so both the old-ABI setup in
main.cpp and the new-ABI Driver.cpp include it) — every formula a direct transcription of markv1,
audited against Xplace/DREAMPlace in 5c:
- `initDensityWeight` = (Σ|g_wl| / Σ|g_density|)·init_mult
- `bbStepLength` (BB α on host, v1) = ‖Δv‖/‖Δg_total‖, clamped [1e-4, 4000]
- `updateGammaValue` = 10^((ovfl−0.1)·20/9−1)·base_γ
- `momentumCoeff` = (a_k−1)/a_{k+1}, a_{k+1}=(1+√(4a_k²+1))/2
- `updatePrecondWeights` = max(1, degree + precond_coef·λ·area/avg_area)  ← the audit's key PL gap
- HPWL / overflow computed on host in-loop (the verified PL `metrics` module replicated in double
  to avoid an extra device pass; identical result).

**⚑ First-iteration choice.** The audit flagged that markv1 iteration 1 steps with the HPWL-only
gradient (it skips `combineGradients` before the first step). `runPlacement` does the **clean**
version (iter 1 combines with the freshly-initialized λ). So the pl_algo trajectory will differ
slightly from markv1 at iters 1–2 by design; this is the corrected algorithm, not a bug.

The loop is driven by `--place <bench> <xclbin> [max_iters]` (make `run-place`, `PLACE_ITERS`).

## 5c.6 — whole-placer verify vs markv1

**Status:** run pending (below).

**⚑ sw_emu feasibility.** A full ePlace solve is 50–200 iterations; each iteration here is one
`--density-grad`-class evaluation (8 field passes), which the checkpoint clocks at ~15–20 min in
sw_emu. Full convergence in sw_emu is therefore **infeasible** (days) — it is a real-hardware task
(Geert's card). The sw_emu deliverable is a **short trajectory run** that proves the loop closes
correctly end-to-end. The per-module numerics were already verified exactly (5c.1–5c.4, Stage 5b).

### Run 1 — 2 iterations, `mgc_pci_bridge32_b` (M=28914, die 800000², γ=7995). EXIT=0.

```
iter 1: HPWL=8.80562e+08  overflow=0.9981  lambda=2.898e-19  alpha=0.01   coeff=0.0000
iter 2: HPWL=8.80560e+08  overflow=0.9999  lambda=2.898e-19  alpha=4000   coeff=0.2818
final positions finite/in-bounds -> PASS (loop closes correctly)
```

**What this validates.** The full loop runs end-to-end in sw_emu: hpwl_CU → g_hpwl, density solve
(density_bin → 8-pass field → force_gather) → g_density, host metrics, host policy, iteration_update
→ new u,v, feed back. Strong correctness signals:
- iter-1 **HPWL=8.80562e8 exactly matches the standalone metrics verify** for this benchmark
  (initial DEF positions) — the in-loop HPWL and the coords fed to the gradient pipeline are correct.
- overflow=0.998 at the initial (pre-spread, heavily overlapping) placement is physically sensible.
- the policy scalars evolve correctly: iter-1 seeds α=0.01 / coeff=0 (nesterov_ak=1), iter-2's
  Nesterov coeff=0.2818 = (a_1−1)/a_2 with a_1=1.618 — exactly the recurrence.
- final positions finite and inside the die.

**⚑ Flags (expected, but worth stating).**
1. **HPWL barely moves over 2 iters** (8.80562e8 → 8.80560e8). This is *expected*, not a bug: iter 1
   takes the deliberately tiny seed step (α=0.01) so cells move ~fractions of a unit on an 800000-unit
   die. Meaningful movement only comes once BB α ramps up.
2. **α saturates to the 4000 clamp at iter 2.** BB α = ‖Δv‖/‖Δg_total‖; after the tiny iter-1 step,
   both Δv and Δg_total are ~0, so the ratio is unstable and clamps — exactly markv1's behavior
   (its clamp is [1e-4, 4000]). The *first real* step happens with this large α at iter 2, producing
   v_3, which 2 iterations don't measure.
3. **λ stays 2.898e-19.** `updateDensityWeight` fires only every 3rd iteration (markv1's cadence), so
   λ hasn't updated yet. The tiny magnitude is the ratio-based init absorbing the field-vs-HPWL scale
   gap (markv1 does the same); with λ this small the early steps are effectively HPWL-gradient
   descent, matching markv1's early behavior.

**⚑ 2 iterations is too few to show placement progress or to stress-test BB-without-backtracking**
(v1 has no backtracking; a saturated α=4000 step could in principle overshoot, caught only by the die
clamp). A longer run (6 iterations) is underway to (a) show real cell movement past the seed step and
(b) confirm the large-α step stays stable. Results appended below when it completes.

### Run 2 — 6 iterations, `mgc_pci_bridge32_b`. EXIT=0, PASS.

```
iter  HPWL         overflow  alpha   coeff
 1    8.80562e+08  0.9981    0.01    0.0000
 2    8.80560e+08  0.9999    4000    0.2818
 3    8.80232e+08  0.9995    4000    0.4340
 4    8.79977e+08  0.9989    4000    0.5311
 5    8.79573e+08  0.9983    4000    0.5988
 6    8.78953e+08  0.9975    4000    0.6489
final positions finite/in-bounds; overflow decreasing -> PASS
```

**This is a clean, stable, monotonic descent — the whole placer works.**
- **HPWL descends monotonically** from iter 2 (once the seed step is past): 8.8056e8 → 8.7895e8,
  −0.19% over 6 iters and *accelerating* (−0.075% in iter 6 alone) as the Nesterov coeff ramps
  0.00 → 0.28 → 0.43 → 0.53 → 0.60 → 0.65 (exactly the (a_k−1)/a_{k+1} recurrence).
- **overflow declines steadily** from its iter-2 peak (0.9999 → 0.9975) — cells are beginning to
  de-overlap even with λ still tiny (pure-wirelength phase; real spreading comes once λ grows).
- **The α=4000 BB step is stable — no overshoot/explosion.** After α saturates the clamp at iter 2
  (Δv≈0 following the tiny seed step), the large step at iters 2–6 moves cells in a
  HPWL-reducing direction and the die clamp keeps everything in-bounds. So **BB-without-backtracking
  is stable on this benchmark** for these iterations (a real-HW long run should still confirm it
  over the full solve — see Stage 6 item 7).
- Deterministic: the 2-iter run's iters 1–2 reproduce exactly here.

**Conclusion (5c.6).** The full PL placement loop is functionally correct end-to-end in sw_emu:
gradients → combine/precond/step → write-back → next-iteration gradients, with the host policy
driving λ/α/γ/precond/momentum. HPWL and overflow both move the right way and the step is stable.
Full convergence + final-quality comparison vs markv1 is the real-HW task (Stage 6, item 1).

---

## Stage 5c status & the next stage

**Stage 5c (functional draft of the iteration loop) is complete.** Every sub-item is implemented,
and everything that can be verified in sw_emu is verified:
- 5c.1 combine, 5c.2 Nesterov+precond, 5c.3 memory_writer — `--iter-update` rel_rms 3.28e-08.
- 5c.4 metrics — `--metrics` HPWL/overflow rel_err ~4e-09.
- 5c.5 orchestration + 5c.6 loop — `--place` runs end-to-end, loop closes, HPWL matches the metrics
  golden, policy scalars evolve per the recurrences.

Per the project workflow (math → software golden → hardware draft → **hardware optimization**), the
functional hardware draft is done. The next stage is **Stage 6: real-HW validation + hardware
optimization**, roughly in priority order:

1. **Real-hardware convergence (Geert's card).** The true completion of 5c.6: run the placement to
   full convergence (50–200 iters) on real HW — infeasible in sw_emu — and compare final HPWL /
   overflow against markv1's DSE numbers. This is the one remaining *correctness* gate.
2. **Re-tune the λ / γ schedule for the 1024² grid.** The field magnitudes at 1024² differ from
   markv1's 64² by orders of magnitude (this run's λ_init ≈ 3e-19). The *ratio*-based init is
   scale-robust, but the multiplicative λ growth cadence and the γ schedule constants were tuned at
   64² and should be re-checked at 1024² once real-HW iteration counts are available.
3. **Re-enable fillers.** Excluded in v1 (standing pending item); materially affects density/quality.
   Needed before a fair final-quality comparison vs markv1.
4. **Fuse the density solve.** The 8-pass field solve currently round-trips each matrix through host
   memory per iteration (~16 host↔device copies/iter). Fusing the passes into on-chip dataflow (and
   widening the coord/matrix ports from 32-bit to 128-bit beats per DATAFLOW.md) is the biggest
   throughput lever and the core of the "hardware optimization" stage.
5. **Single-kernel iteration.** Collapse the per-iteration `MODE_*` dispatch into one fused datapath
   (hpwl + density + step) once the multi-pass density is streamlined.
6. **BB α on the PL.** Currently a host reduction (v1); move the ‖Δv‖/‖Δg‖ reduction onto the PL.
7. **Backtracking / stability.** v1 has no backtracking; the BB α can saturate to the 4000 clamp
   after a tiny step (seen at iter 2). The die clamp bounds it, but real-HW long runs should confirm
   this doesn't cause overshoot — if it does, port markv1's Algorithm-2 backtracking.
8. **Convergence + best-solution tracking** on the host (overflow-countdown + snapshot/restore),
   currently minimal in `runPlacement`.
