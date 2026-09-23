# HANDOFF #40 — rebuild the Dhar HPWL-gradient module from scratch as `bring_up/hpwl_gradient_dhar_v2/`

*2026-09-18. Task #40. Status (2026-09-21): **bbox/HPWL-only milestone built, synthesis-verified to
II=1 on the compute side.** D1–D9 decided; build-order items 0–7 done in a deliberately narrowed
scope (bbox/HPWL span only, no gradient yet — see "Progress" below); item 8 (kernel top) done for
that narrowed scope. Current work has moved to a **new, separate bring_up/ module, `hpwl_computer`**
(direct multi-net-per-beat feed, Dhar Method 1 reused with comparators instead of adders — a
standalone beat-parsing controller was considered and abandoned, see that file's PIVOT note) — see
[[REPORT_40_hpwl_computer_20260921.md]] for that active thread. This file is the running draft
for the overall v2 rewrite; convert to `REPORT_40_...` once v2 is verified (see the handoff policy in
`CLAUDE.md`).*

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
- **Big picture (2026-09-20): this module is meant to be heavily replicated.** The HPWL gradient is
  the compute-intensive bottleneck (lessons 6–8 are literally v1 hitting that wall), so the PL-area
  budget plan is many duplicate instances of whatever module design v2 lands on, not one large
  instance. Every module-shape decision from here on should ask not just "does one instance work"
  but "does this replicate cheaply, and how does data get orchestrated to keep N instances fed."
  This hasn't produced a concrete interconnect/data-movement design yet — it's a standing constraint
  on later choices, not yet its own decision.

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

## Step 2 — data format and dataflow: DECIDED 2026-09-19
Settled with Mark before any datapath code.

**D1. Per-axis data layout (structure-of-arrays) — DECIDED: adopt.** v1 read a 16-byte
`NodePin {node_idx, x, y, net}` per pin, but Phase D needs only the coordinate (plus `.net` of the
first pin, as a mask), and Phase 3 only `node_idx`. Layout for one axis:
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

**D1 amendment (2026-09-19) — degree-grouped batching.** Mark's insight: instead of feeding nets to
the module in arbitrary degree order, the host does a one-time sort/group of live nets by degree
(2..16 for now, per D3), so the module always receives runs of same-degree nets — "eight 2-pin nets,
then a run of 3-pin nets," etc. `net_ptr` stays as the CSR that locates a net's pins, but nets are
reordered so equal-degree nets are contiguous, and a new small **batch descriptor list**
`(degree, batch_start, num_nets_in_run)` — at most 15 entries (degree 2..16) — is what the on-device
state machine actually walks. The FSM asserts the current run's degree as a `net_degree` signal into
every downstream stage. This avoids paying full max-width (16) cost on the common small-degree nets,
the mirror image of PLAN #40's chunking, which pays extra passes only on the rare large-degree tail.
Net effect: the datapath width tracks actual degree at both ends of the distribution instead of being
sized for the worst case throughout. Consequence for D5, see below.

**D1 superseded 2026-09-20/21 — `net_ptr` and the "batch descriptor list" are GONE, replaced by
CUMULATIVE `net_count`.** Once nets are stored contiguously by degree (above), a per-net CSR is
redundant: `net_count[i]` is now defined as **the cumulative count of nets with degree ≤
i+MIN_NET_DEGREE** (not a per-degree count), computed once on the host. A net's degree resolves as
`degree = MIN_NET_DEGREE; for(k=0..NET_DEGREE_COUNTS-1) if (n >= net_count[k]) degree++;` — every
comparison depends only on the flat net index `n` and the (cached on-chip) table, with **no
carried state across net iterations at all**. This is what actually unlocked pipelining for the
control logic (see the beat-controller handoff for the full synthesis trace); the earlier
"per-degree count + running counter" formulation had a real carried-dependency chain that capped
II at 8 regardless of loop shape. `pin_offset` (into `pin_x`) is still a genuine running
accumulator — that one dependency is unavoidable given DDR addressing, not a bug.

**D2. Drop dead nets on the host — DECIDED: yes.** Masked (>100 pins), empty and single-pin nets are
left out of v2's CSR entirely. Removes the `.net` field, the mask check, and the per-net slot those
nets cost in v1 (lesson 5). HPWL is then the sum over included nets, which matches the golden's
definition.

**D3. Net size: cap 16, or chunk to 100 — DECIDED: cap 16 first, chunk later.** Get a simple ≤16-pin
datapath correct and C-synthesized first; extend to PLAN #40's chunking (LOAD → SUM → GRAD) as a
later build-order step once the base shape is proven. Note: D5 (delay lines, below) is the natural
fit for a fixed 16-pin net; when chunking lands, the delay-line depth and net-level accumulator
carry-across-chunks will need rework — flag this when that step starts.

**D4. Where does the random access go — DECIDED: (a) scatter in D.** D writes
`pin_grad[pin_to_npin[p]]` (random writes); Phase 3 streams sequentially. Keeps continuity with v1's
structure; the random-access cost stays in Phase D.

**D5. How net-level values meet per-pin work — DECIDED: Dhar Method 2 delay lines.** Each pin is used
three times: for the bbox (needed before any exponential), in the B/C sums (needed before any
combine), and in its own combine. Use shift-register delay lines (paper Method 2) to re-present each
pin's data at the right pipeline stage, rather than an on-chip net buffer or re-reading DDR.

**D5 amendment (2026-09-19) — dynamic depth, not fixed-16.** Because of the D1 amendment, the FSM
knows the current batch's degree via `net_degree`, so the delay line's active depth tracks that value
at runtime instead of always running as a fixed 16-deep shift register with padded/masked stages. This
is a materially more complex structure than a compile-time-fixed delay line — worth designing
deliberately (not discovering mid-chunk) once we reach the module's LOAD/delay-line step.

**Confirmed 2026-09-20 — NOT Dhar's multi-net block packing.** Degree-grouping here is serial: one
net at a time through the datapath, delay-line depth tracking that net's degree. This is explicitly
NOT Dhar's Fig. 6/7 technique (several same-degree nets packed into one shared 16-wide adder-tree
pass via a multi-output tree + per-index result selectors) — that remains v1's documented
"follow-on hardware-opt step, not yet built," out of scope for v2 unless revisited.

**D6. Stage structure inside Phase D — DECIDED: DATAFLOW region from the start.** Load (reads, bbox)
→ terms+sums → combine → write as stream-connected processes from the first working version, not
deferred to a later pragma experiment. Note: tier-1's `hls::stream`-as-`std::deque` (lesson 10) can't
catch FIFO-depth deadlocks — those only show up in co-simulation, so budget for that check once the
DATAFLOW region is in place.

**D7. Port width — DECIDED: one pin (32 bits) per cycle first.** 512-bit reads (16 floats per beat,
Dhar's 64 B/clock) come later, once the stages can consume at that rate.

**D8. LUT — DECIDED: packed pairs from the start.** `(lut[i], lut[i+1])` packed into one 64-bit word
(lesson 7); host-side table change plus a 64-bit read.

**D9. Lane width W — DECIDED 2026-09-20: `LANES = 8`, a named/configurable constant** (not
hardcoded), so trying `LANES = 16` later is a one-constant change, not a rewrite. This resolves the
Method-1-vs-Method-2 fork directly: v1's actual measured failure (lessons 6–8) was routing
congestion from having all 16 lanes of term-gen/adder-tree/LUT-reads physically instantiated and
simultaneously active, not wasted cycles — so a dynamically-degree-guarded but still-16-wide block
(Method 1 with a runtime `if (k<degree)` guard) would NOT fix it: HLS still builds the worst-case
width regardless of the guard. Narrowing the physically-instantiated width to `LANES=8` is what
actually shrinks the hardware, at the cost of needing more than one pass for nets wider than
`LANES` (see the simplifying assumption below).

**D9 simplifying assumption (2026-09-20) — nets of degree > LANES are out of scope for now.** To
keep the build reviewable, v2's current pass assumes every live net's degree is `<= LANES` (i.e.
the effective cap for this phase is 8, not `MAX_NET_DEGREE`'s eventual 16) — a whole net fits in one
`LANES`-wide pass, so **no delay line is needed yet**: this is still Method-1-shaped (load, then
compute) but at width `LANES` instead of 16. Multi-wave processing (`ceil(degree/LANES)` waves per
net, needed to reach `MAX_NET_DEGREE=16`) is deferred to a later build step; that is precisely where
the delay-line design (holding an earlier wave's per-pin terms until the last wave completes the
net-level sum) becomes necessary again — see the D1/D5 amendments above. Net degrees in
`(LANES, 100]` are excluded from the host data entirely for now (same treatment as the existing
17–100 deferral), so there is currently no CAP-test / Phase-Z need either.

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

## Progress (2026-09-19 → 2026-09-21) — bbox/HPWL-only milestone
Build-order items 0–8 done, but **deliberately narrowed to bbox/HPWL span only** (no exp-LUT
term-gen, no B/C sums, no gradient combine, no phase-3 reduction) — a scoped-down "does the control
shape work at all" milestone before the full gradient datapath. Files:
- `vck5000/bring_up/hpwl_gradient_dhar_v2/src/modules/hpwl_gradient_dhar_v2.hpp` — the module.
- `vck5000/bring_up/hpwl_gradient_dhar_v2/src/hpwl_gradient_dhar_v2_top.cpp` — kernel top, **trimmed**
  to the 4 params this milestone uses (`net_count`, `pin_x`, `out_hpwl`, `num_nets`); the rest of the
  eventual signature is nulled inside, not exposed as ports yet.
- `vck5000/bring_up/hpwl_gradient_dhar_v2/synth_check.tcl` — lightweight C-synthesis-only project
  (mirrors `test/synth_check.tcl`'s pattern; does NOT need the remote build server — see Pointers).
- `vck5000/test/hpwl_dhar_v2_test.cpp` — tier-1 harness, bit-exact vs. an independent golden.

**What's inside the module**: `max_scan8`/`min_scan8` — a Hillis-Steele/Kogge-Stone parallel-prefix
scan (3 stages, distances 1/2/4) computing `max(v[0..i])`/`min(v[0..i])` for **every** `i` at once,
so a net's own degree just indexes the right lane (`l3[degree-1]`) — no padding-with-neutral-values
needed, exact for any degree 2..`LANES`. Split into two pipeline stages connected by two parallel
`hls::stream`s (a `PinBlock{v[LANES]}` data channel + a plain `int` "degree" sideband, deliberately
NOT bundled into one struct — see the beat-controller handoff for why a per-net sideband is correct
and a "once-per-run" descriptor would reintroduce state into the wrong stage):
  - **`input_controller`**: resolves each net's degree (cumulative `net_count`, see the D1
    supersession above) and loads its pins narrow (1 float/cycle) from `pin_x`.
  - **`compute`**: pure value-in/value-out — `max_scan8(v,d) - min_scan8(v,d)`, no memory access at
    all, deliberately, so it can eventually replicate without carrying any DDR ports along.

**Synthesis results** (`vitis_hls -f synth_check.tcl`, part `xcvc1902-vsvd1760-2MP-e-S`, 3.33 ns):
| loop | II | depth | note |
|---|---|---|---|
| `cache_net_count` | 1 | 3 | on-chip cache of `net_count`, clean from the start |
| `input_controller` (v1: `while`) | — | — | **would not pipeline at all** — "variable loop bound," can't unroll a `while` inside a `PIPELINE`d loop |
| `input_controller` (v2: bounded `for` + running counter) | 8 | 88 | synthesizable now, but a real `icmp`/`add` carried-dependency chain caps it |
| `input_controller` (v3: cumulative `net_count`, parallel boundary-count) | 8 | 81 | dependency chain is GONE; II=8 is now purely `gmem1` (pin_x) port contention — 8 unrolled reads/cycle, one port |
| `compute` (no `ARRAY_PARTITION`) | 5 | 12 | `l3`/`l3_1` (scan stage arrays) synthesized as limited-port memories |
| `compute` (+ `ARRAY_PARTITION complete` on `l1`/`l2`/`l3`) | **1** | 10 | 🎯 target hit |

**The remaining `input_controller` II=8 is a DDR-bandwidth-shape problem, not a logic problem** —
see [[REPORT_40_hpwl_computer_20260921.md]] for the full diagnosis and the new module this
spawned.

## Pragma experiment log (fill in as Mark adds pragmas — C-synthesis numbers, before → after)
| date | loop | pragma added | II | depth | LUT | FF | DSP | BRAM | note |
|---|---|---|---|---|---|---|---|---|---|
| 2026-09-21 | `input_controller`'s pin-load | `ARRAY_PARTITION` NOT the fix here (see beat-controller handoff) | 8→8 | — | — | — | — | — | DDR port, not an array; partitioning an array can't fix an m_axi port conflict |
| 2026-09-21 | `max_scan8`/`min_scan8`'s `l1`,`l2`,`l3` | `ARRAY_PARTITION variable=l{1,2,3} complete dim=1` | 5→1 | 12→10 | — | — | — | — | matches v1's `dhar_adder_tree16` precedent exactly |

## Pointers
- v1 module and its #40 comments: `vck5000/bring_up/hpwl_gradient_dhar/src/modules/hpwl_gradient_dhar.hpp`
- Timing-failure trace: [[_NEW_HANDOFF_40_hpwl_gradient_dhar_hw_grad_bug_20260910.md]], tasks.md #40
- Large-net chunking: [[_NEW_PLAN_40_dhar_large_net_chunking_20260918.md]]
- Deck brief (v1): [[_NEW_EXPLAINER_40_dhar_deck_brief_20260918.md]]
- Paper: `.claude/2_ARTIFACTS/papers/dhar_fpga_accel/` (Method 2, Figs. 10–11, for D5/D6)
- C-synthesis check (v2): `vck5000/bring_up/hpwl_gradient_dhar_v2/synth_check.tcl` — **runs locally,
  no remote build server needed**: `source /tools/Xilinx/Vitis_HLS/2022.2/settings64.sh` (note: a
  SEPARATE product directory from `/tools/Xilinx/Vitis/2022.2/`, which does not itself contain the
  `vitis_hls` binary), then `vitis_hls -f synth_check.tcl` from the module's directory. ~30–50 s.
- C-synthesis check (shared modules): `vck5000/test/synth_check.tcl` (different modules, unrelated).
- **Active thread**: [[REPORT_40_hpwl_computer_20260921.md]] — `hpwl_computer`, the
  multi-net-per-beat HPWL bbox module, spawned directly from the `input_controller` II=8 finding
  above (a standalone beat-parsing controller was the first idea; see that file's PIVOT note for
  why it was abandoned in favor of direct multi-net beat feed).

## Next action
See [[REPORT_40_hpwl_computer_20260921.md]] — that is the live thread. Once `hpwl_computer`
lands, the gradient work (term-gen/LUT/B-C-sums/combine, this milestone's deliberately-skipped
scope) is **task #41, `hpwl_gradient_computer`** — see tasks.md.
