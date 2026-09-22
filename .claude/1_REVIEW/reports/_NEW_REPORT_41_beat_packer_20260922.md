# #41 — Pin→node gradient summation: on-chip scatter-add + host conflict-free packing (2026-09-22)

## Problem
`hpwl_computer` consumes 16 pins/cycle in net-major order. The per-node gradient needs the pin
gradients summed in node-major order, a permutation of 16 values/cycle = **4.8 G accesses/s per
axis at 300 MHz**. Through DDR, either as a scatter (Option A) or a gather (Option B), that is a
random access. The measured rate for exactly this access pattern on this card is
`hpwl_pl/HANDOFF.md` `node_pos`: **734 MB/s at 8 B ≈ 92 M accesses/s per port**, rising in latency
with volume. That is a 10× (platform ceiling) to 50× (measured) shortfall. A vs B is a
second-order choice on a first-order wall. The refresh gather (node positions → net-major pins)
is the same permutation run in reverse.

## Decision (Mark, 2026-09-22)
- The permutation moves on chip: `node_grad` (and `node_pos`) held in banked URAM, one axis at a time.
- The host precomputes a conflict-free packing (this prototype). The netlist is static, so it is a one-time setup cost.
- Float accumulation for now.
- URAM budget 8 MB. Budgeting against density's bin scatter is deferred until the whole-iteration dataflow is understood.

## Prototype
[[vck5000/bring_up/beat_packer/README.md]] covers the model, the constraints and the checker. It
works in three steps: color nodes into banks so each net's pins are distinct → pack same-degree
nets with disjoint bank masks → order beats around the RMW hazard. An independent checker
re-derives every invariant and was mutation-tested against 3 injected bugs, catching all three.

Raw tables (gitignored): `.claude/2_ARTIFACTS/beat_packer/run_{banks16,banks16mergedups,banks32mergedups}.txt`.
Basis: all 44 manifest designs, nets of degree 2..16 only, hazard H=4, window 4096, seed 1.

| config | min efficiency | typical | what limits it |
|---|---|---|---|
| 16 banks | 72.8% (mms/adaptec1) | 80–98% | same node with 2+ pins on one net: ~10% of ISPD2005 nets |
| 16 banks + merge-dups | 94.3% (pci_bridge32_a) | 97–99% | packing loss of 1–6% (first-fit on masks) |
| **32 banks + merge-dups** | **99.69%** | 99.9–100% | nothing material |

- **Duplicate pins were the surprise.** In adaptec1, 21,433 of 221,142 nets (24,352 extra pins)
  have one cell with two or more pins on the net. No coloring can separate a node from itself, so
  without a pre-crossbar lane merge it is the dominant cost.
- **Coloring before packing, not after.** Packing first and then coloring plateaued at 67% (16 banks)
  and 81% (32 banks) on adaptec1 even with min-conflicts repair. See the README for why.
- **Hazard tolerance is large.** At 32 banks + merge, H=8 costs ≤0.5% and H=16 ≤1.6%. The only
  losses are on the small ISPD2015 designs, at the tails of the degree groups. Big designs show 0
  bubbles even at H=32. The real RMW latency is a few cycles (see below).
- **Bank balance is perfect** (imbalance 1.000–1.001), so URAM depth = nodes / banks.
- **Runtime:** ≤9 s on the largest design (newblue7, 2.5 M nodes), single thread, unoptimized.

## Operator latency (the fixed-vs-float question)
From this repo's C-synthesis reports (xcvc1902, 3.33 ns clock): `fadd`/`fsub` map to the
**Versal DSP58 hardened FP32 mode** (`primitivedsp`) at **latency 0** (chained within a cycle),
and `fmul` at latency 1. Integer `add` is fabric, latency 0. So on Versal, float and fixed
point cost about the same in the RMW loop, unlike UltraScale+, where a fabric `fadd` takes
around 7–11 cycles. The difference is resources: each float add uses a DSP, an int add uses LUTs.
The RMW round-trip is dominated by URAM read latency, not by the add.

## Capacity against the 8 MB budget
`URAM_MB` = two float arrays (pos + grad) × one axis. Designs over 8 MB: bigblue3 (8.75),
superblue12 (10.3), newblue5/6 (9.8/10.0), bigblue4 (17.3), newblue7 (19.8). With the grad
accumulator only (8.6 MB for bigblue4), only bigblue4 and newblue7 stay over.
⚠️ The column counts only nodes that touch a ≤16-pin net. Once 17..100-pin nets are chunked
(20–29% of ISPD2005 pins), more nodes need slots.

## Open
- Pos on chip too, or grad only? This decides which designs need K=2 range tiling.
- Should 17..100-pin chunks pack under the same constraints? Presumably yes, as chunk beats.
- Emit the renumbering and per-lane records (node slot, offset, merge flag) as the real
  host→device format.
