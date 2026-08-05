# AIEplace — project notes for Claude

## TL;DR — where the project stands
*Last updated 2026-08-05.* **Whenever you write a handoff, checkpoint, or end-of-session
summary, update these four lines as part of writing it.** A stale TL;DR is worse than no
TL;DR, because it gets believed. If what you're summarising doesn't change any of these
lines, say so and leave them alone.

- **Working on:** `pl_algo` (branch `pl_algo`) — moving the entire placement iteration onto the PL.
- **Done:** all datapath modules written; HLS C-synthesis clean; each module verified against the
  sw_only CPU golden one at a time; `bb_reduce` + `param_scheduler` built and verified.
- **In progress:** composing the datapath + device-resident control into one resident `top` loop
  (Stage 5). `top.cpp` is still a mode-switch bring-up scaffold, and the host still owns the
  γ/λ schedule one round-trip per iteration.
- **To verify anything:** `cd vck5000 && make test` — seconds, no Vitis needed. See
  *Verification Loop* below before writing or checking any module.

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

**Before inventing a heuristic, go read how XPlace does it** — don't reason it out from first
principles and don't guess from memory:
```bash
grep -rn "<the quantity>" ~/phd/Xplace/src/
```
If XPlace has a formulation, match it. If it genuinely has none, say so explicitly and flag
that the choice is ours — that is exactly the kind of decision that must be written down
(TODO.md or memory), or a later session will re-derive it from scratch.

## pl_algo current state
**`vck5000/pl/src/pl_algo/DATAFLOW.md` is the single authoritative source** — read it before
touching pl_algo, and update it (not this section) when the state changes. `README.md` next to it
is the orientation/entry point. Only the stable summary lives here:

- **HLS C-synthesis passes** (`cd vck5000/pl && make PL=pl_algo TARGET=hw` → 0 errors, `top.xo`
  built). The datapath modules are written and sw_emu-verified against the sw_only CPU golden,
  one at a time.
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

## Verification Loop
**Every PL module is verified offline against a golden before it goes near the device.**
A module that hasn't cleared steps 2 and 3 isn't done, and optimizing it wastes the effort.

1. **Write the harness.** `test/<module>_test.cpp` — pure g++, no XRT, no HLS,
   runs in seconds. Add it to `HARNESSES` in `test/Makefile`.
2. **Compare against a golden, and state the tolerance in the file.** Goldens come in three
   kinds, all in use today:
   - a **sw_only CPU function** (`host/src/sw_only/src/placer/`) — `computeHpwlPartials_CPU`
     (`Partials.cpp`), `compute_eField_DCT` (`Density.cpp`), `computeOverlaps` (`Density.cpp`)
   - a **naive double-precision reference** written inline in the test (see `field_solve_test.cpp`)
   - a **recorded sw_only trace** replayed row-for-row (see `sched_verify.cpp`)

   Scalar/control paths must match **bit-exact**; float datapath ~1e-6 rel_rms.
3. **Confirm it synthesizes** — `test/synth_check.tcl`. This is a *separate* check from
   numerical correctness; passing one says nothing about the other.
4. **Only then** wire it into `top.cpp` and sw_emu-verify the trajectory vs the golden.

### A test asserts; it does not print
The harness must compute the verdict itself and **exit 0 (pass) / non-zero (fail)**. Printing
`rel_rms=9.8e-07` next to the words "PASS if ~1e-6" is not a test — it's a report that requires
a human to read it, and it will pass forever once nobody does. Three of the five harnesses were
exactly this until 2026-08-05. Keep printing the numbers (drift is informative), but always
*also* compare in code. When adding a threshold, take it from the observed value with real
margin — `field_solve_test` sits at 0.98× a 1e-6 bound, so its bound is 2e-6; a genuine
regression here is orders of magnitude, not a few percent.

### Three tiers, by cost
| tier | what | cost | needs | how |
|---|---|---|---|---|
| **1 — offline** | `test/*.cpp` vs golden | **seconds** | just `g++` | `cd vck5000 && make test` |
| **2 — synthesis** | `test/synth_check.tcl` | minutes | Vitis | `vitis_hls -f synth_check.tcl` |
| **3 — emulation** | the `run-*` bring-up modes | slow | Vitis + built xclbin | `make run-<mode>` (see `vck5000/Makefile`) |

**Run tier 1 after every edit under `pl/src/pl_algo/src/modules/`** — it costs nothing and it is
the only thing standing between a normalization typo and finding out three weeks later in sw_emu.
Tier 3 is for integration points. A slow test you skip protects nothing.

Test *inputs* live in `test/fixtures/` and are committed. They deliberately do **not** live in
`vck5000/results/`, which is gitignored — an automated test cannot depend on a file that isn't
in the repo. See `test/fixtures/README.md` before swapping a fixture; `sched_verify`'s
convergence config must match its trace's `config_used.json`.

### Known gap: sw_only has no automated tests
All five harnesses cover `pl_algo`. **sw_only — the most-tuned code in the repo and the golden
everything else is checked against — has no tripwire at all.** `make run` exercises it end to
end, but nothing asserts. The shape of the fix (not yet built, don't assume it exists): one
tier-2-speed regression test running the smallest benchmark for N iterations with a pinned
`random_seed`, asserting final HPWL and overflow against recorded values. **Full plan and the
open decisions: TODO #17.** Until that exists,
**changes to sw_only need manual A/B against a known-good run** — treat "the tests passed" as
saying nothing whatsoever about sw_only.

### Other references
- Toy bring-up templates (outside this repo, both build + emulate cleanly):
  `~/phd/toy_design` (pure-PL vadd) and `~/phd/toy_aie` (minimal AIE + PL). See auto-memory
  `toy_reference_designs`.

## Running it
- `make run` — builds if needed, then runs `HOST=sw_only` with `host/src/sw_only/run_config.toml`.
- `make test` — the tier-1 suite above.
- `make help` — current variable settings and every build target.

## Coding style for this repo
General style rules are in `~/.claude/CLAUDE.md`; this is the hardware-specific addition.

**HLS code reads differently from pure software.** Annotate the datapath: pragmas,
memory-resource intent (`_URAM`/`_BRAM`/`_DDR` suffixes), and a short note on why a loop is
pipelined/unrolled the way it is. The hardware structure is not obvious from the C, so make it
explicit. Host and model code is plain C++ and wants none of that — there, favor clarity and
brevity and let idiomatic control flow carry the meaning.
