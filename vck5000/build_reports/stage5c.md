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
`--density-grad`-class evaluation (11 field passes), which the checkpoint clocks at 20–40 min in
sw_emu. Full convergence in sw_emu is therefore **infeasible** (days) — it is a real-hardware task
(Geert's card). The sw_emu deliverable is a **short trajectory run** (2–3 iterations) that proves
the loop closes correctly: gradients feed the step, positions stay finite and in-bounds, and the
HPWL/overflow trajectory is sane (ePlace HPWL *rises* early as cells spread from overlap; overflow
falls). The per-module numerics were already verified exactly (5c.1–5c.4, Stage 5b).
