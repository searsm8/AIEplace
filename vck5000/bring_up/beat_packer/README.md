# beat_packer -- host prototype: conflict-free beat packing for on-chip scatter-add (#41)

Plain g++, no Vitis, no XRT. Reads a real netlist and produces the pin-beat stream that
`hpwl_computer` (and its gradient successor) consumes, arranged so the per-pin gradients can be
**scatter-added into banked on-chip URAM at one beat per cycle** with no runtime arbitration.
Evidence and numbers: [[_NEW_REPORT_41_beat_packer_20260922.md]].

## Hardware model it packs for
- `node_grad[axis]` (and `node_pos[axis]`) live on chip, split over `B` URAM banks.
  Node `v` lives in bank `bank(v)`; the host renumbers nodes so address = (bank, slot).
- Each beat = 16 lanes = the pins of `16/d` same-degree nets (hpwl_computer's layout; degree
  groups contiguous and ascending, the `resolve_beat` contract).
- A 16→B crossbar routes each lane's gradient to its node's bank. Each bank does one
  read-add-write per cycle (URAM: port A reads, port B writes).
- Fixed pins have no bank: their position is a constant in the stream, and their gradient is dropped.
- `--merge-dups`: a node with k pins on one net occupies k lanes but gets **one** update. The host
  places those pins in adjacent lanes and flags them, and the hardware adds them before the crossbar.

## The three constraints and how each is met
| # | constraint | why | met by |
|---|---|---|---|
| 1 | movable pins of one **net** hit distinct banks | a net always shares a beat with itself | step 1: color nodes (Welsh-Powell, least-loaded bank, then min-conflicts repair) |
| 2 | nets sharing a **beat** have disjoint bank masks | one RMW per bank per cycle; also rules out one node in two nets of a beat | step 2: first-fit packing on 16/32-bit bank masks, `--window` nets of lookahead |
| 3 | a node is not updated again within `H` cycles | the RMW round-trip (URAM read + fadd + write) must finish first | step 3: list-schedule beats inside each degree group; bubble if nothing in the window is ready |

Coloring **before** packing matters: coloring to fit a fixed packing needs every full beat to be a
perfect 16-way permutation, and it plateaued at 67–81% efficiency. Coloring first only constrains
nets (which is easy), and the packer then has all of a degree group to choose disjoint masks from.

Violations are not fatal to the hardware; they cost cycles, and the tool counts them:
`stalls` (extra cycles when a bank is hit twice in a beat), `bubbles` (hazard waits), and
`packloss` (beats shipped part-empty). `effic` = ideal beats / total cycles.

## Verification
`check()` re-derives every property from the emitted cycle sequence and bank map, independently of
the construction code. It checks: each in-scope net issued exactly once, degree order, beat
capacity, no node in two nets of a beat, stall cycles equal to the bank multiplicity, and no RAW
hazard. The exit code is non-zero on any violation. Mutation-tested 2026-09-22 against three
injected bugs: a scheduler off by 3 on the hazard, stalls not emitted, and the mask test removed.
All three were caught.
The bookshelf reader asserts parsed net/pin counts equal the `.nets` header.

## Run
```bash
make run                                   # all 44 manifest designs, banks=16
make run ARGS="--banks 32 --merge-dups"    # the configuration the report recommends
```
Scope: nets of degree 2..16. Nets >100 are masked (XPlace's ignore_net_degree). Nets of 17..100
are out of scope until chunking (#40 plan); the `pin>16` column shows their share of pins.
