# PLAN #47 — Mailbox redesign: do the chunk exchange where each chunk is already being streamed (2026-10-06)

**Question (Mark):** the external slots are static, so why does the chunked gradient spend whole passes
moving them? Can an external slot just be loaded with the chunk, leaving only a combine pass for the
gradients? Opened from that discussion. Builds on [[_NEW_REPORT_42_mailbox_widening_20261002.md]],
[[_NEW_REPORT_42_partitioning_20261005.md]] and [[_NEW_PLAN_45_integration_20261006.md]] (D1, D6, steps 4–7).

## The mailbox today (`pl/src/modules/hpwl_gradient/mailbox.hpp`)
Only one chunk fits in URAM at a time, so anything that crosses between chunks goes through DDR.
Static, computed once by the packer: which owned slots others hold (`shared_slots`, per parcel,
**ascending owner slot**), which local slot each external copy occupies (`external_slots`), and the
mailbox layout (one inbox per consumer, one parcel per owner). Per gradient evaluation, per axis:

| pass | per chunk | mailbox loop | why it costs |
|---|---|---|---|
| send | reload positions + refresh macro pins | **send**: own shared slots → consumers' inboxes | a whole extra position load per chunk, only to copy out |
| compute | load positions, zero grad | **receive**: inbox → external slots; beat loop; **return**: external grads → inbox | |
| fold | reload partial grads | **collect**: inbox grads += into owned slots; macro fold; drain | a second gradient reload + drain per chunk |

## Where the time goes today (FM 1 pass + burst fix, cycle model)
`chunk_profile --unburst 1`, capacity 1 M, per axis per evaluation, 300 MHz.
Raw output: `.claude/2_ARTIFACTS/mailbox_47/chunk_profile_fm1_burstfixed_20261006.txt`.

| design | K | external | cycles | mailbox (4 loops) | send reload + fold pass | beat loop |
|---|---|---|---|---|---|---|
| ispd2005/bigblue3 | 2 | 51 K (4.7%) | 0.98 M | 21% | 32% | 27% |
| mgc_superblue12 | 2 | 30 K (2.3%) | 0.83 M | 14% | 30% | 36% |
| mms/newblue7 | 3 | 186 K (7.5%) | 2.51 M | 30% | 25% | 29% |
| ispd2005/bigblue4 | 3 | 267 K (12.3%) | 2.49 M | **43%** | 19% | 25% |

FM cut bigblue4's externals 868 K → 267 K and K 4 → 3, so the mailbox share fell from 68% (the
baseline-partition figure quoted on 2026-10-06 earlier in the session) to 43%. That makes the passes'
fixed cost (the send reload, the fold pass's second reload and drain) as large as the mailbox on most
designs. **The redesign should remove passes, not just make the four loops faster;** widening (D1
B/C) only does the latter.

## The redesign: duplicate at the producer, combine at the consumer of the gradient
In v1 the positions stay on the device between launches (#45 D6, step 7). So the producer of the
positions is the PL `iteration_update`, which streams every slot once per iteration in global slot
order, and #45 step 4 makes that order chunk-major. That stream already passes every owned slot in
ascending order, and the static lists are sorted the same way. So:

- **R1. Send rides on the update.** While `iteration_update` streams chunk j's slots, it also emits
  each shared slot's new position into its consumers' inbox parcels. There is one sequential output
  stream per consumer, each in ascending owner-slot order: the parcel order. Bursted writes, about
  E/16 beats of extra bandwidth. **This removes the send pass and its reload.**
- **R2. Receive: keep it, or make it a load.** (a) Keep `receive` (E cycles, or E/16 widened).
  (b) The packer gives the externals their own **tail rows**: in every bank, after the owned movable
  rows and starting at a common row, so the tail is one contiguous region of the chunk's position
  image. Then the producer writes the externals' positions into that region, and `load` brings them
  in with everything else. That is Mark's "external slots local to URAM, loaded along with the
  internal ones".
  **The catch (corrected 2026-10-07):** slot = row × 32 + bank, and each external's bank is fixed by
  the consumer's bank colouring (B4/L4). So the tail's memory order interleaves banks; it is not
  owner order. The producer emits in owner-slot order. Matching the two needs one of: a write-side
  lane permutation (a crossbar, which is exactly what `receive` gets for free from the URAM banks);
  padding; or a colouring preference that gives the external at parcel position p bank p mod 32
  wherever B4 allows. M1 measures how far the third gets.
- **R3. Return: keep it, or make it a drain.** External slots are movable slots, so `drain` already
  writes their gradients to `grad_DDR`; return only re-copies them in inbox order. With R2(b)'s
  tail rows, the drained tail is the outgoing data, in the tail's bank-interleaved order. R4's merge
  must read it in that order, which is the same alignment question as R2(b).
- **R4. Collect rides on the update too.** Before the update moves chunk j, it needs j's full
  gradient. The returned gradients for j sit in K−1 parcels, each sorted by j's slot, and the update
  streams j's gradient in slot order. So collect becomes a **K−1-way sorted merge-add inside the
  update stream**, steered by per-beat counts the host computes statically (how many entries each
  parcel contributes to each 16-slot beat, and to which lanes). **This removes the fold pass: its
  gradient reload, collect and drain.**
- **Macro fold must move.** `fold_macro_pins` runs in the fold pass because a macro pin can be
  external elsewhere. With no fold pass, it runs after collect: in the update stream (random access
  within the chunk, so it needs a small on-chip structure), or in the compute pass for macros whose
  pins are all in-chunk. Open question, below.

`hpwl_gradient_CU` shrinks to its compute pass: load, (receive), beat loop, (return), drain. The
mailbox logic moves into `iteration_update`. K=1 is unchanged.

## Estimate (cycle-model terms; update-stream overhead and macro fold not included)
| design | today | R1+R4, receive/return kept 1-float | R1–R4, contiguous tail |
|---|---|---|---|
| bigblue3 | 0.98 M | 0.57 M (−42%) | 0.46 M (−53%) |
| mms/newblue7 | 2.51 M | 1.50 M (−40%) | 1.13 M (−55%) |
| bigblue4 | 2.49 M | 1.48 M (−40%) | **0.95 M (−62%)** |

For comparison, D1-B (all four loops widened, ~128 K LUT) gives bigblue4 1.49 M. The contiguous
tail also shrinks the beat loop's surroundings to load + beats + drain, the same shape as K=1.

## M0 result (2026-10-07): the model says yes
`host/sw_flat/model/` (`cycle_model.hpp` copied from the legacy model @ ef62dba, + `chunk_model.cpp`).
`make check` runs the legacy `chunk_profile --unburst 1` live and requires TODAY to equal it phase by
phase: **PASS on all 8 chunked designs.** A mutant that changes one phase makes it fail. Raw output:
`.claude/2_ARTIFACTS/mailbox_47/m0_{1M_adders1,1M_adders2,512K_adders1}_20261007.txt`.
Cycles per axis per evaluation, as a fraction of TODAY:

| capacity | chunked designs | FUSED (R1+R4) | FUSED_TAIL (R1–R4) | bigblue4 today → fused → tail |
|---|---|---|---|---|
| 1 M | 8 | 61–65% | 40–56% | 2.49 M → 1.52 M → **0.99 M** |
| 512 K | 17 | 58–64% | 28–55% | 3.84 M (K=6) → 2.25 M → **1.09 M** |

- **The more external slots, the bigger the win for the tail:** 28% at bigblue4/512K (27% external),
  55% at superblue19 (2% external). FUSED is a near-constant ~38% saving, because it removes whole
  passes whatever E is.
- **Tail rows cost almost nothing in slots** (same colouring): ≤ 80 slot beats per design, every
  chunk still within capacity at both capacities. The alignment (write-side permutation) is
  **not charged**; that's M1.
- **The merge needs 2 adders per lane:** with 1, stalls are ≤ 0.7% of bigblue4/512K's cycles; with
  2, zero on every 1 M design.
- **R1 has a gap: shared macro pins.** Up to 6,763 shared entries (mms/newblue6 at 512K) are
  MACRO_PIN slots. The send pass refreshes them before sending. The update stream must compute
  pin = macro + offset for them, or the consumer must refresh them itself. Not charged; joins
  decision 2.
- For comparison, D1-B (all four loops widened, ~128 K LUT): bigblue4 at 1 M = 1.49 M, i.e. FUSED's
  level, with all its passes still in place.

## M1 result (2026-10-07): the tail works with today's colouring, and the price is a write-side buffer
`host/sw_flat/model/tail_layout.cpp`. Raw output: `.claude/2_ARTIFACTS/mailbox_47/m1_tail_{1M,512K}_20261007.txt`
(8 + 17 chunked designs). Each run self-checks that re-encoding a chunk's copied inputs reproduces
it exactly (records and slots); every check passed.

**Layout measured (M0's):** each external takes the next tail row *of its own bank*. The update
writes a consumer's tail sequentially, row by row, through an on-chip **assembly buffer**: entries
go in at (row, bank) as they're emitted, and a row is written to DDR once it's final (every bank has
filled it, or will put nothing more there, which is known statically).

| measure | 1 M (8 designs) | 512 K (17 designs) |
|---|---|---|
| tail size / E (padding) | 1.002–1.009 | 1.002–1.029 |
| assembly buffer, rows of 32 floats (per consumer stream) | 32–108 | 23–162 |
| update stalls at 2 writes/cycle per consumer | ≤ 2.3% (bigblue4 ×2) | up to 27% (bigblue4 K=6) |
| update stalls at **4 writes/cycle** per consumer | 0% | ≤ 0.34% |
| FIFO ahead of the buffer at 4 writes/cycle | ≤ 6.6 K entries | ≤ 8.2 K entries |
| max entries for one bank among 16 consecutive emissions | 5 | 6 |
| every chunk within capacity with its tail | yes | yes |

- **No colouring change is needed.** The tail rows add ≤ 3% slots with the banks `encode_chunked`
  already picks.
- **Colouring hints don't pay.** A hint of bank = (tail position mod 32), owned banks frozen, puts
  79–88% of externals in emission order (vs ~3% by chance), with 0 new colliding nets and ±13 record
  beats. But the assembly buffer doesn't shrink consistently (bigblue4/1M: 105 → 89 rows;
  mms/bigblue3/512K: 58 → 76), and the tail grows slightly. Measured with temporary packer hooks
  (`bank_hint` / `bank_frozen`, byte-identical to HEAD when unused on 4 designs), commit ed888f9;
  removed afterwards.
- **So the tail's hardware cost is K−1 assembly buffers in the update** (≤ 5.2 K floats each, BRAM),
  each fed by a **4-lane scatter into 32 banks**, plus the mirror on the gradient side: the merge reads
  a consumer's drained tail rows and needs them back in owner order, a 32 → 4 gather per stream. M0's
  bigblue4/1M gap between FUSED and FUSED_TAIL (1.52 M vs 0.99 M cycles, receive + return) is what
  that hardware buys. The alternative for the same cycles is widening just receive and return
  (D1-B for two loops, ~2 × 32 K LUT). **M5 compares the two.**
- **Not built in M1:** the tail layout in `assign_node_slots` and the device arrays. The model shows
  it fits (≤ 3% padding, within capacity) with no colouring change, so it can be written when M3
  needs real arrays.

## Build order (2026-10-09, Mark: "begin building")
M2-M5 as written wait on #45 (sw_flat, iteration_update). So the redesign is built from the device
side inward, each increment verified on its own, and the exchange moves into iteration_update last:

- **B1. Rule C6, macro pins never external: DONE (2026-10-09).** A chunk using a pin of a macro owned
  elsewhere holds the pin as a local slot (in its refresh/fold list) and the MACRO as the external.
  - Packer: `build_chunks_from_owner`; the FM model (`partition.hpp`: a pin's presence counts for its
    macro, only cells and macros count as externals); contract text C6; `check_chunked` enforces C6
    and now asserts the partition model's externals and loads equal the builder's.
  - Device (`hpwl_gradient_CU`): compute pass = load, receive, **refresh**, beats, **fold**, return,
    drain; the send pass no longer refreshes; the fold pass is reload, collect, drain. The chunked
    flow is now the K=1 flow plus the exchange. K=1 is unchanged.
  - Resolves **decision 2** (macro fold placement): it sits in the compute pass, so nothing about
    macros is left for iteration_update. It also closes M0's R1 gap: no macro-pin position is sent.
  - Verified: `make -C pl test` PASS; all 8 chunked designs pass tier 1 in real mode
    (`.claude/2_ARTIFACTS/mailbox_47/c6_sweep_20261009.txt`; same K, externals down 0.1-2%:
    mms/newblue7 186.5 K -> 182.7 K); 4/4 mutants caught (builder, partition model, refresh order,
    fold order); FM's incremental deltas equal a fresh recount after 3000 random moves on
    mms/bigblue4 and mms/newblue7. Model re-run (`m0_1M_c6_20261009.txt`): FUSED 61-66%, FUSED_TAIL
    40-56% of today.
- **B2. Tail rows in `assign_node_slots`** (externals after every bank's owned rows; the capacity
  check then sees the tail through `num_slots`), plus the static tail tables the assembly and
  disassembly need. Verify: check_chunked rules for the tail; tail/E and capacity as M1 measured.
- **B3. Exchange through the tails, still as passes in `hpwl_gradient_CU`**: `send_to_tails`
  (assembly buffers) replaces send + receive; `merge_from_tails` (disassembly + merge-add) replaces
  return + collect. Verify: tier 1, mutants, co-sim of the buffers; then compare its synthesis cost
  with widened receive/return (M5's question).
- **B4. Move the two exchange functions into `iteration_update`** when #45 step 6 lands; the send and
  fold passes disappear.

## Steps
Each step: what, then how it's verified. M0–M1 are offline and cheap, and decide whether the rest
is worth doing. M2 onward depends on #45 step 6 (`iteration_update` in slot order).

- **M0. Model it.** Add the fused variants to `cycle_model.hpp` (R1+R4 with receive/return kept;
  R1–R4 with tail), including the update stream's extra beats and a stated macro-fold cost.
  → verify: on the 8 chunked designs the model reproduces today's `chunk_profile` numbers exactly
  when the fusion is off; publish the fused numbers at 1 M and 512 K capacity (17 chunked designs).
- **M1. External tail rows in the packer** (option, off by default). `assign_node_slots` puts
  external slots in rows after every bank's owned movable rows. The colouring gets a soft
  preference: the external at parcel position p prefers bank p mod 32. → verify: `check_chunked`
  extended (tail rows hold only externals; every external is in the tail); and, on all 44 designs:
  record beats and padding vs today, slots added by the common tail start, K and externals
  unchanged, and the **alignment rate**, the fraction of tail beats whose lane order already equals
  the emission order. **Falsifier:** if alignment is poor, R2(b)/R3(b) need a write-side
  permutation; if its cost exceeds receive + return, keep R2(a)/R3(a).
- **M2. Golden in `sw_flat`.** The fused update in software: positions → per-consumer parcels;
  parcels → merge-add into the owner's gradient; macro fold placed per the decision below.
  → verify: bit-identical gradients and positions vs today's three-pass order on synthetic + the 8
  chunked designs. The merge's summation order is defined to match collect's parcel order (k
  ascending), so bit-identity is the bar, not a tolerance.
- **M3. `hpwl_gradient_CU` loses its send and fold passes**; `iteration_update` gains the send streams
  and the merge-add. → verify: tier-1 against M2 for each module; mutants (wrong parcel order, merge
  count off by one, merge skips a stream) caught.
- **M4. RTL co-sim** of the merge-add with the hazard negative control, as for collect today.
- **M5. Synthesis:** II=1 and LUT cost of the merge (per-beat static counts → lane steering), against
  the ~32 K LUT per widened loop measured in #42.

## Decisions for Mark
1. **Pursue this, or widen (D1 C)?** They aren't exclusive: the merge-add (R4) is a widened collect
   with static steering. But this plan removes the passes as well, which widening can't. Proposed:
   M0–M1 now (offline), then decide.
2. ~~**Macro fold placement**~~ **Resolved by rule C6 (B1, 2026-10-09):** the compute pass folds every
   chunk's macro pins, external macros included.
3. **Bring-up shortcut:** to test a receive-free `hpwl_gradient_CU` standalone before
   `iteration_update` exists, `chunk_slot_positions` could write real positions into external slots.
   Today it writes NaN on purpose, so a missing receive fails loudly. Keep the NaN for the legacy
   path, and add the real values only behind the M1 option?

## What would falsify this plan
- `iteration_update` cannot stream chunk-major slot order with K−1 extra output streams at II=1 (M5).
- The contiguous tail breaks bank-conflict freedom badly (M1), *and* receive/return are a large share
  after R1+R4. Then the fallback is R1+R4 with widened receive/return.
- The static per-beat merge counts need a crossbar as large as D1-B's (M5): then the LUT argument
  for this plan over widening weakens. The pass-removal argument still stands.
