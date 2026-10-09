# REPORT #45 step 4: the whole-design slot layout

*2026-10-09. Branch `pl_algo`. Plan: [[_NEW_PLAN_45_integration_20261006.md]] step 4. Status: [[tasks.md]] #45.*

## What landed (all in `host/sw_flat/`)

| file | what |
|---|---|
| `fillers.hpp` | `make_fillers`: sw_only's `DataBase::addFillers` on a FlatDesign (count, size, effective target density); `effective_target_density` (placement.constraints overrides the config, as in Setup) |
| `slot_layout.hpp` | `build_slot_layout`: global slot = `chunk_first_slot[k]` + chunk k's local slot, then the filler tail. Per slot: `role`, `node`, `width`/`height` (non-zero only on slots that deposit density). Per node: `home_slot` |
| `test/slot_layout_check.{cpp,sh}` | the gate, `make -C host/sw_flat check-layout` (~3 min, 44 designs) |

Roles: `OWNED` (a movable node's one slot), `EXTERNAL` (a copy of a node owned in another chunk,
packer rule C7), `MACRO_PIN`, `FIXED_PIN`, `FILLER`, `EMPTY` (row padding). The density path
streams the whole range and deposits `OWNED` and `FILLER` only.

## A deviation from the plan: netless nodes are NOT in the tail

The plan put netless movable nodes in a density-only tail together with the fillers. Its premise
was that "`assign_node_slots` skips any node with no in-scope pin (`NO_BANK`)". That is **no longer
true**: `assign_banks` gives every movable node a bank, and a node on no net gets the least-loaded
one. So every movable node, netless or not, already owns a slot in its chunk, and the tail holds
only fillers.

Leaving them there is the simpler choice: the chunking is unchanged by construction, and a netless
node's gradient slot just stays 0. The cost is the URAM capacity they occupy, at most 3,560 slots
(mgc_superblue14, 0.58%; 16 of 44 designs have any). The change is cheap to reverse (drop them from
`to_netlist`'s node set) if a chunked design ever needs the room.

## Verification (`.claude/2_ARTIFACTS/scratch_45/layout44.txt`)

- **44/44 PASS.** Per design:
  - **[fillers]** count, width, height and effective target density bit-equal to `DataBase::addFillers`. adaptec1: 160,067 fillers, the number that matches XPlace exactly. On 3 designs the target density is raised to the standard-cell utilization and the result is 0 fillers (mgc_des_perf_b, mgc_matrix_mult_1, mgc_superblue14).
  - **[layout]** node↔slot bijection over movable nodes and fillers. Externals are copies owned in another chunk and sit in that chunk's tail rows. Pin slots point at the right kind of node. Sizes appear only on the slots that deposit density.
  - **[self-test]** 5–7 corruptions per design (two slots owning one node, an external marked owned, a dropped filler, a pin slot given a size, filler count +1, …). Every one was caught on every design.
- **Chunking unchanged.** Chunk counts equal the step-3 sweep on all 44. External counts equal
  HEAD's `hpwl_gradient_CU_test` on all 44 (checked directly on mms/bigblue3 54,446 and mms/newblue6
  71,110). They differ from `sweep44_netlists.txt` on 5 MMS designs only because that sweep
  (12:46) predates #47 B1's rule C6 (13:12), which lowered externals on macro designs.
- `make -C pl test` PASS. sw_only untouched.
- The first run of the script found 0 designs (a quoting bug) and still printed "ALL PASS". It now fails on an empty design list.

## For step 6: the filler tail is as big as the netlist

| design | slots | in chunks | fillers |
|---|---|---|---|
| ispd2005/bigblue4 | 5.50 M | 2.62 M | 2.89 M |
| mms/bigblue4 | 5.92 M | 2.54 M | 3.39 M |
| mms/newblue7 | 6.15 M | 2.79 M | 3.35 M |
| mgc_fft_a | 65.5 K | 34.6 K | 30.9 K |

The density path streams roughly 2× the gradient's slot count. The fillers have no URAM home in any chunk, so their
positions are DDR-resident and stream like the density maps. Size the density modules' bandwidth on
the whole range, not on the gradient's.

## Next
Step 5: `sw_flat`, CPU phase-1 GP on these arrays. Filler positions (XPlace `get_filler_pos`,
sw_only's `rand() % die`) and the per-node preconditioner weight belong there, not in the layout.
