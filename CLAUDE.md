# AIEplace — project notes for Claude

## ⚠️ The Bash tool runs on Windows, not WSL — wrap every command in `wsl`
The `Bash` tool executes in **Git Bash / MINGW64 on Windows**, even though this project
lives in WSL. Symptoms when you forget: `uname` reports `MINGW64_NT…Msys`, WSL paths like
`/home/msears/phd/AIEplace` return `No such file or directory`, and the Linux toolchain
(Vitis, XRT, `make`, `v++`) is missing. The working dir shows up as the UNC path
`//wsl.localhost/Ubuntu/…`.

**Fix — run everything inside WSL by wrapping the command:**
```bash
wsl -e bash -c "cd /home/msears/phd/AIEplace && <your command>"
```
Use real WSL/Linux paths (`/home/msears/…`) *inside* the wrapper, not the Windows UNC path.
Anything touching the repo build, the toolchain, or Linux tools must go through `wsl -e bash -c`.

## What this is
AIEplace ports the ePlace analytical placement algorithm onto the AMD Versal VCK5000
(Programmable Logic + AI Engines) for acceleration. Design variants live under
`vck5000/{aie,pl,host}/src/<variant>/`, selected by the make vars `AIE=`/`PL=`/`HOST=`
(host defaults to `sw_only`; `AIE`/`PL` default to `markv1`).

- **`sw_only`** (`HOST=sw_only`) — the working, tuned software-only golden reference: the full
  placement iteration on the CPU. Golden reference used to verify the new design, but still
  under construction to match XPlace. (Renamed from `markv1`; the partial-offload hardware
  kernels it was co-developed with remain under `AIE=markv1`/`PL=markv1`.)
- **`pl_algo`** — the new PL-centric design (git branch `pl_algo`): the entire placement
  iteration runs on the PL; the AIE does only the FFT and the HPWL gradient graph.

## Algorithm goal: mimic XPlace as closely as possible
sw_only's placement algorithm should track the XPlace reference (`~/phd/Xplace/src/`) as
faithfully as possible. Prefer matching XPlace's formulation over ad-hoc heuristics or
"crutches" that XPlace does not use — e.g. XPlace bounds the Barzilai-Borwein step with its
backtracking line search alone and applies **no magnitude clamp**, so sw_only does the same
(the fixed `[0.0001, 4000]` step clamp was removed). When sw_only diverges from XPlace, that
divergence should be deliberate and documented, not an accidental workaround.

## pl_algo current state (2026-06)
- Written under `vck5000/pl/src/pl_algo/`: `src/top.cpp` (top-level kernel that just wires the
  modules per iteration), `src/formats.hpp` (the data-flow format contract), `src/modules/*.hpp`
  (memory_writer, hpwl_manager, density_manager, iteration_update, metrics), `DATAFLOW.md`, and
  build glue (`makeflags.mk`, `design.cfg`, `generate_link_cfg.py`).
- **Module internals are stubs** — each module is defined by its I/O contract only (see
  `DATAFLOW.md` and `formats.hpp`).
- **Next step = Gate 1:** synthesize the top kernel with
  `cd vck5000/pl && make PL=pl_algo TARGET=hw` (HLS C-synthesis; needs no emulation). Then fill
  modules one at a time, each verified against the sw_only CPU reference.

## Key design decisions
- Hardware grid is **1024×1024** (sw_only used 64). Matrices (bin density, Ex, Ey) are
  **DDR-resident**, streamed in row tiles; on-chip RAM holds only working tiles.
- DCT/IDCT/IDXST **pre/post-processing runs in PL; the AIE does only the FFT** (one
  forward-FFT config; the transform_mode FSM lives entirely in the PL). IDXST is
  implemented in v1 (Stage 4): the golden `compute_eField_DCT` uses IDXST on *both*
  Ex (x-axis) and Ey (y-axis), and IDXST is nearly free — same FFT + twiddle ROM as
  IDCT, plus an input reversal and an odd-output sign-flip.
- v1 keeps the γ/λ schedule + convergence test **on the host** (it's the most-tuned part);
  expose them as register-mapped values so the policy can migrate onto the PL later.
- **No backtracking in v1** — Barzilai-Borwein/Lipschitz step length only.

## Build / emulation (Versal VCK5000, Vitis 2022.2)
- Source first: `/tools/Xilinx/Vitis/2022.2/settings64.sh` and `/opt/xilinx/xrt/setup.sh`.
  `PLATFORM_REPO_PATHS` and the license MAC-pin are already in `~/.bashrc` / `/etc/wsl.conf`
  (details in auto-memory `hardware_bringup`).
- **Only `sw_emu` is viable for AIE designs** — `hw_emu` lacks `xclGraphOpen` graph control on the
  VCK5000 QDMA platform. Real-hardware (`TARGET=hw`) runs on colleague **Geert's** card.
- Versal is a **3-step flow**: `v++ -c` (.xo) → `v++ -l` (.xsa) → `v++ -p` (.xclbin; include the
  AIE `libadf.a` as a package input).
- An emulation host run needs `$XILINX_VITIS/lib/lnx64.o` and `$XILINX_XRT/lib` on
  `LD_LIBRARY_PATH`.

## Verification references
- sw_only CPU golden functions: `computeHpwlPartials_CPU` (`Partials.cpp`),
  `compute_eField_DCT` (`Density.cpp`), `computeOverlaps`.
- Toy bring-up templates (outside this repo, both build + emulate cleanly):
  `~/phd/toy_design` (pure-PL vadd) and `~/phd/toy_aie` (minimal AIE + PL). See auto-memory
  `toy_reference_designs`.
