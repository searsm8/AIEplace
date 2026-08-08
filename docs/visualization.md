# Visualization (offline, post-processing)

sw_only does not render anything during a placement run. It only dumps node positions; two
separate command-line tools turn that dump into images afterward, as many times and in as many
ways as you like, without re-running the placement. This replaced an earlier design where the
placer itself rendered PNGs with cairo in-loop (`Visualizer.{h,cpp}`, config `output.visualize` /
`output.zoom*`) — removed entirely 2026-08-05 (TODO #16 step 5). Background and the full design
history: `.claude/1_REVIEW/handoffs/_NEW_HANDOFF_viz_offline_tool_20260805.md` (tracked
in git) and `.claude/tasks.md` TODO #16 / #18 (tracked, the durable record).

## 1. Turning the dump on

In `default_config.toml`, under `[output]`:

```toml
dump_positions      = false   # master gate. OFF by default -- these files are large
                               # (~96 MB for adaptec1, ~480 MB for bigblue4) and no run
                               # should pay for them unasked.
iterations_per_dump = 20      # cadence: dump a frame every N iterations
```

This writes `<run_dir>/coord_dump/{manifest.json, nodes_gen<N>.bin, frames_gen<N>.bin}`. A run's node SET
can change mid-run (phase 2 freezes movable macros and rebuilds fillers — see TODO #13), so a dump
may span more than one `nodes_gen<N>.bin`/`frames_gen<N>.bin` pair, one per "generation." Both
rendering tools below handle this automatically.

Every per-iteration *scalar* (HPWL, overflow, step length, density weight) is already written to
`<run_dir>/iterations.dat` unconditionally, regardless of `dump_positions` — neither tool below
re-exports those; `tools/plot_histories.py` reads that file directly.

## 2. Placement GIFs/PNGs — `tools/generate_viz.py`

Renders cell layouts (macros, standard cells, fillers, IO pads) from the position dump.

```bash
# whole die
python3 tools/generate_viz.py <run_dir> --gif

# a zoomed window: CENTER and SPAN are fractions of the die (span = side length, as a
# fraction of the SHORTER die dimension, so one span means the same magnification on any
# benchmark)
python3 tools/generate_viz.py <run_dir> --view zoom --center 0.5,0.5 --span 0.05 --gif
```

Output defaults to `<run_dir>/viz_render/<full|zoom>/`, one PNG per dumped frame plus
`0_placement.gif` (via `tools/gif_builder.py`, which now takes the frame order explicitly).

**Before rendering a full zoom animation, preview one late-run frame at the candidate window** —
a badly-centered window is easy to land on (solid inside one macro, or empty whitespace):

```bash
python3 tools/generate_viz.py <run_dir> --view zoom --center 0.3,0.35 --span 0.04 \
    --iters <late_iter>:<late_iter> --out /tmp/preview --quiet
```

Look at the resulting PNG before committing to the full `--gif` render.

Other flags: `--iters A:B:S` (subset/stride of exported frames, any field optional),
`--canvas N` (longest side in px, default 2048), `--supersample N` (antialiasing quality,
default 2 full-die / 4 zoom — raise for a jaggier-than-expected frame, at N² cost). Only the
zoom view draws row pitch, bin-grid lines and per-cell outlines; a layer denser than
`MAX_DETAIL_LINES` (256 lines) is silently dropped rather than drawn as a solid wash — expected
at a very wide zoom span, not a bug.

## 3. Convergence-history charts — `tools/plot_histories.py`

Renders `iterations.dat` (HPWL, overflow, step length, density weight) to PNG — the direct
successor to the old in-loop `CairoPlotter` (TODO #18), now matplotlib instead of cairo, and run
whenever you want rather than once at the end of every run.

```bash
python3 tools/plot_histories.py <run_dir>
```

Writes `<run_dir>/graphs/{hpwl_history,ovfw_history,step_length_history,density_weight_history,overview}.png`
— four individual per-metric charts plus `overview.png`, all four stacked on one shared x-axis
(iteration count since run start; density weight on a log scale). `--out DIR` overrides the
default output directory.

## 4. Batch rendering — `tools/make_viz_gifs.py`

Runs a set of MMS designs with `dump_positions` on, then calls `generate_viz.py --gif` (and the
zoom variant, if `--zoom` is given) on each resulting run directory. See its own `--help` /
docstring; it is a driver around the two tools above, not a separate rendering path.

## Gotchas

- **A run must have opted into `dump_positions = true` at run time.** There is no way to render a
  GIF after the fact from a run that didn't dump — re-run the placement with the toggle on.
- **Two-phase (mixed-size macro) runs have multiple generations.** If a GIF looks like it "jumps"
  partway through, that's the phase-1 → phase-2 legalization + standard-cell re-seed transition
  (frames tagged `_legalized` / `_reseeded` in the manifest), not a rendering bug.
- Every GIF produced by the old cairo renderer before 2026-08-05 is vertically mirrored relative
  to anything produced by `generate_viz.py` (a die-y-is-up bug fixed the same day cairo was
  ported away from). Do not compare an old GIF against a new one and conclude the placement moved.
