---
name: viz-gif
description: >
  Generate full-die or zoomed placement GIFs/PNGs from an AIEplace sw_only run's node-position
  dump (TODO #16), using tools/generate_viz.py. Trigger on requests like "make a zoom gif of this
  run", "show me what's happening at region X", "animate the placement", "render the viz dump",
  or any mention of placement_zoom / viz_render / node-position dump. Covers picking a good zoom
  window, phase-2 (two-generation) runs, and known performance/quality tradeoffs.
  Do NOT use this for: node-locked "follow one cell" views or multi-view sweeps (not implemented
  yet, TODO #16 step 4); the convergence-history charts (HPWL/overflow/step/lambda vs iteration --
  that is tools/plot_histories.py reading iterations.dat); or any visualization outside
  vck5000/sw_only (pl_algo has no equivalent). If the run has no dump, this skill cannot help
  after the fact -- see step 0.
---

# Placement viz GIFs (TODO #16)

The placer dumps node positions to `<run_dir>/coord_dump/` (binary frames + `manifest.json`); this tool
reads that dump and renders PNGs/GIFs entirely offline, in seconds, however many times you want.
Full spec and history: `1_REVIEW/handoffs/NEW_HANDOFF_viz_offline_tool_20260805.md`.

## 0. Check the run has a dump

```bash
ls <run_dir>/coord_dump/manifest.json
```

If missing, the run was made without `output.dump_positions = true` (the default is `false` —
nothing is written unless a run opts in). **You cannot render a GIF after the fact from a run that
didn't dump.** The only fix is re-running the placement with the toggle on; the `run-benchmark`
skill covers that, and setting `iterations_per_dump` there decides how many frames you get. Say
this plainly rather than producing something else and calling it the GIF.

## 1. Full-die GIF — the simple case

```bash
python3 tools/generate_viz.py <run_dir> --gif
```

Writes PNGs to `<run_dir>/viz_render/full/` and `0_placement.gif` alongside them. This is fast
(well under a second per frame even at MMS scale) and rarely needs any options.

## 2. Zoom GIF — pick the window BEFORE committing to a full render

**The window is square.** `--span` is one number, a fraction of the *shorter* die dimension, so a
request like "the lower half" or "the left third" cannot be rendered as a full-width strip. Pick
the square that best covers what was asked (lower half → `--center 0.5,0.25 --span 0.5`), and say
you did — don't silently render something else. `--center` is in die fractions with **y measured
from the bottom**, so 0.25 is the middle of the lower half.

A zoom window can easily land somewhere uninformative — solid inside one macro, or in empty
whitespace. **Preview single frames at a late iteration before rendering the whole animation:**

```bash
# render ONE frame near the end of the run at a candidate window
python3 tools/generate_viz.py <run_dir> --view zoom --center 0.3,0.35 --span 0.04 \
    --iters <late_iter>:<late_iter> --out /tmp/zoom_preview --quiet
```

**Read the preview PNG yourself** — a window is not good because the arithmetic says so, and the
failure modes (uniform red macro, blank whitespace) are obvious in the image and invisible in the
command. Try 2-3 candidate centres if the first is empty or saturated. Once you have a window that
shows real structure (a mix of cells, whitespace, and ideally the bin grid / row pitch), render the
full animation:

```bash
python3 tools/generate_viz.py <run_dir> --view zoom --center X,Y --span S --gif
```

- `--center X,Y` — fractions of die width/height (0.5,0.5 = die centre).
- `--span S` — window side as a fraction of the *shorter* die dimension (0.05 = 1/400th of the die
  area; same magnification on any benchmark). Smaller = more zoomed in.
- Only the zoom view draws row pitch, bin-grid lines, and per-cell outlines — that's the whole
  point of it; the full-die view is a grey wash at MMS scale.

## 3. Useful options (either view)

- `--iters A:B:S` — subset/stride of the exported frames, e.g. `600:1400:20`. Any field may be
  omitted (`:1400:` = up to 1400, every frame).
- `--out DIR` — default `<run_dir>/viz_render/<full|zoom>/`. The output folder leads with
  `0_placement.gif` and `1_best_solution_iter<N>.png` (named to sort above the `iter_<N>.png`
  trajectory), so those are what to point the user at.
- `--supersample N` — antialiasing quality. Default 2 (full-die) / 4 (zoom); raise it (e.g. 8) if
  a frame looks noticeably jaggier than the equivalent cairo PNG would, at N² cost in time/memory.
- `--canvas N` — longest side in pixels, default 2048.

## Gotchas

- **Two-phase (MMS macro) runs have multiple generations.** The node SET changes at the phase-1 →
  phase-2 boundary (macros freeze, fillers rebuild), so a run's dump may span 2-3 `nodes_gen<N>.bin`
  / `frames_gen<N>.bin` pairs. The tool concatenates them automatically — nothing to do — but if a
  GIF looks like it "jumps" partway through, that's the legalization + re-seed transition, not a
  bug. Frame filenames carry a tag (`_legalized`, `_reseeded`) at those boundaries.
- **There is no in-loop renderer any more.** The cairo path (`Visualizer.{h,cpp}`,
  `output.visualize`, `output.zoom`) was deleted in TODO #16 step 5 and those config keys no longer
  exist — if you find a note telling you to avoid it, the note is stale, not a live option. Why it
  went: at MMS scale a cairo **zoom** frame measured ~400× slower than this tool, one frame taking
  **24 minutes**, because it handed cairo the whole node set and let the clip sort it out instead
  of pre-filtering by window the way `View.visible()` does here.
- **A GIF is ~100 MB at 140 frames / 2048 px.** Worth mentioning to the user when you hand one
  over, along with the lever: `--canvas 1024` or an `--iters` stride cuts it a lot.
- **Size the frame count at run time, not render time.** Frames come from whatever
  `output.iterations_per_dump` the run used; the renderer can only subset what is there. A typical
  ISPD2005 run is 600-800 iterations, so cadence 20 yields a choppy ~35 frames and cadence 5 a
  smooth ~140. Re-rendering cannot add frames — only another run can.
- **`MAX_DETAIL_LINES = 256`** — row/bin grid lines are dropped (not drawn as a solid wash) once a
  window is wide enough to cross that many of them. A very wide zoom span will silently lose the
  grid; that's expected, not a rendering failure.
- **Fidelity to cairo is verified, not assumed** — `2_ARTIFACTS/compare_viz_frames.py <run_dir>
  --view {full,zoom}` is the regression gate if you're ever touching `tools/generate_viz.py`
  itself (not needed for routine GIF-making). Compares corr/lag/ink against the old renderer's
  PNGs when both exist for the same run.

## If you edit the renderer

Two coupling traps, both found the hard way:

- **The caption header holds 3 lines, not 4.** `DIE_START = 0.10` and header lines step 0.035 from
  0.04, so a 4th line renders *inside* the die box and is illegible over the cells. Benchmark +
  phase + zoom already fills it on a two-phase MMS zoom.
- **Frame filenames no longer control animation order** — `generate_viz.py` hands `gif_builder.py`
  an explicit `--frames` list. That coupling used to be implicit (natural sort of `iter_<N>`), and
  renaming one frame silently moved it to the front of the GIF. Keep the order explicit if you add
  new frame kinds.
