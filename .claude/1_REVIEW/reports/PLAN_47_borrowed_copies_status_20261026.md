# PLAN #47 — borrowed copies: what is built, what is decided but not built (2026-10-26)

Re-read of [[PLAN_47_mailbox_redesign_20261006.md]], [[EXPLAINER_47_tail_exchange_deck_20261009.md]]
and tasks.md #47 against the code at the "tail" → "borrowed" rename (this date). Names below are the
new ones: `borrowed_copies.hpp` (was `tail_exchange.hpp`), `send_borrowed_pass` / `merge_borrowed_pass`,
`BorrowedBlockDesc`, `host/sw_flat/borrowed_fusion.hpp` (M2), `model/borrowed_layout.cpp` (M1).

## Mark's suspicion is right
The plan's headline was **"remove passes, not just make the loops faster"** (PLAN_47, "Where the time
goes"). What is built removed the mailbox's receive / return / collect loops, but **both extra passes
are still there**: `hpwl_gradient_CU` still runs `send_borrowed_pass` (reload the owner's shared rows,
replay send ops) and `merge_borrowed_pass` (reload, replay merge ops, drain). The piece that removes
them, B4 = A, exists only as a software golden (M2) and a cycle model (M0).

| bigblue4, 1 M slots, cycles per axis (cycle model) | |
|---|---|
| mailbox, before #47 | 2.49 M |
| B3, built | 1.93 M |
| **C8, built (today)** | **1.57 M** |
| B4 = A, decided, not built | 0.98 M (62.6% of C8) |

So roughly 0.6 M cycles per axis on bigblue4 (~37% of today's chunked evaluation) is the unbuilt part.

## Built and verified
| piece | where | verified |
|---|---|---|
| B1, rule C6: macro pins never borrowed | packer, `hpwl_gradient_CU` | tier 1, 8/8 real, mutants |
| B2, rule C7: borrowed rows after the owned rows | `assign_node_slots`, `order_borrowed_blocks` | tier 1, 8/8 real |
| B3: owner sends / merges through a 32 × 256 row buffer, host-scheduled ops | `borrowed_copies.hpp`, `BorrowedBlockDesc`, `borrowed_block_ops` | tier 1, C-synth II=1, RTL co-sim, real card at 200 MHz |
| C8: shared-first owned rows, so the passes reload only the prefix | `first_private_slot` | tier 1, mutant [5], co-sim |
| M0: cycle model TODAY / FUSED | `model/cycle_model.hpp` | equals legacy model 8/8 |
| M1: borrowed-row padding and assembly-buffer depth | `model/borrowed_layout.cpp` | measured on M0's layout |
| M2: golden of A, entry at a time | `borrowed_fusion.hpp` | bit-identical to B3, 8/8 real, mutants |
| decision 3: bring-up shortcut | `chunked_device_arrays(..., fill_borrowed)` | `borrowed_copies_test` [2] |

## Decided but not built
Each line: the piece, then what it blocks on.

1. **Send rides on the update (R1).** `iteration_update`, streaming owner j's owned beats, writes each
   shared slot's new position into every borrower's borrowed block: K−1 output streams, each through
   a per-borrower assembly buffer taking up to 4 entries per cycle (≤ 162 rows × 32 floats, BRAM).
   *Blocks on #45 step 6* (no slot-order `iteration_update` exists; only the legacy node-ordered one
   in `vck5000/`).
2. **Merge rides on the update (R4).** Before moving owner j, the update reads each borrower's drained
   block from `grad_DDR` (K−1 input streams), puts it back in owner-slot order (the 32 → 4 gather),
   and adds it into j's gradient in **ascending borrower order** (bit-identity with B3 and M2). Two
   adders per lane, and they must be a chain, not a tree, or the order breaks. *Blocks on step 6.*
3. **The host's beat-level steering for A.** PLAN_47 R4: "per-beat counts the host computes
   statically (how many entries each parcel contributes to each 16-slot beat, and to which lanes)".
   Today's ops (`BorrowedBlockDesc`, ENTRY / BORROWED_BEAT) are one entry per cycle, made for B3's
   replay; A's update moves 16 slots per cycle and needs a different list. *Not blocked:* the owned
   beats' order is already fixed by the slot layout.
4. **`hpwl_gradient_CU` loses `send_borrowed_pass`, `merge_borrowed_pass`, the `chunked` branch and the
   `borrowed_blocks` / `borrowed_ops` ports.** *Blocks on 1-2 landing* (until then nothing else does
   the exchange).
5. **M3 verification:** tier 1 per module against M2; mutants for wrong parcel order, merge count off
   by one, a skipped stream. *Blocks on 1-2.*
6. **M4: RTL co-sim of the merge-add** with the hazard negative control. *Blocks on 2.*
7. **M5: synthesis of the send and merge** (II=1 at 16 slots/cycle, LUT cost of the lane steering
   vs ~32 K LUT per widened loop measured in #42). *The integrated version blocks on step 6; a
   standalone block does not (item C below).*
8. **DDR ports and bandwidth for K−1 concurrent streams each way** (5 at 512 K, K=6). The model
   charges the block beats serially "as if DDR-bound"; port count and contention are not decided.
   *Blocks on step 6's own port budget.*

## Can be built now, without #45 step 6
- **A. Re-run M1 on the as-built layout.** `borrowed_layout.cpp` builds M0's layout itself, before
  B3's one-block-per-owner rows and before C8 reordered the owned rows. C8 changed the owner's
  emission order, so the assembly-buffer depth and the 4-writes/cycle stall figures A will face are
  unmeasured. Read `Chunk::borrowed_blocks` instead. Cheap.
- **B. The beat-level op format (item 3)** in `pin_record.hpp`, generated by the packer, and replayed on
  node ids in `check_chunked`, the way B3's ops are today.
- **C. A beat-level golden plus standalone HLS blocks.** Extend `borrowed_fusion.hpp` to run 16 slots
  per step with the 4-wide assembly buffer and the 2-adder chain, asserting bit-identity with M2 and
  counting stalls (today the model counts stalls only). Then `borrowed_send` / `borrowed_merge` as
  stream-in / stream-out HLS functions on `SlotBeat`, tier-1 against that golden, and `synth_check` for
  II=1 and LUT: M5's measurement, early. Wiring them into `iteration_update` waits for step 6.
- **D. A test of the pass-less CU:** compute pass only, with the exchange done by M2 around it (the
  `fill_borrowed` shortcut for positions), checking the whole gradient bit-exact. This proves the CU
  side of item 4 before the update exists. Low value until C is done.

## One thing to decide
The 250 MHz timing work now under way (#45) is pipelining B3's loops (`send_ops`' URAM read,
`merge_ops`' add). A deletes those loops. Worth doing if B3 stays in service for long (step 6 is
not close); otherwise it is effort on a structure with a known end date.
