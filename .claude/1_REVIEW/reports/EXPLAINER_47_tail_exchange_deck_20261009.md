# Slide deck brief — #47: replacing the chunk mailbox with the tail exchange (2026-10-09)

A brief for an 11-slide deck: what each slide says, what it shows, and the numbers it quotes. The
audience knows placement and FPGAs but not this code base. Every number below has a source in
[[PLAN_47_mailbox_redesign_20261006.md]] or `.claude/2_ARTIFACTS/mailbox_47/`; all cycle
figures are the cycle model's (per axis, per gradient evaluation, 300 MHz), not hardware
measurements.

**One-line story:** designs too big for on-chip memory are split into chunks; nodes shared across
chunks used to travel through a DDR mailbox in four extra loops and two extra passes. Now each
chunk's borrowed nodes live in its own memory image as a "tail", the owner writes and reads that
tail directly through a small row buffer, and the remaining exchange is ready to fold into the
position update.

---

## Slide 1 — Title
**"The tail exchange: how chunks share nodes without a mailbox"**
Subtitle: AIEplace #47, HPWL-gradient kernel on the VCK5000 PL. B1–B3 built and verified; B4 next.

## Slide 2 — Why chunks exist
- The gradient kernel keeps every node position in on-chip URAM: 1 M slots per axis (256 of 463
  URAMs).
- 8 of the 44 benchmark designs (bigblue3/4, superblue12, MMS bigblue3/4, newblue5–7) exceed that
  and are split into K = 2–3 chunks; at a 512 K budget, 17 designs, K up to 6.
- A net is processed in one chunk (its "home"); its nodes owned by other chunks are **external
  slots**: local copies that need fresh positions in, and their gradients sent back.
- **Visual:** a die split into 3 colored regions; one net crossing a boundary; the borrowed node
  drawn as a ghost copy in the home chunk.

## Slide 3 — The old way: a DDR mailbox
- Three passes per gradient evaluation: **send** (reload every chunk, copy shared positions out),
  **compute**, **fold** (reload every chunk's gradients, add the returned contributions, drain).
- Four mailbox loops: send, receive, return, collect, each one entry per cycle.
- **Visual:** pass diagram, with the two extra passes and four loops highlighted in red.
- **Number:** bigblue4 at 1 M: 2.49 M cycles per axis, of which the mailbox is **43%** and the
  extra passes' reloads another 19%; the actual gradient work (the beat loop) is only 25%.

## Slide 4 — Key insight
- Which nodes are external is **static**. It is fixed by the partition, and it never changes
  across iterations.
- Only the **values** move: positions out, gradients back.
- So the exchange can be laid out once by the host and replayed by the device as a fixed schedule,
  and it can go where each chunk is already being streamed.
- **Visual:** two columns, "static (host, once)" vs "per iteration (device)".

## Slide 5 — Rule C6: macro pins never cross chunks (B1)
- Before: a macro pin could be external, so its position had to be recomputed (macro + offset)
  before sending, and macro folds had to wait for a separate pass.
- Now: a chunk borrowing a macro's pins holds **the macro** as the external and the pins as local
  slots, refreshed and folded on chip like its own macros.
- Effect: only cells and macros cross chunks; every chunk folds its macros in the compute pass;
  the chunked flow becomes the single-chunk flow plus the exchange.
- **Visual:** macro with 4 pins split across two chunks, before (pins as ghosts) / after (macro as
  ghost, pins local).
- **Number:** externals down 0.1–2% on MMS (newblue7 186.5 K → 182.7 K); no chunk counts changed.

## Slide 6 — The tail layout (B2, rule C7)
- Each chunk's slot space: **owned rows | tail rows | fixed rows**. External slots are the tail.
- The tail is one **block per owner chunk**, owners ascending, each block starting on a fresh row.
  Inside a block, each bank fills its rows in the owner's slot order.
- The banks are the same ones the bank colouring already chose (so beats stay conflict-free).
- **Visual:** a chunk's URAM as a 32-column grid (one column per bank): owned rows on top, then
  block A and block B (different colors), then fixed rows; ragged bank columns inside each block.
- **Numbers:** the tail adds ≤ 3% slots over the external count; every chunk still fits, and no
  chunk count changed at 1 M or 512 K.

## Slide 7 — The row buffer (B3)
- The owner produces externals in **its** slot order; the consumer's tail is row-major across
  banks. A small on-chip **row buffer** (32 banks × 256 rows) converts between the two.
- **Send:** each entry is parked at (its bank, that bank's next row); a half-row is written to DDR,
  in order, as soon as every bank of that half has filled it. That's a sequential burst into the
  consumer's position image.
- **Merge:** half-rows of the consumer's drained gradients are read in order; each entry adds
  (its bank, next row) into the owner's gradient.
- The host pre-schedules everything as a list of 32-bit ops (ENTRY or TAIL_BEAT); the device just
  replays it, one op per cycle.
- **Visual:** animation of 3 entries arriving out of order into the buffer and a full half-row
  leaving as one beat.
- **Number:** at most 134 rows ever in flight (of 256) on any design.

## Slide 8 — The new pass structure
- **Send pass:** load the owner's own rows; replay its send streams.
- **Compute pass:** load (the tail arrives with everything else), refresh macro pins, beat loop,
  fold macros, drain (the tail's gradients leave with everything else).
- **Fold pass:** reload own rows, replay merge streams, drain own rows.
- Gone: receive, return, collect, the mailbox buffer, and the hazard padding it needed.
- **Visual:** old vs new pass diagram side by side.

## Slide 9 — Verification
- **Tier 1** (C++ against a double-precision golden): all configs pass; 614 tail blocks at
  capacity 2560, up to 25 per tail.
- **Real designs:** all 8 chunked designs pass, with blocks up to 3,107 rows (the 256-row buffer
  wraps over many times).
- **Packer checker** replays every block's ops on **node ids**: every tail slot must receive exactly
  the right node, and every merge must add into that node's owner slot.
- **Mutants:** 15 deliberate bugs across B1–B3 (4 + 4 + 7), all caught.
- **C-synthesis:** every loop II = 1, estimate 3.48 ns (was 3.62 ns with the mailbox).
- **RTL co-sim:** PASS at 16 chunks, with 361 send and 542 merge back-to-back row-buffer uses; the hazard-1 negative control FAILS as required.

## Slide 10 — Payoff
- **Chart:** bar chart of cycles per axis, bigblue4 at 1 M:

  | design point | cycles / axis |
  |---|---|
  | mailbox (before #47) | 2.49 M |
  | tail exchange, built (B3) | **1.93 M** (−23%) |
  | fused into the update (B4, modeled) | **0.98 M** (−61%) |
  | for reference: all four mailbox loops widened 16× (~128 K LUT) | 1.49 M |

- Across the 8 chunked designs, B4 models at 51–61% of the B3 device.
- The exchange's share drops from 43% (mailbox) to 30% (B3) to ~3% (B4).

## Slide 11 — Next: B4, and open questions
- **B4:** move the send and merge replays into `iteration_update`, which already streams every
  chunk's slots in order. The send pass and the fold pass's reload/drain disappear.
  - Assembly side: up to 4 entries per cycle into each consumer's row buffer (0–0.34% stall).
  - Merge side: 2 adders per lane, 0 stalls.
- **Open:**
  - post-route timing of the row-buffer loops (they read a row the cycle after writing it; co-sim
    checks correctness, Vivado will tell the cost);
  - the 4-wide write path's LUT cost versus simply widening;
  - the per-owner block rounding: negligible at real sizes, but at tiny test capacities it forces
    larger chunks (synthetic tests moved from 768 to 2560 slots).

---

### Speaker notes / anticipated questions
- *"Why not just widen the mailbox?"* Widening speeds up the four loops but keeps both extra passes;
  even fully widened it reaches only 1.49 M on bigblue4, versus 0.98 M for B4 without any
  crossbars.
- *"Isn't the tail wasted memory?"* It replaces the external slots that already existed, interleaved;
  the tail only adds row rounding, ≤ 3%.
- *"Is the result bit-identical?"* The gradient matches the double-precision golden within the same
  tolerance (rel_rms ~3e-7 on the synthetic design, ≤ 8.5e-7 on real ones); each owner slot still adds
  its consumers' contributions in ascending consumer order.
