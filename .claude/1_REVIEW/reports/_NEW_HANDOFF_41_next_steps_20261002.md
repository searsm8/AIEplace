# HANDOFF #41 — what to work on next (2026-10-02)

Where `hpwl_gradient_computer` (the WA-gradient PL module on the static pin-record stream) stands
after the 2026-10-01/02 sessions, and the ranked next steps. Background:
[[_NEW_EXPLAINER_41_hpwl_gradient_computer_20261001.md]] (what the module is),
[[_NEW_REPORT_41_ddr_bundles_20261001.md]] (DDR / NoC / URAM findings),
[[_NEW_REPORT_41_record_datapath_20260922.md]] (how it was built and verified).

## State
- **Committed as `8e33f7f`:** bundles grouped by width, gradient zeroing fused into the position
  load (`load_pos_zero_grad`, inline, in `hpwl_gradient_computer` and per chunk in
  `hpwl_gradient_computer_v2`), the top-file comments fixed, the Big Fix note in DATAFLOW.md, the
  explainer and bundle report.
- **Uncommitted when this was written:**
  - the NoC/DDR platform note in `_NEW_REPORT_41_ddr_bundles_20261001.md` (mine);
  - Mark's two new comments in the fused loop in `hpwl_gradient_computer.hpp` (see Housekeeping);
  - `vck5000/test/Makefile`: header-dependency tracking, from a separate session started from task
    chip `task_12dcba5e`. Check that session's result before committing it.
- **Verified for the fused loop:** tier 1 passes (forced rebuild; a no-zeroing mutant fails both
  harnesses) and C-synth gives II=1, depth 3, burst kept, LUT −168. **Not re-run:** RTL co-sim. The
  fusion does not touch the read-add-write, so the HAZARD_DISTANCE contract is unaffected.

## Next steps, ranked

### 1. Nets of 17–96 pins — HPWL DONE 2026-10-02, gradient next
- **Landed (HPWL only), Mark's calls:** large nets are an opt-in packer section (`Config::large_nets`)
  of span groups 2..`MAX_SPAN`=8 after the degree-16 group (protocol: `beat_packer/README.md`,
  "Large nets"). The span is whatever bank packing needs, and 97..100-pin nets are dropped.
  `hpwl_computer_v2` keeps a running bbox per net. That bbox is compared as an integer order key,
  because a carried `fcmp` cost −0.25 ns of estimated slack. Tier 1 is bit-exact (synthetic: 63
  nets over all 7 spans; adaptec1 7,013 and newblue2 11,531 large nets, 0 dropped). 3/3 mutants
  caught. C-synth: II=1, depth 21→23, slack −0.00, LUT +2.4 K. `hpwl_computer_v3` passes empty
  span groups.
- **Packing at its minimum (same day):** large-net-aware coloring (a soft per-bank cap of
  ⌈degree/16⌉) plus balanced packing, and nodes with more than 16 pins on a net split across
  beats. All 44 designs: 48 excess beats in 1.58 M, 0 dropped; 36/36 fitting designs bit-exact.
- **Open:**
  - **Chunking:** `encode_chunked` homes only small nets.
  - **Gradient:** plan [[_NEW_PLAN_41_large_net_gradient_fifo_20261002.md]] replaces L3's three
    passes with three DATAFLOW stages joined by FIFOs. It needs the HAZARD_DISTANCE schedule
    (with padding beats) on the span section.

*Original framing, kept:* **Decide how nets of 17–100 pins are handled (Mark's call, blocks the engine design)**
- **Why first:** the biggest correctness gap. Those nets are dropped today: 20–29% of ISPD2005 pins,
  +12.4% HPWL. Whatever is chosen changes the beat loop, so it should land before timing is tuned.
- **The proposal:** L3, exact, three II=1 passes (bbox / sums / combine) on the existing engine, a
  chunk beat = a degree-16 beat plus a small net-state table. About 2× gradient cycles on ISPD2005,
  1.04–1.5× on ISPD2015. L2 (online rescale, about 1.7×) would be a deliberate divergence from
  sw_only. → [[_NEW_PLAN_41_large_nets_on_records_20260923.md]]
- **Done when:** Mark picks; then implement in `hpwl_gradient_computer`, add a tier-1 case with
  17–100-pin nets against the golden, re-run C-synth and the co-sim hazard sweep.

### 2. Check the per-NMU bandwidth (cheap; could change the stream design)
- **Why:** the beat loop's `records` stream needs 64 B/cycle = 19.2 GB/s at 300 MHz on one m_axi
  port. My expectation, **not verified**, is that one NoC master unit (NMU) carries about 16 GB/s per
  direction (128-bit NoC channel at ~1 GHz). If so, one port cannot feed the loop at 300 MHz (it
  can at today's routed 231 MHz, 14.8 GB/s), and the stream must split over two ports or the
  clock target is moot.
- **How:** AMD Versal NoC docs (PG313 NoC IP, AM011 architecture manual): NMU512 data rate, NoC
  clock for the -2MP speed grade, and whether PL kernels use NMU512. Cross-check against the
  platform's NoC traffic file `platform_xsa/xilinx_vck5000_gen4x8_qdma_2_202220_1_noc_traffic.nts`
  (in `.claude/2_ARTIFACTS/`) and, after a hardware link, the v++ NoC compile report.
- **Done when:** a measured or documented number replaces the "expected ~16 GB/s" line in the
  report's platform note, with a verdict on the records stream.

### 3. Close timing (needs the build server)
- **Status:** post-route 4.335 ns vs 3.33 ns target (about 231 MHz), worst slack −1.0 ns, 20 K
  failing endpoints. Continue [[_NEW_HANDOFF_41_pnr_timing_20260923.md]]; don't restart.
- **First fix:** the top paths start at one float adder HLS shared between `refresh_macro_pins` and
  `fold_macro_pins`, fanning out to all 64 URAM banks (each bank is 4 cascaded URAMs, so the fanout
  is physically spread). Unshare it and register its output before the bank broadcast. Then group
  the remaining failing endpoints (`path_groups.tcl`, described in that handoff).
- **Watch:** deeper pipelines lengthen the read-add-write. If HLS raises II, raise HAZARD_DISTANCE
  (8 costs <0.5% bubbles) and re-run the co-sim spacing sweep: co-sim, not tier 1, gates that.
- **Build-server rule:** the tunnel needs Mark to run `wsl ssh -fN build`; ask once, launch long
  jobs detached, don't poll.

### 4. Small fix: `hpwl_gradient_computer_v2` loses bursts on its two LUT loads
`offset_table` and `exp_lut` (32-bit) share gmem0, which is 1024 bits wide for `ChunkDesc`, so both
loads have no burst (pipeline depth 75). Give them their own 32-bit bundle, re-run C-synth, check
the *Inferred Burst Summary* in `csynth.rpt`. Low runtime cost today (small loads), so do it when v2
is next touched.

### 5. Chunking walkthrough + the resident-loop URAM budget (the Big Fix)
- **Why later:** chunking only matters once the resident loop decides what stays on chip.
- **The Big Fix:** in the resident `pl_algo` loop, positions and gradients stay in URAM, so the
  load and drain disappear (≥26 K of ~65 K cycles per axis on adaptec1 today).
- **Open constraints** (recorded in `vck5000/pl/src/pl_algo/DATAFLOW.md`, section "#41
  record-stream gradient inside the resident loop"):
  - one axis is 256 of 463 URAMs at 1 M slots, so both axes resident at once does not fit;
  - the 8 of 44 designs over 1 M slots keep per-chunk DDR traffic;
  - overlap belongs between modules via `axis` streams, not DATAFLOW between phases sharing a
    URAM array.
- **Reading list for the walkthrough:**
  - `vck5000/bring_up/hpwl_gradient_computer_v2/src/modules/hpwl_gradient_computer_v2.hpp` (its
    doc block explains the export / compute / fold passes);
  - `hpwl_computer_v3.hpp`;
  - `vck5000/bring_up/beat_packer/README.md` (protocol, `encode_chunked`);
  - Part 2 of [[_NEW_REPORT_41_record_datapath_20260922.md]].

## Housekeeping
- **Mark's new comment in the fused loop** says odd beats write "the second half of the URAM bank".
  It is the second group of **banks** (16–31), at row `b >> 1`: each beat's 16 values go to 16
  different banks, never half of one bank. Suggest: "odd beat: banks 16..31 (LANES offset)".
  Also a trailing space after `= 0.0f;`.
- **Makefile session (`task_12dcba5e`):** once it reports, confirm `make test` rebuilds on a header
  `touch`, then commit the Makefile with Mark's OK.
- **RTL co-sim of the fused modules:** optional confirmation; `bring_up/hpwl_gradient_computer/cosim/`
  has the bench.

## Facts established (don't re-derive)
- **A pointer narrower than its m_axi port gets no burst** (HLS 2022.2), and the report still says
  II=1: check the *Inferred Burst Summary* in `csynth.rpt`. Group bundles by width.
- **`offset=slave`:** the pointer's base address is a register on the AXI-Lite control port, written
  by XRT at launch.
- **Each bundle** = one AXI master built in PL fabric by HLS; v++ link connects it to a NoC NMU.
- **Platform:** one DDR target `MC_NOC0` = 4 DDR4-3200 controllers interleaved at 4 KB, 102.4 GB/s
  theoretical; at most 30 kernel AXI-MM managers.
- **Device:** 28 NMU512 + 26 NMU128, 28 NSU512 + 22 NSU128, 4 DDRMC, 16 AIE NoC tiles, 463 URAM288,
  967 RAMB36.
- **URAM banks:** each URAM is 4 K × 72 bits, two ports. A 1 M-slot array is 128 URAMs: 32 banks × 4
  cascaded URAMs, two floats per word (measured count; the packing is inferred).
- **DATAFLOW does not fit** the module's phases: random-access dependencies, URAM ping-pong would
  need 512 of 463, and `pos_URAM` has several writers.
- **`vck5000/test/Makefile` (before the fix)** did not rebuild a harness on a header edit: a stale
  `make test` PASS is possible. Force a rebuild (delete `test/build/<harness>`) if in doubt.
