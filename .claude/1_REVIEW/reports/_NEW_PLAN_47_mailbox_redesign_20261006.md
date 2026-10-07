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
2. **Macro fold placement:** in the update stream (needs on-chip macro accumulators per chunk), or
   split (in-chunk macros in the compute pass, the rest in the update)?
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
