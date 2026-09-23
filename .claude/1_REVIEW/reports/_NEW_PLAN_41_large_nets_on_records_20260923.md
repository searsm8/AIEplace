# PLAN #41 — nets of 17..100 pins on the record pipeline (2026-09-23, not built)

*Proposal for discussion, per the working agreement: dataflow before datapath code. It supersedes
the mechanics (not the motivation) of [[_NEW_PLAN_40_dhar_large_net_chunking_20260918.md]], which was
written for v1's one-net-per-pass loop. Cost model: `vck5000/bring_up/beat_packer/large_net_stats.cpp`;
output in `.claude/2_ARTIFACTS/beat_packer/large_net_cost_model.txt`.*

## Why it matters
The record-stream modules handle nets of 2..16 pins; nets over 100 are masked, as XPlace does.
The 17..100-pin nets are still dropped. They are ~3% of ISPD2005 nets but **20–27% of its pins**,
and dropping them cost **+12.4% mean post-DP HPWL, 28 of 28 designs worse** (#40 measurement).
This is the largest functional gap left in the WA datapath.

## The constraint
A pin's WA partial needs its own position plus five net-level values per axis: the bbox (max and
min, the exponent shift) and B±, C±. A large net spans ⌈d/16⌉ beats, so none of its pins can be
combined until every chunk has contributed the bbox, and then every chunk has contributed the
sums. That means three dependent sweeps over the net's chunks. The golden
(`computeHpwlPartials_CPU`) uses the **exact** bbox as the shift, and the exp table truncates at
12γ relative to it, so skipping the bbox sweep changes the arithmetic.

## Proposal L3: exact, three passes, on the existing engine
- **Chunk beats.** Each large net is ⌈distinct nodes/16⌉ chunk beats; its pins spread so that no
  two pins in a chunk share a bank, and a node's pins stay in one chunk. **A chunk beat is a
  degree-16 beat**: the beat loop's degree-16 tree outputs are exactly that chunk's max/min and sums.
- **Net state.** A small on-chip table per group of G nets: max, min, B±, C± per axis.
- **Three II=1 passes** over a group's chunk stream, all on the existing gradient beat loop:
  - A: gather, then read-add-write max/min into the table.
  - B: gather, exps with the table's shift, then read-add-write the sums.
  - C: gather, exps and combiner using the table's totals, then merge and scatter-add as today.

  One mode register selects "accumulate into the net table" (A, B) or "take totals from the net
  table" (C). No new arithmetic units: the new hardware is the table plus some muxes.
- **Hazards.** Chunks of one net would hit the same table entry back to back, so the host
  interleaves a group's nets round-robin (a net's chunks at least `HAZARD_DISTANCE` apart). This is
  `schedule_macro_pins`' algorithm and the same verified contract.
- **Groups.** With G = 1024 nets, the table is 6 K floats (BRAM), and the pipeline drains once per
  pass per group (84 cycles).
- **Exact:** same arithmetic as the golden, so tier 1 can hold the same 1e-5 bound.

## What it costs (model, not measurement)
Cycles relative to today's small-net stream (the 2..16-pin beats, which are unchanged):

| design | pins >16 | L3 exact, 3 passes | L2 2 passes (see below) | separate engine |
|---|---|---|---|---|
| adaptec1 | 20.9% | 2.02× | 1.68× | 1.02× |
| adaptec3 | 26.8% | 2.32× | 1.88× | 1.32× |
| bigblue4 | 26.8% | 2.30× | 1.87× | 1.30× |
| mgc_superblue12 | 9.0% | 1.37× | 1.25× | 1.00× |
| mgc_fft_1 | 3.4% | 1.19× | 1.13× | 1.00× |
| mms/newblue7 | 24.6% | 2.21× | 1.81× | 1.21× |

(15 designs in the artifact.) ISPD2005 roughly doubles; ISPD2015 costs 4–49%.

## Alternatives, and why they need your call
- **L2, online rescaling (fuses passes A and B).** Carry a running max and rescale the running sums
  when it grows, as in online softmax: B ← B·e^{(M−M′)/γ} + b·e^{(m−M′)/γ}. The WA gradient is
  invariant to the shift, so this is exact in real arithmetic. But the golden's terms become
  products of two table lookups, and table truncation happens relative to a chunk max, not the net
  max. That is **not bit-comparable to sw_only**: a deliberate divergence in the sense of
  `CLAUDE.md`, so it's your decision. It saves about a third of L3's large-net cycles.
- **A separate large-net engine** running beside the small-net stream hides almost all the cycles
  (1.0–1.3×). But it needs its own URAM ports, and the scatter-add already uses both ports of every
  grad bank. It would take a second grad accumulator (URAM) plus a merge, and a second copy of the
  exp / tree / combiner datapath (the small engine is 675 DSPs, 34%). That's an area-for-time trade
  to consider after place-and-route.
- **Rejected again:** capping at 16 (+12.4% HPWL), and Dhar's sub-net decomposition (changes the
  objective).

## Recommendation
Build **L3 first**: exact, verifiable against the unchanged golden, and almost no new arithmetic
hardware. It's a flag and a table on the existing engine, the host changes reuse the macro-fold
scheduler, and the tier-1 fixture already contains 17..40-pin nets. Treat L2 and the separate
engine as optimizations once place-and-route says where time and area actually go.

## Open questions for Mark
1. L3 first (exact) and optimize later, or go straight to L2 (the arithmetic divergence)?
2. Is ~2× gradient time on ISPD2005 acceptable as a first cut, given it's one stage of the whole
   iteration (density and FFT run too)?
