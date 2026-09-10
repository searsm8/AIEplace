# common — the parser and data model shared by every host variant

Everything here is used *identically* by `HOST=sw_only` and `HOST=pl_algo`. Nothing here knows
about the optimizer, the visualizer, or XRT: no file in this directory includes `AIEplace.h`,
`Visualizer.h`, or anything from `pl/`. That is the boundary — if a change needs the Placer, it
belongs in the variant, not here.

Created 2026-08-04 (TODO #9) by promoting sw_only's copies and deleting pl_algo's. The two trees
had been a **silent fork**: `pl_algo/` carried a June/July snapshot of these same 15 files under
the same class names, so a fix landed in one tree simply did not exist in the other, and nothing
caught it. One such divergence had already gone live — see "What the merge exposed" below.

## Contents

| | |
|---|---|
| `DataBase.h/.cpp` | The parsed design — macros, components, IO pads, nets — read from LEF/DEF or Bookshelf via the Limbo parsers. Also generates filler cells and writes DEF. |
| `Grid.h/.cpp` | The die partitioned into `bins_per_row × bins_per_col` bins. `computeNodeFootprint` is the single definition of density footprint geometry (the PL mirrors it in `pl/src/pl_algo/src/modules/node_footprint.hpp`). |
| `Node.h` → `Component.h`, `IOPad.h` | A placeable object and its per-iteration state. `MacroClass.h` is the cell type it points at. |
| `Net.h/.cpp` | A net (hyperedge), its pins, and HPWL. |
| `Bin.h` | `Box` (axis-aligned rectangle) and `Bin` (one density-grid cell). |
| `Logger.h/.cpp` | Static logger with an ordered severity scale, `tabulate` tables, `TIME_FUNCTION()` scope profiling. |
| `Common.h/.cpp` | Project-wide includes/aliases, the `XY`/`Position`/`Gradient` value types, the `g_deterministic` reduction policy and `OrderedReduce`. |

The Limbo parser libraries this links against are **not here**. They used to be five checked-in
`.a` under `common/lib/`; Limbo is a git submodule now, built by
`vck5000/tools/bootstrap_third_party.sh` — see `host/README.md`.

## How it is built

There is no library target — each variant **compiles these sources itself**, because the two
variants need different flags (sw_only `-O2 -fopenmp`; pl_algo `-O0`, and a mixed
`_GLIBCXX_USE_CXX11_ABI` so the XRT TU can use the new ABI). Each `makeflags.mk` lists them in
`COMMON_SRCS`; `host/Makefile` has a second pattern rule pointing at `$(HOST_COMMON_DIR)/src`, and
the objects land in that variant's own `build/.../obj`.

The OpenMP pragmas in `Grid.cpp` / `DataBase.cpp` are inert when a variant builds without
`-fopenmp` (pl_algo): every loop runs serially and the result is unchanged. Nothing here calls the
OpenMP runtime API, only pragmas, so no `-lgomp` is implied.

## What the merge exposed

`pl_algo`'s frozen `Grid.cpp` had **no √2 density clamp**, while the PL gained it on 2026-07-05
(`node_footprint.hpp`, commit `0237e57`). So `make run-density` was comparing a clamped device
result against an unclamped software golden for a month — any PASS recorded in that window is
void. The shared `Grid` clamps, so re-run it. See `host/src/pl_algo/src/DensityVerify.cpp`.
