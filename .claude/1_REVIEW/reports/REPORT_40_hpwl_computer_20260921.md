# REPORT #40 — `hpwl_computer`: multi-net-per-beat HPWL bbox module (new bring_up/ module)

*2026-09-21/22. Task #40, spawned from [[_NEW_HANDOFF_40_dhar_v2_rewrite_20260918.md]] (the main v2
handoff — read that first for full background: the parallel-prefix scan, the controller/compute
split, the cumulative-`net_count` degree resolution). **Status: DONE.** `hpwl_computer` is built at
`vck5000/bring_up/hpwl_computer/`, tier-1-verified (bit-exact over 279 real nets), and C-synthesizes
its main loop at **II=1**. See the RESULT section below for the full build; the PIVOT section below
it is the design discussion that got there, kept for the reasoning trail. Converted from
`_NEW_HANDOFF_40_beat_controller_20260921.md` → `_NEW_HANDOFF_40_hpwl_computer_20260921.md` →
this file, in place, per the handoff-is-a-report-in-progress policy in `CLAUDE.md`.*

## RESULT (2026-09-22) — built, verified, II=1

**Files** (`vck5000/bring_up/hpwl_computer/`):
- `src/modules/hpwl_computer.hpp` — the module (all datapath code; no Claude-authored comments per
  the working agreement, Mark's own annotations are throughout).
- `src/hpwl_computer_top.cpp` — thin kernel-top wrapper, no interface pragmas yet.
- `synth_check.tcl`, `.gitignore` — mirror the v2 module's own pattern.
- `vck5000/test/hpwl_computer_test.cpp` — tier-1 harness (registered in `vck5000/test/Makefile`).

**The module, end to end, one `beat_loop` iteration per input beat:**
1. `resolve_beat(beat, net_count_BRAM, beat_count_BRAM)` — beat-granularity version of v2's
   cumulative-count degree resolution (Q3 below): which degree-group beat `b` falls in, its
   `net_offset` into the output, and `real_net_count` (how many of this beat's segments are real —
   the tail beat of a degree-group is usually partial).
2. `dhar_tree<MaxOp>`/`dhar_tree<MinOp>` — **Dhar's Fig. 6/7 multi-output tree, reused verbatim with
   comparators instead of adders**, genericized as a template over the combining operator (`MaxOp`/
   `MinOp` functors; `AddOp` is #41's to add). All 34 labeled outputs per tree, depth ≤4, verified by
   hand for every one of the 15 degrees before writing code (see PIVOT's padding-waste table for why
   depth-4 held even on the awkward non-power-of-2 degrees — the "merge the shallowest pieces first"
   rule, not naive left-to-right).
3. `select_lane_hpwl` — the 16 muxes (Fig. 7's per-lane selector), one per lane: `segment_id =
   lane/degree`, index `[degree][segment_id]` into both trees, subtract. Redundant across a
   segment's lanes exactly like Dhar's own design (confirmed against the paper: "we can use the
   same selector for indices 0 and 1" — this is inherited, not a defect).
4. Pack into one `OutBeat` (guarded by `real_net_count`, zero-padded otherwise) and issue **one**
   wide DDR write per beat — the output-side mirror of the input-side wide-beat fix (see below).

**Tier-1 (`make run-hpwl_computer_test`):** an independent, brute-force golden at every level —
`resolve_beat` vs. a from-scratch degree-grouped walk; the tree's all 34×2 outputs and the
selector's all 16×15 outputs vs. a direct per-segment max/min scan (50 random beats); the full
module vs. per-net HPWL computed straight from unpacked pin coordinates (279 real nets, packed into
181 output beats). All bit-exact.

**C-synthesis journey (`vitis_hls -f synth_check.tcl`, `xcvc1902-vsvd1760-2MP-e-S`, 3.33 ns) — every
step measured, not predicted:**
| step | `beat_loop` result | cause |
|---|---|---|
| `resolve_beat` alone (early probe) | auto-pipelined, II=3→1 once output artifact fixed | baseline, before the tree existed |
| full loop, no `PIPELINE` pragma | not pipelined at all | body too large for HLS's automatic threshold |
| `PIPELINE II=1` added | **rejected outright** — "contains subloop(s) that are not unrolled or flattened" | `net_hpwl_write`'s trip count (`real_net_count`) was a runtime variable — can't unroll an unbounded loop |
| fixed-width write loop (`MAX_NETS_PER_BEAT`, guarded) | pipelines, II=12 | `TreeOutputs::max_deg`/`min_deg` (34-output arrays) not partitioned — port contention on the writes `build_tree_outputs` makes into them |
| `ARRAY_PARTITION complete` on `max_deg`/`min_deg` | II=8 | bottleneck moved to `lane_hpwl` (same class of problem) |
| `ARRAY_PARTITION complete` on `lane_hpwl` | II=8 (unchanged) | bottleneck moved again — this time to `out_hpwl`'s scalar DDR writes, up to 8/beat on one `m_axi` port |
| **`OutBeat` — pack all real values, one wide write/beat** | **II=1** 🎯 | mirrors the *input* side's own fix: one wide transaction beats N narrow ones, symmetric top to bottom |

`cache_counts` (loading `net_count`/`beat_count`) sits at II=2 throughout, unrelated — both arrays
share one default `gmem` bundle; an interface-pragma fix (separate bundles), not attempted yet,
deferred to whenever the real kernel-top interface gets designed (Mark's call per the working
agreement).

**Output is beat-aligned, not tightly packed** — `out_beats[beat].v[net]` for `net < real_net_count`
is real, the rest of that beat's slots are `0.0f` padding. Any downstream consumer needs the same
`resolve_beat`-style bookkeeping to know which slots are real, same as the input side.

**Bandwidth note, precise:** because `OutBeat` (32B) is exactly half `InBeat`'s width (64B) and
there is exactly one of each per `beat_loop` iteration, total output DDR traffic is **always exactly
50% of input traffic**, regardless of the degree mix — this holds even though the *per-degree*
output padding-waste table is the mirror image of the input one (worst at high degree, 87.5% waste
for degree 9–16, vs. the input table's worst being the 9–15 midrange) — the aggregate bound doesn't
depend on the mix.

**`dhar_tree` is now a template over the combining operator** (`MaxOp`/`MinOp`), not hand-duplicated
max/min code — confirmed via C-synthesis that the refactor is hardware-identical (`beat_loop` still
II=1, byte-for-byte same report) before and after. This is deliberately what unblocks #41: an
`AddOp` instantiation of the same template gives Dhar's exponent-sum adder trees with no
re-derivation risk on the depth-4 construction.

**Diagram:** [[DIAGRAM_hpwl_computer.md]] (`.claude/2_ARTIFACTS/diagrams/`).

**What #41 needs to settle, now that this is proven:** module boundary (extend `hpwl_computer` vs.
new module reusing it), the per-pin (not per-beat) selector Fig. 7 actually needs for gradients, and
the combiner (Fig. 8). See tasks.md #41.

---

## PIVOT (2026-09-21, same-session) — no separate beat-controller; direct multi-net beat feed
The rest of this file (starting at "Why this module exists" below) is the **original proposal**:
a standalone controller module that reads wide 512-bit beats and de-interleaves a tight-packed,
continuous stream into variable-length net blocks via an elastic on-chip buffer (shift-on-consume,
refill-on-underrun). **That design is superseded before any code was written.** Read it for the
platform bandwidth facts (still valid) and the reasoning trail (Q1's (a)-vs-(b) fork), but the
buffer/shift mechanism it proposed is not what got built.

**What replaced it, decided with Mark 2026-09-21:**
- **`LANES` changes from 8 to 16** — matches the beat width exactly (512 bits = 16 floats) and
  matches Dhar's own width.
- **Q1's answer FLIPS from (a) packed+parse to (b) padded+grouped.** The host lays out DDR in
  Dhar's **Method 1** shape: nets bucketed by degree (already the plan per the D1 amendment in the
  main handoff), then packed into fixed 16-float blocks, `nets_per_beat = floor(16/degree)` nets
  per block, zero-padded to fill the rest. A beat never spans a net boundary and never mixes
  degrees, so **no elastic buffer, no shift, no refill logic is needed at all** — one beat/cycle
  feeds the compute stage directly. The padding cost this accepts is real but small in practice
  (see the waste table below) and buys a much simpler, lower-timing-risk module.
- **The compute stage is a genuine redesign, not a width bump.** `max_scan8`/`min_scan8` (the v2
  prefix-scan design) do not generalize to "several nets sharing one wide vector" — the right
  structure is **Dhar's Fig. 6/7 multi-output tree + selector, reused unchanged except each
  2-input adder oval becomes a 2-input compare-select (max, and separately min)**. Max/min are
  associative and commutative exactly like sum, so every property Fig. 6/7 relies on (shared
  partial results across degrees, depth-4 reachability for all 34 segment alignments, a
  degree-only selector) carries over directly. Only **two trees** are needed here (max-tree,
  min-tree) — no term generator (Fig. 5) or combiner (Fig. 8), since this module computes bbox/HPWL
  only, matching the v2 milestone's existing scope. Output is Fig. 9's idea generalized: for each
  of the 34 tree-output pairs `(a,b)`, `hpwl_span[a,b] = max_tree[a,b] - min_tree[a,b]`.
- **New module name: `hpwl_computer`** ("thing that computes HPWL"), in its own new directory
  `bring_up/hpwl_computer/` (keeps `hpwl_gradient_dhar_v2` untouched as the reference it was built
  as, per Mark's explicit call to keep v2 as reference rather than extend it in place).
- **Degree/beat resolution reuses the same cumulative-count pattern, one level up.** Same shape as
  today's cumulative `net_count`, just counted in beats-per-degree-group instead of nets: "which
  degree-group is beat `b` in."
- **Open detail, not yet resolved in code: the tail beat of each degree-group is usually partial**
  (e.g. a degree-3 group with 12 real nets needs `⌈12/5⌉=3` beats, the last with only 2 of 5 slots
  real). Needs a per-group remaining-net counter (decremented by `nets_per_beat` each beat, clamped)
  so the FSM knows how many of a partial tail beat's tree outputs to actually write to `out_hpwl[]`.
- **Term generation (Fig. 5), the LUT, and the combiner (Fig. 8) — i.e. turning this bbox result
  into the actual WA gradient, not just HPWL — are explicitly deferred to a separate task, #41
  (`hpwl_gradient_computer`).** This module perfects the multi-net-per-beat pattern on the simplest
  possible payload (bbox) first; #41 extends the same pattern with the additional stages.

**Padding-waste table (per-degree, informs why (b) was accepted over (a)):**
| degree | nets/beat | padding waste | degree | nets/beat | padding waste |
|---|---|---|---|---|---|
| 2 | 8 | 0% | 9 | 1 | 43.75% |
| 3 | 5 | 6.25% | 10 | 1 | 37.5% |
| 4 | 4 | 0% | 11 | 1 | 31.25% |
| 5 | 3 | 6.25% | 12 | 1 | 25% |
| 6 | 2 | 25% | 13 | 1 | 18.75% |
| 7 | 2 | 12.5% | 14 | 1 | 12.5% |
| 8 | 2 | 0% | 15 | 1 | 6.25% |
| — | — | — | 16 | 1 | 0% |

Real netlists are heavily right-skewed toward degree 2–4, which land on exact or near-exact fits —
the bad cases (9–15) sit on the rare tail of the distribution. This matches Dhar's own reported
90.6% average efficiency across their benchmark suite (§III-B).

**Next action, as of this pivot:** directory skeleton, tier-1 harness, then the tree/selector build —
all done; see the RESULT section above for what actually landed.

---

## Original proposal (superseded — kept for the platform facts and the reasoning trail, see PIVOT above)

## Why this module exists
The v2 bbox/HPWL milestone (see main handoff) got `compute` to II=1 and `input_controller`'s
control-flow logic to a fully parallel, dependency-free form (cumulative `net_count`, boundary-count
degree resolution). That fully resolved the *scheduling* problem. What's left is a *bandwidth*
problem, and it's the same one v1 already hit (main handoff, lesson 3):

`input_controller` loads a net's up-to-`LANES`(8) pins with `for(k) blk.v[k] = pin_x[pin_offset+k]`.
Nested inside a `PIPELINE`d loop, this is forced to fully unroll (lesson 2) into **8 simultaneous
same-cycle reads issued to one `m_axi` port**. Confirmed by actual C-synthesis (not predicted —
measured): `Final II = 8`, and the log is explicit about why:
```
WARNING: [HLS 200-885] Unable to schedule bus request operation ('gmem1_load_...') on port 'gmem1'
... due to limited memory ports (II=1..7)
```

**This is not a burst problem in the HLS sense, and widening the burst depth won't fix it.** A burst
is HLS proving a *sequence of accesses over multiple cycles* are at consecutive addresses (one
request, many beats back-to-back — `cache_net_count` already gets this, cleanly, for its 7
sequential reads). What we have is 8 *simultaneous* requests in *one* cycle — bursting doesn't apply;
no burst depth serves 8 addresses in the same cycle from one port. The only real fix is a **wider
beat**: fetch more bytes per single transaction so 8 logical values arrive in fewer physical
requests. This is exactly D9's deferred port-width lever (main handoff): "512-bit reads (16 floats
per beat, Dhar's 64 B/clock) come later, once the stages can consume at that rate." That time is now.

## The goal, as Mark framed it
> Let's start with the assumption that we max out each beat of data, since that is a precondition
> for maximizing throughput, which is the ultimate goal.

I.e.: design for full DDR port utilization (every beat fully packed) as the starting assumption, not
an optimization to bolt on later. Build a **new bring_up/ module** — a controller that:
1. Reads `pin_x` via genuinely wide (512-bit) bursts from DDR.
2. Parses/distributes the beat's contents to **one or more downstream compute workers** (the
   already-built, already-II=1 `max_scan8`/`min_scan8` engine from `hpwl_gradient_dhar_v2.hpp`).

This is explicitly a new architectural layer, not a patch to `input_controller` — it sits between
raw DDR and the compute stage, and per the "big picture" replication goal (main handoff), one
beat-controller feeding **multiple** compute workers is the intended shape: replicate the cheap,
purely-arithmetic compute core many times; do not replicate the DDR-facing control logic alongside
it.

## Platform facts gathered this session (load-bearing for this design)
- **VCK5000: 4× DDR4-3200 channels, 4 GB each, 16 GB / 102.4 GB/s total.** Cross-confirmed two ways:
  UG1531 (install guide) lists "four 4GB modules, part MT40A512M16TB-062E" at 3200 MT/s; independently,
  4 × (3200 MT/s × 8B) = 102.4 GB/s matches AMD's own product-brief bandwidth figure exactly.
- **Only 3 of 4 channels (12 GB, ~76.8 GB/s) are exposed to user kernels** — the
  `xilinx_vck5000_gen4x8_qdma_2_202220_1` platform docs state "12GB of on-card DDR-4 for user-kernel
  usage"; the 4th channel is shell/QDMA-reserved. **Use 76.8 GB/s, not 102.4, for any budget math.**
- **All DDR traffic goes through the Versal NoC — no bypass path.** Confirmed via AMD's PG313 (NoC +
  integrated memory controller architecture) and the VCK5000 product-brief block diagram (DDR →
  Memory Controllers → NoC → every kernel type). N replicated workers/controllers all arbitrate for
  the *same* NoC fabric and DDRMC ports — they do not get independent point-to-point wires.
- **A 512-bit `m_axi` port's theoretical per-port ideal ≈ 19.2 GB/s** (64 B × 300 MHz) — this is a
  *per-port* ceiling, already matched against real measured numbers in
  `vck5000/bring_up/hpwl_pl/HANDOFF.md`'s Data Transfer table for an unrelated but structurally
  similar kernel. It is NOT the platform's real aggregate ceiling; multiple ports share the NoC pool.
- **Prior, already-measured finding, same HANDOFF.md — worth re-checking here, not assuming away**:
  a related kernel in this repo was found to be *latency-bound on small scattered transactions*, not
  bandwidth-bound (85–96% of every port's ideal capacity sitting idle), and per-transaction latency
  was found to *grow with request volume* even on a single kernel instance with zero cross-CU
  contention. Widening the beat should help throughput, but don't assume it eliminates the latency
  risk without measuring the new design the same way.
- Rough bandwidth ceiling on worker count (narrow, 32-bit framing, likely to change once the wide
  controller's real per-worker consumption rate is known): `LANES(8) × 4B = 32B/cycle` per worker →
  9.6 GB/s/worker @ 300 MHz → `76.8 / 9.6 ≈ 8` workers as an upper bound. **Redo this once the
  beat-controller's fan-out ratio (beats consumed per worker per cycle) is known** — it will change.

## Open design questions — settle before writing code (per the working agreement)
**Q1. Tight-packed DDR layout + a beat-parser that peels off variable-length net blocks, or pad
every net's DDR block to a fixed beat-aligned width?**
- *(a) Keep packing (current layout), parse a continuous wide-beat stream.* Every net occupies
  exactly `degree` floats in DDR, no waste — but a 512-bit (16-float) beat generally doesn't align to
  net boundaries (a degree-3 net's floats might span two beats, or share a beat with parts of two
  other nets). The beat-controller needs real logic to de-interleave a continuously-arriving,
  fixed-width beat stream into variable-length net blocks — structurally similar to Dhar's own
  Method 2 marker/queue problem (main handoff's D5), but at the *beat* level instead of the *pin*
  level. HLS's burst inference explicitly cannot handle data-dependent/variable-length access
  patterns on its own (main handoff lesson 4) — this parsing has to be done deliberately, in logic,
  not left to automatic burst inference.
- *(b) Pad every net to a fixed `LANES`-wide (or full-beat-wide) DDR slot.* Trivial addressing (net
  `n`'s block is always at a fixed stride, one beat, always) — but wastes real DDR bandwidth on the
  small-degree nets that dominate real netlists (a degree-2 net's true payload is 8B; padded to
  `LANES=8` floats it costs 32B fetched — 4× inflation), directly shrinking the worker-count budget
  above.

Mark's "max out each beat" framing leans toward (a) — filling every beat with real, useful data is
the more literal reading of "max out," and it's the harder, more interesting design. **This needs to
be confirmed explicitly before building, not assumed** — it's the single biggest fork in this
design.

**Q2. How many compute workers does one beat-controller feed?** Ties to the worker-count budget
above and the "big picture" replication note. A fan-out of 1:N (one controller, N compute engines)
is the intended shape; N itself is unresolved pending Q1 and a real bandwidth/latency measurement.

**Q3. Does the beat-controller reuse the existing degree-resolution logic verbatim?** The cumulative
`net_count_BRAM` + parallel boundary-count scheme (main handoff, D1 supersession) is already built
and synthesis-verified as dependency-free. The beat-controller almost certainly needs the same
"which net, what degree" resolution to know how to peel data from the incoming beat stream — this
should very likely be reused as-is rather than reinvented, but confirm once Q1 is settled (the exact
shape of "which net is at this position" may differ slightly depending on packed-vs-padded).

## What's already built and reusable (don't rebuild these)
- `vck5000/bring_up/hpwl_gradient_dhar_v2/src/modules/hpwl_gradient_dhar_v2.hpp`:
  - `max_scan8`/`min_scan8` — parallel-prefix scan, exact for any degree 2..`LANES` without padding
    sensitivity, `ARRAY_PARTITION`'d, verified II=1. **This is the compute worker** — the
    beat-controller's job is to keep this fed, not to replace it.
  - The cumulative-`net_count` degree-resolution pattern (see Q3).
  - `PinBlock`/two-parallel-`hls::stream` pattern (data channel + `int` degree sideband, not bundled)
    — reuse this shape for the beat-controller → compute-worker interface too, most likely.
- `vck5000/test/tier1_stub.hpp` — the `hls::stream` stand-in for tier-1/g++ builds (`PL_TIER1_STUB`
  guard). Needed again for the new module's own tier-1 harness.
- `vck5000/bring_up/hpwl_gradient_dhar_v2/synth_check.tcl` — copy this pattern for the new module's
  own lightweight C-synthesis check (see main handoff's Pointers for the exact local-Vitis-HLS
  invocation — no remote build server needed).

## Suggested next action
1. Settle Q1 (packed-and-parse vs. padded-and-uniform) with Mark — this determines almost everything
   else about the module's internal structure.
2. New directory, name TBD (something like `vck5000/bring_up/hpwl_beat_controller/` or folded into
   `hpwl_gradient_dhar_v2/` as an additional stage — also worth asking rather than assuming).
3. Small chunks, same working agreement as the main handoff (Mark places pragmas, reviews each
   10–20-line chunk, no Claude-authored comments in the module/datapath code).
4. Verification: tier-1 (g++, bit-exact vs. a golden that reads the same synthetic DDR image) before
   any C-synthesis; C-synthesis to confirm the wide read actually infers as a burst (check the log's
   burst messages per lesson 4) and to re-measure the achievable II once the port-conflict is
   resolved this way.

## Pointers
- Main v2 handoff (read first): [[_NEW_HANDOFF_40_dhar_v2_rewrite_20260918.md]]
- VCK5000 DDR bandwidth measurement, same-repo precedent: `vck5000/bring_up/hpwl_pl/HANDOFF.md`
  (the latency-bound-not-bandwidth-bound finding, and per-CU replication risk)
- AMD VCK5000 product brief (102.4 GB/s figure, block diagram):
  https://www.xilinx.com/content/dam/xilinx/publications/product-briefs/amd-xilinx-vck5000-product-brief.pdf
- AMD PG313 (NoC + integrated memory controller architecture):
  https://docs.amd.com/r/en-US/pg313-network-on-chip/NoC-Architecture
- VCK5000 install guide (physical DDR module part numbers):
  https://www.eetrend.com/files/2022-03/wen_zhang_/100558299-244977-ug1531-vck5000-install.pdf
- Dhar paper (Method 2 marker/queue mechanism, relevant to Q1 option (a)):
  `.claude/2_ARTIFACTS/papers/dhar_fpga_accel/03_wirelength_gradient_computation.md`
