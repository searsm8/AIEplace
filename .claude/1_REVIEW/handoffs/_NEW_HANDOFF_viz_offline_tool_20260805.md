# HANDOFF — move visualization out of the placer into an offline tool (TODO #16)

**Status: PLAN ONLY. Nothing built.** Written 2026-08-05 at Mark's request, to be implemented in
a new session. Everything below is design + a staged build order with verification gates.

---

## 1. Why

Mark, 2026-08-05:

> In general for visualizations, we should rethink how they are made. With the zoom feature, I
> might want to generate multiple gifs of different zoom levels, or perhaps locked to a particular
> node. So, let's move all of the visualization creation to a separate tool, which can be run
> repeatedly without rerunning the full placement.

The concrete problem, measured today: `output.zoom_center_*` / `zoom_span` are read **once** in
`Placer::initializeZoomView()` during setup. Changing the window means re-running the placement.
On `mgc_pci_bridge32_a` (29.5k cells, tiny) that is 121 s; on an MMS design it is tens of minutes
to hours. Rendering is also *inside* the optimizer's loop, so every frame costs placement wall
time whether or not anyone looks at it.

Everything the renderer needs is a pure function of **node positions at iteration k** plus static
design data. So dump positions once, render offline, as many times and as many ways as you like.

Second-order benefits: `Visualizer.cpp` + the cairo link dependency leave the host build
(`BUILD_VIZ`, `CREATE_VISUALIZATION`, `-lcairo` all go away); a Python renderer is far easier to
iterate on than a C++ one that requires a rebuild; and the frames become a reusable artifact
(density overlays, trajectory plots, per-cell movement heatmaps — none of which need new host code).

---

## 2. What already exists — reuse map

Survey done 2026-08-05. **Read this before writing anything new.**

| Existing | What it does | Use it how |
|---|---|---|
| `tools/gif_builder.py` | directory of PNGs → animated GIF, natural sort, `-d` duration, `--quiet` | **Unchanged.** The new tool writes PNGs into a directory and calls this, exactly as `Output.cpp` does today. |
| `tools/make_viz_gifs.py` | writes per-design configs, runs the exe, relies on the exe to build GIFs | **Retire or repoint.** Its job splits: "run the placement with export on" (keep) and "make GIFs" (moves to the new tool). |
| `host/src/sw_only/src/Visualizer.cpp` | the cairo renderer — colour scheme, layer order, footprint geometry, overlay, zoom window, row/bin layers | **Port, don't reinvent.** This is the spec. Layer order, the `MIN_SIZE` floor, `DIE_START`/`DIE_SCALE` framing, the y-inversion in `mapY`, and `mapRectTop` all encode decisions that were debugged; §5 lists them. |
| `tools/compare_density.py` | loads CSV/`.npy` grids, renders with matplotlib + `Agg` | Precedent for "python reads a placer dump and renders". Reuse its argparse/`--out` shape. |
| `tools/def_to_bookshelf_pl.py` | parses sw_only's output DEF (`- name macro` / `PLACED ( x y )`) | Precedent for parsing placer output. **Do not** build the new format on DEF — see §4. |
| `tools/post_dp_density.py`, `tools/check_row_spans.py` | read bookshelf `.pl` / `.scl`, compute per-bin occupancy and row spans | Row/site geometry helpers; the row-line layer may be able to use `check_row_spans.py`'s real per-row model instead of the uniform-pitch approximation the C++ visualizer uses. |
| `Placer::dumpBinDensity` (`Density.cpp:357`) | ρ maps → CSV, gated on `params.dump_density` | The density-overlay path already exists. A future viz layer can consume it; **out of scope for v1**. |
| `iterations.dat` | per-iteration CSV: `Iter, HPWL, OVFW, step_len, density_weight, BkSteps` | **Already has every scalar the overlay prints.** Do not re-export these; the tool reads this file. |
| `run_summary.md`, `config_used.toml` | run metadata, already copied into the run dir | `config_used.toml` gives the tool the grid, target density, benchmark path. |

**The single most important reuse point:** `iterations.dat` already exists and already carries
HPWL / overflow / step / λ per iteration. The new dump is therefore **positions only**.

---

## 3. Config change

`output.visualize` (bool) → **`output.export_iterations`** (int, 0 = off).

```toml
[output]
export_iterations = 0    # dump node positions every N iterations; 0 disables. Replaces
                         # `visualize` + `iterations_per_export`. Shipped default is 0 (§8);
                         # 20 is the default of `make_viz_gifs.py --every` (§7 decision 1).
```

- `iterations_per_export` is **absorbed** — the new key is that interval, and the separate bool
  disappears (interval 0 means off).
- These `output.*` keys **move to the new tool's CLI and are deleted from `run_config.toml`**:
  `zoom`, `zoom_center_x`, `zoom_center_y`, `zoom_span`.
- These `output.*` keys are **deleted outright, not migrated** (§7 decision 4 dropped
  `--focus-net`): `focus_nets`, `rand_focus_nets`, `rand_focus_nodes`, `rand_macro_nets`,
  `rand_focus_IO`.
- These stay (not visualization): `quiet`, `interactive`, `iterations_per_status`, `results_dir`,
  `DSE_info`.

Call sites to change (all of them, verified 2026-08-05):
- `Setup.cpp` — `initializeVisualization()`, `initializeZoomView()`, `initializeFocus()` and the
  four `addRandom*` helpers all delete.
- `Output.cpp` — `exportIterationVisualization()` becomes `exportIterationPositions()`;
  `exportPhaseBoundaryVisualization()` becomes a *frame marker* (§4.3), not a render;
  `exportVisualizationArtifacts()` deletes, including both `gif_builder.py` `system()` calls.
- `AIEplace.h` — `Visualizer viz`, `viz_zoom`, `zoom_view_enabled`, `phaseLabelForPlot`,
  `drawPlacementViews` all delete.
- `makeflags.mk` — `BUILD_VIZ`, `-DCREATE_VISUALIZATION`, `-lcairo`, `Visualizer.cpp` all delete.

⚠ `2_ARTIFACTS/run_mms_viz.sh`, `tools/make_viz_gifs.py`, `2_ARTIFACTS/gen_*_configs.py` and any
saved `run_config` in `results/` set `visualize = true`. Grep for it and migrate them, or they
silently stop producing anything.

---

## 4. The export format

### 4.1 Shape

Three things per run, under `<run_dir>/viz/`:

```
viz/manifest.json      text  -- everything needed to interpret the binaries
viz/nodes_gen<N>.bin   bin   -- STATIC per-node data for topology generation N
viz/frames_gen<N>.bin  bin   -- concatenated position frames for generation N
```

Raw binary + a JSON manifest describing dtype/shape/offsets, rather than `.npy`: trivial to write
from C++, trivial to read with `numpy.fromfile`, and the manifest is human-readable when something
goes wrong. This mirrors what `compare_density.py` already consumes (`.npy` + `_meta.json`).

### 4.2 Contents

`manifest.json`:
```jsonc
{
  "benchmark": "mgc_pci_bridge32_a",
  "die":       {"x0": 0.0, "y0": 0.0, "w": 400000.0, "h": 400000.0},  // x0/y0 REQUIRED: MMS
                                    // frames are not origin-zero (newblue4 SubrowOrigin 7728)
  "row_height": 2000.0,          // DataBase::getRowHeight(), 0 if the input had none
  "bins_per_row": 512,           // for the bin-grid layer
  "target_density": 0.384,       // EFFECTIVE (post-addFillers raise), not the requested value
  "export_interval": 10,
  "generations": [
    {"id": 0, "phase": "mixed_size", "first_iter": 1,   "num_nodes": 29525, "frames": 62,
     "movable_end": 29521, "filler_start": 29521},
    {"id": 1, "phase": "stdcell_fixed_macro", "first_iter": 618, "num_nodes": 31004, ...}
  ],
  "frame_iters": [[1,10,20,...],[618,628,...]]   // per generation, the actual iteration numbers
}
```

`nodes_gen<N>.bin` — one record per node, in the **same index order** the frames use:
```
float32 w, float32 h      // size
uint8   kind              // 0 movable std cell, 1 movable macro, 2 fixed, 3 IO pad, 4 filler
```
(13 bytes/node; pad to 16 if alignment matters. Fixed/IOPad nodes also need a position — they
never move, so store their x,y here rather than in every frame.)

`frames_gen<N>.bin` — `frames × movable_count × 2 × uint16`, x then y, node-index order. Fixed
nodes are excluded (they are in the static file). No per-frame header; the manifest gives the
count and the iteration numbers. Quantization is **decided** (§4.4) — see §4.5 for the encoding,
which has a silent-failure mode that must be handled at write time.

### 4.5 uint16 quantization — the escapee trap

Naively quantizing over the die box clamps any node outside the die to the boundary. Cells *do*
leave the die during a run — that is a pathology the GIFs exist to show — so a clamp would erase
the exact signal we are looking for, silently.

Encoding, therefore, is over a box **inflated 2× about the die centre**:

```
qx0 = die_x0 − 0.5·die_w      qw = 2·die_w        // and likewise y
u   = round( (x − qx0) / qw · 65535 )             // clamped to [0, 65535]
x   = qx0 + u / 65535 · qw                        // decode
```

- Costs one bit of resolution vs. the die-box encoding: **12 die-units** on a 400000-wide die,
  still far below a pixel at any usable zoom.
- The manifest carries a top-level `quant: {x0, y0, w, h, max}` — the die does not change across
  generations, so one box covers the run. **Decode with the per-axis step** (`w/65535` for x,
  `h/65535` for y): dies are not square and using one step for both is a silent ~0.1 % skew.
- **`die_shift` must be in the manifest** — this is the field that carries the MMS frame offset,
  *not* `die.x0/y0`. Bookshelf inputs are translated so the die lower-left is the origin, so
  `die.x0/y0` reads 0 while the real offset (459 on `mms/adaptec1`) lives in `die_shift`. Add it
  back to label anything in benchmark/DEF coordinates. Both fields are emitted as of step 1.
- The writer counts nodes that hit the clamp and records `clamped` per frame in the manifest. A
  nonzero count means that frame lost information — the tool must print it, not hide it. This is
  also a free escapee detector: a nonzero count *is* the pathology.

Optional `nodes_gen<N>.names` (newline-separated, index order) — only needed for
`--lock-node <name>`. Gate it behind `output.export_node_names = false` because it is ~10 MB on
MMS designs and most runs never need it.

### 4.3 The phase-2 trap — get this right or the tool renders garbage

**The node set is not constant across a run.** `Placer::beginFixedMacroPhase()` (TODO #13) freezes
every movable macro (movable → FIXED) *and* discards and rebuilds the filler set
(`DataBase::rebuildFillers`), so both the count and the kind of nodes change mid-run. A single
static file cannot describe both halves.

Hence **generations**: the placer writes a new `nodes_gen<N>.bin` + starts a new
`frames_gen<N>.bin` at each phase transition. The manifest records which iteration each generation
starts at. The tool concatenates generations in order when animating.

This also replaces `exportPhaseBoundaryVisualization`: instead of forcing two extra *renders*, the
placer forces two extra *frames* either side of the transition, tagged in the manifest so the tool
can caption them ("macros legalized", "cells re-seeded") the way the overlay does today.

Phase 2 is the main thing the GIFs are *for* (memory `phase2-implemented-newblue5-converges`), so
this is not an edge case to defer.

### 4.4 Size — measured, and it is the main risk

Positions are `8 bytes × movable_count` per frame.

| design | movable+filler | bytes/frame | frames @ every-10, 1200 iters | total |
|---|---|---|---|---|
| mgc_pci_bridge32_a | 29.5k | 236 KB | 120 | **28 MB** |
| adaptec1 | ~400k | 3.2 MB | 120 | **384 MB** |
| bigblue4 / newblue7 | ~2M | 16 MB | 120 | **1.9 GB** |

Compare: today's PNG output for the 617-iteration demo was 1.9 MB total, because only 3 frames
were kept. The dump is strictly bigger than what it replaces, and on the largest designs it is
big enough to matter.

**Headroom, measured 2026-08-05 (not in the original draft):** `vck5000/results` is already **87 G**
and the disk is at **96 % — 40 G free**. The table above is being sized against 40 G, not a fresh
volume. A 16-design MMS viz sweep at the draft's own numbers is several GB.

**RESOLVED 2026-08-05 (Mark).** Cadence 20 + uint16; fillers stay ON.

1. ✅ **Cadence 20, not 10.** Halves everything; at 10 fps nobody perceives 10-iteration
   granularity. This is the default of `make_viz_gifs.py --every`, **not** of `run_config.toml` —
   the shipped config default is `export_iterations = 0` (§8: DSE must not start dumping).
2. ❌ **Fillers stay exported** (`export_fillers` is not added). Rejected because turning fillers
   back on would mean re-running the placement — the exact cost this TODO exists to remove — and
   the filler distribution was TODO #14's stated motivation.
3. ✅ **uint16 quantization**, adopted *instead of* dropping fillers. 4 bytes/node. Net effect:
   fillers-on at uint16 costs the same disk as fillers-off at float32, with nothing lost. Combined
   with (1): adaptec1 ~96 MB, bigblue4 ~480 MB. Encoding + its silent-failure mode: **§4.5**.
4. `results/` pruning is already TODO #1's problem; note the new artifact there.

---

## 5. The new tool

`vck5000/tools/place_viz.py`. **Python + Pillow + numpy** — no new dependencies: Pillow is already
in `requirements.txt` and `gif_builder.py` already uses it; numpy likewise.

Not matplotlib for the placement view: drawing 10⁵–10⁶ rectangles through matplotlib is minutes
per frame. `PIL.ImageDraw.rectangle` in a loop is seconds, and for zoom views a numpy bounding-box
filter cuts it to the handful of cells actually in frame first.

### CLI sketch

```
python3 tools/place_viz.py <run_dir> [options]

  --view full                       whole die (default)
  --view zoom --center 0.5,0.5 --span 0.03
  --view zoom --lock-node <name>    window follows one node through the run
  --view zoom --lock-index <i>      ... by node index, no names file needed
  --views views.toml                several views in one pass over the frames

  --iters 1:1200:10                 subset/stride of the exported frames
  --out <dir>                       default <run_dir>/viz_render/<view_name>/
  --gif                             call gif_builder.py on the output dir
  --layers rows,bins,outlines,fillers,focus
  --focus-net <name> ...            rat's-nest overlay; reads nets from the BENCHMARK
  --canvas 2048
```

`--views views.toml` is what makes "multiple gifs of different zoom levels" a single command and
one pass over a 384 MB frame file rather than N passes.

### Node-lock (TODO #14 follow-up, Mark's idea)

Per frame, recompute the window centre from the tracked node's current position instead of using a
fixed centre. Variants worth having:
- `--lock-node <name>` / `--lock-index <i>` — follow one cell.
- `--lock-largest-move` — follow whichever movable node moved most since the previous frame; finds
  the pathology without knowing its name.
- `--lock-macro <name>` — follow a movable macro through phase 1 and watch phase 2 legalize it.

Keep the *span* fixed while the centre moves, and print the centre in the overlay, or the animation
is impossible to interpret.

### What to port from `Visualizer.cpp` verbatim (these were debugged; do not re-derive)

1. **Layer order**: fillers → fixed → frozen macros → movable std cells → movable macros → IO pads.
   Later layers paint over earlier ones deliberately. (`Visualizer.cpp` has a final focus-highlight
   layer on top — **not ported**, §7 decision 4.)
2. **Colours**: filler grey `0.9`, fixed red `0.8/0/0` + black border, frozen (phase-2) macro
   purple `0.55/0/0.55`, movable std cell blue, movable macro red `1/0/0`, IO pad orange
   `1/0.64/0`. Frozen macros are a *distinct* colour on purpose — see the comment on
   `drawFrozenMacros`.
3. **Framing**: `DIE_START = 0.1`, `DIE_SCALE = 0.8` — the view occupies 80 % of the canvas with a
   10 % margin, and fixed terminals legitimately sit in that margin (do **not** clip the full-die
   view; the zoom view *does* clip).
4. **y is UP** (fixed 2026-08-05). Die y → canvas y is inverted, and rectangles anchor at their
   **top** edge because the raster grows downward. Verified against DEF/LEF ground truth to 6 px
   by **`2_ARTIFACTS/check_viz_orientation.py`** (working, run today):

   ```
   python3 2_ARTIFACTS/check_viz_orientation.py host/benchmarks/ispd2015/mgc_pci_bridge32_a <run>/best_solution.png
   ```

   It reads the FIXED macros from the benchmark's DEF/LEF — data that never passes through the
   renderer — predicts their canvas pixels under the y-up mapping, finds the red blocks by
   connected-component flood fill, and compares. A mirrored render is ~1228 px out, so it
   genuinely discriminates. **Promote it to `tools/` as a regression test** and point it at the
   new tool's output in step 2; a silent re-flip is exactly the kind of thing nobody notices for
   months. (It currently hardcodes `DIE_START`/`DIE_SCALE` and the ISPD2015 `floorplan.def` /
   `cells.lef` layout — generalize when promoting.)
5. **`MIN_SIZE = 0.001`** of canvas — sub-pixel cells are floored so they stay visible. Keep, and
   check whether it still fires at zoom (it should not).
6. **Zoom-only layers**: row pitch, bin boundaries, per-cell outlines, and `MAX_DETAIL_LINES = 256`
   above which a layer is dropped rather than drawn as a solid wash.
7. **Overlay**: benchmark / phase / zoom lines stacked at `y = .04, .075, .11`; iteration, HPWL,
   overflow, α, λ along the bottom. A line runs off the canvas past ~70 characters at font `.02`
   and cairo neither wraps nor warns — that bit me today.

---

## 6. Build order, with a gate at each step

Each step leaves the repo working. Do not merge steps.

1. ✅ **DONE 2026-08-05 — export path in the host, renderer still present.**
   `host/src/sw_only/src/placer/PositionDump.cpp` (new), hooked into `printIterationResults`,
   `printFinalResults` and both phase-2 boundaries. `Visualizer.cpp` untouched and still building.

   **Config differs from §3 as built** — Mark, 2026-08-05: *"make the default dump every 20
   iterations, but add a toggle `dump_positions = false` [so it] prevents any large data dumps
   from happening unless the user toggles it on."* So the master gate is a **bool**, not
   interval-0:
   ```toml
   dump_positions      = false   # master gate; nothing is written unless this is on
   iterations_per_dump = 20      # cadence
   ```
   `iterations_per_dump` is deliberately **separate from** `iterations_per_export` rather than
   shared with it: that key belongs to the cairo renderer and dies in step 5, and leaving it at 10
   avoids changing PNG cadence for existing runs. 20 being a multiple of 10 means every dumped
   frame still has a matching PNG for the step-2 comparison. `visualize` is untouched, so §3's
   rename of it to `export_iterations` does **not** happen — it is simply deleted in step 5.

   **Gate: PASSED on both a single-phase and a two-phase design.** Checker:
   `2_ARTIFACTS/check_position_dump.py <run_dir>` — it compares the last frame's node positions
   against the output DEF.

   | design | frames | gens | nodes compared | max marginal error | LSB |
   |---|---|---|---|---|---|
   | `ispd2015/mgc_pci_bridge32_a` (632 iters, no movable macros) | 33 | 1 | 29 521 | 6.61 | 12.21 |
   | `mms/adaptec1` (1398 iters, **phase 2**, 62 movable macros) | 73 | 3 | 211 447 | 0.213 | 0.326 |

   Both sit at **half an LSB with no systematic offset** — precisely what uniform quantization
   predicts, on dies three orders of magnitude apart in coordinate scale.

   The `mms/adaptec1` run also confirms the generation machinery end to end: gen 0 (`mixed_size`,
   31 frames) → gen 1 (`mixed_size`, the single `legalized` frame, `frame_nodes` 371033 → 370971
   as 62 macros freeze) → gen 2 (`stdcell_fixed_macro`, 40 frames from `reseeded` to
   `best_solution`). `clamped` was 0 in every generation.

   **Four traps this shook out. Three were in the checker, one was a real gap in the format.**

   - 🐞 **FORMAT GAP — `die_shift` was missing from the manifest, and it is not zero on MMS.**
     Bookshelf inputs are translated at parse time so the die's lower-left sits at the origin
     (`DataBase::bookshelf_end`); the DEF adds the shift back on the way out. On `mms/adaptec1`
     that is **459 units on both axes** — the dump was in one frame and the DEF in another, and
     nothing in `viz/` said so. Fixed: `manifest.die_shift`, fed by a new
     `DataBase::getDieShift()`. §4.5's earlier note about `die.x0/y0` was aiming at this and
     named the wrong field — `die.x0/y0` is 0 on the bookshelf path; **`die_shift` is the one that
     carries the offset.** `place_viz.py` must add it back for any output labelled in benchmark
     coordinates.
   - **The quantization LSB is per-axis.** The box is the die inflated 2×, and dies are not square
     (`mms/adaptec1` is 10692 × 10680). The C++ writer always had separate `scale_x`/`scale_y`;
     the checker's decode used the x step for both. That is a 0.11 % error — invisible at the
     origin, ~11.6 die units at the far edge, and it reads exactly like a real placement offset.
     A square die (`mgc_*` is 400000 × 400000) hides it completely. **Any decoder must use both.**
   - **Do not pair points by lexsort rank.** Without names there is no join key, and sorting by x
     with y as tiebreak means a 1-LSB x difference reshuffles the whole y-order within a column of
     standard cells. That reported a 44000-unit mean displacement on a placement whose bounding
     boxes agree to 3 units. Compare the two axes' **marginals** instead.
   - **The output DEF's coordinates are not integers.** `DataBase::writeComponents` streams a
     float at default precision, so a cell at 12345.6 prints with a decimal point and one at
     171714 does not. An integer-only regex silently matched 17672 of 29521 components.
2. ✅ **DONE 2026-08-05 — full-die view.** Built as **`tools/generate_viz.py`** (Mark named it;
   the `place_viz.py` of §5 does not exist). Python + numpy + Pillow, no new dependencies.

   ```
   python3 tools/generate_viz.py <run_dir> [--iters A:B:S] [--out DIR] [--canvas N] [--gif]
   ```
   Output defaults to `<run_dir>/viz_render/full/`, named `iter_<N>.png` to match the cairo
   renderer so the two are directly comparable and `gif_builder.py` orders them naturally.

   **Rasterization is vectorized, not a per-rectangle loop.** At full-die scale `MIN_SIZE` floors
   essentially every standard cell to ~2 px, so cells up to 8 px are painted by broadcasting over
   a fixed 8×8 offset grid and only the macros take a slicing loop. Measured: **~0.4 s per
   371k-node MMS frame** including PNG encode, against ~4 s/frame for the cairo renderer inside
   the placement loop — a 10× speedup on top of not having to re-run the placement at all.

   **Gate: PASSED on both designs.**
   `2_ARTIFACTS/compare_viz_frames.py <run_dir>`:

   | run | frames compared | worst corr | worst emd |
   |---|---|---|---|
   | `mgc_pci_bridge32_a` (focus nets ON in cairo) | 32 | 0.9945 | 0.00125 |
   | `mms/adaptec1` (**phase 2**, focus nets off) | 70 | 0.9973 | 0.00052 |

   Thresholds 0.99 / 0.002. The MMS run covers what mgc cannot: three generations, the purple
   frozen-macro layer, 160k fillers and the phase banner.

   Independently, `check_viz_orientation.py` run against the *port's* output gives a **5 px** worst
   corner error vs DEF/LEF ground truth (tolerance 20 px; a mirrored render is ~1228 px out) — so
   y-up is confirmed without going through either renderer.

   Why ink profiles and not pixel equality: cairo anti-aliases every edge and PIL does not, so a
   correct port still differs on the boundary pixel of all ~30k cells. Correlation catches a
   mirror or a wrong window; the normalized 1-Wasserstein distance (`emd`) is what catches a small
   systematic shift, which correlation alone sails through at >0.999.

   **The one trap here — cairo centres a stroke on its path.** Filling the die-boundary rectangle
   inward instead offsets that fully-saturated 8 px line by half its width, and because the
   comparison crop started exactly on it, that single line dragged frame correlation to **0.93**
   with a 2.6 % ink excess. Two fixes, both kept: `stroke_rects()` now straddles the path, and the
   comparison insets 1 % past the die edge so the boundary decoration cannot dominate a metric
   that is supposed to be about where the cells are.

   Known, deliberate differences from the cairo frame:
   - **No focus-net overlay** (§7 decision 4). A run with `rand_macro_nets > 0` shows yellow net
     boxes and X marks in the cairo frame that the port does not draw. Still passes at 0.9945;
     set `rand_macro_nets = 0` for the cleanest comparison.
   - **Phase banner** is shown when the manifest has >1 generation, where the C++ uses
     `enable_phase2 && num_movable_macros > 0`. These differ only for a run with movable macros
     that never transitions. Text sits outside the compared region either way.
   - Boundary frames are named `iter_N_legalized` / `_reseeded` here vs `iter_N_a_legalized` /
     `_b_reseeded` in cairo, so the comparison skips them rather than pairing them.
3. ✅ **DONE 2026-08-05 — zoom view + the detail layers.**

   ```
   python3 tools/generate_viz.py <run_dir> --view zoom --center 0.5,0.5 --span 0.05
   ```
   `--center`/`--span` mirror `output.zoom_center_*` / `output.zoom_span` exactly (square window,
   span as a fraction of the SHORTER die dimension). Adds clipping (zoom only — the full-die view
   must not clip, fixed terminals live in the margin), row pitch, bin grid, per-cell outlines, the
   zoom overlay line, and the `MAX_DETAIL_LINES = 256` drop rule.

   **Gate: PASSED on every frame available.**

   | run | view | frames | worst corr | lag | ink |
   |---|---|---|---|---|---|
   | `mgc_pci_bridge32_a` | zoom | 32 | 0.9956 | 0 | 0.992–1.004 |
   | `mgc_pci_bridge32_a` | full | 32 | 0.9995 | 0 | 0.9996 |
   | `mms/adaptec1` | zoom | 1 | flat† | 0 | 1.0000 |
   | `mms/adaptec1` | full | 1 | 0.99984 | 0 | 0.9900 |

   † the frame is a saturated block — see "flat frames" below.

   The `MAX_DETAIL_LINES` rule was verified at its boundary on `mms/adaptec1`: span 0.3 (267 rows,
   154 bins) drops the rows and keeps the bins; span 0.6 (534/307) drops both; span 0.05 (44/26)
   draws both.

   ### ⚠ The cairo zoom renderer is unusable at MMS scale — so there is almost no reference

   On `mms/adaptec1` (371k movable nodes), **one cairo zoom frame took 23 min 58 s** — measured
   from file mtimes, cleanly isolated: nothing happens between the full-die write at 17:28:10 and
   the zoom write at 17:52:08 except that one zoom render. `generate_viz.py` does the same window
   in **3.6 s/frame** (73 frames in 260 s). A 140-frame MMS zoom GIF would take **~56 hours** in
   cairo — which is why TODO #14's zoom was only ever demonstrated on the 29.5k-cell
   `mgc_pci_bridge32_a`.

   ⚠ **Do not quote a cairo full-die per-frame number from this run.** An earlier draft said 9 s,
   taken from the gap between two consecutive full-die writes — but that gap also contains nine
   placement iterations, so it is an upper bound on the render, not a measurement of it. The
   full-die render was never isolated. What *is* known: run `135926` did 1398 iterations plus 141
   full-die renders in 454 s total, so the full-die path is not the problem. **The zoom is.**

   Two things make the zoom slow, and only one of them is cairo's:

   1. **`Visualizer.cpp` does not pre-filter by window.** It adds all 371k rectangles to the path
      and leaves the clip to cairo, and a clip does not avoid tessellation cost. `generate_viz.py`
      drops everything outside the window first — at span 0.05 that is 99.75 % of the design gone
      before any rasterization.
   2. **`outlineIfZoomed()` strokes that whole path.** Stroking is far more expensive than filling,
      which is why the full-die view (fill only, no per-cell outline) is unremarkable.

   So the fair statement is that the **C++ zoom implementation is unoptimized**, not that cairo is
   unusable: adding the same window pre-filter to `Visualizer.cpp` would close most of the gap. The
   conclusion for this TODO is unaffected — the zoom as written is impractical at MMS scale, and
   moving rendering offline removes the cost from the placement loop entirely rather than tuning it.

   **It is slow, not broken.** A one-iteration MMS run with `zoom = true` under `/usr/bin/time -v`
   returns **exit status 0, 0 signals, 482 MB peak RSS** — it renders the frame correctly and the
   memory cost is unremarkable. This is CPU time in path tessellation, nothing else. (An earlier
   draft of this section said the MMS zoom runs "failed to get past the first few frames"; that was
   wrong — the runs I abandoned, I abandoned because I was not willing to wait, and the one I let
   finish finished cleanly.)

   Consequence for verification: **the MMS zoom has exactly one cairo frame to compare against**
   (salvaged, with a hand-reconstructed manifest, from a run killed mid-render). The 32-frame mgc
   zoom comparison is what proves the window arithmetic and the detail layers; the MMS zoom is
   covered by that plus the 70-frame MMS full-die gate from step 2. Do not plan on more MMS zoom
   reference frames — generating them costs a day each.

   This is also the strongest argument yet for the whole TODO: the zoom feature only becomes usable
   on real designs once it leaves the placer.

   ### Antialiasing — new, and it is not cosmetic

   Geometry is rasterized at `ss`× and box-downsampled (`--supersample`, default **2 for full-die,
   4 for zoom**). Box averaging *is* the coverage fraction cairo antialiases with. This also lifted
   the step-2 full-die numbers from 0.9945 to 0.9995.

   The zoom needs the higher factor because it is the only view with **per-cell outlines**: in a
   dense region two neighbouring cell edges sit well under a pixel apart, and at 2× they land on
   the same raster pixel and merge into one. Measured on the dense mid-run mgc frames: 2× loses
   **4–6 % of the frame's ink**, 4× lands within 1 %.

   ### Three cairo stroke conventions, all found by the gate, all fixed

   1. **Strokes are CENTRED on the path.** Filling the die boundary inward offset that saturated
      8 px line by half its width → frame correlation 0.93. (Found in step 2.)
   2. **Grid-line width must stay fractional.** Rounding the width to whole pixels and adding it to
      a rounded centre quantizes upward every time — a 1.638 px row line became 1.75 px, 7 % too
      much ink on every row. The residual autocorrelated at **lag 164 px, exactly the row pitch**,
      which is what identified it. Rounding the two EDGES independently lets the width average out
      across sub-pixel phases, as the cell rectangles already did.
   3. **Same for rectangle outlines**, which had the identical integer-width bug.

   ### Gate metric changed: `emd` replaced by `lag` + `ink`

   `emd` was the original translation proxy and it is the wrong tool for the zoom view. It
   normalizes each profile to sum 1, so the uniformly spaced grid-line ink interacts with the
   normalization: a ~1 % ink excess there redistributes the whole distribution and reads as a
   shift. On mgc iter_220 that produced emd 0.0033 with corr 0.999 and **zero** actual
   displacement — and it converged 0.0033 → 0.0019 purely by taking supersampling 4× → 8×, proving
   the residual was antialiasing fringe rather than geometry.

   The gate now checks three orthogonal things: **corr** (shape), **lag** (best cross-correlation
   offset in px — the direct translation test emd was proxying, immune to uniform ink offsets), and
   **ink** (total ratio, catching a dropped layer or wrong colour). `emd` is still reported as a
   diagnostic.

   ### Flat frames

   A zoom window at the die centre on iteration 1 is *solid cells* — every movable node starts in a
   σ = 0.001·die Gaussian there. Both renderers produce the same saturated block (identical mean
   ink to four decimals) and correlation still reads 0.24, because correlating two constants
   measures only their noise. The gate now abstains from the shape check when both profiles have
   relative std < 0.005, and says `flat`; ink and lag still gate the frame.

   ### Known cosmetic issue (shared with cairo, not a port defect)

   With a three-line header (benchmark + phase + zoom) the third line sits at y = 0.11 and overlaps
   the die box top edge at y = 0.10. Same coordinates in both renderers, so the port is faithful —
   but worth fixing in step 4, where the overlay is no longer constrained to match cairo.
4. **Node-lock, multi-view, `--iters`.** No C++ reference exists for these — gate on the
   orientation regression test from §5.4 plus eyeballing.
5. **Delete the renderer.** Remove `Visualizer.{h,cpp}`, `BUILD_VIZ`, `CREATE_VISUALIZATION`,
   `-lcairo`, the `output.*` viz keys, and the `gif_builder.py` `system()` calls. Migrate
   `make_viz_gifs.py` and `2_ARTIFACTS/run_mms_viz.sh`. Gate: sw_only builds with no cairo on the
   link line and a full run still produces a GIF, now via the tool.

Steps 1–2 are the real work. 5 is the one that must not be done early — keeping the C++ renderer
until the Python one is proven against it is the whole verification strategy.

---

## 7. Decisions — RESOLVED 2026-08-05 (Mark)

All four settled before step 1. Do not re-litigate these mid-build.

1. ✅ **Cadence 20.** `make_viz_gifs.py --every` defaults to 20; `run_config.toml` ships
   `export_iterations = 0`. Two different defaults, both required — see §4.4.
2. ✅ **Fillers exported by default**, paid for with uint16 quantization rather than by dropping
   them (§4.4 item 3, encoding in §4.5). No `export_fillers` key is added. Rationale: making
   fillers opt-in reintroduces the re-run cost this TODO exists to remove.
3. ✅ **`viz/` lives inside the run dir** — `<run_dir>/viz/`. It travels with `config_used.toml`
   and `iterations.dat`, which the tool reads anyway, so a run dir stays self-describing. Bulk
   pruning is one glob (`rm -rf results/*/*/viz`); hand that glob to TODO #1.
4. ✅ **`--focus-net` is DROPPED.** With it goes the tool's only dependency on the netlist —
   `place_viz.py` never parses a benchmark. The `focus_nets`, `rand_focus_nets`, `rand_macro_nets`,
   `rand_focus_IO`, `rand_focus_nodes` keys and `initializeFocus()` / the four `addRandom*` helpers
   are deleted outright in step 5 rather than migrated. (`rand_focus_IO` and `rand_focus_nodes` are
   already marked "not yet implemented" in `run_config.toml` — they are dead keys today.)
   If the rat's-nest is ever wanted back, re-add it as a tool-side feature that reads the
   benchmark; nothing in this design forecloses that.

### Consequences of (4) for the build order

Step 5's deletion list grows and step 3's port list shrinks: the "focus highlights" layer named in
§5's layer order (item 1) **is not ported**. Layer order becomes fillers → fixed → frozen macros →
movable std cells → movable macros → IO pads.

---

## 8. Things not to break

- `iterations.dat` is consumed by existing analysis scripts. The tool should *read* it, not
  replace it, and the host must keep writing it.
- `dse.py` runs with `visualize` off; the rename must not make DSE runs start dumping frames.
  Default `export_iterations = 0`.
- `results.csv` schema is the DSE path and is deliberately untouched (TODO #4 step 1) — do not add
  viz columns to it.
- Every GIF produced before 2026-08-05 is **vertically mirrored** relative to anything produced
  after. Do not use an old GIF as the reference image in step 2.
