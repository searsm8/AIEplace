# EXPLAINER #48: how `bin_density_CU` fits together — slide-deck brief (6 slides)

*2026-10-22. The bank-FIFO design (Mark's FIFO in front of every bank), as built and synthesized in
`pl/synth/bin_density_banked/` and `pl/synth/bin_density_producer/`. Measurements:
[[PLAN_48_bin_density_lanes_20261020.md]] (rounds 1-3, producer cost), [[REPORT_48_bin_density_fixed_point_20261019.md]]
(fixed point, forwarding). Supersedes the row-lane dispatcher of [[EXPLAINER_48_lane_dispatcher_20261020.md]].*

---

## Slide 1 — The whole CU, stage by stage

**Shows:** one row of boxes, left to right, with DDR at both ends.

```
 DDR: fixed base ─▶ [A] load base ─┐
                                   ▼
 DDR: cell slots ─▶ [B] splitter ─▶ [C] 8 producers ─▶ [D] 512 FIFOs ─▶ [E] 64 URAM banks
                                                                              │   (+ slow path for wide cells)
                                                                              ▼
                                                          [F] overflow reduce ─▶ excess (int64) ─▶ host
                                                                              │
 DDR: filler slots ─▶ [B]→[C]→[D]→[E] again (fillers on top of the cells) ◀───┘
                                                                              ▼
                                                          [G] drain ─▶ DDR: rho ─▶ FFT (AIE)
```

**Say:** the CU builds the density map: how much cell area sits in each of the 1024 × 1024 bins.
- **A** starts every bin at its fixed-cell area (the host computes this once per design).
- **B–E** run once for the cells. Each cell becomes up to 16 "add this much to this bin" messages,
  and each bank adds them in.
- **F** reads the map once to compute the overflow, before any filler is added, as XPlace does.
- **B–E** run again for the fillers.
- **G** sends the finished map to DDR for the field solve.

Every add is an integer, so the order cells arrive in never changes the result: the CU equals the
software golden bit for bit (44/44 designs on today's 16-bank CU; the 64-bank deposit matches the plain
deposit on 1 M bins).

---

## Slide 2 — The dispatcher: who gets which cell, and which bank gets which amount

**Shows:** two halves. Left: the cell stream cut into 8 slices, one per producer. Right: inside one
producer, 16 amounts fanning out to 64 bank FIFOs.

**Say:** in this design "dispatching" happens in two places, and neither is a separate unit.

| | splitter (B) | fill step (in each producer, C → D) |
|---|---|---|
| **input** | the slot arrays in DDR: per cell x, y (floats) and a footprint index; 16 bytes a cell | one cell's window corner (first column, first row) and its 16 amounts |
| **output** | one cell per cycle to each of the 8 producers | up to 16 messages per cycle, at most one per bank: (address in bank, amount) into that bank's FIFO |
| **rule** | producer p reads its own slice of the slots on its own memory port | bank b = 8·(col mod 8) + (row mod 8) takes the window position ((b/8 − col₀) mod 8, (b mod 8 − row₀) mod 8) |
| **why it's simple** | slices are fixed; no routing by position, so no "which lane owns this cell" | the 8 × 8 pattern guarantees a ≤ 4 × 4 window hits 16 different banks, so there is never a collision inside one cell |

Each producer has its **own** FIFO into each bank (8 × 64 = 512), so every FIFO has one writer and nothing
arbitrates. A producer pauses only when one of its own FIFOs is full. (The earlier design routed cells to
row-owning lanes; that is what this replaced.)

---

## Slide 3 — Inside a producer, and why producers are most of the CU

**Shows:** the six steps (cell arrives → footprint lookup → corner bins → 4 column + 4 row overlaps → 16
amounts → 16 messages), with the last two shaded; beside it a cost bar.

**Say:** a producer is an assembly line, one new cell per clock:
1. read x, y, footprint index; 2. look up the footprint's half sizes and weight (a small table);
3. divide the edges by the bin size to get the first and last column and row; 4. measure how much of each
of the 4 columns and 4 rows the footprint covers; 5. for each of the 16 bins, width × height × weight,
rounded to an integer; 6. hand each amount to its bank's FIFO.

Where the resources go, per producer (C-synthesis estimate, II = 1, 141 DSP and 32 K LUT in the pipeline):
- **Step 5: ~39 float multiplies, 1 DSP each.** The 16 amounts are 2 multiplies each (ow × oh, then
  × weight); the rest are the overlap arithmetic.
- **Step 6, the bank addresses: 64 DSPs.** Each bank computes its address as (col/8) × (grid/8) + row/8, an
  integer multiply by a value only known at run time: one DSP per bank, per producer. Avoidable: compute
  the 4 column bases once per cell and let each bank pick one (→ ~77 DSP per producer).
- **Step 6, the bank selection: ~15 K LUT.** Each of the 64 banks picks one of the 16 amounts every cycle.
- **Step 3's 4 divides: ~7.8 K LUT and 2.7 K FF per producer, no DSPs** (Vivado synthesis: a producer is
  21.7 K LUT with divides, 13.8 K with multiplies). HLS's estimate had left the divider cores out entirely.
  Across 8 producers that is ~63 K LUT, 7% of the chip.
- **× 8 producers ≈ 1,136 DSP and ~316 K LUT: ~70% of the CU's logic and all of its DSPs.** The 64 banks
  with forwarding are ~63 K LUT and no DSP; the 512 FIFOs and glue the rest.

---

## Slide 4 — The memory side: banks, FIFOs and forwarding

**Shows:** one bank: 8 small FIFOs in, a priority pick, the URAM, a 6-entry "recent writes" box looping
back.

**Say:** a bank holds 16 K bins (one in 64, spread out by the 8 × 8 pattern), in 2 URAMs. Every cycle it
takes one message from the first of its 8 FIFOs that has one, reads the bin, adds, writes it back. The
write lands a few cycles after the read, so a second message for the same bin can arrive in between; the
bank keeps its last few writes in registers and uses those instead of the stale URAM value (forwarding,
proven in RTL on the 16-bank CU: one register short fails, the right depth passes). 64 banks × 2 URAMs =
128 URAMs, the same as one float per bin would have cost.

Throughput: a cell averages ~5.8 messages, so 64 banks could absorb ~11 cells per cycle; 8 producers make
at most 8. Modelled on five real designs: 5.2–8.3× faster than one cell per cycle (bigblue4 ~8×, ~2.3 ms
per map vs XPlace's ~4 ms scatter on a 3090).

---

## Slide 5 — What it costs, and whether it fits

**Shows:** a table, then a bar of the chip with the gradient CU and the density CU stacked.

C-synthesis estimates (VC1902: 1,968 DSP, 900 K LUT, 463 URAM), 1024 grid, everything at II = 1:

| | DSP | LUT | FF | URAM |
|---|---|---|---|---|
| 8 producers (incl. bank selection and addresses) | 1,136 | ~316 K | ~195 K | — |
| 64 banks with forwarding (drain) | 0 | 63 K | 49 K | 128 |
| 512 FIFOs, clear, write-out, glue | 0 | ~75 K | ~33 K | — |
| **bin-density deposit** | **1,136 (57%)** | **454 K (50%)** | **277 K (15%)** | **128 (27%)** |
| `hpwl_gradient_CU` (for comparison) | 708 (36%) | 279 K (31%) | 300 K | 64 |

Two known problems in these numbers: the 64 address multiplies per producer (~512 DSP across 8, removable),
and the drain misses 3.33 ns by ~2.1 ns (it needs a shorter path per stage). The divider cores are not
counted at all.

**Say:** alone, the deposit fits. Beside the gradient unit it would not: 1,844 DSP (94%) as estimated. With
the address fix, ~616 DSP for the deposit, ~1,324 (67%) with the gradient: tight but plausible. The
remaining levers are on slide 6.

---

## Slide 6 — Next steps

1. **Remove the 64 address multiplies per producer** (4 column bases per cell, picked per bank): ~−512 DSP.
2. **Close timing on the 64-bank drain** (−2.1 ns: break the per-stage path), then **prove the forwarding
   depth in RTL co-sim**, as for the 16-bank CU (C simulation cannot).
3. **Replace the dividers with multiplies** (−63 K LUT across 8 producers, measured): change the sw_flat
   golden's bin index to a multiply by 1/bin first and measure the drift (the C check already found 0
   differing bins on 200 K cells), then the device copies it exactly.
4. **Re-run the speed model** for this exact design (private FIFOs ⇒ effectively 8 pushes per bank per
   cycle, independent producers), so slide 4's numbers describe what was built.
5. **Make it fit**, cheapest first: (a) compute `ow × weight` once per column → 20 multiplies instead of 32
   (~40% fewer DSPs; changes rounding, so the sw_flat golden changes first and drift is measured);
   (b) cheaper bank selection (two small rotations instead of 64 sixteen-way picks); (c) 6 producers;
   (d) let the density and gradient CUs share the chip in time, since they run in different phases.
6. **Complete the CU:** fixed-base load, overflow reduce, slow path for wide cells and the filler pass in
   the 64-bank version; settle rho's output format with the FFT side (Geert).
7. **Fold it into `bin_density_CU`** and hold it bit-exact to sw_flat on all 44 designs
   (`make -C host/sw_flat check-bin-density`), plus the real-design fixtures (branch
   `bin-density-real-fixtures`, not yet merged).
8. **Then the force gather**, the mirror image (read bins, no adds): the other half of the 52% XPlace
   spends on scatter + gather.
