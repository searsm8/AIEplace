# PLAN #40 — `hpwl_gradient_dhar`: exact large-net support by 16-pin chunking

*2026-09-18. Task #40. Not implemented yet (Mark's call): this records why and how, so the work can
be picked up without re-deriving it. The measurement behind it is
[[_NEW_REPORT_40_net_degree_cap16_20260918.md]].*

## Why
`hpwl_gradient_dhar` follows Dhar (FPL 2019) and **ignores every net with more than 16 pins**. It drops
both the net's gradient and its HPWL. Dhar reports this slightly *improves* quality on FPGA benchmarks,
where such nets are <0.25% of nets. On our ASIC benchmarks it does not:

- `ignore_net_degree = 16` vs the frozen golden (100), 28 ISPD designs, post-DP legal-vs-legal:
  **28/28 worse, mean +12.4%** (ISPD2005 +30.6%, up to +51% on bigblue4; ISPD2015 +5.2%).
- 17–100-pin nets are only ~3% of ISPD2005 nets but carry **20–27% of all pins**. Dropping them leaves
  those cells with no wirelength pull, and the loss tracks that pin share.

The kernel has to handle nets up to 100 pins exactly. Nets over 100 stay ignored: XPlace and the
sw_only golden both mask them (`ignore_net_degree = 100`), so this costs nothing against the golden.

## How
**Key constraint:** a pin's WA gradient depends on its own position *and* on whole-net values — the
bounding box (the exponent shift) and four sums per axis, B± = Σa±, C± = Σx·a±. Chunks therefore
cannot be finished independently; doing so would compute sub-net gradients, which changes the
objective. All the net-level values are associative, so they can be accumulated across chunks
first.

Per net of degree d, a small state machine in `net_loop`:

1. **LOAD** — stream the d pins (sequential burst, as today) into an on-chip net buffer of
   (x, y, `npin_slot`), reducing the bbox on the way in (`load_net` already does this).
2. **SUM** — for each of ⌈d/16⌉ chunks, run `term_gen` + the adder trees and add the chunk's 8
   partial B/C sums into 8 per-net accumulators. No scatter.
3. **GRAD** — for each chunk, run the combiners with the net totals and scatter the 16 pin gradients
   to `pin_grad_DDR` (node-major, as today).

For d ≤ 16, SUM and GRAD are one chunk and collapse into today's single pass: **one datapath, not a
fast path and a slow path.** `node_reduce` (phase 3) is unchanged.

Design points:
- **Buffer:** 7 × 16 = 112 slots, since the host masks nets over 100 pins. Read 16-wide, so it is partitioned into 16 banks of 7:
  LUTRAM/registers next to `net_loop`, which is #40's congested region. Size it knowingly.
- **a± terms in GRAD:** recompute them (more LUT reads on large nets only) rather than store 4 more
  112-entry arrays. Pick recompute first, given #40's congestion.
- **Phase Z (zero `pin_grad_DDR`) can be deleted.** It exists only because capped nets left slots
  unwritten; with every live pin written exactly once, it is dead.
- **Beyond 100 pins, if ever needed:** re-read the net's pins from DDR for GRAD instead of buffering
  them. That removes the degree limit for one extra sequential burst per large net.

**Cost:** 17–100-pin nets add **+6–9% more 16-pin blocks on ISPD2005**, +2.5–3.8% on superblue and
≤2.3% on other ISPD2015. With SUM and GRAD both walking a large net's blocks, the worst case is
~12–18% more `net_loop` cycles on ISPD2005. Source: `.claude/2_ARTIFACTS/nd16/chunk_stats.py`.

## Do it together with #40's lane-narrowing
#40's timing fix is to process each net in waves of W = 4–8 lanes instead of all 16 at once. That is
the same restructure: "a net takes ⌈d/W⌉ waves" and "a net takes ⌈d/16⌉ chunks" are one loop. Build
it once.

## Verification (per the loop in `CLAUDE.md`)
1. Tier-1 `hpwl_dhar_test`: switch its golden from the capped WA gradient to the uncapped one
   (`computeHpwlPartials_CPU` at `ignore_net_degree = 100`, as `hpwl_gradient_test` uses). Add nets at
   the chunk boundaries — 16, 17, 32, 33, 100 pins — plus one of 101 pins that must be ignored.
   Asserted, ~1e-6 rel_rms like the sibling module.
2. C-synthesis: II and resources before any P&R run (#40's rule: no P&R on a guess).
3. P&R timing, together with the lane-narrowing.

No quality experiment is needed: the arithmetic is the golden's, so the frozen golden is this
option's quality.

## Rejected alternatives
- **Hybrid** (Dhar for ≤16 pins + the existing streaming `hpwl_gradient` for 17–100): exact and least new
  logic, but it needs two gradient datapaths on the device. It's a stopgap only.
- **Sub-net decomposition** (Dhar's "break into ≤16-pin nets with a common driver"): changes the
  objective, needs driver pins our data model lacks, and has no meaningful static grouping.
