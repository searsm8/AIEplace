### pl_algo -- PL-centric AIEplace design

This variant reworks the whole placement iteration onto the PL. The AIE does **only** the
FFT (an 8-lane pool) and the HPWL gradient graph; everything else -- bin density, the
DCT/IDCT/IDXST pre/post-processing, e-field gather, gradient combine, Barzilai-Borwein
step, Nesterov update, and the metric reductions -- runs in the PL.

markv1 remains the working/tuned software+partial-offload reference and is untouched.

#### Layout
- `src/top.cpp` -- top-level kernel; owns the per-iteration FSM and wires the modules.
- `src/formats.hpp` -- formal data-flow format definitions (the inter-stage contract).
- `src/modules/*.hpp` -- one module per diagram block, each a documented black box:
  `memory_writer`, `hpwl_manager`, `density_manager`, `iteration_update`, `metrics`.
- `design.cfg` -- static compile-time PL config (fixed interfaces; authored by hand).
- `generate_link_cfg.py` -- emits the static PL<->AIE connectivity at link time.
- `DATAFLOW.md` -- narrative of the per-iteration data flow and stage formats.

#### Build
- PL kernels only (Gate 1 synthesis check):
  `cd vck5000/pl && make PL=pl_algo TARGET=hw_emu`
- Full app (later, needs `aie/src/pl_algo` + `host/src/pl_algo`):
  `make PL=pl_algo AIE=pl_algo HOST=pl_algo TARGET=hw_emu`

#### Status
Top-level + module signatures + data-flow formats are defined; module internals are
stubs. The host `pl_algo` variant, the AIE `pl_algo` variant (FFT pool + HPWL graph), and
IDXST are not yet written. v1 keeps the gamma/lambda schedule and convergence test on the
host (one host call per iteration).
