# sw_only — the software-only golden reference

A VLSI global placer implementing the ePlace/RePlAce electrostatics formulation. **This variant
runs entirely on the CPU** and is the golden reference every hardware block is verified against.
Hardware offload (AIE + PL) lives in the `pl_algo` variant; `AIE=markv1`/`PL=markv1` hold the older
partial-offload kernels. See TODO #9 — the two hosts merge once pl_algo bring-up completes.

The algorithm tracks XPlace (`~/phd/Xplace/src/`) as faithfully as possible; where it deliberately
diverges, the comment at that line says so and why.

```
make host HOST=sw_only
./build/hw/host/sw_only/aieplace_sw_only.exe [config.toml]   # defaults to host/src/sw_only/run_config.toml
```

## Where to start reading

`src/placer/AIEplace.cpp` is ~110 lines and holds the whole loop skeleton — read it first, then
`include/Node.h` for the per-node state the loop acts on.

## Core classes

| | |
|---|---|
| **Placer** (`AIEplace.h`, `src/placer/`) | The optimizer. Owns every hyperparameter and all iteration state. Split across `AIEplace.cpp` (loop skeleton), `Setup.cpp` (bring-up + initial placement), `Partials.cpp` (∇wirelength), `Density.cpp` (∇density), `Step.cpp` (BB/Nesterov step), `Schedule.cpp` (γ/λ policy + convergence), `Output.cpp` (reporting). |
| **DataBase** (`DataBase.h/.cpp`) | The parsed design — macros, components, IO pads, nets — read from LEF/DEF or Bookshelf via the Limbo parsers. Also generates filler cells. |
| **Grid** (`Grid.h/.cpp`) | The die partitioned into `bins_per_row × bins_per_col` bins. Scatters cell area into bins (ρ), holds per-bin `a_uv` and E-field, computes the overflow metric. `computeNodeFootprint` here is the single definition of density footprint geometry. |
| **Node** (abstract) → **Component**, **IOPad** | A placeable object. Holds `current`/`next` iteration state (`node_pos` u, `probe_pos` v, `probe_grad` ∇f(v)), net memberships, and bin overlaps. `Node::step()` is the Nesterov update. |
| **Net** (`Net.h/.cpp`) | A net and its pins; HPWL computation. |
| **Logger** (`Logger.h/.cpp`) | Singleton logger with key-based filtering, `tabulate` tables, and `TIME_FUNCTION()` scope profiling. |
| **Visualizer** (`Visualizer.h/.cpp`) | Cairo placement rendering and convergence plots. Built only under `BUILD_VIZ` / `CREATE_VISUALIZATION`. |
| **DCT** (`DCT.h/.cpp`) | 1D DCT / IDCT / IDXST — a naive O(N²) reference *and* an O(N log N) FFT (Makhoul) implementation, verified equal. |

## Algorithm flow

1. **`Placer::Placer`** — parse the TOML config (toml++), read LEF/DEF or Bookshelf into the
   DataBase, add fillers, size the bin grid (explicit `bins_per_row`, else the ePlace formula),
   auto-enable the preconditioner iff the design has movable macros, finalize the γ schedule.
2. **`run()`** → `initializePlacement()` (movable cells in a tight Gaussian cluster at die center,
   fillers uniformly at random — XPlace-style), then `performIterationZero()` to bootstrap the
   first gradients and λ, then iterate until `checkConvergence()`.
3. **`performIteration()`**, each iteration:
   - `updatePrecondWeights()` — diagonal preconditioner + the force-balance ratio the schedule reads
   - `estimateInitialStep()` — iteration 1 only: one trial step to calibrate the BB step length
   - `performNextStep()` — Algorithm 2: trial step → recompute ∇HPWL and ∇density at the probe
     position → combine → fresh Barzilai-Borwein estimate → accept, or backtrack and retry
   - `recordIterationResults()` / `printIterationResults()` — HPWL, overflow, best-solution tracking
   - `updateSchedule()` — throttled γ and λ update (XPlace's shared `perform_update` gate)
4. **`printFinalResults()`** — restore the best solution found, write DEF + CSV + summary.

## Compute-method selection (`run_config.toml`)

- `partials_compute_method` — `cpu` (exact weighted-average gradient, the golden) or `simple`
  (LUT-based approximation).
- `density_compute_method` — `cpu` only. It runs the DCT spectral solve (`compute_a_uv_DCT` +
  `compute_eField_DCT`); the naive O(N⁴) `*_naive` pair is kept alongside as the verification
  reference for that path.

## Verification references

`computeHpwlPartials_CPU` (`Partials.cpp`), `compute_eField_DCT` (`Density.cpp`), `computeOverlaps`
and `computeOverflow` (`Density.cpp`) are the functions the hardware blocks are checked against.
