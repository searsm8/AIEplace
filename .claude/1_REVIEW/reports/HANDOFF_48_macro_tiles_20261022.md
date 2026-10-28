# HANDOFF #48: deposit wide footprints (macros) as 4×4 tiles through the producers

*2026-10-22. Branch `pl_algo`. Design agreed in discussion with Mark; nothing built yet. Evidence:
[[REPORT_48_xplace_scatter_gather_suite_20261020.md]] (T3), [[PLAN_48_bin_density_lanes_20261020.md]] (round 3).*

## Problem

Footprints wider than 2.99 bins (`densrec::fits_window` false: movable macros, big cells) are NO_DEPOSIT
in the fast stream and go through `deposit_slow` (`pl/src/modules/bin_density_CU.hpp`): one bin at a
time, not pipelined. They are ≤ 0.5 % of slots but cover many bins. At our grid (≤ 1024²), MMS during
phase 1 (macros movable; `.claude/2_ARTIFACTS/xplace_profile_suite/macro_bins.py`, estimate
(w/bw+1)(h/bh+1) bins each):

| design | wide footprints | largest (bins) | bins covered | cycles @1 bin/cyc | @64 bins/cyc | cells @8/cyc |
|---|---|---|---|---|---|---|
| adaptec2 | 956 | 151,182 | 663 K | 663 K | 10.4 K | 76 K |
| newblue2 | 4,933 | 64,067 | 575 K | 575 K | 9.0 K | 118 K |
| newblue3 | 51 | 41,426 | 792 K | 792 K | 12.4 K | 244 K |
| bigblue4 | 273 | 31,811 | 348 K | 348 K | 5.4 K | 694 K |

At 1 bin/cycle wide footprints cost up to 8.8× all the cells (adaptec2); at the banks' 64 bins/cycle,
1-14 %. Phase 2 freezes macros into the fixed base map, so this is MMS phase 1 only. XPlace has the
same problem on the GPU (one thread walks a whole macro in its gather: 11.9 ms vs 0.097 ms per launch).

## The design

**A tile** = the part of a wide footprint's bin window inside one 4×4 block of bins aligned to the bank
grid (columns 4i..4i+3, rows 4j..4j+3). A small **tiler** in front of the producers turns each wide
footprint into tile items: (slot, footprint index, tile col0, row0), one per 4×4 block its window
touches (a counter over col_lo/4..col_hi/4 × row_lo/4..row_hi/4).

**The producer** handles a tile item like a cell, except its window is the footprint window clipped to
the tile's 4 columns and 4 rows: `col_lo = max(col_lo, col0)`, `col_hi = min(col_hi, col0+3)`, same for
rows. Every overlap is the golden's own float expression (`produce_cell`,
`pl/synth/bin_density_producer/producer.hpp`), so the result stays **bit-exact** with no new arithmetic.
Up to 16 updates per tile, one per bank.

**Why it suits the bank FIFOs:** in the 8×8 interleave (`pl/synth/bin_density_banked/banked_top.cpp`,
bank = 8·(col%8) + row%8), the four 4×4 tiles of one 8×8 block hit every one of the 64 banks exactly once.
Issue a macro's tiles block by block, spread over the 8 producers: 8 tiles = 128 updates/cycle offered,
64 drained, so the banks run saturated and the FIFOs absorb the excess. Macros are the best-balanced
input the bank array sees.

## Open points for the next session

1. **Producer interface:** add a tile origin (or a "clip" range) to `SlotIn`; cells carry "no clip".
   Decide whether the tiler emits into the same slot stream (merged) or a second input per producer.
2. **Ordering against the overflow barrier:** cells (incl. cell tiles) before the excess reduce, fillers
   after, as today. Fillers are never wide, so only the cell phase needs tiles.
3. **Gather (force) side is NOT a straight copy:** the force on a macro sums E over all its tiles. If the
   tile order differs from the golden's column-major bin order, float sums differ. Either make the golden
   sum in tile order or accumulate the macro's force in fixed point. Decide before building the gather.
4. **Cheaper alternative, not chosen:** interior bins are fully covered, but the golden's
   `(col·bw + bw) − col·bw` is not exactly `bw` in float, so interior increments are not constant. A
   multiplier-free macro unit needs a golden change first (sw_flat, drift measured).

## Steps

1. Model: add tile items to `host/sw_flat/model/bank_fifo_model.cpp` (MMS phase-1 positions), measure
   cycles with and without macros on mms/adaptec2, newblue2, newblue3, bigblue4.
2. sw_flat: a tiled deposit of wide footprints, held bit-equal to `deposit_slow` / the golden on 44/44
   (`make -C host/sw_flat check-bin-density`).
3. Producer + tiler in `pl/synth/bin_density_banked`; C-check bit-exact; csynth cost of the clip logic.
4. Replace `deposit_slow`.
