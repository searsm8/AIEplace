# beat_packer -- host prototype of the pin-record stream (#41)

Plain g++, no Vitis, no XRT. Reads a real netlist and encodes it as the **static per-axis record
stream** the gradient datapath consumes. The stream carries no positions: the datapath reads
node positions from on-chip URAM (gather) and accumulates gradients back into URAM
(scatter-add), both addressed by the record. Evidence and numbers:
[[_NEW_REPORT_41_beat_packer_20260922.md]].

## The record: 32 bits per lane, 16 lanes per 512-bit beat
```
record     = node_slot << offset_bits | offset_idx        (offset_bits: per-design register)
node_slot  = row * 32 + bank          bank = node_slot & 31, URAM row = node_slot >> 5
offset_idx = index into this axis's on-chip table of distinct pin offsets
```
Datapath per lane: `pin_pos = pos[node_slot] + offset_table[offset_idx]` → trees → combiner →
`grad[node_slot] += g`. There is one stream per axis; the two differ only in `offset_idx`.

Flags cost no bits, because each is a comparison on `node_slot`:
- **EMPTY lane**: `node_slot` = all ones (never assigned). No read, no contribution, no write.
  It fills the lanes left over by `16/d*d < 16` and the net positions of part-empty beats.
- **Fixed**: `node_slot >= first_fixed_slot` (register). Read the position, skip the gradient write.
- **Repeated node on one net**: equal `node_slot` in adjacent lanes. Share one read, and
  pre-add the lanes before the crossbar.

Side data, tiny and loaded once: `beat_count[15]` (cumulative per degree, `resolve_beat`'s
contract), the two offset tables, `offset_bits`, and `first_fixed_slot`.

**Fixed pins are pre-resolved.** Each distinct (fixed node, offset) becomes its own fixed
position slot holding `node_pos + offset`, with `offset_idx` = 0. The offset tables then hold
only movable pins' offsets, which are bounded by the cell library rather than the macro pin
count: ≤127 distinct on ISPD2005, ≤42 on ISPD2015.

## The host guarantees, per beat
| # | rule | why | met by |
|---|---|---|---|
| 1 | a net's distinct nodes use distinct banks | a net always shares a beat with itself | color nodes (Welsh-Powell, least-loaded bank, min-conflicts repair) |
| 2 | nets in one beat have disjoint bank masks | one URAM access per bank per cycle | first-fit packing on bank masks |
| 3 | a movable `node_slot` is not repeated within `hazard` positions | the read-add-write must land first | list-schedule inside each degree group; all-EMPTY bubble if nothing is ready |

## Verification
`check()` decodes the streams using only what the device sees (records, `beat_count`, bit
split, offset tables, `first_fixed_slot`) plus the slot→node inverse. It requires the decoded
**geometry**, meaning (node as parsed, total offset) for every pin, to equal the in-scope netlist
as parsed *before* the fixed-pin rewrite. It also checks rules 1–3, EMPTY placement, adjacency
of repeated nodes, x/y slot agreement, and movable/fixed slot ranges. The exit code is
non-zero on any violation. Mutation-tested 2026-09-22 against 6 injected bugs, all caught:
hazard off by 3, weakened mask test, unsorted pins, y using x offsets, and two kinds of wrong
fixed-pin rewrite. A control mutant that only weakens the checker passes.

Offsets: bookshelf from `.nets`; DEF from `cells.lef`, using the sw_only parser's rule (center of
the first RECT of the first PORT).

## Run
```bash
make run                        # all 44 manifest designs, banks=32 hazard=4
make run ARGS="--hazard 8"
```
Scope: nets of degree 2..16. Nets >100 are masked (XPlace's ignore_net_degree). Nets of 17..100
are out of scope until chunking; the `pin>16` column shows their share of pins.
