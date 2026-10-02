# REPORT #41 — m_axi bundle experiment, and whether to DATAFLOW `hpwl_gradient_computer` (2026-10-01)

Question (Mark): the module's DDR streams are read one after another — do they each need their own
m_axi bundle? And should the phases be overlapped with DATAFLOW?

**Answer in one line:** merging bundles keeps every loop at II=1 but **silently loses DDR bursts on
every pointer narrower than the merged port**, including the per-beat HPWL write in the main loop,
and saves no area. Keep separate bundles, grouped **by data width**, not by "one per stream".
DATAFLOW does not fit this module; its setup/drain cost is real but is better removed structurally.

## The experiment
Three C-synthesis variants of `hpwl_gradient_computer_top`, identical except for the `bundle=` names.
Vitis HLS 2022.2, xcvc1902, 3.33 ns. Inputs: snapshot of the working tree on 2026-10-01 (includes
Mark's uncommitted `slot`→`slot_idx` rename). Everything, including logs, is in
`.claude/2_ARTIFACTS/ddr_bundle_experiment_20261001/{A_7bundles,B_rd_wr,C_1bundle}/`.

| variant | bundles |
|---|---|
| A (current) | 7: one per stream; `offset_table` + `exp_lut` share gmem4 |
| B | 2: all six reads on `gmem_rd`, both writes on `gmem_wr` |
| C | 1: everything on `gmem_all` |

## Results
| | A | B | C |
|---|---|---|---|
| every loop II | 1 | 1 | 1 |
| est. clock | 3.037 ns | 3.037 ns | 3.037 ns |
| m_axi adapter LUT | 33.2 K | 16.1 K | 8.1 K |
| **total LUT** | **197.8 K** | **199.1 K** | **191.0 K** |
| total FF | 246.2 K | 245.0 K | 240.5 K |
| DDR bursts inferred | 9 of 9 accesses | 3 | 3 |
| `beat_loop` depth | 84 | 89 | 89 |
| setup-loop depth (cache_counts, refresh, offsets, lut, fold) | 2–9 | 74–81 | 74–81 |

(HLS estimates. The 2026-09-23 place-and-route found HLS overestimates LUTs about 2×; the *relative*
picture is what matters here.)

**Which bursts survive a merge:** only the three 512-bit accesses (`records`, `pos`, `grad`). Every
narrower pointer on a 512-bit port lost its burst: `beat_count` (32), `offset_table` (32),
`exp_lut` (32), `macro_pins` (128, in both refresh and fold), and `out_beats` (256), which is written
**every beat of the main loop**. In A, each of those had its own port sized to its type and kept its
burst. The "Inferred Bursts and Widening Missed" table is the same in all three variants (it lists
widening misses, not burst misses), so the evidence is the *Inferred Burst Summary*, 9 rows vs 3.
Observed on 2022.2. The likely rule (not confirmed against the docs) is that HLS infers a burst only
when the access fills the port width.

**Why II stays 1 anyway:** without a burst, each loop iteration issues a single-beat request. HLS
keeps II=1 by deepening the pipeline to absorb its assumed DDR latency (depth 2–9 → 74–81). On real
DDR, single-beat requests are capped by the adapter's outstanding-request limit divided by the real
latency, so **the II=1 in the report would not hold in hardware**. The report gives no sign of this;
only the burst table and the jump in pipeline depth show it. Not measured: RTL co-simulation would
show it (the `cosim/` bench exists).

**Why the area did not drop:** B saved 17 K LUT of adapters and spent 18 K inside the kernel, about
3 K in each narrow-stream loop: lane-select muxes to pull a 32/128/256-bit value out of a 512-bit
word, plus the deeper pipelines. C nets −6.8 K LUT (3%) for the same burst loss.

## What this means for this module
- **Separate bundles here are about width, not concurrency.** The streams never overlap (only
  `records` read + `out_beats` write run together, on separate AXI channels), so sharing would be
  safe for correctness. What sharing costs is bursts on the narrow pointers.
- **Safe merges are same-width only.** A already does one (`offset_table` + `exp_lut`, both 32-bit).
  `beat_count` could join them (saves one ~1 K-LUT adapter); `records` + `pos` (both 512-bit,
  sequential phases) could share a port. Small gains, not worth a change today.
- **The comment in the top file** ("so the sequential streams never share a port") gives the wrong
  reason. The real reason is to keep each pointer on a port of its own width so it keeps its burst.

## DATAFLOW
**Not for this module.**
- **The phases have true dependencies through random-access arrays.** The beat loop needs all of
  `pos_URAM`, the offsets and the LUT loaded before its first beat (any record can address any slot),
  the fold needs every gradient, and the drain needs the fold. There is nothing to stream between
  them.
- **DATAFLOW would double the big arrays.** Arrays passed between DATAFLOW processes become ping-pong
  buffers. `pos_URAM` and `grad_URAM` are 128 URAMs each, so doubling them needs 512 of the 463 on
  the device. `pos_URAM` is also written by three phases (load, refresh, fold-as-accumulator), which
  breaks DATAFLOW's single-producer rule.
- **The only independent phases are the loads and the zeroing,** which could overlap without
  DATAFLOW by fusing the loops.

**The setup cost is throughput, not just latency, though.** Per axis on adaptec1 (211,447 nodes,
210,904 movable, 51,516 beats): load ≥13.2 K + zero 13.2 K + drain 13.2 K ≈ **≥40 K cycles against a
51.5 K-cycle beat loop**, so the beat loop is at most ~57% of the kernel's time. (Cycle estimate from
trip counts, not measured.)

**Where that cost should go:**
- **Now, cheap, optional:** fuse `fill_slot_array` into `load_slot_array` (one loop writes positions
  and zeros), saving ~13 K cycles per call. Or have the drain write zeros back as it reads, so the
  next call needs no fill.
- **The bigger picture:** in the resident `pl_algo` iteration, positions and gradients stay on chip,
  so the load and the drain disappear and only the beat loop and fold remain. That is the right place
  to win this back, and the right level at which to think about DATAFLOW (between modules, through
  streams, not between phases that share a URAM array).

## Follow-up, same day: the cheap fix landed
Mark: do the fusion inline (no shared helper). `fill_slot_array` is gone; the position load zeroes
the gradient in the same loop (`load_pos_zero_grad`), in `hpwl_gradient_computer` and per chunk in
`hpwl_gradient_computer_v2`.
- **Tier 1 passes** after a forced rebuild. A mutant with the zeroing removed fails both harnesses
  (rel_rms 0.94 and 0.61). ⚠️ `vck5000/test/Makefile` does not rebuild a harness when a module header
  changes, so an earlier run reported PASS from September binaries; flagged as its own task.
- **C-synth:** the fused loop is II=1, depth 3, and keeps its 512-bit burst. `hpwl_gradient_computer`
  LUT 197,821 → 197,653, clock estimate unchanged (3.037 ns). v2 unchanged apart from the loop
  (slack −1.46 before and after).
- **Same burst lesson, already in v2:** its 32-bit `offset_table` / `exp_lut` share gmem0, which is
  1024 bits wide for `ChunkDesc`, so both loads have no burst (depth 75). Filed under #41.
- Top-file comments fixed: bundles are grouped by width, not one per stream.

## Platform note
`platforminfo` on `xilinx_vck5000_gen4x8_qdma_2_202220_1`: one DDR target, `MC_NOC0` (four DDR4-3200
channels behind the NoC), plus `BRAM`. Every m_axi port reaches the same memory, so separate bundles
cannot be pinned to separate DDR banks on this card.

Measured 2026-10-02 (unpacked platform XSA in `.claude/2_ARTIFACTS/platform_xsa/`, and a Vivado
`get_sites` query on an empty xcvc1902 design, script in `platform_xsa/noc_sites/q.tcl`):
- **`MC_NOC0` = 4 DDR4 controllers interleaved at 4 KB** (`top_axi_noc_mc_0.hwh`: `NUM_MC=4`,
  `MC_INTERLEAVE_SIZE=4096`, `DDR4-3200AA`, 72-bit with ECC = 64 data bits, 4 GB each). Theoretical
  peak 4 × 3200 MT/s × 8 B = **102.4 GB/s** total; sustained is lower (not measured).
- **Device NoC sites:** 28 `NOC_NMU512` + 26 `NOC_NMU128` masters, 28 `NOC_NSU512` + 22 `NOC_NSU128`
  slaves, 4 `DDRMC`, 16 `AIE_NOC` interface tiles, 2 `CPM`, 1 `PS9`. The platform allows at most 30
  kernel AXI-MM managers (`maxAXIMMManagers` in `xsa.json`).
- **Not yet checked against the docs:** which NMU type serves PL kernels (expected NMU512), and the
  per-NMU bandwidth (expected ~16 GB/s per direction, a 128-bit NoC channel at ~1 GHz). If that
  holds, one 512-bit port at 300 MHz (19.2 GB/s) is NMU-limited, and the `records` stream alone needs
  that rate.

## Open
- Confirm the hardware cost of a non-burst stream with RTL co-simulation (A vs C), if the intuition
  needs a number.
