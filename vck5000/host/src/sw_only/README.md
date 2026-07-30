# AIEplace C++ host code source

### What it is:
 A VLSI global placement tool implementing the ePlace algorithm, with optional hardware acceleration on Xilinx Versal AI Engines (VCK5000).

### Architecture
Core classes:

Placer (AIEplace.h/cpp) — Main orchestrator. Runs the Nesterov-based optimization loop: gradient computation, step length (BB estimate), momentum update, density weight adaptation, convergence checking.
DataBase (DataBase.h/cpp) — Reads design input (LEF/DEF or Bookshelf formats via Limbo parsers). Stores macros, components, pins, nets. Handles packet preparation for AIE offloading.
Grid (Grid.h, Grid.cpp) — 2D bin grid over the die area. Computes bin overlaps (density/rho), overflow metrics, and stores electric fields per bin.
Node (abstract), Component, Pin — Node hierarchy. Components have a MacroClass for size; Pins have bounding boxes. Each node tracks position, probe position (for Nesterov lookahead), gradients, bin overlaps, and partials.
Net (Net.h/cpp) — Represents nets connecting nodes. Supports HPWL wirelength computation, sorting for AIE data format, and stores per-node partials.
Computation modules:

Partials.cpp — HPWL gradient (partial derivative) computation. Three CPU methods (cpu = log-sum-exp WA model, simple = threshold-based linear, orig = TBB-parallel) plus AIE-accelerated path.
Density.cpp — Electric field computation from density (Poisson's equation). Uses DCT-based spectral method or naive O(n^4). AIE path sends rows/columns to hardware for DCT/IDCT/IDXST.
DCT.cpp — CPU implementations of DCT, IDCT, IDXST (naive definitions; FFT-based stubs are empty).
Infrastructure:

Logger (Logger.h/cpp) — Singleton logger with key-based filtering, colored output via tabulate, function profiling (ScopeTimer/TIME_FUNCTION()), markdown export.
GraphDriver (GraphDriver.h) — XRT-based driver for AIE compute graphs (partials and density). Manages buffer I/O with VCK5000 hardware.
Visualizer (Visualizer.h) — Cairo-based placement visualization and convergence plotting (behind CREATE_VISUALIZATION flag).
Output.cpp — Results reporting, CSV export, DEF output, GIF generation.
Algorithm Flow
Constructor reads TOML config (toml++), initializes DB from LEF/DEF, creates grid, optionally initializes XRT/AIE.
run() — Random initial placement, then iterate until convergence.
performIteration() — Each iteration:
Reset per-iteration state
On iter 1: bootstrap probe positions, compute initial gradients, set initial density weight
performGradientStep() — BB step length + u_{k+1} = v_k - α∇f(v_k) (stub)
computeMomentumStep() — Nesterov momentum v_{k+1} (stub)
updateDensityWeight() — Adaptive λ based on HPWL trend
Record and print results