### pl_algo -- PL-centric AIEplace design

This variant reworks the whole placement iteration onto the PL. The AIE does **only** the
FFT (an 8-lane pool) and the HPWL gradient graph; everything else -- bin density, the
DCT/IDCT/IDXST pre/post-processing, e-field gather, gradient combine, Barzilai-Borwein
step, Nesterov update, and the metric reductions -- runs in the PL.

sw_only (`HOST=sw_only`, formerly `markv1`) remains the working/tuned software-only golden
reference; the partial-offload hardware kernels remain under `AIE=markv1`/`PL=markv1`.

**`DATAFLOW.md` is the current, authoritative description** of the per-iteration data flow,
the module set, and build status. This file is the orientation/entry point; where the two
disagree, `DATAFLOW.md` wins.

#### Layout
- `src/top.cpp` -- the one PL kernel. During bring-up its active datapath is selected by a
  `mode` arg (`host_interface.hpp` `top_mode`), so a single xclbin can verify each module
  independently. Stage 5 replaces the mode switch with the unified per-iteration datapath.
- `src/host_interface.hpp` -- the host<->PL contract (POD only; compiles under g++ AND
  Vitis HLS). Owns `top_mode` and its per-mode port-alias documentation, plus the packed
  record types.
- `src/formats.hpp` -- inter-stage data-flow formats (128-bit beat/AXIS layouts, HLS types).
- `src/modules/*.hpp` -- one module per diagram block:
  - *datapath, wired into `top.cpp`*: `hpwl_gradient` (`hpwl_CU`), `density_bin`,
    `node_footprint` (shared clamped-footprint geometry), `dct_1d`, `transpose`,
    `dct_transpose`, `spectral`, `force_gather`, `iteration_update`, `memory_writer`,
    `metrics`.
  - *control, built + verified but not yet wired into `top.cpp`*: `bb_reduce`,
    `param_scheduler` (see Status).
  - *PL-only alternates*: `fft_pl` + `field_solve_pl` -- the whole density solve with no
    AIE, for the small-grid (`-DPL_GRID`) build.
- The offline models and verification harnesses that cover these modules live in
  **`vck5000/test/`** (moved out of here 2026-08-05 so tests are visible at the top level):
  `density_model`, `density_bin_model`, `fft_pl_test`, `field_solve_test`, `sched_verify`,
  plus `synth_check.{cpp,tcl}` (the HLS C-synthesis gate for the control core).
  Run them with `cd vck5000 && make test` -- seconds, pure g++, no Vitis needed.
- `design.cfg` -- static compile-time PL config (fixed interfaces; authored by hand).
- `generate_link_cfg.py` -- emits the static PL<->AIE connectivity at link time.
- `DATAFLOW.md` -- per-iteration data flow, stage formats, and current status.

#### Build
- PL kernels only (Gate 1, HLS C-synthesis -- no emulation needed):
  `cd vck5000/pl && make PL=pl_algo TARGET=hw`
- Full app: `make PL=pl_algo AIE=pl_algo HOST=pl_algo TARGET=sw_emu BUILD_XRT=1 AIE_DENSITY_INSTANCES=8`
- Per-module sw_emu verifies have `run-*` targets in `vck5000/Makefile`
  (`run-hpwl-grad`, `run-density`, `run-dct`, `run-dct-rowpass`, `run-transpose`,
  `run-dct-transpose`, `run-auv`, `run-idct-transpose`, `run-idxst-transpose`,
  `run-spectral`, `run-field`, `run-force-gather`, `run-density-grad`, `run-iter-update`,
  `run-metrics`, `run-place`), each driving one `--flag` of the `host/src/pl_algo` binary.
  Only **`sw_emu`** is viable for AIE designs on this platform.

#### Status
Gate 1 (C-synthesis) passed. The datapath modules above are written and sw_emu-verified
against the sw_only CPU golden; IDXST is implemented (Stage 4). The host `pl_algo` variant
and the AIE `pl_algo` variant (FFT pool + HPWL graph) both exist. `bb_reduce` and
`param_scheduler` are built and verified but deliberately not yet included by `top.cpp` --
the device-resident loop that consumes them is the next step. Until that lands, the host
(`host/src/pl_algo/src/{Driver.cpp,Placement.hpp}`) keeps the gamma/lambda schedule and the
convergence test, one host round-trip per iteration. See `DATAFLOW.md` for the full status.
