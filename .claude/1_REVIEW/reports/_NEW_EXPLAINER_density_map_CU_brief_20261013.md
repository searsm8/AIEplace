# Density grid compute unit (`density_map_CU`) — slide brief

Source material for a short deck (5 slides). Each section is one slide: a headline, 3–5 bullets, and
a note on the visual. Numbers carry their basis in the speaker notes.

---

## Slide 1 — The bottleneck isn't the FFT, it's moving cells to and from the grid

**Headline:** On the GPU, scatter and gather take over half of each placement step; the DCTs take ~4%.

- XPlace on the GPU, bigblue4: **density scatter 25%, density force gather 29%** of kernel time.
- DCT / eField transforms: **3.6%**. Wirelength gradient: 13.9%.
- XPlace runs wirelength and density **one after the other** (one CUDA stream, zero overlap).
- So the VCK's chance is in **scatter and gather**, not the FFT that the AIE array is sized for.

*Visual:* the bigblue4 donut chart (gradient / scatter / DCT / gather / update / other).

*Notes:* torch.profiler kernel time, 300 iterations, XPlace deterministic kernels, one GPU under
WSL2; bigblue4 18.16 ms per iteration of kernel time. Source:
[[_NEW_REPORT_xplace_gpu_iteration_breakdown_20261013.md]]. On small designs (adaptec1) the GPU is
launch-bound and the gradient dominates instead.

---

## Slide 2 — What the unit computes

**Headline:** Every cell spreads its area over the bins it covers; the sum is the density map rho.

- Input per cell: position, plus its footprint (size stretched to at least √2 bins, with a weight
  so total area is unchanged).
- Each cell adds `overlap area × weight` to every bin its footprint touches: 2×2 or 3×3 bins for a
  standard cell or filler, many for a macro.
- Fixed nodes never move, so their contribution is deposited **once** on the host, capped at
  `min(rho, target density)` (our deliberate #35 divergence), and used as the starting value.
- Output: rho = overlap / bin area, a 1024×1024 map handed to the FFT field solve.

*Visual:* one cell's footprint over a 3×3 patch of bins, overlaps shaded.

---

## Slide 3 — The interface (defined in the skeleton)

**Headline:** Reuse the gradient's data, and look up footprints in a table.

| in / out | what | why |
|---|---|---|
| in | x, y positions, slot order, 16 per DDR beat | the same arrays the gradient unit reads |
| in | one footprint index per slot | points into an on-chip table of distinct sizes, as pin records point into the offset table |
| in | footprint table | cells come in few sizes; every filler shares one entry (15 entries on the test design) |
| in | fixed-node base map, already capped | computed once by the host |
| out | rho, 1024×1024, column-major | same layout as the software golden |

- Slots that hold no cell of their own (external copies, pin slots, padding) carry `NO_DEPOSIT`, so the unit
  streams the whole slot range with no filtering.

*Visual:* block diagram, DDR (positions, indices, table, base map) → `density_map_CU` → rho → AIE FFT.

---

## Slide 4 — Status: a correct baseline to optimize from

**Headline:** The contract is fixed and tested; the body is still the plain loop.

- Tier-1 harness vs a double-precision golden, at 128 and 1024 bins:
  **rel_rms 2.9e-6**, worst bin 8.3e-6 of peak, total area conserved to 1.6e-7.
- Test design covers a cell library, movable macros, cells hanging off the die edge, fillers, and
  skipped slots (filled with NaN, so a slot read by mistake shows up).
- Four deliberate bugs (offset, weight, skipped slots, normalization) are all caught.
- Not yet synthesized. Commit `2dad68f`.

*Visual:* small results table (check, 128 bins, 1024 bins, bound).

---

## Slide 5 — Plan: from one cell at a time to faster than the GPU

**Headline:** Keep the grid on chip, as we did for the gradient, and process several cells per cycle.

- **On chip:** a 1024² fp32 map is 128 of 463 URAMs, so the whole grid stays on chip. Today's legacy
  module re-reads every cell 16 times (strip by strip); this reads each cell once.
- **One cell per cycle:** interleave the grid across 4×4 banks, so a cell's whole ≤3×3 window is
  written in one cycle.
- **No read-modify-write stall:** accumulate in fixed point (one-cycle integer add,
  order-independent), as the gradient already does for HPWL.
- **Faster than the GPU:** one cell per cycle at ~300 MHz is ~10 ms on bigblue4, slower than the
  GPU's 4.2 ms scatter. That needs **≥4 cells per cycle**: private copies at 512, row bands at 1024.
- **Later:** 2048 grids by row-band tiling; the gather (the mirror of the scatter, read-only) next.

*Visual:* roadmap, skeleton → banking → fixed point → lanes → 2048 tiling.

*Notes:* the ~10 ms is an estimate from cell count and clock, not a measurement. bigblue4 at a 1024
grid lost only 0.16% final HPWL vs 2048 (one seed, `results/DSE_20261013_130904/`), which is why
1024 is the v1 target; 2048 is still required for modern designs.

---

## Open questions to mention

- Footprint table size: 4096 is assumed, not yet counted on the 44 designs (MMS macros are the risk).
- The overflow metric needs a second map without fillers: a second accumulator or a second pass.
- The rho layout the AIE FFT should consume is still to be agreed with Geert.
