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

## Part 2 — the host→device record (Mark, 2026-09-22, same day)

**Decisions:**
- Positions go on chip too. That removes the refresh gather, the other half of the same wall.
- The DDR stream becomes static: it carries indices, not positions.
- Names: `node_slot`, `offset_idx`.
- "Assume it fits" covers URAM node capacity only; chunking comes later. The offset encoding
  must not add a second size limit.

**Record:** `node_slot << offset_bits | offset_idx`, 32 bits, 16 per beat, one stream per axis.
- The bit split is a per-design register.
- The flags (EMPTY, fixed, repeated node) are all comparisons on `node_slot`, so they cost no bits.
- Fixed pins are pre-resolved into their own slots. See [[vck5000/bring_up/beat_packer/README.md]].

**Result.** 41 of 44 designs pass the full encode→decode round trip at ≥99.64% efficiency.
Raw table: `.claude/2_ARTIFACTS/beat_packer/run_records_banks32_h4.txt` (32 banks, H=4).

| suite | offset bits | node bits | record bits |
|---|---|---|---|
| ISPD2005 (8) | 6–7 | 18–22 | 25–29, **all fit, bigblue4 included** |
| ISPD2015 (20) | 3–6 | 15–21 | 19–27, all fit |
| MMS (16) | 8–13 | 18–22 | 28–35. **bigblue3/4 and newblue7 don't fit** (33–35) |

What moved the offset count:
- **Fixed-pin slots.** ISPD2005 went from 559–3,635 distinct offsets (≤12 bits) to ≤127 (7 bits).
  The cost is more fixed position slots: bigblue2 goes from 20 K to 103 K. The URAM count doesn't
  change (192), because fixed rows fill otherwise-empty depth.
  Without it: `run_records_banks32_h4_fixednodes.txt`.
- **Real LEF offsets.** The superblue "22 K offsets" were (master, pin) keys over an obfuscated
  5,677-master library. The actual distinct values are 9–42 on movable cells.

**The one remaining offset pressure is MMS movable macros.** Their pins can't be pre-resolved,
because they move, so they keep 11–13 offset bits. Combined with node counts past 1 M (21–22
bits), 3 designs overflow, and those same 3 are also over the URAM budget. A later lever that
adds no new limit: give each movable-macro pin its own slot, refreshed per iteration from
the macro position, with its gradient summed back into the macro. This is a small side pass
over macro pins only.

**URAM** (2 floats per 72-bit word, 32 banks, whole URAMs per bank): 64–192 URAMs for every
design up to about 700 K nodes. The 8 MB budget is about 222 URAMs. Designs at 256 or more
need chunking: adaptec5, superblue11_a, bigblue3, superblue12, newblue5/6 at 256–320, and
bigblue4/newblue7 at 576–640. Physical URAM is 4K×72 only, so 1 float per word would waste 56%.

## Open
- The hazard H is still a placeholder. Take it from synthesis (URAM read latency plus
  pipeline registers). The sweep says H≤16 costs <1.6%.
- Chunking (URAM capacity, and 17..100-pin nets) comes as one extension. `node_slot` bits and the
  movable-macro offsets both ride on it.
- `hpwl_computer`'s `resolve_beat` assumes only the last beat of a degree group is partial.
  With EMPTY lanes, `real_net_count` should come from the lanes instead.
