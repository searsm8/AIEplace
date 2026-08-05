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

## 🔧 A fresh clone must bootstrap third_party first
`bash vck5000/tools/bootstrap_third_party.sh` — fetches the **Limbo submodule**
(`third_party/Limbo`, pinned to upstream tag 3.5.2) and builds it out of tree into
`third_party/limbo_{build,install}` (both gitignored). Without it `third_party/Limbo/` is empty
and the host build fails on missing `limbo/parsers/...` headers. **No `.a` is tracked in this
repo** — if you find yourself wanting to commit one, that is the bug. Details + the
`-DBoost_NO_BOOST_CMAKE=ON` gotcha: `vck5000/host/README.md`.

## 📋 Always read vck5000/0_TODO/TODO.md for context
Before starting work, read `vck5000/0_TODO/TODO.md` to understand current priorities, blockers,
and in-progress tasks. This file is the source of truth for project state and helps avoid
re-doing work or working on stale branches.

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

**`vck5000/host/src/common/` is NOT a variant** — it is the parser + data model (DataBase, Grid,
Node/Component/IOPad, Net, Bin, Logger, Common) that both host variants build into themselves,
plus the prebuilt Limbo parser libs in `common/lib/`. Landed 2026-08-04 (TODO #9) to end the
silent fork between the two hosts. Fix a parser or geometry bug **there**, once. Nothing in
`common/` may include `AIEplace.h`, `Visualizer.h`, or anything from `pl/`; see its README.

## Algorithm goal: mimic XPlace as closely as possible
sw_only's placement algorithm should track the XPlace reference (`~/phd/Xplace/src/`) as
faithfully as possible. Prefer matching XPlace's formulation over ad-hoc heuristics or
"crutches" that XPlace does not use — e.g. XPlace bounds the Barzilai-Borwein step with its
backtracking line search alone and applies **no magnitude clamp**, so sw_only does the same
(the fixed `[0.0001, 4000]` step clamp was removed). When sw_only diverges from XPlace, that
divergence should be deliberate and documented, not an accidental workaround.

## pl_algo current state
**`vck5000/pl/src/pl_algo/DATAFLOW.md` is the single authoritative source** — read it before
touching pl_algo, and update it (not this section) when the state changes. `README.md` next to it
is the orientation/entry point. Only the stable summary lives here:

- Gate 1 (HLS C-synthesis, `cd vck5000/pl && make PL=pl_algo TARGET=hw`) **passed**. The datapath
  modules are written and sw_emu-verified against the sw_only CPU golden, one at a time.
- `top.cpp` is still a **bring-up scaffold**: one kernel, one xclbin, a `mode` arg selecting which
  module runs (`host_interface.hpp` `top_mode`). Stage 5 replaces the mode switch with the unified
  per-iteration datapath.
- The device-resident control modules (`bb_reduce`, `param_scheduler`) are **built and verified but
  not yet wired into `top.cpp`** — that is the correct in-progress state, not an oversight. Until
  the resident loop lands, the host (`host/src/pl_algo/src/{Driver.cpp,Placement.hpp}`) owns the
  γ/λ schedule and convergence test, one round-trip per iteration.

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
- sw_only CPU golden functions (all under `vck5000/host/src/sw_only/src/placer/`):
  `computeHpwlPartials_CPU` (`Partials.cpp`), `compute_eField_DCT` (`Density.cpp`),
  `computeOverlaps` (`Density.cpp`).
- Toy bring-up templates (outside this repo, both build + emulate cleanly):
  `~/phd/toy_design` (pure-PL vadd) and `~/phd/toy_aie` (minimal AIE + PL). See auto-memory
  `toy_reference_designs`.
