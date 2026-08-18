#!/usr/bin/env python3
"""
Render placement frames from a run's position dump (TODO #16).

The placer no longer draws: it writes node positions to <run_dir>/coord_dump/ (see
PositionDump.cpp) and this turns them into PNGs. Re-runnable in seconds against a placement that
took an hour, which is the whole point -- a new window or a new cadence no longer costs a re-run.

    python3 tools/generate_viz.py <run_dir>                 # every exported frame, full die
    python3 tools/generate_viz.py <run_dir> --iters 1:600:2 # a subset/stride of them
    python3 tools/generate_viz.py <run_dir> --gif           # ...and animate the result
    python3 tools/generate_viz.py <run_dir> --view zoom --center 0.4,0.6 --span 0.02
    python3 tools/generate_viz.py <run_dir> --add-view full --add-view zoom:0.4,0.6,0.02  # many

At MMS scale a full-die frame is 200k-1M cells in ~2000 px, so everything below the macro scale is
a grey wash; the zoom is where the standard-cell rows, the filler distribution and the macro edges
become visible. Only the zoom draws row pitch, density-bin boundaries and per-cell outlines, and
only the zoom clips to the window -- in the full-die view fixed terminals legitimately sit in the
margin outside the core-row die and clipping would silently hide them.

Multi-view landed (TODO #14): --add-view renders any number of windows in ONE pass, decoding each
frame once. Node-lock -- a window that re-centres on a tracked cell every frame -- is still to come;
the coordinate map already takes an arbitrary window, so it is additive.

Fidelity: the geometry constants, layer order and colours below are ported from Visualizer.cpp and
must stay in step with it while both renderers exist. `.claude/2_ARTIFACTS/compare_viz_frames.py` is the
regression test for that -- it compares this output against the cairo frames of the same run.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

# --- Ported verbatim from Visualizer.h. Changing one of these desyncs this from the C++ -------
MAX_CANVAS_PX = 2048          # longest canvas dimension in pixels
DIE_SCALE = 0.80              # the view occupies 80% of the canvas...
DIE_START = (1 - DIE_SCALE) / 2   # ...leaving a 10% margin on all sides
MIN_SIZE = 0.001              # sub-pixel cells are floored to this so they stay visible

# Kind byte written by PositionDump.cpp.
K_STDCELL, K_MOV_MACRO, K_FIXED, K_IOPAD, K_FILLER, K_FROZEN_MACRO = range(6)

# A detail layer denser than this is a solid wash, so it is dropped rather than drawn.
MAX_DETAIL_LINES = 256

# Digit prefixes so the two summary outputs sort above the per-iteration frames in a file listing
# (digits precede letters), GIF first.
GIF_NAME = "0_placement.gif"
BEST_PNG_PREFIX = "1_best_solution_"

# Layer order + colours from Visualizer.cpp::drawPlacement. Later layers paint over earlier ones
# deliberately; the outlines are part of that ordering, so they are drawn in sequence rather than
# all at the end. Colours are cairo's 0..1 floats scaled to 8-bit.
#
# Third field is the outline:
#   "border"  black, width 0.001, ALWAYS -- the two fixed layers (cairo fill_preserve + stroke)
#   "zoom"    dark grey 0.15, width 0.0006, ZOOM ONLY -- outlineIfZoomed(); at zoom a cell is big
#             enough that an outline separates neighbours instead of blackening them
#   None      no outline at any zoom
LAYERS = [
    (K_FILLER,        (230, 230, 230), "zoom"),    # grey: whitespace held open, not a cell
    (K_FIXED,         (204, 0, 0),     "border"),  # red
    (K_FROZEN_MACRO,  (140, 0, 140),   "border"),  # purple: phase-2 output, NOT input floorplan
    (K_STDCELL,       (0, 0, 255),     "zoom"),    # blue
    (K_MOV_MACRO,     (255, 0, 0),     None),      # red
    (K_IOPAD,         (255, 163, 0),   None),      # orange
]
OUTLINE = {"border": ((0, 0, 0), 0.001), "zoom": ((38, 38, 38), 0.0006)}

STATIC_DTYPE = np.dtype([("x", "<f4"), ("y", "<f4"), ("w", "<f4"), ("h", "<f4"), ("kind", "u1")])

# Cells small enough to paint by broadcasting instead of by slicing. At full-die scale MIN_SIZE
# floors essentially every standard cell to ~2 px, so this covers all but the macros -- which is
# what makes a 371k-node frame render in a second rather than a minute.
SMALL_PX = 8

# Antialiasing supersample factor, per view. The zoom needs more than the full die because it is
# the only view that draws PER-CELL OUTLINES: in a dense region two neighbouring cell edges sit
# well under a pixel apart, and at 2x they land on the same raster pixel and merge into one, where
# cairo's continuous rasterizer keeps them distinct. Measured on the dense mid-run frames of
# mgc_pci_bridge32_a: 2x loses 4-6% of the frame's ink, 4x lands within 1%.
SUPERSAMPLE_DEFAULT = {"full": 2, "zoom": 4}


def load_run(run_dir):
    """manifest + per-generation static records, and a flat frame index across generations."""
    dump = run_dir / "coord_dump"
    if not dump.is_dir():
        sys.exit(f"{dump} not found -- was the run made with output.dump_positions = true?")
    manifest = json.loads((dump / "manifest.json").read_text())

    frames = []   # (generation, index within its frame stream, iteration, tag)
    for gen in manifest["generations"]:
        gen["static"] = np.fromfile(dump / f"nodes_gen{gen['id']}.bin", dtype=STATIC_DTYPE)
        gen["path"] = dump / f"frames_gen{gen['id']}.bin"
        gen["names_path"] = dump / gen.get("names", f"names_gen{gen['id']}.txt")
        for i, (it, tag) in enumerate(zip(gen["frame_iters"], gen["frame_tags"])):
            frames.append((gen, i, it, tag))
    return manifest, frames


def name_to_index(gen):
    """{node name -> static-record index} for one generation, from its sparse names file.

    Built per generation, and that is the whole point of locking by NAME rather than by index:
    freezeMovableMacros() and rebuildFillers() reshuffle the node set at the phase-2 boundary, so
    index i is a different node either side of it. The name is what survives.
    """
    if "name_index" not in gen:
        path = gen["names_path"]
        if not path.is_file():
            sys.exit(f"{path} not found -- this dump predates node names; re-run the placement "
                     f"(the placer writes names_gen<N>.txt alongside nodes_gen<N>.bin).")
        table = {}
        for line in path.read_text().splitlines():
            index, _, name = line.partition(" ")
            if name:
                table[name] = int(index)
        gen["name_index"] = table
    return gen["name_index"]


def resolve_lock(manifest, frames, target):
    """Lock target -> a node NAME, resolving the two computed forms against generation 0.

    Returned as a name rather than an index so the caller re-resolves it per generation.
    """
    gen0 = manifest["generations"][0]
    if target.startswith("index:"):
        index = int(target.split(":", 1)[1])
        for name, i in name_to_index(gen0).items():
            if i == index:
                return name
        sys.exit(f"--lock index:{index} is not a named node in generation 0 "
                 f"(fillers are anonymous and cannot be locked onto).")

    if target == "most-moved":
        # Total displacement over the run's FIRST generation, not "this iteration": a per-frame
        # winner changes cell every frame, which pans the window randomly rather than following
        # anything. This picks one cell and stays on it, which is the question the view answers.
        gen_frames = [f for f in frames if f[0] is gen0]
        if len(gen_frames) < 2:
            sys.exit("--lock most-moved needs at least 2 frames in generation 0.")
        first = read_frame(manifest, gen0, gen_frames[0][1])
        last = read_frame(manifest, gen0, gen_frames[-1][1])
        moved = np.hypot(last[:, 0] - first[:, 0], last[:, 1] - first[:, 1])
        # Fillers move furthest and are anonymous; restrict to the real, named, movable cells.
        movable_named = {i: n for n, i in name_to_index(gen0).items() if i < gen0["filler_start"]}
        if not movable_named:
            sys.exit("--lock most-moved found no named movable nodes in generation 0.")
        best = max(movable_named, key=lambda i: moved[i])
        return movable_named[best]

    if target not in name_to_index(gen0):
        sys.exit(f"--lock: no node named '{target}' in generation 0.")
    return target


def read_frame(manifest, gen, index):
    """Positions of EVERY node for one frame, in static-record order.

    The frame stream holds only the movable prefix; the nodes that never move keep the exact
    float32 coordinates in the static file rather than a quantized copy.
    """
    n = gen["frame_nodes"]
    raw = np.fromfile(gen["path"], dtype="<u2", count=2 * n, offset=index * 4 * n)
    quant = manifest["quant"]
    # Per-axis step: the box is the die inflated 2x and dies are not square, so one step for both
    # axes is a silent skew (~11 die units at the far edge of mms/adaptec1).
    step = np.array([quant["w"] / 65535.0, quant["h"] / 65535.0])
    origin = np.array([quant["x0"], quant["y0"]])

    pos = np.empty((len(gen["static"]), 2), dtype=np.float64)
    pos[:n] = origin + raw.reshape(-1, 2).astype(np.float64) * step
    pos[n:] = np.stack([gen["static"]["x"][n:], gen["static"]["y"][n:]], axis=1)
    return pos


def fill_rects(canvas, x0, y0, x1, y1, color, clip=None):
    """Paint axis-aligned rectangles given as EXCLUSIVE integer pixel bounds.

    `clip` is the cairo_clip() equivalent: rectangles are trimmed to it and ones falling entirely
    outside are dropped. The zoom view clips to the die box; the full-die view must NOT, because
    fixed terminals and blockages legitimately sit in the margin outside the core-row die.
    """
    h_px, w_px = canvas.shape[:2]
    cx0, cy0, cx1, cy1 = clip if clip is not None else (0, 0, w_px, h_px)
    cx0, cy0 = max(cx0, 0), max(cy0, 0)
    cx1, cy1 = min(cx1, w_px), min(cy1, h_px)

    # A box that rounds to zero pixels still has to be visible -- widen BEFORE clipping, so that
    # "too small to see" and "outside the window" stay different things.
    x1 = np.maximum(x1, x0 + 1)
    y1 = np.maximum(y1, y0 + 1)

    x0, y0 = np.maximum(x0, cx0), np.maximum(y0, cy0)
    x1, y1 = np.minimum(x1, cx1), np.minimum(y1, cy1)
    w, h = x1 - x0, y1 - y0

    live = (w > 0) & (h > 0)
    small = live & (w <= SMALL_PX) & (h <= SMALL_PX)
    col = np.array(color, dtype=np.uint8)

    # Cells up to SMALL_PX are painted by broadcasting over a fixed offset grid rather than by
    # looping: at full-die scale MIN_SIZE floors nearly every standard cell to ~2 px, so this is
    # the difference between a second and a minute on a 371k-node frame.
    for dy in range(SMALL_PX):
        for dx in range(SMALL_PX):
            m = small & (dx < w) & (dy < h)
            if m.any():
                canvas[y0[m] + dy, x0[m] + dx] = col

    for i in np.nonzero(live & ~small)[0]:
        canvas[y0[i]:y1[i], x0[i]:x1[i]] = col


def stroke_rects(canvas, x0, y0, x1, y1, color, width, clip=None):
    """Outline rectangles as four filled edges, in the same layer order cairo uses.

    Two properties, for the same reasons as fill_lines: the edges STRADDLE the path (cairo centres
    a stroke, so an inward-only border shifts the outline by half its width -- on the die boundary
    that alone dragged frame correlation to 0.93), and the width stays FRACTIONAL with both edges
    rounded independently, so it averages out across sub-pixel phases instead of quantizing upward
    on every one of the thousands of cell outlines a zoom frame contains.
    """
    half = 0.5 * float(width)

    def band(center):
        lo = np.rint(center - half).astype(np.int64)
        return lo, np.maximum(np.rint(center + half).astype(np.int64), lo + 1)

    x1 = np.maximum(x1, x0 + 1)
    y1 = np.maximum(y1, y0 + 1)

    ta, tb = band(y0)      # top edge band
    ba, bb = band(y1)      # bottom edge band
    la, lb = band(x0)      # left edge band
    ra, rb = band(x1)      # right edge band

    # Horizontal edges run the full straddled width, vertical edges the full straddled height, so
    # the four bands meet at the corners exactly as a single stroked path does.
    fill_rects(canvas, la, ta, rb, tb, color, clip)   # top
    fill_rects(canvas, la, ba, rb, bb, color, clip)   # bottom
    fill_rects(canvas, la, ta, lb, bb, color, clip)   # left
    fill_rects(canvas, ra, ta, rb, bb, color, clip)   # right


class View:
    """The rendered die region and its map onto the canvas -- Visualizer's ViewWindow + map*().

    The full-die window is anchored at the ORIGIN rather than at the die box's lower-left, matching
    Visualizer::init(Box): node coordinates are already in the die-corner frame.
    """

    def __init__(self, die, canvas_px=MAX_CANVAS_PX, center=None, span=None, supersample=1):
        self.die_w, self.die_h = die["w"], die["h"]
        self.zoomed = center is not None

        self.span_frac = span   # kept verbatim for slug(); `side` below is already in die units
        if not self.zoomed:
            self.xl, self.yl = 0.0, 0.0
            self.width, self.height = self.die_w, self.die_h
        else:
            # A SQUARE window in die units, sized as a fraction of the SHORTER die dimension, so
            # one --span means the same magnification on any benchmark (MMS die sizes span two
            # orders of magnitude). Matches Placer::initializeZoomView.
            side = min(max(span, 1e-4), 1.0) * min(self.die_w, self.die_h)
            cx, cy = center[0] * self.die_w, center[1] * self.die_h
            self.xl, self.yl = cx - 0.5 * side, cy - 0.5 * side
            self.width = self.height = side

        # The canvas aspect follows the VIEW, not the die, so a square zoom window gets a square
        # image whatever the die's shape.
        if self.width >= self.height:
            self.out_w = canvas_px
            self.out_h = max(1, int(canvas_px * self.height / self.width))
        else:
            self.out_h = canvas_px
            self.out_w = max(1, int(canvas_px * self.width / self.height))

        # Geometry is rasterized at ss x the output size and box-downsampled, which computes
        # exactly the area coverage cairo antialiases with. Without it every edge is hard: at zoom
        # a few hundred cells contribute ~400 px of outline each, and that edge treatment alone --
        # not any positional error -- is what separates 0.989 from 0.999 frame correlation.
        self.ss = max(1, supersample)
        self.px_w, self.px_h = self.out_w * self.ss, self.out_h * self.ss
        self.scale_px = min(self.px_w, self.px_h)
        # The die box in pixels -- the clip region when zoomed, and the extent of every grid line.
        self.box = (int(DIE_START * self.px_w), int(DIE_START * self.px_h),
                    int((DIE_START + DIE_SCALE) * self.px_w),
                    int((DIE_START + DIE_SCALE) * self.px_h))
        self.clip = self.box if self.zoomed else None

    def center_frac(self):
        """Window centre as a fraction of the die -- i.e. what --center was given."""
        return ((self.xl + 0.5 * self.width) / self.die_w,
                (self.yl + 0.5 * self.height) / self.die_h)

    def slug(self):
        """Directory name for this view. Carries the WINDOW, not just the mode: rendering several
        zooms of one run is the point of --add-view, and they would otherwise all land in
        viz_render/zoom and overwrite each other frame by frame."""
        if not self.zoomed:
            return "full"
        cx, cy = self.center_frac()
        return f"zoom_c{cx:g}-{cy:g}_s{self.span_frac:g}"

    def map_x(self, die_x):
        return DIE_START + (die_x - self.xl) * DIE_SCALE / self.width

    def map_y(self, die_y):
        """Die y -> canvas y, INVERTED: die y grows up, the raster grows down."""
        return DIE_START + DIE_SCALE - (die_y - self.yl) * DIE_SCALE / self.height

    def px(self, width_unit):
        """A cairo line width (unit space) as an integer pixel count, never below 1."""
        return max(1, int(round(width_unit * self.scale_px)))

    def px_f(self, width_unit):
        """The same width left FRACTIONAL, for callers that round the two edges independently
        instead of rounding the width itself (see fill_lines)."""
        return width_unit * self.scale_px

    def rects(self, pos, size):
        """Die-space boxes -> exclusive integer pixel bounds.

        Boxes anchor at their TOP edge (mapRectTop) because the raster grows downward. Without the
        inversion every frame is vertically mirrored -- harmless-looking at full-die scale,
        actively misleading anywhere a frame is matched against a DEF.
        """
        ux = self.map_x(pos[:, 0])
        uy = self.map_y(pos[:, 1] + size[:, 1])
        uw = np.maximum(MIN_SIZE, size[:, 0] * DIE_SCALE / self.width)
        uh = np.maximum(MIN_SIZE, size[:, 1] * DIE_SCALE / self.height)

        x0 = np.rint(ux * self.px_w).astype(np.int64)
        y0 = np.rint(uy * self.px_h).astype(np.int64)
        x1 = np.rint((ux + uw) * self.px_w).astype(np.int64)
        y1 = np.rint((uy + uh) * self.px_h).astype(np.int64)
        return x0, y0, x1, y1

    def visible(self, pos, size):
        """Mask of boxes that intersect the window. A numpy pre-filter, not a correctness step --
        clipping already handles the rest -- but at zoom it drops ~99.99% of an MMS design before
        any per-rectangle work happens."""
        if not self.zoomed:
            return np.ones(len(pos), dtype=bool)
        return ((pos[:, 0] + size[:, 0] >= self.xl) & (pos[:, 0] <= self.xl + self.width) &
                (pos[:, 1] + size[:, 1] >= self.yl) & (pos[:, 1] <= self.yl + self.height))


def fill_lines(canvas, view, centers, width, color, horizontal):
    """Grid lines, CENTRED on their (fractional) coordinate with a fractional width.

    Both properties were learned the hard way against the cairo reference:

    * **Centring.** cairo centres every stroke on its path. Drawing from the coordinate
      rightward/downward instead offsets each line by half its width, which showed up as adjacent
      +/- spikes 256 px apart in the bin-grid column profile and pulled frame correlation to 0.94
      while the rest of the frame already matched at 0.9994.
    * **Fractional width.** Rounding the width to whole pixels and adding it to a rounded centre
      quantizes it upward every time -- a 1.638 px row line becomes 1.75 px, 7% too much ink on
      every row. Rounding the two EDGES independently instead lets the width average out across
      sub-pixel phases, exactly as the cell rectangles already do. That 7% was the entire residual
      on the zoom view: the row-profile residual autocorrelated at lag 164, the row pitch.
    """
    lo = np.rint(centers - 0.5 * width).astype(np.int64)
    hi = np.maximum(np.rint(centers + 0.5 * width).astype(np.int64), lo + 1)
    bx0, by0, bx1, by1 = view.box
    if horizontal:
        fill_rects(canvas, np.full_like(lo, bx0), lo, np.full_like(lo, bx1), hi, color, view.clip)
    else:
        fill_rects(canvas, lo, np.full_like(lo, by0), hi, np.full_like(lo, by1), color, view.clip)


def draw_row_lines(canvas, view, row_height):
    """Standard-cell row pitch -- zoom only.

    The point of the zoom: global placement is easy to reason about as a continuous density field
    and forget the target is a ROW-BASED layout. Rows are on a uniform pitch from the origin, which
    is the grid sw_only implicitly targets -- it has no per-row site model.
    """
    if row_height <= 0:
        return
    first = int(np.floor(view.yl / row_height))
    last = int(np.ceil((view.yl + view.height) / row_height))
    if last - first > MAX_DETAIL_LINES:
        return   # denser than this is a solid wash; drop the layer rather than draw it
    rows = np.arange(first, last + 1) * row_height
    fill_lines(canvas, view, view.map_y(rows) * view.px_h, view.px_f(0.0008),
               (204, 204, 224), horizontal=True)


def draw_bin_grid(canvas, view, bins_per_row):
    """Density-bin boundaries -- zoom only. Shows how many cells share a bin, i.e. what the
    density term can actually resolve."""
    if not bins_per_row:
        return
    bin_w, bin_h = view.die_w / bins_per_row, view.die_h / bins_per_row
    col_lo, col_hi = int(np.floor(view.xl / bin_w)), int(np.ceil((view.xl + view.width) / bin_w))
    row_lo, row_hi = int(np.floor(view.yl / bin_h)), int(np.ceil((view.yl + view.height) / bin_h))
    if col_hi - col_lo > MAX_DETAIL_LINES or row_hi - row_lo > MAX_DETAIL_LINES:
        return
    w, color = view.px_f(0.0012), (158, 199, 158)

    cols = np.arange(col_lo, col_hi + 1) * bin_w
    fill_lines(canvas, view, view.map_x(cols) * view.px_w, w, color, horizontal=False)

    rows = np.arange(row_lo, row_hi + 1) * bin_h
    fill_lines(canvas, view, view.map_y(rows) * view.px_h, w, color, horizontal=True)


def read_iteration_scalars(run_dir):
    """iterations.dat -> {iteration: (hpwl, overflow, step_length, density_weight)}.

    Read, never duplicated into the dump: these are already written per iteration and consumed by
    other analysis scripts.
    """
    path = run_dir / "iterations.dat"
    if not path.is_file():
        return {}
    out = {}
    for line in path.read_text().splitlines()[1:]:
        f = [c.strip() for c in line.split(",")]
        if len(f) >= 5:
            try:
                out[int(f[0])] = tuple(float(v) for v in f[1:5])
            except ValueError:
                pass
    return out


def load_font(px):
    for name in ("DejaVuSans-Bold.ttf", "LiberationSans-Bold.ttf", "Arial_Bold.ttf"):
        try:
            return ImageFont.truetype(name, px)
        except OSError:
            continue
    return ImageFont.load_default()


def draw_overlay(image, view, manifest, iteration, tag, phase, phase_iter, scalars, lock_label=""):
    """The text overlay from Visualizer::drawPlacementInfoOverlay, in the same canvas positions."""
    draw = ImageDraw.Draw(image)
    W, H = view.out_w, view.out_h
    font = load_font(max(8, int(0.02 * H)))

    def text(ux, uy, s):
        draw.text((ux * W, uy * H), s, fill=(0, 0, 0), font=font, anchor="ls")

    # Benchmark, frame tag and phase all share the top line. Header lines are scarce: with
    # DIE_START = 0.10 the 4th one lands inside the die box and is unreadable against the cells,
    # and a locked zoom already wants three. The tag used to be printed in the footer's alpha slot,
    # which cost tagged frames alpha and lambda -- exactly the two scalars a phase-boundary frame
    # is worth looking at for.
    top = f"Benchmark: {manifest['benchmark']}" + (f"   [{tag}]" if tag else "")
    if phase:
        top += f"   Phase: {phase} (phase iter {phase_iter})"
    text(0.01, 0.04, top)

    # Optional header lines, each its OWN line rather than a suffix on the one above: past ~70
    # characters a line runs off the canvas and neither renderer wraps or warns.
    header_y, HEADER_LINE = 0.075, 0.035
    # Where on the die this window is, and how much of it. Without this a zoom frame is
    # unreadable -- a few hundred cells with nothing to locate them by.
    if view.zoomed:
        # Centre as a die FRACTION, not the window's lower-left in die units: it is the number
        # --center was given, so a frame can be reproduced from its own caption. Absolute die
        # coordinates are unreadable without knowing the die size, which the caption does not carry.
        cx, cy = view.center_frac()
        # The locked cell rides THIS line. Merging Benchmark+Phase above freed exactly one header
        # slot, and giving it back to a "Locked:" line spends it at y=0.110 -- inside the die box,
        # which starts at DIE_START=0.10 -- so the name is drawn over the cells. Two header lines
        # is the budget that clears the box; the zoom line already says WHERE the window went, and
        # under a lock the only thing missing is what took it there.
        text(0.01, header_y,
             f"Zoom: {100.0 * view.width / view.die_w:.2g}% x "
             f"{100.0 * view.height / view.die_h:.2g}% of die @ "
             f"({cx:.3f}, {cy:.3f})" + (f"   Locked: {lock_label}" if lock_label else ""))
        header_y += HEADER_LINE

    # The footer is now the same four scalars in every view and on every frame, tagged or not.
    text(0.01, 0.99, f"Iter: {iteration}")
    if iteration in scalars:
        hpwl, ovfw, step, lam = scalars[iteration]
        text(0.18, 0.99, f"HPWL: {hpwl:.3e}")
        text(0.40, 0.99, f"OVFW: {ovfw:.2g}")
        text(0.55, 0.99, f"alpha: {step:.3e}")
        text(0.78, 0.99, f"lambda: {lam:.3e}")


def locked_view(manifest, gen, pos, lock_name, span, canvas_px, supersample):
    """A zoom window centred on one tracked node, for THIS frame.

    Rebuilt every frame rather than fixed at setup -- that is the difference between watching cells
    drift through a static box and watching what the optimizer does to one cell. View construction
    is a handful of arithmetic, so per-frame is free next to the raster.
    """
    index = name_to_index(gen).get(lock_name)
    if index is None:
        return None   # node absent from this generation; caller decides what to do
    die = manifest["die"]
    size = gen["static"][index]
    # Centre on the node's MIDDLE, not its origin: a macro's origin is its lower-left corner, so
    # centring there puts three quarters of the macro outside the window.
    cx = (pos[index, 0] + 0.5 * size["w"]) / die["w"]
    cy = (pos[index, 1] + 0.5 * size["h"]) / die["h"]
    return View(die, canvas_px, center=(cx, cy), span=span, supersample=supersample)


def render_frame(manifest, view, gen, pos, iteration, tag, scalars, lock_label=""):
    """Draw one already-decoded frame into one view.

    `pos` is passed in rather than read here so that rendering N views of a run decodes each frame
    ONCE -- read_frame is the file I/O plus the dequantization, and at MMS node counts it dominates
    a zoom render, which discards ~99.99% of the nodes in View.visible().
    """
    static = gen["static"]
    size = np.stack([static["w"], static["h"]], axis=1).astype(np.float64)
    kinds = static["kind"]

    canvas = np.full((view.px_h, view.px_w, 3), 255, dtype=np.uint8)
    bx0, by0, bx1, by1 = view.box

    # View boundary first, so cells paint over it -- same order as drawPlacement. Never clipped:
    # it IS the clip region.
    stroke_rects(canvas, *(np.array([v]) for v in (bx0, by0, bx1, by1)),
                 (0, 0, 0), view.px_f(0.004))

    # Under the cells, so the cells read against them: the two structures a continuous density
    # field hides -- the rows the layout must land on, and the bins density is measured over.
    if view.zoomed:
        draw_row_lines(canvas, view, manifest.get("row_height", 0.0))
        draw_bin_grid(canvas, view, manifest.get("bins_per_row", 0))

    keep = view.visible(pos, size)
    x0, y0, x1, y1 = view.rects(pos, size)
    for kind, color, outline in LAYERS:
        sel = keep & (kinds == kind)
        if not sel.any():
            continue
        rect = (x0[sel], y0[sel], x1[sel], y1[sel])
        fill_rects(canvas, *rect, color, view.clip)
        if outline == "border" or (outline == "zoom" and view.zoomed):
            oc, ow = OUTLINE[outline]
            stroke_rects(canvas, *rect, oc, view.px_f(ow), view.clip)

    image = Image.fromarray(canvas)
    if view.ss > 1:
        # BOX = plain area average, i.e. the coverage fraction of each output pixel. Anything
        # fancier (LANCZOS) would ring on the hard edges this image is made of.
        image = image.resize((view.out_w, view.out_h), Image.BOX)

    draw = ImageDraw.Draw(image)
    # Reticle at the canvas centre. Drawn from canvas coordinates after downsampling, so it is
    # outside the clip and unaffected by the y inversion -- as in cairo, where the flip lives in
    # the arithmetic rather than in a transform.
    r, lw = 0.008, max(1, int(round(0.001 * min(view.out_w, view.out_h))))
    draw.line([((0.5 - r) * view.out_w, 0.5 * view.out_h),
               ((0.5 + r) * view.out_w, 0.5 * view.out_h)], fill=(0, 0, 0), width=lw)
    draw.line([(0.5 * view.out_w, (0.5 - r) * view.out_h),
               (0.5 * view.out_w, (0.5 + r) * view.out_h)], fill=(0, 0, 0), width=lw)

    phase = gen["phase"] if len(manifest["generations"]) > 1 else ""
    draw_overlay(image, view, manifest, iteration, tag, phase,
                 iteration - gen["first_iter"], scalars, lock_label)
    return image


def parse_iters(spec):
    """"A:B:S" -> a predicate over iteration numbers. Any field may be empty."""
    if not spec:
        return lambda _: True
    parts = (spec.split(":") + ["", "", ""])[:3]
    lo = int(parts[0]) if parts[0] else None
    hi = int(parts[1]) if parts[1] else None
    stride = int(parts[2]) if parts[2] else 1
    return lambda it: ((lo is None or it >= lo) and (hi is None or it <= hi)
                       and (lo is None or (it - lo) % stride == 0))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run_dir", type=Path)
    ap.add_argument("--view", choices=("full", "zoom"), default="full")
    ap.add_argument("--center", default="0.5,0.5", metavar="X,Y",
                    help="zoom window centre as fractions of die width/height (default 0.5,0.5)")
    ap.add_argument("--span", type=float, default=0.05,
                    help="zoom window side as a fraction of the SHORTER die dimension "
                         "(default 0.05 = 1/400th of the die area)")
    ap.add_argument("--out", type=Path, help="default <run_dir>/viz_render/<view>")
    ap.add_argument("--iters", default="", metavar="A:B:S", help="subset/stride of exported frames")
    ap.add_argument("--canvas", type=int, default=MAX_CANVAS_PX, help="longest side in pixels")
    ap.add_argument("--supersample", type=int, default=None, metavar="N",
                    help="rasterize at Nx and box-downsample for antialiasing. Default 2 for the "
                         "full-die view, 4 for zoom (which draws per-cell outlines that need the "
                         "resolution -- see SUPERSAMPLE_DEFAULT). 1 disables; costs N^2.")
    ap.add_argument("--add-view", action="append", default=[], metavar="SPEC",
                    help="render an ADDITIONAL window in the same pass; repeatable. SPEC is "
                         "'full' or 'zoom:CX,CY,SPAN'. Each window gets its own output "
                         "directory named after itself. Given at least once, --view/--center/"
                         "--span are ignored. Every frame is decoded once and drawn into each "
                         "window, so N windows cost far less than N invocations.")
    ap.add_argument("--lock", metavar="TARGET",
                    help="follow ONE node: the window re-centres on it every frame instead of "
                         "staying put. TARGET is a node name, 'index:N', or 'most-moved' (the "
                         "named movable cell that travels furthest across generation 0). Implies "
                         "a zoom window of --span; incompatible with --add-view.")
    ap.add_argument("--gif", action="store_true", help="animate the output with gif_builder.py")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    manifest, frames = load_run(args.run_dir)
    scalars = read_iteration_scalars(args.run_dir)

    def build_view(mode, center, span):
        ss = args.supersample if args.supersample else SUPERSAMPLE_DEFAULT[mode]
        if mode == "zoom":
            return View(manifest["die"], args.canvas, center=center, span=span, supersample=ss)
        return View(manifest["die"], args.canvas, supersample=ss)

    views = []
    if args.lock:
        if args.add_view:
            ap.error("--lock follows one node in one window; --add-view renders several fixed ones")
        lock_name = resolve_lock(manifest, frames, args.lock)
        lock_ss = args.supersample if args.supersample else SUPERSAMPLE_DEFAULT["zoom"]
        out_dirs = [args.out or args.run_dir / "viz_render" / f"lock_{lock_name.replace('/', '_')}"]
        out_dirs[0].mkdir(parents=True, exist_ok=True)

        wanted = parse_iters(args.iters)
        selected = [f for f in frames if wanted(f[2])]
        if not args.quiet:
            print(f"{len(selected)} of {len(frames)} frames, locked on '{lock_name}' "
                  f"(span {args.span}) -> {out_dirs[0]}")

        written, skipped = [], 0
        for gen, index, iteration, tag in selected:
            pos = read_frame(manifest, gen, index)
            view = locked_view(manifest, gen, pos, lock_name, args.span, args.canvas, lock_ss)
            if view is None:
                skipped += 1
                continue
            name = (f"{BEST_PNG_PREFIX}iter{iteration}.png" if tag == "best_solution"
                    else f"iter_{iteration}" + (f"_{tag}" if tag else "") + ".png")
            image = render_frame(manifest, view, gen, pos, iteration, tag, scalars, lock_name)
            image.save(out_dirs[0] / name)
            written.append(out_dirs[0] / name)
            if not args.quiet:
                print(f"  {name}", flush=True)

        if skipped:
            print(f"warning: '{lock_name}' is absent from {skipped} frame(s); those were skipped.",
                  file=sys.stderr)
        if not written:
            sys.exit(f"--lock produced no frames: '{lock_name}' matched no selected frame.")
        if args.gif:
            builder = Path(__file__).resolve().parent / "gif_builder.py"
            cmd = [sys.executable, str(builder), "-o", str(out_dirs[0] / GIF_NAME),
                   "--frames", *(str(p) for p in written)]
            subprocess.run(cmd + (["--quiet"] if args.quiet else []), check=True)
        return

    if args.add_view:
        if args.out:
            ap.error("--out names a single directory; with --add-view each window gets its own")
        for spec in args.add_view:
            mode, _, rest = spec.partition(":")
            if mode == "full":
                if rest:
                    ap.error(f"'full' takes no parameters, got: {spec}")
                views.append(build_view("full", None, None))
            elif mode == "zoom":
                try:
                    cx, cy, span = (float(v) for v in rest.split(","))
                except ValueError:
                    ap.error(f"--add-view zoom wants zoom:CX,CY,SPAN, got: {spec}")
                views.append(build_view("zoom", (cx, cy), span))
            else:
                ap.error(f"--add-view SPEC must start with 'full' or 'zoom', got: {spec}")
    else:
        center = None
        if args.view == "zoom":
            center = tuple(float(v) for v in args.center.split(","))
            if len(center) != 2:
                ap.error("--center takes X,Y as fractions of the die, e.g. 0.5,0.5")
        views.append(build_view(args.view, center, args.span))

    # Single-view keeps the historical viz_render/<view> path (make_viz_gifs.py and the viz-gif
    # skill both reach straight into it); only multi-view needs the window-qualified name.
    if len(views) == 1 and not args.add_view:
        out_dirs = [args.out or args.run_dir / "viz_render" / args.view]
    else:
        out_dirs = [args.run_dir / "viz_render" / v.slug() for v in views]
    for d in out_dirs:
        d.mkdir(parents=True, exist_ok=True)

    wanted = parse_iters(args.iters)
    selected = [f for f in frames if wanted(f[2])]
    if not args.quiet:
        print(f"{len(selected)} of {len(frames)} frames, "
              f"{len(manifest['generations'])} generation(s), {len(views)} view(s)")
        for view, out_dir in zip(views, out_dirs):
            where = (f", window {view.width:.4g} x {view.height:.4g} @ centre "
                     f"({view.center_frac()[0]:.3f}, {view.center_frac()[1]:.3f})"
                     if view.zoomed else "")
            print(f"  canvas {view.out_w}x{view.out_h} ({view.ss}x supersampled)"
                  f"{where} -> {out_dir}")

    written = [[] for _ in views]
    for gen, index, iteration, tag in selected:
        pos = read_frame(manifest, gen, index)   # decoded ONCE, drawn into every view
        # The two files worth reaching for first are named to sort first: the GIF, then the
        # best-solution frame. Everything else keeps the cairo renderer's iter_<N> naming, so the
        # rest of the folder still reads in trajectory order below them.
        name = (f"{BEST_PNG_PREFIX}iter{iteration}.png" if tag == "best_solution"
                else f"iter_{iteration}" + (f"_{tag}" if tag else "") + ".png")
        for i, (view, out_dir) in enumerate(zip(views, out_dirs)):
            image = render_frame(manifest, view, gen, pos, iteration, tag, scalars)
            image.save(out_dir / name)
            written[i].append(out_dir / name)
        if not args.quiet:
            print(f"  {name}", flush=True)

    if args.gif:
        # Hand gif_builder the frame order explicitly. It otherwise natural-sorts the directory,
        # which put the trajectory in order only as long as every frame was named iter_<N> -- the
        # best-solution frame is deliberately no longer, and would sort to the front of the GIF.
        builder = Path(__file__).resolve().parent / "gif_builder.py"
        for out_dir, paths in zip(out_dirs, written):
            cmd = [sys.executable, str(builder), "-o", str(out_dir / GIF_NAME),
                   "--frames", *(str(p) for p in paths)]
            subprocess.run(cmd + (["--quiet"] if args.quiet else []), check=True)


if __name__ == "__main__":
    main()
