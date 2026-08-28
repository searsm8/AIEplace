# REPORT #20 — hpwl_gradient: measured bottleneck and proposed restructuring

**Date:** 2026-08-28
**Module:** [[hpwl_gradient.hpp]] (`hpwl_CU`), `vck5000/pl/src/pl_algo/src/modules/`
**Status:** analysis + proposal. Nothing here is implemented. The prerequisite —
tier-1 coverage — landed first as `0f9e9ef` ([[hpwl_grad_test.cpp]]).
**Related:** [[DATAFLOW.md]], tasks.md #20 step 3, [[REPORT_pl_algo_stage5_assessment_20260806.md]]

---

## 1. Verdict

`hpwl_CU` is **memory-bound on random DDR gathers, not compute-bound**, and it occupies
**~1 % of the VC1902** while doing it. The two reported II violations are real but are the
smallest of three problems. Ranked by leverage:

| | problem | evidence | proposed fix | est. gain |
|---|---|---|---|---|
| **L3** | no spatial parallelism: 35 DSP, one pin per 2–3 cycles | csynth utilization | replicate P-ways over nets/nodes | 10×+, **blocked by L2** |
| **L2** | 3 truly-random `node_pos` gathers of `num_pins` each per iteration (one of them in `metrics`), plus the random scratch reads; none burst | HLS burst log | P1b + P1 + P2 below | ~2.5–3× |
| **L1** | `sweep_bbox` II=2, `sweep_sums` II=3 against a target of 1 | csynth loop tables | P3 | 2.5× on the compute floor |

The order matters: **L2 gates L3.** Replicating a gather-bound pipeline P ways just puts P
random streams on one DDR channel. Fix the access pattern first, then parallelize.

---

## 2. Evidence

All from the last real `TARGET=hw` C-synthesis,
`build/hw/pl/pl_algo/_x_plpl_algo_aiemarkv1_P1D1/temp/top/top/top/solution/syn/report/`
(dated 2026-07-13; `hpwl_gradient.hpp`'s last functional commit is `c48fba0`, 2026-07-01,
so the report still describes the current datapath).

### 2.1 Loop initiation intervals — measured

```
sweep_bbox  II = 2  (target 1)   depth 146
sweep_sums  II = 3  (target 1)   depth 166
seg_reduce  II = 1               depth 173
clear_grad  II = 1
cache_lut   II = 1
```

`vitis_hls.log` names both causes exactly:

- `sweep_bbox` — carried dependence between the `select` and `fcmp` for `miny` (line 102).
  The min/max chain does not close in one cycle.
- `sweep_sums` — carried `fadd` on `Bpx` (line 135) against the `br` at line 118 (the
  `if (r.net < 0) continue`), plus a second violation on the `fmadd` for `Cpx`.

### 2.2 Memory interfaces — measured

From the HLS burst-inference log:

```
sweep_bbox / sweep_sums   gmem2 (net_pins)   burst inferred, bit width 128
seg_reduce                gmem3 (node_pins)  burst inferred, bit width 128
clear_grad                gmem7 (node_grad)  burst inferred, bit width  64
gmem0 (node_pos), gmem5 (bb), gmem6 (sums)   -- NO burst inferred at all
```

Two independent problems in that table:

1. **The sequential streams run the bus at 12.5–25 %.** The physical `m_axi` is 512-bit.
   `NodePin` is 16 B so pin streams move 128 bits/beat; `coord_t` is 8 B so `clear_grad`
   moves 64 bits/beat. HLS could not widen because each loop iteration consumes exactly one
   record.
2. **Three ports do not burst at all** — they are indexed gathers.

For contrast, the density path in the same log: `gmem10/gmem11`, **512-bit, bursts of 65536**,
and those are the only two bundles that were given `num_read_outstanding=32 /
max_read_burst_length=64` ([top.cpp:130](vck5000/pl/src/pl_algo/src/top.cpp:130)). None of the
HPWL bundles were given anything.

### 2.3 Resources — measured

```
hpwl_CU total:  14 BRAM_18K   35 DSP   21.9k FF   16.9k LUT
device:       1934 BRAM_18K 1968 DSP  1799k FF   899k LUT      -> ~1 %
```

The LUT table was auto-replicated by HLS into 2 memories × 7 BRAM_18K, so table read ports
are **not** the II limiter; the log agrees, naming the float accumulator recurrences instead.

### 2.4 Design sizes — measured

Counted directly from the bookshelf files (`~/phd/Xplace/data/raw/ispd2005/`):

| design | nodes | nets | pins | masked nets | masked pins | max degree |
|---|---|---|---|---|---|---|
| adaptec1 | 211,447 (543 fixed) | 221,142 | 944,053 | 1,350 | 4,127 (**0.44 %**) | 2,271 |
| bigblue4 | 2,177,353 | 2,229,886 | 8,900,078 | 1,671 | 125,411 (**1.41 %**) | 20,766 |

"Masked" = degree ≤ 1 or degree > `IGNORE_NET_DEGREE` (100), i.e. what
[Packer.cpp:57](vck5000/host/src/pl_algo/src/Packer.cpp:57) tags `net = -1`.

Two consequences, both used below:

- **Masked pins are only 0.4–1.4 % of the stream.** Compacting them out on the host is
  therefore *not* a bandwidth win — see P4, whose value is entirely II and memory safety.
- **Every gradient-bearing net has degree ≤ 100 by construction.** This is what makes the
  phase-1/2 fusion (P1) cheap, and it is a guarantee, not a heuristic.

### 2.5 Cost model — ESTIMATE, not measured

There is no real-hardware timing (Geert owns the card), so the following is a model with
stated assumptions, not a measurement.

Compute floor, adaptec1, at 300 MHz, ignoring all DDR stalls:

```
sweep_bbox  2 x 944k = 1.89M
sweep_sums  3 x 944k = 2.83M
clear_grad  1 x 211k = 0.21M
seg_reduce  1 x 944k = 0.94M
                     --------
                       5.87M cycles ~ 20 ms / iteration
```

Memory side. Assume ~150-cycle DDR round trip and the **default 16 outstanding reads** on
`gmem0`. Sustained random-read throughput ceilings at `D/L = 16/150 ≈ 0.107` txn/cycle, so the
944k-entry `node_pos` gather in `sweep_bbox` alone needs ~8.8M cycles against a 1.89M-cycle
compute budget — **~4.7× above the compute floor**. The module is gather-bound.

This is the same shape as the closest published precedent: Dhar FPL-2019 got 3.03× on the
gradient kernel but only 2× end-to-end, and Fig. 12 attributes it to gather (26.8 %)
exceeding the kernel itself (21.2 %). See [[dhar_fpga_accel_paper]].

---

## 3. Current datapath

Five sequential loops, no `DATAFLOW`, one scalar pipeline each.

| # | loop | trip | sequential streams | random gathers | writes |
|---|---|---|---|---|---|
| 0 | `cache_lut` | ≤1024 | `exp_lut` | — | `lut_BRAM` |
| 1 | `sweep_bbox` | `num_pins` | `net_pins` | `node_pos[node_idx]` | `bb[net]` on net change |
| 2 | `sweep_sums` | `num_pins` | `net_pins` **again** | `node_pos`, `bb[net]` | `sums[net]` on net change |
| 3 | `clear_grad` | `num_movable` | — | — | `node_grad[n]` = 0 |
| 4 | `seg_reduce` | `num_node_pins` | `node_pins` | `node_pos`, `bb[net]`, `sums[net]` | `node_grad[node]` on node change |

Phases 1→2 are a sparse adjacency in net-major order; phase 4 is its transpose in node-major
order. `bb`/`sums` are DDR scratch `[num_nets]` bridging the two orderings.

### 3.0 There is a fourth pass, in another module

[[metrics.hpp]] `hpwl_sweep` (lines 38–53) is a **fourth pass over pin data** — the same
`net_pins` array — running every iteration to produce the HPWL the scheduler consumes. It is a segmented
bounding-box reduction over nets — its own comment says *"mirrors hpwl_CU phase A1"* — with
its own **truly random** `node_pos[r.node_idx]` gather and the same `r.net < 0` mask.

It is a different module, so it is easy to miss when reading `hpwl_gradient.hpp` alone, but it
is in the same per-iteration critical path (DATAFLOW.md resident-loop step 4) and it is
evaluated at the same probe `v_k` as the gradient (step 1). Two consequences:

- The per-iteration random-gather count is **three** truly-random `num_pins` passes, not two.
- It computes a bounding box that `sweep_bbox` has already computed and thrown away. See P1b.

### 3.1 The gathers are NOT equally expensive

This distinction drives everything below and is easy to miss.

`node_pins` is `stable_sort`ed by `node_idx`
([Packer.cpp:70](vck5000/host/src/pl_algo/src/Packer.cpp:70)), so in `seg_reduce` the index
`r.node_idx` is **monotonically non-decreasing**. That gather is forward-only with excellent
page locality — HLS still did not infer a burst, but its real DDR cost is far below a true
random access.

`net_pins` is net-major, so in `sweep_bbox`, `sweep_sums` and `metrics::hpwl_sweep` the node
index is **arbitrary**. Those three are the genuinely random gathers, and they are the ones
worth attacking.

---

## 4. Proposals

### P1 — Fuse phases 1 and 2 behind a bounded FIFO

**What.** One pass over `net_pins`. Push each net's pin coordinates into a FIFO while
reducing the bounding box in registers; at the net boundary, drain the FIFO and compute the
B/C sums against the now-final bbox.

**Why it is safe here.** The module's own comment defends the two-pass structure on the
grounds that it "has no degree cap". That premise is false: `IGNORE_NET_DEGREE = 100` is
already enforced by the host, and §2.4 confirms **max gradient-bearing degree = 100 by
construction** on every design in scope. A 100-deep FIFO can never overflow. At 8 B/entry
that is 800 B — well under one BRAM18.

**Gain.** Removes one full `num_pins` sweep *and* one of the two truly-random `node_pos`
gathers. Also **eliminates `bb_DDR` entirely** — no scratch array, no `[num_nets]` DDR
allocation, and the random `bb[net]` read in phase 2 disappears with it. Phase 4 still needs
a bbox per net, so `bb_DDR` survives only if phase 4 stays separate (it does).

> Correction to the recommendation I gave verbally: `bb_DDR` is **not** fully removable,
> because `seg_reduce` reads `bb[r.net]`. P1 removes phase 2's read of it, not the array.

**Cost / risk.** This is the change that makes the `hpwl_lut_exp` lower-bound hazard live —
see §5. A fused phase 2 that consumes the FIFO before the net closes sees a partial bounding
box, `b.mxx - x` goes negative, and the LUT is indexed at a negative offset. **Mutation M5 in
the harness is exactly this bug** and it fails loudly ([1], [2], [4]).

**Verify.** `make test` — M5 coverage already exists.

---

### P1b — Emit HPWL as a by-product of the bbox pass  *(largest single saving; do this first)*

**What.** `sweep_bbox` (or the fused P1 pass) already has each net's final `maxx/minx/maxy/miny`
in registers at the net boundary. Accumulate `(maxx-minx)+(maxy-miny)` into a double there and
hand it out as a second output. `metrics::hpwl_sweep` then deletes entirely; `metrics` keeps
only its `ovfl_sweep` over `bin_density`.

**Why this is free.** Both passes reduce the same quantity over the same array with the same
mask at the same probe position. The HPWL accumulator is one `fadd` per *net* (~221k on
adaptec1), not per pin, and it is off the critical recurrence.

#### Is the timing safe? Checked, and yes — this is an invariant the codebase already enforces

The obvious worry is that the convergence signal wants HPWL measured *after* the step, while
`hpwl_CU` runs *before* it, which would make the fused value one iteration stale. That is not
what any of the three implementations do:

- **sw_only.** `performIteration` (AIEplace.cpp:40) runs `performNextStep()` and *then*
  `recordIterationResults()` — so it does look post-step. But `performNextStep` (Step.cpp:287)
  is `stepAllNodes()` **first**, then `computeHpwlPartials()` at the *new* probe. And
  `recordIterationResults` (Output.cpp:568) measures with `at_probe = true`. So the step, the
  gradient and the metric all land on the same v̂. The comment there is explicit: *"This is the
  one `hpwl` XPlace's evaluator_fn produces: it feeds the recorder's delta_hpwl, convergence,
  AND update_best_sol alike, so all three describe the same placement as the overflow computed
  just below."*
- **It was a bug, and it was fixed on purpose.** AIEplace.cpp:123 records that sw_only *used
  to* store `u` while measuring HPWL at `u` and overflow at `v` — *"a pair no single iteration
  ever held"* — and TODO #32/7a changed it so everything measures at `v`, matching XPlace's
  single position variable (`nesterov_optimizer.py:71`, "directly use p as v_k to save memory").
- **pl_algo host loop.** `eval_gradients(v)` writes `node_pos[n] = v[n]`
  ([Driver.cpp:996](vck5000/host/src/pl_algo/src/Driver.cpp:996)) and `hostHPWL(node_pos, …)` is
  called on that same buffer three lines later, before the step.
- **`metrics.hpp` states it as its contract** (lines 16–17): *"node_pos here carries the probe
  positions v (the same positions the gradient pipeline was evaluated at this iteration); the
  host binds them before this call."*

So HPWL/overflow and the gradient are **already required to be co-located at v_k**, and
keeping them so is a landed bug-fix rather than an accident. P1b does not introduce that
coupling — it makes an existing invariant structural instead of conventional. If the two ever
drift apart, that is the #32/7a bug returning.

**The one real timing constraint** is backtracking. sw_only's `performNextStep` can run
`stepAllNodes` + `computeHpwlPartials` several times per iteration (Step.cpp:300-319), and HPWL
is recorded once, at the **accepted** trial. So a by-product HPWL must be taken from the last
trial, not the first. DATAFLOW.md says pl_algo v1 has no backtracking, so this does not bite
today — but it must be written at the emission site before backtracking lands.

**Second constraint:** `recordIterationResults` calls `computeTotalWirelength(wirelength_method,
…)` where the method is config-driven. The by-product computes half-perimeter specifically. Fine
while `wirelength_method = "HPWL"`; it silently diverges if that config knob ever selects
another metric.

**Gain.** Removes an entire `num_pins` pass **and one of the three truly-random gathers** —
proportionally the single largest saving in this report, and it needs no format change and no
new contract.

**Cost / risk.** Couples two modules that are currently independent: `metrics` stops being
self-contained, and `top.cpp` must route the HPWL out of the gradient module. If the resident
loop ever wants HPWL at a probe *other* than the gradient's `v_k`, this fusion has to be
undone — worth a comment at the site saying so. Check the double-accumulation order matches,
since `metrics` sums in double and per-net order.

**Verify — DONE, the gate is in place.** Assertion **[6]** was added to [[hpwl_grad_test.cpp]]
on 2026-08-28: it reduces the `bb_DDR` bounding boxes phase 1 already writes to an HPWL and
compares against a double golden. That is exactly the quantity P1b would emit, so the check
exists *before* the implementation. Observed relative error **0.0 exactly** — the bbox is a
min/max over the same float pin positions and both sides accumulate in double in net order.

`metrics.hpp` itself cannot be covered by a tier-1 harness as written: it includes
`../formats.hpp`, which pulls `ap_int.h` / `hls_stream.h` / `ap_axi_sdata.h`. That is the same
wall that made `density_bin_model.cpp` keep its own copy of `node_footprint`, and it is the
real blocker on #20 step 3's remaining modules — see §9.

Assertion [6] is **not redundant with [1]**. Mutation M8 (perturb `maxy` by 1.0 in
`sweep_bbox`) is caught by [6] *only*: phases 2 and 4 both read the same corrupted `bb_DDR`, so
the gradient stays self-consistent, and the WA partial is smooth enough in the bounding box
that a 1.0-unit error on a 10000-unit die lands inside [1]'s tolerance. HPWL is a direct sum of
extents and has no such slack. M9 (drop the last-net flush) is likewise [6]-only.

---

### P2 — Denormalize absolute pin position into `NodePin`  *(the enabler for L3)*

Mark's proposal, analysed in full in §6. **Yes, it is a win**, and the constant-size form is
better than the 24-byte variant I suggested verbally.

---

### P3 — Fix the two II violations

**What.** Both are the standard segmented-reduction fix: keep 2–3 interleaved partial
accumulator sets and merge them at the key change. For `sweep_bbox` that means 2 partial
bounding boxes merged at the net boundary; for `sweep_sums`, 2–3 partial B/C register sets.

**Gain.** Compute floor 5.87M → ~2.4M cycles, i.e. **2.5×** — but only once L2 is fixed, since
today the compute floor is not what the module is waiting on.

**Cost / risk.** This **reassociates the float summations**, so results change in the last
bits. Assertion [1] in the harness is tolerance-based (2e-6 / 2e-5 against observed 1.32e-6 /
1.13e-5) precisely so a legitimate reassociation passes while a restructuring bug — which
lands at 1e-1..1e0 — fails. Do not tighten those bounds.

**Verify.** `make test`; expect [1] to move within tolerance, not to zero.

---

### P4 — Compact masked pins out of `net_pins` on the host

**What.** `net_ptr` is already dead inside `hpwl_CU` (the signature says *"unused: kept for
ABI"*; only `net_ptr[num_nets]` is read, to recover `num_pins`). So the host can drop masked
pins from `net_pins` entirely and pass `num_pins` as a scalar, deleting both
`if (r.net < 0) continue;` branches.

**Gain — smaller than it looks.** §2.4 measures masked pins at **0.44 % (adaptec1) / 1.41 %
(bigblue4)** of the stream, so this is *not* a bandwidth win. Its real value is twofold:
it removes the `br` that HLS named in the `sweep_sums` II violation (helping P3), and it
removes the `bb_DDR[-1]` hazard structurally rather than by a guard.

> This corrects the verbal framing, which implied the pin-count reduction "could be
> meaningful". Measured, it is not.

**Cost / risk.** `net_ptr` stops being a valid CSR into `net_pins`. Anything that later wants
per-net indexing must rebuild it. Worth confirming nothing else in `top.cpp` relies on it.

---

### P5a — Size the m_axi adapters to the access pattern  *(IMPLEMENTED 2026-08-28)*

**What.** Per-port `num_read_outstanding` / `max_read_burst_length` on the HPWL-path bundles,
each tuned to the pattern the HLS burst log actually shows. No C restructuring.

> ### ⚠️ Correction: there is no bank-spreading to be had on this platform.
> This proposal originally had a second half — assign the hot bundles to different memory banks
> with `sp=` tags, since none exist in `design.cfg` or the generated link config. **That was
> wrong, and checking it was the first thing done at implementation time.** `platforminfo` on
> `xilinx_vck5000_gen4x8_qdma_2_202220_1` reports exactly two SP tags:
>
> ```
> Memory Information
> ==================
>   Bus SP Tag: BRAM
>   Bus SP Tag: MC_NOC0
> ```
>
> **One memory controller.** Every bundle necessarily shares it, so `sp=` tags cannot separate
> the gathers from the density path's bursts — there is nothing to separate them onto. The
> absence of `sp=` tags in the build is correct, not an oversight. Contention between the twelve
> bundles is real but is not addressable this way; it is an argument for *reducing the number of
> random passes* (P1b/P1/P2), which is what those proposals do.

**Why outstanding depth is the knob that matters for a gather.** Throughput is Little's law:
`in-flight ÷ latency`. For a *sequential* stream the burst does the latency hiding — one
address buys 64 beats, so depth barely matters. For a *random* gather every element is its own
transaction, so burst length cannot help and **outstanding depth is the only latency-hiding
mechanism left**. At the default 16 and a ~150-cycle round trip that is ~0.107 txn/cycle;
at 64 it is ~4× that, until the controller saturates. The ports that most need depth
(gmem0/5/6, the gathers) are exactly the ones that were given none.

**The rule applied.** Sequential and random ports want *opposite* settings, and the adapter
buffer costs `num_outstanding × max_burst × width/8`, so raising both on one port wastes BRAM:

| bundle | pattern | setting | why |
|---|---|---|---|
| gmem0 `node_pos` | random gather (hpwl_CU, metrics) **but** bursts in `iteration_update` | `num_read_outstanding=64` | depth only — shortening the burst would cripple the shared stream |
| gmem2 `net_pins` | sequential, bursts | `max_read_burst_length=64` | burst does its own latency hiding; default depth is enough |
| gmem3 `node_pins` | sequential, bursts | `max_read_burst_length=64` | as above |
| gmem5 `bb` | random read, boundary write | `num_read_outstanding=64` | never bursts; depth is the only lever |
| gmem6 `sums` | random read, boundary write | `num_read_outstanding=64` | as above |
| gmem7 `node_grad` | sequential writes (`clear_grad`, `force_gather`) | `max_write_burst_length=64` | shared with `iteration_update` reads, so writes only |

gmem1/4/8/9 were left alone: out of the HPWL path, and a change there would not be surgical.

**Gain.** Cheapest item in this report, and the **cheapest way to test whether §2.5's model is
right** — if deeper queues move the wall-clock, the module is gather-bound as claimed; if not,
the model is wrong and P1/P2 should be re-argued before being built. **That experiment needs the
card, which is Geert's** — C-synthesis can confirm the design still builds and what it costs,
but it cannot confirm the speedup. Do not record P5a as "working" on synthesis evidence alone.

**Cost — MEASURED, 2026-08-28.** Full `v++ -c --target hw` C-synthesis, before and after, same
flow and same platform:

| metric | baseline | P5a | delta |
|---|---|---|---|
| BRAM | 406 (20 %) | 406 (20 %) | **0** |
| DSP | 220 (11 %) | 220 (11 %) | 0 |
| FF | 165,053 (9 %) | 165,179 (9 %) | +126 |
| LUT | 323,638 (35 %) | 337,793 (37 %) | **+14,155 (+4.4 %)** |
| timing slack | −0.58 ns | −0.58 ns | **0** |
| `sweep_bbox` / `sweep_sums` / `seg_reduce` II | 2 / 3 / 1 | 2 / 3 / 1 | 0 |
| synthesis errors | 0 | 0 | — |

Estimated Fmax on the P5a build is 331.90 MHz. Burst inference is unchanged everywhere: the
sequential ports still burst at the same widths, and gmem0/5/6 still do not burst — which is
the point, they are gathers.

> **Prediction that was wrong, recorded deliberately.** The cost was estimated beforehand as
> ~96 KB of adapter buffering ≈ 25–45 BRAM36 (~6 % of device BRAM), with timing as the main
> risk. **Both were wrong.** HLS allocated **zero** extra BRAM and paid in LUTs instead
> (+4.4 %), and the timing slack did not move at all. The lesson generalizes: `num_read_outstanding`
> on this platform buys queue depth out of LUT/FF, not BRAM, so the area argument against deep
> queues on the *other* bundles (gmem1/4/8/9) is weaker than assumed.

**Cost / risk — what is still unknown.** `top` already missed timing at −0.58 ns *before* this
change and still does; that is a pre-existing issue this proposal neither causes nor fixes.

---

### P5b — Widen the sequential pin streams to 512 bits

**What.** Pack 4 `NodePin` per 512-bit beat (and 8 `coord_t` for `clear_grad`) so the
sequential streams stop running the bus at 12.5–25 %.

**Why HLS did not do this already.** `m_axi_max_widen_bitwidth=512` is *already set globally*
(vitis_hls.log line 24) — the density path got 512-bit beats from it. It could not be applied
to the pin sweeps because each loop iteration consumes exactly one `NodePin`, so there is
nothing to coalesce.

**Cost / risk.** This is **not** a one-liner, and grouping it with P5a was misleading. Consuming
4 records per iteration interacts with the segmented-reduction flush logic — a net or node
boundary can now fall mid-beat, so each of the three sweeps needs its boundary handling
rewritten. Real work, and it should come after the structural changes (P1/P1b/P2) that decide
how many sweeps there even are.

---

### P6 — Overlap `clear_grad` with `sweep_bbox`

Disjoint buffers, no data dependence — put both in a `DATAFLOW` region so they run as
concurrent processes. Worth ~4 % of trip count. Smallest item; list it last and do it last.

Note this is not "rolling clear_grad into sweep_bbox" as a fused loop body — the trip counts
differ (`num_movable` vs `num_pins`) and the buffers are unrelated. It is process-level
concurrency, not loop fusion.

---

## 5. A latent hazard P1 would activate

`hpwl_lut_exp` bounds its index **from above only**:

```c
int idx = (int)idx_f;
if (idx >= lut_size - 1) return 0.0f;      // upper bound checked
// no lower bound: lut_BRAM[idx] with idx < 0 is out of bounds
```

Today every distance handed to it is `>= 0` by construction (`b.mxx - x` where `mxx` is that
net's final max), so this is safe. It stops being safe the moment a partial bounding box can
reach it — which is precisely what P1 introduces if the FIFO drain is mis-ordered.

Found via harness mutation M2 (drop the `sweep_sums` mask), which is invisible to every value
comparison — the gradient comes out **bit-identical**, because masked nets never reach
`node_pins` and both scratch writes are already guarded by `>= 0`. It only shows up under
`make test-asan`.

**Recommendation:** add the lower bound (`if (idx < 0) return 1.0f;`) as part of P1, not
before — it is dead code today, and the harness documents why it would stop being dead.

---

## 6. Mark's `NodePin` contract — analysis

### 6.1 The proposal

```c
struct NodePin {
    int32_t node_idx;   // index into node_pos[]
    float   x;          // ABSOLUTE pin position, offset already folded in
    float   y;
    int32_t net;        // owning net id, or -1 if the net has no gradient
};
```

Store the absolute pin position rather than the static offset, and make it a contract that
the Memory Writer refreshes it. **Struct stays 16 B.**

### 6.2 Is it a win? Yes.

It is strictly better than the 24-byte "add coordinates alongside the offset" variant I
suggested verbally, and for the reason Mark gives: node position and offset are only ever
consumed as their sum, so carrying both is carrying a value and its addend when only the
total is read. Keeping 16 B means **all the gather savings with none of the sequential-traffic
penalty** — the 24 B form would have traded 2× sequential traffic for the gather, which is a
good trade but a strictly worse one than this.

Direct effects inside `hpwl_CU`:

- All three `node_pos_DDR[r.node_idx]` gathers **disappear**. Every loop becomes a pure
  sequential stream.
- The `gmem0` port drops out of the module entirely (one fewer AXI master).
- The `c.x + r.off_x` add leaves the datapath (minor, but it is on the critical recurrence in
  `sweep_bbox`).

### 6.3 What it actually costs — be precise about this

The refresh is **not** free, and the honest accounting is narrower than "3 gathers → 0".

Per §3.1, the three gathers are not equal:

| gather | pattern today | after the contract |
|---|---|---|
| `sweep_bbox` `node_pos` | **truly random** | gone |
| `sweep_sums` `node_pos` | **truly random** | gone |
| `metrics::hpwl_sweep` `node_pos` | **truly random** | gone — same array, free benefit |
| `seg_reduce` `node_pos` | monotone (node-major sort) — cheap | gone |

Note the third row: because `metrics` reads the *same* `NodePin` array, P2 fixes it with no
extra work. That is a point in P2's favour that P1 and P1b do not share.

And the refresh reintroduces:

| array | refresh pattern | cost |
|---|---|---|
| `node_pins` (node-major) | node_idx is monotone, so `node_pos` is read forward-only and `node_pins` written sequentially | **near-free** |
| `net_pins` (net-major) | node_idx arbitrary → one **truly random** gather of `node_pos`, `num_pins` deep | one random pass |

So the net effect is **three truly-random passes → one**, not three → zero: the refresh of
`net_pins` still has to gather. P1 and P1b each independently remove one of those three, so
the three proposals overlap and their gains do not simply multiply. Roughly:

| state | truly-random `num_pins` passes / iteration |
|---|---|
| today | 3 (`sweep_bbox`, `sweep_sums`, `metrics::hpwl_sweep`) |
| + P1 (fuse 1+2) | 2 |
| + P1b (HPWL by-product) | 1 |
| + P2 (absolute positions) | 1 — but now *outside* the float datapath |

### 6.4 Why it is still clearly worth doing, even given that overlap

The qualitative argument is stronger than the count:

1. **The remaining random pass becomes pure data movement.** No dependent float pipeline, no
   segmented-reduction state, no II violation. It can be given a dedicated wide port and a
   deep outstanding-request queue and tuned in isolation — none of which is possible for a
   gather embedded in the `sweep_sums` accumulator recurrence, where every stalled read stalls
   the float datapath behind it.
2. **It can overlap with unrelated work.** A standalone refresh pass can sit in a `DATAFLOW`
   region alongside the density solve, hiding its latency behind the FFT path. A gather inside
   the sums pipeline cannot.
3. **It unblocks L3.** Once `hpwl_CU` is pure sequential streaming, replicating it P ways over
   net ranges (phases 1–2) and node ranges (phase 4) is straightforward — nets and nodes are
   independent, and the outputs are disjoint. That is the 10×, and it is unreachable while the
   inner loops gather.

### 6.5 Costs to accept, explicitly

- **Denormalization.** Node position now lives in three places (`node_pos`, `net_pins`,
  `node_pins`) instead of one. Today a stale pin position is *structurally impossible*; after
  this it becomes a class of bug that exists. The "Memory Writer is the single coords writer"
  invariant in [[DATAFLOW.md]] stage 4 must be restated as **single writer of coords and both
  pin-position arrays**, and that line is the whole defence.
- **The arrays stop being static.** `net_pins` becomes read-write per-iteration state rather
  than read-only design data uploaded once. That changes the host boundary described in
  DATAFLOW.md's "Host boundary (once each)".
- **The gradient is evaluated at the probe `v_k`, not `u_k`.** The contract must say
  explicitly that the pin arrays carry `v`, matching `node_box.{x,y}`. Getting this wrong is
  silent — it would still converge, just to the wrong trajectory.
- **The static offset is no longer on the device.** Fine for `hpwl_CU`, which never wants it
  separately, but anything later needing the un-offset position must get it from the host.

### 6.6 Suggested guard

The staleness class of bug is cheap to make impossible under test: have the harness carry an
iteration counter in the unused high bits alongside the refresh and assert every consumed pin
record matches the current iteration. That is a test-only check, but it turns "silently stale"
into "fails immediately". Worth doing when P2 lands.

---

## 7. Combined effect and suggested order

```
P5a outstanding+banks -> latency hiding on the gathers; ALSO the cheapest test of the §2.5 model
P1b HPWL by-product   -> deletes metrics::hpwl_sweep: one whole pass + one random gather, 3 -> 2
P1  fuse 1+2          -> one pin sweep instead of two, 2 -> 1; phase 2's bb_DDR read gone
P2  NodePin abs pos   -> hpwl_CU AND metrics become gather-free; the one remaining random pass
                         moves out of the float datapath into a tunable refresh
P3  II fixes          -> compute floor 5.87M -> ~2.4M cycles
P4  host compaction   -> removes the branch P3 fights, and the negative-index hazard
P5b stream widening   -> sequential streams 12.5-25% -> 100% of the 512-bit bus
P6  DATAFLOW          -> hides clear_grad
                      -> THEN L3: replicate P ways. This is where the order of magnitude is.
```

Suggested sequence: **P5a → P1b → P1 → P2 → P3 → P4 → P5b → P6 → L3.**

- **P5a first** — pragmas plus link-time bank tags, no C restructuring, and it is the cheapest
  experiment that can *falsify* §2.5. If deeper queues move nothing, the gather-bound diagnosis
  is wrong and P1/P2 need re-arguing before anyone builds them.
- **P5b late**, not with P5a. Widening is a real rewrite of all three sweeps' boundary handling
  (a segment boundary can fall mid-beat), and it should follow the changes that decide how many
  sweeps exist at all.
- **P1b second** — proportionally the largest single saving, needs no format change and no new
  contract, and its prerequisite — the HPWL assertion — is already in the harness as [6].
- **P1 before P2** — P1 is self-contained inside the module; P2 changes the host/PL contract
  and the DATAFLOW.md single-writer invariant, so it wants the module stable underneath it.
- **P3 after the memory work**, because until then the compute floor is not what the module is
  waiting on and a 2.5× on it would not show up end to end.

## 8. Verification

`make test` covers every proposal here except P5 (a pragma change is invisible offline) and
L3. Mutation coverage relevant to these changes, already in place:

| proposal | the way it fails | caught by |
|---|---|---|
| P1b | HPWL wrong, or bbox wrong in a way the gradient tolerates | **[6]** (added 2026-08-28; M8/M9 are [6]-only) |
| P1 | phase 2 sees a partial bbox | M5 → [1][2][4] |
| P1 | negative LUT index | `make test-asan` |
| P2 | stale/mismatched pin positions | [1][2]; §6.6 makes it immediate |
| P3 | reassociation goes wrong | [1] (tolerance sized for legitimate reassociation) |
| P4 | mask stops being honoured | [4], and `test-asan` for the `bb_DDR[-1]` path |
| P6 | `clear_grad` racing `seg_reduce` | [3] (output is poisoned, not pre-zeroed) |

Do **not** run only `make test` for P1 or P4 — both touch the masking path, and §5 is the
worked example of a mask bug that leaves every value bit-identical. `make test-asan` is the
gate for those two.

## 9. Not proposed

- **Caching `node_pos` in URAM.** Fits adaptec1 (211k × 8 B = 1.7 MB) but not bigblue4
  (2.18M × 8 B = 17 MB vs ~13 MB total on-chip), so it needs a tiled fallback and stops being
  a simple win. P2 dominates it and scales.
- **Host-side node renumbering for gather locality.** A real option if P2 is rejected, since
  it improves DDR page locality for `net_pins` with no device change. Moot if P2 lands.
- **Merging phase 4 into the fused 1+2 pass.** Phase 4 needs *every* net's final sums, so it
  cannot start until the last net closes. It is a genuine barrier, not a missed fusion.
- **Tier-1 coverage for `metrics`, `density_bin`, `iteration_update`, `bb_reduce`** (#20 step 3),
  which is out of scope here but blocked on one shared obstacle worth naming: all four
  `#include "../formats.hpp"`, which pulls `ap_int.h` / `hls_stream.h` / `ap_axi_sdata.h`, and a
  pure-g++ harness has none of those. That is *why* `density_bin_model.cpp` carries its own
  stale copy of `node_footprint` rather than including the real header — the duplication the
  DATAFLOW.md note complains about is a symptom, not sloppiness. Unblocking all four wants one
  decision made once: guard the HLS includes in `formats.hpp` behind a macro, or add minimal
  stub headers under `test/`. Worth settling before #20 step 3, not during it.
