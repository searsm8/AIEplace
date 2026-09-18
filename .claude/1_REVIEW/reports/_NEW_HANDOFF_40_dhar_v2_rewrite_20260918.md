# HANDOFF #40 — rebuild the Dhar HPWL-gradient module from scratch as `bring_up/hpwl_gradient_dhar_v2/`

*2026-09-18. Task #40. Status: **not started.** This file is the running draft; convert it to
`REPORT_40_...` when v2 is verified (see the handoff policy in `CLAUDE.md`).*

## Goal
Rebuild [[hpwl_gradient_dhar.hpp]] as a new module in a new directory, applying everything learned
from v1's hardware failure (tasks.md #40) and the 2026-09-14..18 crash course. **v1 stays untouched**
in `bring_up/hpwl_gradient_dhar/` as the functional reference. v2 is a learning build as much as a
product: Mark wants to see how each pragma changes the hardware.

## Working agreement (Mark's rules for this rewrite — read before writing any code)
1. **Small chunks.** Claude proposes 10–20 lines of code at a time. Mark reviews each chunk before
   the next one.
2. **No comments from Claude in the v2 code.** Mark adds the comments himself as we go. (This
   overrides the usual "comment the why" and `Meow.` conventions for v2's source files only.)
3. **Mark places the loop pragmas** (`PIPELINE`, `UNROLL`, `ARRAY_PARTITION`, `DATAFLOW`, `INLINE`,
   …). Claude writes pragma-free C++ and explains the options when asked, but doesn't insert them.
   Interface pragmas (`m_axi` / `s_axilite`) in the kernel top: ask Mark before writing them.
4. **Record the hardware effect of each pragma.** After Mark adds one, run C-synthesis and write down
   the loop's II, depth and resource numbers before and after, in this file (table at the bottom).
   That record is the point of the exercise.
5. **Discuss data format and dataflow before any datapath code** (Step 2 below).

## Decisions already made
- **One axis per invocation.** The module computes x only (or y only). WA wirelength splits
  completely by axis: bbox, the four B/C sums and the partials never mix x and y. So the caller runs
  it twice and adds the two HPWL spans. This halves the datapath per pass. Throughput is recovered
  later by instantiating two copies (area for time), **after** one copy works.
- **New directory** `vck5000/bring_up/hpwl_gradient_dhar_v2/`, same layout as v1: `src/modules/`,
  `src/<top>.cpp`, `src/host.cpp`, `Makefile`, `README.md`, `run_hw.sh`. The tier-1 harness is a new
  `vck5000/test/hpwl_dhar_v2_test.cpp`, added to `HARNESSES`.

## Lessons from v1 (each one is a design constraint for v2)
1. **g++ ignores every `#pragma HLS`.** Tier-1 proves the arithmetic, never the structure. Check
   structure with C-synthesis at every pragma step (Step 4 of the loop in `CLAUDE.md`).
2. **A `PIPELINE`d loop fully unrolls every loop nested inside it, whatever the inner loop's own
   pragma says** (UG1399). This is why v1's `load_net` `PIPELINE` was a no-op, and why "partially
   unroll term_gen" can't work inside a pipelined `net_loop`. **To control width, the per-pin loops
   must not be nested inside a pipelined per-net loop.**
3. **An m_axi port takes about one request per cycle.** In a pipelined loop, II ≥ (accesses to one
   port per iteration). v1 had 16 each on `gmem1` (reads), `gmem3` (reads) and `gmem5` (scattered
   writes), giving three independent II ≥ 16 floors. **Count accesses per port per iteration before
   writing a loop.**
4. **A burst delivers one bus-width beat per cycle.** It hides latency; it doesn't multiply bandwidth.
   Bursts are inferred at compile time, and only when HLS can prove the addresses are consecutive
   (data-dependent start addresses and conditional accesses tend to defeat it). Check the log's burst
   messages.
5. **A fixed-II pipeline charges every iteration a full II**, including skipped ones. v1 spent 16
   cycles on masked, single-pin and oversized nets that did nothing.
6. **The compute width must match what the ports can deliver.** v1's datapath was sized for 1 net per
   cycle behind ports that deliver 1 net per 16 cycles. The result was routing congestion (WNS
   −0.710 ns; later "fails to route" when the LUT was partitioned), not speed.
7. **The LUT's read count is the problem, not the number of copies.** 64 lookups × 2 interpolation
   taps = 128 reads per net. HLS had already made ~14 copies (BRAM_18K = 14). Reduce reads instead:
   pack `(lut[i], lut[i+1])` in one 64-bit word (1 read per lookup), and bound the number of
   concurrent lookups.
8. **HLS's Fmax estimate is made before routing.** It said 349 MHz; P&R said no. Don't run P&R until
   C-synthesis numbers are clean (#40's rule).
9. **The 16-pin cap costs +12.4% HPWL** (28 of 28 ISPD designs worse). Support nets up to 100 pins
   exactly by chunking → [[_NEW_PLAN_40_dhar_large_net_chunking_20260918.md]]. With every live pin
   written exactly once, **Phase Z (zero `pin_grad`) is no longer needed.**
10. **`hls::stream` in tier-1 is an unbounded `std::deque`,** and C-sim runs dataflow processes one
    after another, so it can't reveal FIFO-depth deadlocks. Size FIFOs deliberately and prove them in
    co-simulation.

## Step 2 — data format and dataflow: questions for discussion (proposals, NOT decisions)
Settle these with Mark before any datapath code. Each has a proposed starting point.

**D1. Per-axis data layout (structure-of-arrays).** v1 read a 16-byte `NodePin {node_idx, x, y, net}`
per pin, but Phase D needs only the coordinate (plus `.net` of the first pin, as a mask), and Phase 3
only `node_idx`. Proposal for one axis:
| array | size | order | used by |
|---|---|---|---|
| `net_ptr` | num_nets+1 int | CSR over **live nets only** | D |
| `pin_x` | num_pins float | net-major, absolute coordinate on this axis | D |
| `pin_to_npin` | num_pins int | net-major → node-major slot, −1 = fixed node | D (scatter) |
| `npin_node` | num_node_pins int | node-major, sorted node index (segment key) | 3 |
| `pin_grad` | num_node_pins float | node-major scratch | D writes, 3 reads |
| `node_grad` | num_movable float | output | 3 |
| `exp_lut` | lut_size float | exp(−t) table | D |
Per pin per axis, that's 8 B in Phase D instead of v1's 20 B for both axes together. Open question:
who fills `pin_x` each iteration? In bring-up, the host. On the device, a per-axis variant of
`refresh_net_pins`. This is a pl_algo-wide layout decision; the sibling `hpwl_gradient.hpp` still
uses `NodePin`.

**D2. Drop dead nets on the host.** Leave masked (>100 pins), empty and single-pin nets out of v2's
CSR entirely. That removes the `.net` field, the mask check, and the per-net slot those nets cost in
v1 (lesson 5). HPWL is then the sum over included nets, which matches the golden's definition.

**D3. Net size: cap 16, or chunk to 100?** Proposal: design the loop structure for chunking from the
start (per-net loop → per-chunk loops), even if the first working version only handles ≤16 pins.
Lesson 2 already forces per-pin loops out of a pipelined per-net loop, which is the same shape as
the chunking plan's LOAD → SUM → GRAD.

**D4. Where does the random access go?**
- (a) Scatter in D, as in v1: D writes `pin_grad[pin_to_npin[p]]` (random writes); Phase 3 streams
  sequentially.
- (b) Gather in 3: D writes `pin_grad` in **net-major** order (sequential, burstable); Phase 3 reads
  `pin_grad[npin_to_pin[q]]` (random reads, deep outstanding queue, like `refresh_net_pins`).
  `pin_to_npin` leaves Phase D entirely, and its inverse `npin_to_pin` is used in Phase 3.
Proposal: (b), because it makes Phase D pure sequential-in / sequential-out, which suits dataflow.

**D5. How net-level values meet per-pin work.** Each pin is used three times: for the bbox (needed
before any exponential), in the B/C sums (needed before any combine), and in its own combine. Options:
buffer the net on chip (≤112 slots with chunking); Dhar Method 2 delay lines; or re-read from DDR.
Proposal: an on-chip net buffer filled by the loader.

**D6. Stage structure inside Phase D.** Plain sequential loops, or a `DATAFLOW` region of processes
connected by streams: load (reads, bbox) → terms+sums → combine → write. Proposal: start with plain
sequential loops, get them correct, then try `DATAFLOW` as one of Mark's pragma experiments.

**D7. Port width.** Start at one pin (32 bits) per cycle. 512-bit reads (16 floats per beat, Dhar's
64 B/clock) come later, once the stages can consume at that rate.

**D8. LUT.** Packed pairs (lesson 7) from the start; it's a host-side table change plus a 64-bit read.

**D9. Lane width W** (pins processed per cycle in the compute stage). Defer until C-synthesis shows
what the ports deliver.

## Build order (each item is one or more 10–20-line chunks; Mark reviews each)
0. **Directory skeleton + tier-1 harness first**, before any module code. Adapt the golden from
   `test/hpwl_dhar_test.cpp`: run it once per axis and compare x and y separately. After D3/D4 are
   settled, decide whether the golden is capped at 16 or uncapped to 100 (the chunking plan's
   verification section: `hpwl_grad_test`'s uncapped golden, plus nets of 16, 17, 32, 33, 100 and
   101 pins).
1. Host-side data preparation for the chosen layout (in the harness): live-net CSR, `pin_x`,
   the permutation, packed LUT.
2. Loader: CSR walk, fill the net buffer, bbox.
3. Term generation: LUT lookups, B/C terms.
4. Sums: adder tree(s), per chunk, accumulated per net.
5. Combine + write (scatter or net-major, per D4).
6. Phase 3 reduction (segmented stream, or gather, per D4).
7. HPWL span by-product.
8. Kernel top + interface pragmas (ask Mark first), Makefile, host → sw_emu (tier 3).

Verification bar at each functional chunk: `make test` passes. Final: rel_rms ≈ 1e-6 against the
golden per axis (v1: 1.48e-6), and HPWL relative error ≈ 1e-8. Scalar paths must match bit-exact.

## Pragma experiment log (fill in as Mark adds pragmas — C-synthesis numbers, before → after)
| date | loop | pragma added | II | depth | LUT | FF | DSP | BRAM | note |
|---|---|---|---|---|---|---|---|---|---|

## Pointers
- v1 module and its #40 comments: `vck5000/bring_up/hpwl_gradient_dhar/src/modules/hpwl_gradient_dhar.hpp`
- Timing-failure trace: [[_NEW_HANDOFF_40_hpwl_gradient_dhar_hw_grad_bug_20260910.md]], tasks.md #40
- Large-net chunking: [[_NEW_PLAN_40_dhar_large_net_chunking_20260918.md]]
- Deck brief (v1): [[_NEW_EXPLAINER_40_dhar_deck_brief_20260918.md]]
- Paper: `.claude/2_ARTIFACTS/papers/dhar_fpga_accel/` (Method 2, Figs. 10–11, for D5/D6)
- C-synthesis check: `vck5000/test/synth_check.tcl`

## Next action
Open a session, read this file, and start **Step 2** with Mark: walk through D1–D9 and record each
decision here. Then do build-order item 0. No module code before D1–D5 are decided.
