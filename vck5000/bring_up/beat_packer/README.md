# beat_packer -- host side of the pin-record stream (#41)

Plain g++, no Vitis, no XRT. Encodes a netlist as the **static per-axis record stream** that the
gradient datapath consumes. The stream carries no positions: the device reads node positions
from on-chip URAM (gather) and accumulates gradients back into URAM (scatter-add), both
addressed by the record. Evidence and numbers: [[_NEW_REPORT_41_beat_packer_20260922.md]] (design,
Parts 1–2) and [[_NEW_REPORT_41_record_datapath_20260922.md]] (the device modules and chunking).

| file | what |
|---|---|
| `pin_record.hpp` | **the protocol**: constants, record encode/decode, the DDR beat structs, the macro-pin and chunk descriptors. HLS-safe and shared by host and device, so neither can drift. |
| `beat_packer.hpp` | host library: parse → encode → position image / macro-pin list / chunked device arrays → decode checker |
| `beat_packer.cpp` | CLI over the library: encode the manifest, verify each design by decoding it, report |

Consumers: `bring_up/hpwl_computer_v2`, `hpwl_gradient_computer`, `hpwl_computer_v3` and
`hpwl_gradient_computer_v2`, with tier-1 harnesses `vck5000/test/hpwl_*computer*_test.cpp`.

## The record: 32 bits per lane, 16 lanes per 512-bit beat
```
record     = node_slot << offset_bits | offset_idx        (offset_bits: per-design register)
node_slot  = row * 32 + bank          bank = node_slot & 31, URAM row = node_slot >> 5
offset_idx = index into this axis's on-chip table of distinct pin offsets
```
Per lane the device computes `pin_pos = pos[node_slot] + offset_table[offset_idx]`, then the
trees and combiner, then `grad[node_slot] += g`. There is one stream per axis; the two differ
only in `offset_idx`.

Flags cost no bits:
- **EMPTY lane:** the all-ones record (its node field is the maximum, never assigned). No read, no
  contribution, no write.
- **Fixed:** `node_slot >= first_fixed_slot`. Read the position, skip the gradient write.
- **Repeated node on one net:** equal `node_slot` in adjacent lanes. The lanes share one read and
  are merged by a segmented scan before the scatter.

Node kinds (every macro and fixed pin gets its own slot, so offset tables hold only the cell
library's pin geometry: at most 131 values over all 44 designs):

| kind | slot holds | gradient |
|---|---|---|
| CELL | node position | accumulated |
| MACRO (movable) | macro position | = Σ of its macro-pin slots (fold) |
| MACRO_PIN | refreshed on chip: `pos[macro] + offset` | folded into the macro |
| FIXED_PIN | constant `pos + offset` (host) | not written |

## The host guarantees, per beat
| # | rule | met by |
|---|---|---|
| 1 | a net's distinct nodes use distinct banks | coloring (Welsh-Powell, least-loaded bank, min-conflicts repair) |
| 2 | nets in one beat have disjoint bank masks | first-fit packing on bank masks |
| 3 | a movable `node_slot` is not repeated within `HAZARD_DISTANCE` positions | list scheduling; an all-EMPTY bubble if nothing is ready |

## Chunking (designs larger than one on-chip slot space)
`encode_chunked(nl, cfg, capacity)` splits the design into K chunks:
- Every movable node is **owned** by one chunk (a macro together with its pins). Chunks are cut
  from a breadth-first locality order.
- Every net is **homed** in the chunk owning most of its nodes.
- A homed net's foreign nodes become **ghost** slots, and fixed pins are copied.

Each chunk is an ordinary stream over its own local slots. Ghosts travel through one DDR exchange
buffer laid out consumer-major (a region per consumer, a block per producer). Positions go out as
sequential blocks and come in as one sequential region; gradients return the same way.
`check_chunked` verifies decode, ownership, capacity, exchange consistency and the fold hazard.
At the real 1 M-slot capacity all 44 designs pass. Eight need K=2–3, with ghosts at 4–19% of
movable nodes.

## Verification
- `check()` / `check_chunked()` decode the streams using only what the device sees. They require
  the decoded geometry (node as parsed, total offset) to equal the parsed netlist's in-scope nets.
- The device harnesses compare against goldens computed from the parsed netlist directly:
  bit-exact per-net HPWL, and the double-precision WA gradient.

## Run
```bash
make run                               # all 44 manifest designs, one chunk each where they fit
make run ARGS="--capacity 1048576"     # chunk every design to the on-chip capacity
```
Scope: nets of degree 2..16, plus 17..96 with `Config::large_nets` (opt-in; only
`hpwl_computer_v2` consumes them so far, and `encode_chunked` does not carry them yet).
Nets over 100 pins are masked (XPlace's `ignore_net_degree`). **Nets of 97..100 pins are dropped:
a deliberate divergence from XPlace and sw_only (Mark, 2026-10-02)**, since 96 = 6 full beats.

## Large nets (`Config::large_nets`)
After the degree-16 group, each large net takes `span` consecutive beats, one net per beat.
- **Lanes:** a beat's pins start at lane 0 and EMPTY lanes only trail. Within a beat, banks are
  distinct and a node's pins are adjacent.
- **Split nodes:** a node with more than 16 pins on the net is split into ≤16-pin runs. Its runs
  share a bank, so they land in different beats. MMS has cells and pads with up to ~36 pins on a
  2–3-node net.
- **Span groups:** nets are grouped by span 2..`MAX_SPAN` (8). The cumulative
  `span_beat_count[SPAN_GROUPS]` continues `beat_count`, so the device finds a net's last beat
  with a counter.

Keeping spans at their minimum ⌈degree/16⌉ takes two steps:
- **Coloring:** large nets are a soft per-bank cap of ⌈degree/16⌉ nodes. They rank below every
  small-net constraint and above load balance.
- **Packing:** open the minimum number of beats, then place runs busiest-bank first into the
  least-filled beat that fits.

Over all 44 designs: **48 excess beats in 1.58 M (0.003%), 0 nets dropped**. Before the cap and
the balanced packing, about half the nets took an extra beat. The small-net stream is unchanged
(the coloring is bit-identical with the flag off).

A net needing more than `MAX_SPAN` beats is dropped and counted in `large_dropped`. No hazard
scheduling applies, because no consumer writes gradients from these beats yet.
