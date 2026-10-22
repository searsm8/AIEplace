# #39 — Position dump v2: the channels that make a placement GIF a diagnostic

*2026-08-27. Branch `pl_algo`. Extends [[_NEW_HANDOFF_viz_offline_tool_20260805.md]] (#16) and the
node-lock dump change from #14.*

## 0. The premise was half wrong — check this before reading the rest

The request was framed on `default_config.toml`'s own description of the dump as **"positions
only"**. That comment is stale. Reading `PositionDump.cpp` first (which the request asked for)
found that **most of priority 1 and both cheap wins already shipped**, some of them three weeks
ago:

| asked for | actual state before this change |
|---|---|
| per-node id, width, height, class, area | **already in `nodes_gen<N>.bin`** — `f4 x,y,w,h` + a `u1 kind` byte with **six** classes, not four (movable stdcell / movable macro / fixed / iopad / filler / **frozen macro**). Area is `w*h`. |
| stable IDs across frames | **already** — `names_gen<N>.txt`, sparse, per generation. Landed with #14's node-lock on 2026-08-17. |
| dump self-contained, renderer stops re-parsing the benchmark | **already true** — `generate_viz.py` reads `coord_dump/` and `iterations.dat`, nothing else. |
| frame → iteration manifest, HUD burn-in | **already** — `manifest.json` carries `frame_iters` + `frame_tags` per generation, and `draw_overlay` already burns in HPWL / overflow / alpha / lambda / phase / boundary tag. |
| count of rejected backtracking trials | **already** — `BkSteps`, column 6 of `iterations.dat`, since the file was created. |
| net degree | missing |
| per-frame bin density, optional field | missing |
| per-node WL/density gradient split + preconditioner | missing |
| Nesterov probe `v_k` | missing |

So the deliverable narrowed to **four genuinely new channels plus one static field**, and the
priority-2 note "check whether the one-shot `dump_density` flag can be reused" resolved to **no**:
`dumpBestPlacementDensity()` calls `computeOverflow()`, which *re-deposits* every node into a
private buffer. That is a metric, not the solver's map, and it runs once at the end. The
per-iteration series reads `Bin::total_overlap` / `Bin::eField` out of the grid directly.

**The stale comment is fixed** as part of this change.

## 1. What landed — dump format v2

`format_version` 2. One new static field, four new optional per-frame channels, each its own file
per generation, each written in lockstep with `frames_gen<N>.bin` so one frame index seeks
identically in all of them.

| file | per | record | config key | default |
|---|---|---|---|---|
| `nodes_gen<N>.bin` | node | *(v1)* + `u4 net_degree` | — | always |
| `probe_gen<N>.bin` | frame node | `u2 x`, `u2 y` — Nesterov **v_k** | `dump_probe_positions` | on |
| `density_gen<N>.bin` | viz bin | `f4 rho` | `dump_bin_density` | on |
| `field_gen<N>.bin` | viz bin | `f4 Ex`, `f4 Ey` | `dump_field` | **off** |
| `forces_gen<N>.bin` | frame node | `f4 wl_x, wl_y, den_x, den_y, precond` | `dump_forces` | on |

Three decisions worth carrying forward:

**Area is not a field.** It is exactly `w*h`; writing it separately only creates two numbers that
can disagree.

**The force split is captured inside `combineGradients()` and nowhere else.** `g -= electro` is
destructive, so that one expression is the only point in the run where the wirelength and density
terms exist separately. Both are stored as **gradient contributions** (so `den` is `-electro`),
which makes `wl + den == next.probe_grad` an exact invariant a reader can check rather than a sign
convention it has to be told.

**Density/field are box-averaged to at most 256×256 by an integer factor.** bigblue3 at grid 2048
would otherwise be 16 MB/frame. The block average divides by the block's *actual* member count,
not the nominal factor, so a grid that is not a multiple of 256 does not get a dimmed top and
right edge.

### `frame_valid` — the part that is easy to get wrong

Three frames per run sit at a point where the density map or the gradients describe a *different
placement* from the one being drawn:

| tagged frame | why | bits |
|---|---|---|
| `legalized` | `freezeMovableMacros()` + the LP moved macros; no re-solve, no re-combine | `0` |
| `reseeded` | `computeElectricFields()` ran at the new positions; `combineGradients()` did not | `1` (density only) |
| `best_solution` | positions restored to a placement the last solve never saw | `0` |

Those records are **zero-filled and flagged**, not omitted — omitting them would break the
lockstep that makes frame index a seek key. `manifest.json` carries a per-frame `frame_valid`
bitmask (bit 0 density/field, bit 1 forces) parallel to `frame_iters`. The renderer skips the
underlay/colouring on those frames and captions them `(stale)`.

Freshness is tracked by two flags set by the producers (`computeElectricFields`,
`combineGradients`) and cleared by anything that moves nodes without re-running them
(`beginPositionDumpGeneration`, `restoreBestPlacement`).

## 2. Verification

### The numerics are untouched — with every channel ON

`make test-regress` is bit-identical before and after. That only proves neutrality with the dump
*off*, since the frozen configs set `dump_positions = false`. So both regress designs were re-run
with the dump **on** and every channel enabled, and compared against the committed baselines by
hand:

| design | channels | `iterations.dat` | output `.def` sha256 |
|---|---|---|---|
| `mgc_fft_a`, cadence 5, 126 frames, 1 gen | u_k + v_k + density + **field** + forces | **bit-identical** | `c25636c2…` = baseline |
| `mms/adaptec1`, cadence 10, 131 frames, **3 gens** | u_k + v_k + density + forces | **bit-identical** | `91cbbdee…` = baseline |

The capture is pure stores into a preallocated buffer — no arithmetic added or reordered — which
is why this holds. `mms/adaptec1` also exercises the phase-2 path (both generation breaks) and
box-averaging (grid 512 → 256, factor 2).

### The dump asserts its own correctness — `tools/check_viz_dump.py` (new, tracked)

Every channel is indexed by frame number, so a stream of the wrong length does not fail loudly —
it returns a neighbouring frame, or half of two, and the GIF still renders. Two layers guard that:

- **The placer** checks every stream's length against what the manifest implies, at finalize, and
  names the offending file.
- **`tools/check_viz_dump.py`** re-checks that from the reader's side, then asserts four
  structural invariants that catch a *misaligned record*, which is the realistic failure mode:
  1. fillers carry `net_degree == 0`
  2. fillers carry a wirelength gradient of exactly `0.0`
  3. `precond_weight >= 1.0` everywhere (`updatePrecondWeights` clamps it)
  4. total deposited density is **constant within a generation** to 1e-3 relative

  It also asserts the `frame_valid` table above exactly — every untagged frame fully valid, each
  tagged frame with the bits its transition implies — so a future change to the phase-2 boundary
  has to come here and say so.

**Negative-tested, not assumed.** Truncating `forces_gen0.bin` by one float → caught by the length
check. Shifting the force record stride by one float → caught by invariant 3 (`precond` min
`-3.54e+01`). Both exit 1.

### The renderer

`tools/generate_viz.py` gains `--underlay density`, `--color-by force|precond`, and
`--positions probe`, and now builds the static-record dtype **from the manifest** instead of
hard-coding it — which is what lets one reader open both v1 and v2 dumps. It refuses up front,
naming the config key, if a requested channel is absent, rather than silently rendering the plain
view.

Evidence PNGs in `.claude/2_ARTIFACTS/dump_v2_20260827/`:

- `full_force_early_…_iter100` — λ = 4.0e-12, essentially every cell **blue** (wirelength-dominated)
- `full_force_late_…_iter600` — λ = 2.0e-04, the same cells **red** (density-dominated)
- `zoom_force_density_mms_adaptec1_iter1200` — the payoff frame: red cells sit in the orange
  overfull bins, blue cells in the pale empty ones, purple in between. Mechanism, visible.
- `full_reseeded_stale_…` — captioned `Channels: rho, force(stale)`, underlay drawn, colouring
  correctly withheld.

**One defect found and fixed by looking at the output.** The channel label was first appended to
the caption's top line, which on a two-phase MMS frame is already ~99 characters — it ran off the
canvas and clipped `color=force=stale` to `color=force`, i.e. captioned a stale frame as measured,
the exact failure the label exists to prevent. It now takes its own header line, or rides the zoom
line when there is one, so the header never grows to the third line that renders inside the die box.

## 3. Disk — read this before launching a suite

The force channel is 20 B/node/frame and dominates everything else combined. Projected from the
14-design half-suite's own manifests, at **cadence 5**:

| | all channels | forces off |
|---|---|---|
| 14-design suite | **32.2 GB** | **9.6 GB** |
| worst single design (`mgc_superblue12`, 1.93 M nodes) | 8.5 GB | 2.4 GB |
| `adaptec1` | 1.3 GB | 0.4 GB |

**Free space is 40 GB (96% used).** So the full suite at cadence 5 with forces on does not fit
with any margin. Recommendation:

1. Re-run the 14 designs at cadence 5 with `dump_forces = false` — **~9.6 GB**, gives every design
   the finer cadence, the density underlay, and v_k.
2. Add forces on **3 designs of interest only** (e.g. `adaptec1`, `mgc_matrix_mult_a`,
   `mgc_fft_a`) — **~2.6 GB**.

Each run now prints its own MB/frame at startup, so this is checkable per design rather than
estimated.

**On the run that was in flight:** `results/viz_suite_half_20260827/` **completed** at 18:51
(14/14, `ALL_DONE`, all `rc=0`) on the pre-change binary, before this build landed — so it is
clean, not half-and-half, and is usable now for plain and node-locked GIFs at cadence 20. It has
none of the new channels. Re-running is a choice about cadence and mechanism, not a repair.

## 4. Not done, deliberately

- **Quiver rendering** of the two force vectors. The data is there (full float32 vectors, not
  magnitudes); drawing arrows for a visible subset at zoom is a renderer feature, not a dump one.
- **Mini convergence plot with a frame marker.** Pure indexing on data that already exists
  (`iterations.dat` + `frame_iters`); no dump change needed, so it did not gate on this work.
- **float32-binary / delta-encoded dump** — deprioritized by the request, and the measurements
  above say the lever is `dump_forces` and cadence, not encoding.
- **Velocity, displacement, per-net HPWL, overflow** — derivable from stable IDs + the netlist,
  and excluded by the request for that reason.

## 5. Freeze compliance

sw_only functionality is frozen (2026-08-17). This is **instrumentation, not behaviour** — the
category tasks.md explicitly leaves open ("Cleanup, tooling, docs and tests are NOT frozen") — and
the bit-identical contract is demonstrated above on both a single-phase and a mixed-size design
with every channel enabled, not merely with the dump off.

## 6. Files touched

- `host/src/sw_only/include/AIEplace.h` — dump structs, `frame_valid`, capture buffer, freshness flags
- `host/src/sw_only/src/placer/PositionDump.cpp` — v2 writer, box-average, manifest, stream-length self-check
- `host/src/sw_only/src/placer/Step.cpp` — the force capture inside `combineGradients()`
- `host/src/sw_only/src/placer/Density.cpp` — density freshness flag
- `host/src/sw_only/src/placer/AIEplace.cpp` — freshness cleared in `restoreBestPlacement()`
- `host/src/sw_only/default_config.toml` — four new keys; the stale "positions only" comment corrected
- `tools/check_viz_dump.py` — **new**, tracked
- `tools/generate_viz.py` — manifest-driven dtype, three new flags, caption fix
- `.claude/skills/viz-gif/SKILL.md` — §4 mechanism views, new gotchas; **and a stale description
  fixed** — it said node-lock and multi-view were "not implemented yet", which has been false
  since #14 closed on 2026-08-17, and it was routing work away from a working feature
- `.claude/2_ARTIFACTS/check_position_dump.py` — hard-coded 17-byte static dtype would misparse
  every v2 dump; now reads the manifest
