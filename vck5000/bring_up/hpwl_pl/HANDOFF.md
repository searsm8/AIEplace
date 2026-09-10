# HANDOFF — getting hard throughput numbers for `hpwl_top`, before a spatial-parallelism rebuild

**You are on `hacc-gpu-u55c-01`** (the hw node). Per `../../dct_fft_aie/HANDOFF.md`, this node
has no Vitis build capacity reserved and shouldn't run `v++`/`vitis_hls` — it's for running
xclbins on real VCK5000 hardware only. **Path B below needs the build node,
`hacc-build-01`.** Confirmed this project is on shared storage reachable from both: this
`build/hw` tree already carries `.Xil/configutil-*-hacc-build-01.inf.ethz.ch` markers from the
original build, and we're reading it fine from `hacc-gpu` right now.

## TPWS, and how it differs from TNS

Vivado's timing summary reports three independent constraint classes per clock, each with a
worst-case and a summed-negative-slack number:

| pair | checks | violation means |
|---|---|---|
| WNS / **TNS** | **setup** — data path vs. next clock edge | data arrived too late |
| WHS / THS | **hold** — data path vs. same clock edge | data arrived too early |
| WPWS / **TPWS** | **pulse width** — min high/low time on clocks and pulse-width-constrained nets (async set/reset, some memory control) | a clock or reset pulse was too short, not a two-flop data-path problem at all |

**TPWS is Total Pulse Width Slack**, not "Total Positive Worst Slack" — it's the sum of negative
slack across every pulse-width-constrained endpoint that fails its minimum-pulse-width
requirement, the same way TNS sums negative slack across every setup-failing endpoint. Different
constraint class from TNS, not a variant of it. In our routed report both are 0 (no violations
of either kind) — that's two separate clean bills of health, not one.

## The actual ask: hard throughput numbers for `hpwl_top`. Investigated — there are none yet, for three independent reasons

1. **HLS can't give a static estimate.** `reports/top.hw/hls_reports/hpwl_top_csynth.rpt`
   Performance Estimates table: Latency (cycles) min/max = `?`, Interval = `?`, for both
   `hpwl_top` and `grp_hpwl_CU_fu_214`. This isn't a missing report — it's because
   `hpwl_CU`'s loop trip counts (`num_nets`, `num_movable`, `num_npins`) are runtime scalar
   arguments, not compile-time constants (README.md: "this module's whole point is no size
   cap"). HLS has nothing to bound. Pipeline Type is also `no` — the three segmented-reduction
   passes run sequentially, not as one pipelined loop, consistent with the CSR/SpMV structure
   described in `hpwl_gradient.hpp`'s header comment.

2. **The device trace is empty.** All four `device_trace_{0,1,2,3}.csv` have zero rows under
   `EVENTS`; `device_trace_2.csv` literally labels the CU `Compute Unit hpwl_top_1 - No Trace`.
   `summary.csv` shows `TOTAL_KERNEL_RUN_TIME_MS,all,0` despite
   `APPLICATION_RUN_TIME_MS,all,1993.33` — that 1993 ms is host wall-clock (device open +
   xclbin load + all buffer syncs + kernel run + verify), not a kernel time, and isn't usable as
   a throughput number. Root cause, confirmed by grepping the link log: `grep -in
   "profile\|monitor" logs/link/v++.log logs/top.hw/v++.log` returns nothing — the current
   Makefile's `v++ -l` never passes `--profile_kernel`, so no AXI Performance Monitors were
   placed in the design. `xrt.ini`'s `device_trace=fine` is set correctly but has no monitor
   hardware to read from, regardless of which viewer opens it.

3. **The host has no timing instrumentation either.** `grep -n
   "chrono\|clock\|time\|duration" src/host/host.cpp` finds nothing; the only kernel-related
   call is `run.wait()` at `src/host/host.cpp:251`. So there isn't even a coarse host-side
   number sitting uncollected.

## Two paths, cheapest first

### A. DONE (2026-09-09) — right now, on this node, no rebuild: a coarse but real number

Wrap the existing launch in `src/host/host.cpp` with `std::chrono::steady_clock`:

```cpp
// src/host/host.cpp:249-251, replace:
    xrt::run run = k(bo_node, bo_nptr, bo_pins, bo_npins, bo_lut, bo_bb, bo_sums, bo_grad,
                     inv_gamma, inv_lut_step, lut_size, NUM_NETS, M, num_npins);
    run.wait();
// with:
    auto t0 = std::chrono::steady_clock::now();
    xrt::run run = k(bo_node, bo_nptr, bo_pins, bo_npins, bo_lut, bo_bb, bo_sums, bo_grad,
                     inv_gamma, inv_lut_step, lut_size, NUM_NETS, M, num_npins);
    run.wait();
    auto t1 = std::chrono::steady_clock::now();
    double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    std::printf("[hpwl_pl] kernel wall time: %.1f us  (%.2f Mnets/s, %.2f Mnodes/s)\n",
                us, NUM_NETS / us, M / us);
```

`make host` is pure `g++` (no Vitis needed) — rebuild just the host binary and re-run
`./run_hw.sh` on this node immediately. This bundles AXI-lite launch overhead in with compute
(no per-port breakdown), but it's a real number today, and enough to sanity-check
nets/sec or nodes/sec before investing in path B.

**Result**, `src/host/host.cpp:251` on, rerun with `make host && ./run_hw.sh` on this node
(no hardware rebuild — same `hpwl_pl.hw.xclbin` as before), 20 back-to-back launches of the
existing synthetic netlist (M=800 movable, N=1000 total, 550 nets, 2591 pins):

```
[hpwl_pl] kernel wall time over 20 launches (host chrono around run.wait(), DMA excluded, launch overhead included):
  min=111.3 us  median=113.7 us  mean=118.1 us  max=183.7 us
  @ median: 4.837 Mnets/s  7.036 Mmovable-nodes/s  22.788 Mpins/s
```

Tight min/median (111 vs 114 us) says the AXI-lite launch + `hpwl_CU`'s three passes are
consistent run to run; the 183.7 us max outlier (once in 20) is host/scheduler jitter, not
device behavior — worth a larger sample before trusting the tail. **This number is still a
single lump** (launch overhead + all three segmented-reduction passes together, no per-pass or
per-port split) and, just as important, **it's at one fixed, small synthetic size** — 550
nets / 800 movable nodes is nowhere near a real benchmark (ISPD2015 designs run
100K–2M+ movable nodes). Before this number means anything for the spatial-parallelism
decision, rerun path A (and eventually path B) at sizes that actually track real benchmarks —
`M`/`NUM_FIXED`/`NUM_NETS` in `src/host/host.cpp:41-44` are the knobs, and degree distribution
matters too (`deg_uni(2, 8)` may not match real net-degree histograms). That's the natural next
step once path B's per-port numbers are in for comparison at this same small size.

### B. DONE (2026-09-09) — real per-port breakdown, from `--profile_kernel`

Built via the new `make hw_profile` target (`../../Makefile`, `PROFILE=1` → `--profile_kernel
data:hpwl_top:hpwl_top_1:all` at link) into its own `build/hw_profile/`, kept separate from this
`build/hw/` on purpose — an xclbin built with monitors and one built without are not
interchangeable, and this dir was mid-use for path A when that build ran. Same synthetic netlist
size as path A (550 nets / 800 movable / 2591 pins), 21 kernel calls (the 1 correctness-verify
call + the 20-call timing loop from path A's `host.cpp` edit — both paths ran the *same* host
binary logic, which is what makes the comparison below apples-to-apples).

**On-chip hardware counters** (`build/hw_profile/summary.csv`, Compute Unit Utilization table):

```
Number Of Calls: 21   Clock: 300 MHz
Total Time: 1.84315 ms   Min: 0.0858467 ms   Avg: 0.0877689 ms   Max: 0.0909533 ms
```

i.e. **min 85.8 us, avg 87.8 us, max 91.0 us** — measured by the device's own AXI Performance
Monitors around the CU's actual execution window, with no host/driver/OS overhead in it at all.

**Host-chrono vs device-counter, same netlist, same 20-call loop:**

| | min | mean/avg | max |
|---|---|---|---|
| host `chrono` around `run.wait()` (path A) | 111.3 us | 118.1 us | 183.7 us |
| on-chip AXI Performance Monitor (path B) | 85.8 us | 87.8 us | 91.0 us |
| **gap (host overhead)** | **25.5 us (+30%)** | **30.3 us (+34%)** | **92.7 us (+102%)** |

Two things fall out of this:
- There's a **steady ~25-30 us of host-side overhead per launch** (AXI-lite kernel dispatch +
  XRT/driver command-queue round trip) sitting on top of every real device execution — path A's
  numbers aren't wrong, they're answering "how long does the host wait," not "how long does the
  device compute." For a throughput figure to compare against a spatial-parallelism rebuild,
  **the device-counter number is the one to use** (87.8 us → recompute path A's per-metric
  throughputs at this time instead: **6.27 Mnets/s, 9.12 Mmovable-nodes/s, 29.5 Mpins/s** — all
  ~27-30% higher than what path A reported, since path A was dividing by the inflated
  host-side time).
- The **max is where it really diverges**: host max (183.7 us) is more than 2x the device max
  (91.0 us). The device's own max only drifts 3.6 us above its own min — the hardware is
  consistent call-to-call. The 183.7 us host outlier was pure host/OS scheduling jitter (one
  bad context switch in 20), not device behavior. **Don't use path A's max for anything** —
  it's measuring the host, not the kernel.

**Per-port DDR bandwidth** (`build/hw_profile/summary.csv`, Data Transfer table) — this is the
finding that actually matters for the spatial-parallelism decision:

| port | array | rate (MB/s) | % of ideal port BW | avg transfer | avg latency |
|---|---|---|---|---|---|
| gmem0 | node_pos (R) | 734 | 3.8% | **8 B** | 41.8 ns |
| gmem2 | pins (R) | 1762 | 9.2% | 256 B | 70.8 ns |
| gmem3 | npins (R) | 1712 | 8.9% | 256 B | 68.3 ns |
| gmem4 | exp_lut (R) | 1018 | 5.3% | 61 B | 171.0 ns |
| gmem5 | bb (R+W) | 794 / 350 | 4.1% / 1.8% | 16 B | 44.8 / 21.6 ns |
| gmem6 | sums (R+W) | 2689 / 631 | 14.0% / 3.3% | 32 B | 47.9 / 21.8 ns |
| gmem7 | node_grad (W) | 569 | 3.0% | 16 B | 20.6 ns |

**Every port sits at single-digit-to-low-teens percent of its theoretical ideal bandwidth.**
`node_pos` in particular is moving 8 bytes (one `coord_t`) per transaction — every pin's node
lookup is its own unburst-able random AXI beat, not a streamed read. **This design is
latency-bound on many small scattered DDR transactions, not bandwidth-bound.** That's the
opposite of what you'd assume walking in, and it directly shapes the parallelism plan: blindly
replicating `hpwl_CU` across more DDR ports is chasing bandwidth headroom that already exists
(85-96% of every port's ideal capacity is sitting idle) — it won't help unless the replicas
also get **independent, non-contending request streams** so their per-transaction latency can
overlap. A design that instead widens each existing access (burst-coalescing `node_pos`/`pins`
reads, or caching the working set on-chip) may buy more than replication alone. Worth modeling
both before committing to a CU count.

**One caveat**: `summary.csv`'s User Level Events section logs `Device Trace Buffer Full,1` —
the trace buffer overflowed once during this run. The Compute Unit Utilization table above comes
from hardware counters and should be unaffected, but the raw per-transaction event dump
(`device_trace_2.csv`, 244k lines) may be truncated for whichever call(s) ran after the buffer
filled. Fine for the aggregate numbers used here; if a future pass needs the full per-transaction
trace (e.g. to see *which* call's transactions look different), reduce the timing-loop iteration
count or look for a larger trace-buffer XRT setting first.

## Reproduced at n=200 (2026-09-09) — same overhead, tighter confidence

Bumped `TIMING_ITERS` 20→200 in `src/host/host.cpp` and added p50/p90/p95/p99/stddev (min/mean/max
alone can't distinguish "the tail is real" from "one sample got unlucky"). **This needed no
hardware rebuild in either build dir** — the host binary is plain `g++`, independent of the
xclbin, so `make host TARGET=hw BUILD=build/hw` and `make host TARGET=hw BUILD=build/hw_profile`
(that `BUILD=` override needed `run_hw.sh`'s hardcoded `BUILD=build/hw` changed to
`BUILD=${BUILD:-build/hw}` — small, done) recompiled both in seconds, then reran
`BUILD=build/hw ./run_hw.sh` and `BUILD=build/hw_profile ./run_hw.sh` against the *same*
already-built xclbins from before.

**Host chrono, n=200** (`build/hw`):
```
min=113.7  p50=116.8  p90=120.8  p95=122.6  p99=124.4  max=163.6  us
mean=117.5 us  stddev=3.9 us
```

**Device counter, n=201** (`build/hw_profile/summary.csv`, Compute Unit Utilization —
1 verify call + 200 timed, matching `Number Of Calls,201` exactly):
```
min=85.19 us   avg=86.47 us   max=93.02 us   total=17.3798 ms
```

**Compare to the n=21/n=20 run from the same day:** device min/avg/max then was
85.8/87.8/91.0 us; now 85.2/86.5/93.0 us — within ~1-2 us across a completely separate build
run and 10x the sample count. **The on-chip hardware timing is deterministic to within a
couple of microseconds; nothing here is sample-size noise.** The per-port bandwidth
percentages in `summary.csv`'s Data Transfer table also came back essentially identical
(e.g. `node_pos` still 31.0% of current-port / 3.9% of ideal, `sums` read still ~28.6%/14.3%)
— the latency-bound-not-bandwidth-bound conclusion from path B is a structural property of
this kernel/netlist, not an artifact of the earlier small sample.

**The host-side overhead is also a fixed, repeatable quantity, not something that averages
away with more samples:**

| | min | central | max |
|---|---|---|---|
| host (p50 used as central) | 113.7 us | 116.8 us | 163.6 us |
| device (avg used as central) | 85.2 us | 86.5 us | 93.0 us |
| gap | **+28.5 us (+33%)** | **+30.3 us (+35%)** | +70.6 us (+76%) |

Same ~28-30 us / ~33-35% dispatch overhead as the n=20/n=21 comparison — this is a fixed cost
of the AXI-lite launch + XRT command-queue round trip, not something that shrinks with a bigger
sample. **What *did* change with more samples is the shape of the host tail**: at n=20 the
single max (183.7 us) was the only tail signal available; at n=200, p99=124.4 us shows the real
99th-percentile cost is barely above p90 (120.8 us) — the 163.6 us max is a genuine rare outlier
(here, once in 200) sitting well outside the p99 band, not representative of typical tail
latency. **Use p99, not max, if this needs to feed a timing budget** — max will keep moving
around run to run (183.7 us here, 163.6 us there) while p99 has stayed put.

One more confirmation from this run: XRT printed the trace-buffer-full warning explicitly this
time (`Trace Buffer is full. Device trace could be incomplete... increase trace_buffer_size or
use 'coarse'`), and `Number Of Calls,201` in the Compute Unit Utilization table still matched
the host's call count exactly — good evidence the counter-based aggregate tables used above
survive the overflow intact; it's only the raw per-transaction `device_trace_2.csv` dump that's
at risk of truncation, as suspected before.

## Scaling sweep (2026-09-09) — 1x/10x/100x/1000x, up to 800K movable / 550K nets

Parameterized the synthetic netlist instead of recompiling per size point: `M`/`NUM_FIXED`/
`NUM_NETS`/`TIMING_ITERS` in `src/host/host.cpp` went from `static constexpr` globals to
plain globals set from optional CLI args (`./host <xclbin> [movable] [fixed] [num_nets]
[timing_iters]`, defaults unchanged); `run_hw.sh` now forwards `"$@"` to `./host`. All four
sizes ran on the same `build/hw_profile` xclbin — no hardware rebuild between points, this is
purely a netlist-size sweep against one fixed design.

| scale | movable | nets | host p50 | device avg | **overhead** | Mnets/s (device) | `node_pos` avg latency | `node_pos` % of ideal BW |
|---|---|---|---|---|---|---|---|---|
| 1x | 800 | 550 | 116.6 us | 86.50 us | **+34.8%** | 6.36 | 41.3 ns | 3.88% |
| 10x | 8,000 | 5,500 | 1005.8 us | 962.3 us | +4.5% | 5.71 | 50.0 ns | 3.23% |
| 100x | 80,000 | 55,000 | 10.73 ms | 10.56 ms | +1.6% | 5.21 | 55.2 ns | 2.92% |
| 1000x | 800,000 | 550,000 | 108.93 ms | 107.98 ms | **+0.9%** | 5.09 | 56.8 ns | 2.85% |

(`overhead` = host p50 / device avg, i.e. path A's number relative to path B's ground truth;
device numbers from each run's `summary.csv` Compute Unit Utilization table, archived at
`/tmp/summary_{1x,10x,100x,1000x}.csv` on this node.)

**Three things this settles:**

1. **The host-dispatch overhead that motivated path B is a fixed ~25-30 us cost, not a
   percentage** — so it matters at bring-up scale (35% at 550 nets) and is noise at real scale
   (<1% at 550K nets, comparable to a real ISPD2015 design). **Practical consequence: for
   future throughput experiments at realistic sizes, the cheap host-chrono path alone is
   probably sufficient** — the `--profile_kernel` rebuild's main remaining value is the
   per-port bandwidth breakdown below, not correcting the aggregate number.

2. **The latency-bound-not-bandwidth-bound finding holds at all four sizes, up to 800K
   movable nodes — it's structural, not a small-sample artifact.** `node_pos`'s bandwidth
   utilization doesn't climb toward saturation as the netlist grows; it *drifts down*
   (3.88%→2.85% of ideal). Same pattern on every other port (`pins` 9.24%→6.94% of ideal,
   `sums`-read 14.3%→10.26%). Three orders of magnitude of headroom confirmed, not one lucky
   measurement.

3. **New finding, and it complicates the "just replicate CUs" plan: per-transaction latency
   itself grows with scale, on a single CU with no contention from anything else.**
   `node_pos`'s average AXI read latency climbed 41.3→50.0→55.2→56.8 ns across the sweep
   (+37% total), flattening toward a ~55-60 ns plateau rather than staying flat at 41 ns. This
   is happening with **one kernel instance, no other CU competing for the NoC** — it's the
   memory subsystem's own queueing depth increasing as one stream issues more outstanding
   requests, not multi-CU contention. **That means the "85%+ of every port's bandwidth is
   idle, so N replicas should mostly work" argument from the first pass understates the risk**:
   latency inflation shows up from a single stream's request volume alone, well before the
   raw %-of-ideal-bandwidth number looks concerning. N independent CUs sharing the same NoC
   path should be expected to show *at least* this much latency creep from their combined
   request volume, likely more from actual cross-CU contention on top. This needs modeling
   (or an early small-N replication experiment) before assuming linear scaling — don't take
   the idle-bandwidth number alone as license to pick a large N.

4. **A clean per-CU baseline throughput to design against**: net throughput (device-time
   basis) settles toward **~5.1 Mnets/s at scale** (6.36 at the tiny bring-up size, down to
   5.09 by 550K nets) rather than the bring-up size's more optimistic 6.36 — use ~5.1 Mnets/s,
   not the small-netlist number, as the "one replica, steady state" building block when
   modeling what N replicas should buy.

## Original plan for B (superseded by the results above — kept for the exact commands used)

Add AXI Performance Monitors at the **link** stage (`../../Makefile`'s `$(XSA)` rule uses `v++
-l` — that's the one to edit, not the compile or package rules):

```makefile
$(XSA): $(TOP_XO)
	v++ -l -t $(TARGET) --platform $(PLATFORM) $(VPP_TEMPS) \
		--profile_kernel data:hpwl_top:hpwl_top_1:all \
		$(TOP_XO) -o $@
```

Kernel/CU names (`hpwl_top` / `hpwl_top_1`) confirmed from `device_trace_2.csv`'s `Group_Start`
line. `all` instruments every `m_axi` interface in one pass — all 8 are cheap to monitor at this
design's current utilization (~1-3% LUT/FF/DSP per the csynth report). If a narrower view is
wanted later, the 8 bundle names are (`src/pl/top.cpp:34-41`): `gmem0`=node_pos, `gmem1`=net_ptr,
`gmem2`=pins, `gmem3`=npins, `gmem4`=exp_lut, `gmem5`=bb, `gmem6`=sums, `gmem7`=node_grad — e.g.
`data:hpwl_top:hpwl_top_1:gmem6,gmem7` to focus on the output-heavy last pass.

Then, on `hacc-build-01`:
```bash
cd hpwl_pl
source /tools/Xilinx/Vitis/2022.2/settings64.sh
make TARGET=hw all PLATFORM_REPO=/opt/xilinx/platforms XILINX_XRT=/opt/xilinx/xrt_2024.2
```

Copy **both** `build/hw/hpwl_pl.hw.xclbin` and `build/hw/hpwl_pl.hw.ltx` back — the `.ltx`
(probe/debug metadata) from *this* build has zero monitors in it since none were instrumented;
the new one is required for XRT to address the new monitor hardware, not just the new xclbin.
Given the shared-storage layout, that may just mean re-running here with the same path — verify
the build node actually wrote into this same `build/hw/` before assuming no copy step is needed.

Once that xclbin is loaded here, `xrt.ini`'s existing `device_trace=fine` will start actually
populating `device_trace_*.csv` and `summary.csv`'s Kernel Execution / Data Transfer tables —
per-port beats and MB/s, not just an aggregate wall time. That per-port view is what tells you
whether a spatial-parallelism rebuild is compute-bound or DDR/NoC-bound before you spend HLS
effort on it.

## One flag for whoever does the parallelism rebuild

`hpwl_top` already occupies all 8 `gmem` AXI ports (one per array). Before assuming N replicated
CUs buy N× throughput, check NoC path headroom — `reports/link/imp/impl_1_full_util_placed.rpt`
and `impl_1_system_diagram.json` are the existing static-utilization view of that, cheap to read
before committing to a specific replication factor. Not investigated further here — flagging so
it isn't rediscovered from scratch.
