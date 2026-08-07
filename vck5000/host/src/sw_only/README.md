# sw_only — the software-only golden reference

A VLSI global placer implementing the ePlace/RePlAce electrostatics formulation. **This variant
runs entirely on the CPU** and is the golden reference every hardware block is verified against.
Hardware offload (AIE + PL) lives in the `pl_algo` variant; `AIE=markv1`/`PL=markv1` hold the older
partial-offload kernels. The parser and data model are **shared** with `pl_algo` — they live in
`host/src/common/`, not here (TODO #9, 2026-08-04). What remains in this directory is what is
genuinely sw_only: the CPU optimizer and the DCT. Visualization is not built into the host at all
(TODO #16) -- the placer only dumps node positions (`PositionDump.cpp`, config `output.dump_positions`);
rendering happens afterward in `docs/visualization.md`'s post-processing tools (repo root, alongside `vck5000/`).

The algorithm tracks XPlace (`~/phd/Xplace/src/`) as faithfully as possible; where it deliberately
diverges, the comment at that line says so and why.

```
make host HOST=sw_only
./build/hw/host/sw_only/aieplace_sw_only.exe [config.toml]   # defaults to host/src/sw_only/default_config.toml
```

## Where to start reading

`src/placer/AIEplace.cpp` is ~110 lines and holds the whole loop skeleton — read it first, then
`../common/include/Node.h` for the per-node state the loop acts on.

## Core classes — this variant

| | |
|---|---|
| **Placer** (`AIEplace.h`, `src/placer/`) | The optimizer. Owns every hyperparameter and all iteration state. Split across `AIEplace.cpp` (loop skeleton), `Setup.cpp` (bring-up + initial placement), `Partials.cpp` (∇wirelength), `Density.cpp` (∇density), `Step.cpp` (BB/Nesterov step), `Schedule.cpp` (γ/λ policy + convergence), `Output.cpp` (reporting), `Phase2.cpp`/`MacroLegalize.cpp` (mixed-size phase 2), `PositionDump.cpp` (node-position export for the offline visualizer). |
| **DCT** (`DCT.h/.cpp`) | 1D DCT / IDCT / IDXST — a naive O(N²) reference *and* an O(N log N) FFT (Makhoul) implementation, verified equal. |

## Core classes — shared (`../common/`, also built into `pl_algo`)

| | |
|---|---|
| **DataBase** (`DataBase.h/.cpp`) | The parsed design — macros, components, IO pads, nets — read from LEF/DEF or Bookshelf via the Limbo parsers. Also generates filler cells. |
| **Grid** (`Grid.h/.cpp`) | The die partitioned into `bins_per_row × bins_per_col` bins. Scatters cell area into bins (ρ), holds per-bin `a_uv` and E-field, computes the overflow metric. `computeNodeFootprint` here is the single definition of density footprint geometry. |
| **Node** (abstract) → **Component**, **IOPad** | A placeable object. Holds `current`/`next` iteration state (`node_pos` u, `probe_pos` v, `probe_grad` ∇f(v)), net memberships, and bin overlaps. `Node::step()` is the Nesterov update. |
| **Net** (`Net.h/.cpp`) | A net and its pins; HPWL computation. |
| **Logger** (`Logger.h/.cpp`) | Static logger with an ordered severity scale, `tabulate` tables, and `TIME_FUNCTION()` scope profiling. |

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

## Compute-method selection (`default_config.toml`)

- `partials_compute_method` — `cpu` (exact weighted-average gradient, the golden) or `simple`
  (LUT-based approximation).
- `density_compute_method` — `cpu` only. It runs the DCT spectral solve (`compute_a_uv_DCT` +
  `compute_eField_DCT`); the naive O(N⁴) `*_naive` pair is kept alongside as the verification
  reference for that path.

## Threading

The placement iteration is parallelized with OpenMP (`-fopenmp`; building without it still
compiles and runs, single-threaded). There is no thread-count config key — OpenMP's default is
used, minus one CPU (see `configureThreadPool` in `Setup.cpp` for why), and `OMP_NUM_THREADS`
overrides it. **A concurrent sweep must set `OMP_NUM_THREADS`**, or N runs × all cores each will
oversubscribe the box.

`+` on floats is not associative, so a threaded loop is only reproducible if it does not reorder
additions. Three kinds of loop, and `params.deterministic` governs only the third:

| | example | threaded? |
|---|---|---|
| **Disjoint writes** | `stepAllNodes`, the 1-D transform row passes, bin clears | always — nothing is summed |
| **Scalar reductions** | L1 gradient norms, total HPWL, BB norms | always, via `OrderedReduce` (`Common.h`), which computes terms in parallel and adds them in index order |
| **Scatter reductions** | cell area → shared bins, net gradients → shared nodes | always for the per-item work; the shared add is what the flag switches |

- `deterministic = true` (default) — the shared add is replayed on one thread in the original
  item order. **Bit-identical to the single-threaded golden at any thread count**, which is what
  `tools/verify_swonly.sh` + `tools/compare_swonly.sh` check.
- `deterministic = false` — one atomic add per deposit. Faster, but the order in which threads
  hit the same bin or node follows their interleaving, so results move slightly run to run.

Keep it on unless you only want throughput: sw_only is the reference every `pl_algo` hardware
block is verified against, and the ordered path costs little (see the numbers in TODO #12).

Two loops are deliberately left serial: the final linear scan in `computeOverflow` (already
memory-bound, so parallelizing only buys an ordering caveat), and `computeOverflow`'s deposit
under `deterministic` (a metric, not the solver's field, and it has no per-node list to replay
the way `computeOverlaps` does).

## Verification references

`computeHpwlPartials_CPU` (`Partials.cpp`), `compute_eField_DCT` (`Density.cpp`), `computeOverlaps`
and `computeOverflow` (`Density.cpp`) are the functions the hardware blocks are checked against.

`tools/verify_swonly.sh <dir>` runs a fixed design set with a pinned RNG seed and collects
`iterations.dat` + `RowBasedPlacement.def`; `tools/compare_swonly.sh <ref> <new>` diffs two such
trees. Run it after any change that is supposed to be behavior-preserving.
`tools/profile_swonly.sh` re-measures the per-function split across designs and grid sizes.
