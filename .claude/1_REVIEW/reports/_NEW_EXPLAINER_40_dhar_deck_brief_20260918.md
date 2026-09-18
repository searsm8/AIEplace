# Slide-deck brief — the Dhar HPWL-gradient module (`hpwl_gradient_dhar`)

**What this is:** a design brief to paste into Claude Design to produce a slide deck. It describes
each slide's message and diagram precisely enough to draw, using the code's CURRENT names (after the
2026-09-18 rename). Source of truth for the code: [[hpwl_gradient_dhar.hpp]] (in
`vck5000/bring_up/hpwl_gradient_dhar/src/modules/`); for the paper: `.claude/2_ARTIFACTS/papers/dhar_fpga_accel/`.

⚠️ Slide 14 (status) quotes #40's measurements. Re-check them against tasks.md #40 before presenting —
Module 4 of the crash course (pragmas / synthesis) had not been done when this brief was written.

---

## Instructions for Claude Design

- **Audience:** hardware/EDA researchers. Comfortable with FPGAs and gradient descent; not assumed to
  know placement. Technical, dense, no marketing tone.
- **Style:** clean white background, one idea per slide, diagrams over bullets. Max ~4 short bullets
  per slide. Sans-serif body; **monospace for every code identifier** (`npin_slot`, `Bpx`, …).
- **Consistent colour code across ALL slides** (also use a second cue — outline style or a small
  label — so it reads in greyscale):
  - **Blue** — net-major data (anything ordered by net: `net_pins`, the 16-slot block)
  - **Green** — node-major data (ordered by node: `node_pins`, `pin_grad`, `node_grad`)
  - **Orange** — arithmetic (term generators, adder trees, combiners)
  - **Grey, hatched** — padding slots (value 0)
  - **Purple** — DDR / off-chip memory
- Equations typeset properly (LaTeX style), not as code.
- 16:9. Title on every slide states the takeaway, not the topic (e.g. "The shift cancels exactly",
  not "Bounding-box shift").

---

## Slide 1 — Title
**Accelerating the wirelength gradient on the PL: a Dhar-style HPWL-gradient module**
Subtitle: AIEplace · AMD Versal VCK5000 · bring-up module `hpwl_gradient_dhar`.

## Slide 2 — What the module computes
Message: every iteration, the placer needs the gradient of wirelength with respect to every movable
cell's position.
- Weighted-average (WA) wirelength of one net, x axis:
  $$\mathrm{WA}_x = \frac{C_+}{B_+} - \frac{C_-}{B_-},\quad
    B_\pm=\sum_i e^{\pm x_i/\gamma},\quad C_\pm=\sum_i x_i\,e^{\pm x_i/\gamma}$$
- $C_+/B_+$ is a **soft-max**: a weighted average of pin coordinates whose weights grow
  exponentially with the coordinate. γ is a temperature: γ→0 gives the true max, γ→∞ the mean.
- Diagram: a 1-D number line with 4 pins; mark true min, true max, soft-min, soft-max. The soft
  values sit INSIDE [min, max] (WA always underestimates HPWL). Show a second, smaller γ with the
  soft values closer to the extremes.

## Slide 3 — Four sums per axis, eight per net
Message: the gradient of every pin in a net needs only 4 per-net sums per axis (8 total), plus that
pin's own exponentials.
$$\frac{\partial \mathrm{WA}_x}{\partial x_j} =
  \Big[(1+\tfrac{x_j}{\gamma})B_+ - \tfrac{C_+}{\gamma}\Big]\frac{A_{+,j}}{B_+^2}
 -\Big[(1-\tfrac{x_j}{\gamma})B_- + \tfrac{C_-}{\gamma}\Big]\frac{A_{-,j}}{B_-^2}$$
- Diagram: a 2×2 grid of the four x sums (B+, C+, B−, C−) and an identical grid for y, labelled
  `Bpx Cpx Bmx Cmx` / `Bpy Cpy Bmy Cmy`. Caption: "p/m = plus/minus branch; B = Σ exp; C = Σ x·exp".
- Callout on the sign trap: the C− term is **added** inside its bracket.

## Slide 4 — The bounding-box shift cancels exactly
Message: subtract the net's max (plus branch) or min (minus branch) inside the exponent. The value
and gradient are unchanged, but every exponential lands in (0, 1].
$$\frac{\sum x_i e^{(x_i-s)/\gamma}}{\sum e^{(x_i-s)/\gamma}}
 =\frac{e^{-s/\gamma}\sum x_i e^{x_i/\gamma}}{e^{-s/\gamma}\sum e^{x_i/\gamma}}
 \quad\text{for every } s$$
- Three consequences as icons/bullets: (1) no overflow; (2) the extreme pin contributes exactly 1,
  so $B \in [1, \deg]$ — the denominator can never be 0; (3) a one-sided exp LUT, $e^{-t}, t\ge 0$,
  suffices (242 entries, $e^{-0.05 i}$, covers 12γ).
- Cost (small print): the bbox must be known before any exponential — a serial dependency.

## Slide 5 — Dhar et al. (FPL 2019): the precedent
Message: Dhar split the gradient along memory-access lines — the CPU does the irregular parts, the
FPGA does the regular arithmetic.
- Diagram (recreate the paper's Fig. 4 as a vertical pipeline with a CPU | FPGA | CPU band down the
  right): CPU packs nets **bucket-sorted by degree** into 16-slot blocks (all nets in a block share a
  degree; zero-padding shown hatched) → FPGA: 16 term generators → 4 multi-output adder trees
  (34 outputs each) → 4×16 result selectors driven by ONE `net degree` register → 16 combiners →
  CPU sums pin gradients per cell.
- Facts: nets ≤16 pins only (>16 are <0.25% of nets); 90.6% slot utilisation; 227 MHz; DSP-bound at
  84% (Arria 10, OpenCL).

## Slide 6 — Amdahl's law in the wild
Message: Dhar sped the gradient up 3.03× but global placement only 2.00× — and the gather they left
on the CPU now costs more than the kernel.
- Diagram: pie chart of the accelerated run (paper Fig. 12): Kernel 21.2 %, Gradient sum (CPU gather)
  26.8 %, Nesterov step 6.6 %, Spreading 31.7 %, Others 13.7 %. Emphasise the two gradient slices.
- Formula: $S = 1/((1-p) + p/s)$ with p = 0.726, s = 3.03 → 1.95× (reported 2.00×; ceiling 3.65×).
- Punchline: this is the argument for putting the WHOLE iteration, including the gather, on the PL.

## Slide 7 — Our module at a glance: three phases
Message: one kernel, three sequential phases, no per-net data spilled to DDR.
- Diagram: left column of purple DDR buffers — inputs `net_ptr`, `net_pins`, `pin_to_npin`,
  `exp_lut`, `node_pins`; scratch `pin_grad`; outputs `node_grad`, `out_hpwl`. Right: four boxes in
  order, with arrows to the buffers they touch:
  1. `cache_lut` — exp table DDR → on-chip
  2. **Phase Z** `zero_pin_grad` — writes 0 to all of `pin_grad`
  3. **Phase D** `net_loop` — per net: reads `net_ptr`, `net_pins`, `pin_to_npin`; writes `pin_grad`
     (scattered) and accumulates HPWL
  4. **Phase 3** `clear_grad` + `node_reduce` — reads `node_pins`, `pin_grad`; writes `node_grad`
- Footnote: the 16-pin cap lets a whole net live in registers, so the sibling module's
  `bb_DDR`/`sums_DDR` spill disappears.

## Slide 8 — Two orderings of the same pins
Message: pins are stored twice — net-major for the per-net maths, node-major for the per-cell sum —
and `pin_to_npin` connects them.
- Diagram (worked example, draw exactly): movable nodes 0, 1, 2; fixed node 3.
  - Top row, blue, `net_pins` indices p=0..6 with node_idx `0 1 | 1 3 2 | 0 2` and nets
    `net 0 | net 1 | masked (net=−1)`. Mark p=3 as "fixed node" and p=5,6 as "masked".
  - Bottom row, green, `node_pins` = [p0, p1, p2, p4] with node_idx [0, 1, 1, 2]; bracket the
    two entries of node 1 as one segment.
  - Arrows from each top cell to its bottom slot: `pin_to_npin = [0, 1, 2, −1, 3, −1, −1]`; the three
    −1 cells have no arrow (draw an ⊘).
- Caption: `npin_slot[k] = pin_to_npin[beg+k]` — "where this net's k-th pin lives in node-major
  order". The map is **injective** (no two pins share a slot), so all 16 scatter writes are
  independent.

## Slide 9 — Phase D: one net through the datapath
Message: a net (≤16 pins) is loaded into a padded 16-slot register block and flows through four
stages with no DDR access in between.
- Diagram, left→right, 16 horizontal lanes (show 3 live blue lanes and 13 hatched grey pad lanes for
  a 3-pin net):
  1. `load_net` → `x_block[16]`, `y_block[16]`, `npin_slot[16]`; side output: bbox
     `max_x min_x max_y min_y`
  2. `term_gen` (orange, per lane): 4 LUT exps → `Apx Amx Apy Amy _terms`; 8 term arrays
     `Bpx_terms … Cmy_terms`
  3. 8 adder trees → 8 scalars `Bpx … Cmy`; then 4 reciprocals `inv_Bpx2 … inv_Bmy2` (once per net)
  4. `combine` (per lane) → `partial_x`, `partial_y` → scatter to `pin_grad[npin_slot[k]]`
- Small branch off stage 1: `net_hpwl = (max_x − min_x) + (max_y − min_y)` → HPWL accumulator.
- Guard strip above the lanes: skip if deg < 2 · masked · deg > `MAX_NET_DEGREE` (16).

## Slide 10 — The adder tree: ours vs Dhar's
Message: one net per pass means one segment, so zero-padding replaces Dhar's whole selector network.
- Left panel "Ours": 16 input squares (3 blue, 13 hatched "0"), a balanced binary tree 8→4→2→1,
  15 adders, depth 4, one output. Caption: "+0.0 is exact — the 16-input sum equals the 3-input sum,
  bit for bit."
- Right panel "Dhar": 16 squares coloured as five 3-pin nets + 1 pad; a wide box
  "multi-output adder tree — 34 outputs"; five taps `0_2 3_5 6_8 9_11 12_14`; a MUX "select = net
  degree". Caption: 34 = 8+5+4+3+2+2+2 + 8 (one each for degrees 9–16).
- Bottom strip: why write the tree by hand — float addition is not associative, so HLS will not turn
  `sum += v[k]` (15 dependent adds) into a tree for you.

## Slide 11 — The combiner, and why it needs only 4 divides per net
Message: the shift keeps $B^2 \in [1, 256]$, so $1/B^2$ is computed once per net and shared.
- Show the code line with each factor annotated by its equation term:
  `partial_x = ((1 + x·inv_gamma)·Bpx − Cpx·inv_gamma)·(Apx_terms[k]·inv_Bpx2)
             − ((1 − x·inv_gamma)·Bmx + Cmx·inv_gamma)·(Amx_terms[k]·inv_Bmx2)`
- Comparison bar: divides per 16-pin net — **ours 4** (both axes) vs **Dhar 192** (6 per pin per axis,
  needed for their divide-before-multiply overflow ordering).
- Note: fixed-node pins are in the sums (they pull on neighbours) but skipped at the scatter
  (`npin_slot = −1`).

## Slide 12 — Phase 3: segmented reduction
Message: because `node_pins` is sorted by node, summing each cell's pin gradients is one sequential
pass.
- Diagram: a green strip of `pin_grad` entries with node_idx labels `0 | 1 1 1 | 2 | 4 4`; an
  accumulator that resets at each label change and emits to `node_grad[node]`. Show node 3 as absent
  from the strip and pre-zeroed by `clear_grad`.
- Caption: this is the "gradient sum" that cost Dhar 26.8 % of runtime on the CPU — here it is on
  the PL.

## Slide 13 — Ours vs Dhar, side by side
Table:

| | Dhar Method 1 | ours (bring-up) |
|---|---|---|
| unit of work | one 16-slot block, several same-degree nets | one net, padded to 16 |
| axes per pass | one | both |
| adder trees | 4 × ~40 adders, 34 outputs | 8 × 15 adders, 1 output |
| exponentials | 1 exp + 1 reciprocal per pin | 4 LUT lookups per pin, bbox-shifted |
| overflow guard | divide-before-multiply | bbox shift (B ∈ [1, 16]) |
| divides per 16-pin net | 192 | 4 |
| pin→cell gather | CPU | PL (`node_reduce`) |
| objective by-product | WA wirelength | true HPWL from the bbox |

## Slide 14 — Status and next step (VERIFY BEFORE PRESENTING — see tasks.md #40)
- Functionally verified: tier-1 harness vs a double-precision golden, rel_rms 1.48e-6 (tol 1e-5);
  HPWL rel 2.1e-8.
- On hardware: routes but misses timing at 300 MHz — WNS −0.710 ns, 43,859 / 196,028 endpoints
  failing, all worst paths inside `net_loop`. Diagnosis: the fully parallel 16-lane
  term-gen/tree/combine block is too dense to route.
- HLS reports `net_loop` at II = 16 (limited by the `net_pins` memory port), i.e. one net per 16
  cycles.
- Next: narrow the compute to match the memory rate; then Dhar-style block packing (several small
  nets per pass), which is where the throughput is.
