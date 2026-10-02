# HANDOFF #41 — what to work on next (rewritten evening of 2026-10-02)

Where `hpwl_gradient_computer` (the WA-gradient PL module on the static pin-record stream) stands,
and the ranked next steps. Background:
[[_NEW_EXPLAINER_41_hpwl_gradient_computer_20261001.md]] (what the module is),
[[_NEW_REPORT_41_ddr_bundles_20261001.md]] (DDR / NoC / URAM findings),
[[_NEW_REPORT_41_record_datapath_20260922.md]] (how it was built and verified),
[[_NEW_PLAN_41_large_net_gradient_fifo_20261002.md]] (**the large-net plan: all four steps done;
the evidence for this handoff is there**).

## State
- **Large nets (17..96 pins) are done end to end: HPWL and gradient.** Opt-in via
  `Config::large_nets`. With it off, every output is bit-identical to the previous HEAD.
- **Gradient path:**
  - A (`pin_bbox`) sends a net's bbox to B on `net_bboxes`; B (`wa_sums`) sends the net's sums to C
    on `net_sums`. Each is pushed on the net's last real beat and popped on its first.
  - B's sums use the same window-plus-tree form as A's bbox, so no arithmetic is loop-carried.
    II=1 in every stage.
- **Verification:**
  - Tier 1 rel_rms 3.4e-7 with large nets on, in 3 packer configs; 7/7 mutants caught.
  - RTL co-sim: hazard 4 PASS, hazard 1 FAIL (the negative control), and a depth sweep that pins
    the FIFO bound (next bullet).
- **FIFO bound:** the beat FIFOs need depth ≥ extent − 1 + skew, where skew is how many pipeline
  states after the beat write a stage writes its per-net value (A 6, B 7). Co-sim on a net of
  extent 13: depth 19 passes, depth 18 deadlocks. Production depth is 2 × `MAX_NET_EXTENT` = 32,
  which costs 58 BRAM18; 19 and 32 cost the same.
- **Packer rule L8:** extent ≤ `MAX_NET_EXTENT` = 16. That is the measured maximum (newblue3), so
  nothing is dropped on the 44 designs. The checker enforces it.
- **Tooling:**
  - `.claude/2_ARTIFACTS/grad_identity/` — the bit-identity driver (now passes `span_beat_count`).
    The reference dump is `/tmp/grad_old.bin`, which does not survive a reboot: rebuild it from a
    known commit before relying on it.
  - `.claude/2_ARTIFACTS/large_net_tools/`:
    - `net_extent.cpp` + `extent_sweep.txt` — extent per design;
    - `step3_mutants.sh` — the 7 mutants;
    - `grad_worst.cpp` — the worst nodes and their float-conditioning check;
    - `sweep.sh` — the 44-design HPWL sweep.
  - `bring_up/hpwl_gradient_computer/cosim/cosim_depth.tcl` — the FIFO depth sweep
    (`COSIM_DEPTH`, `COSIM_HAZARDS`).

## Decisions for Mark
1. **newblue3's real-design gradient fails `max_rel` (4.2e-4 vs 1e-4); rel_rms is fine (6.6e-7).**
   - The cause is one cell with 13,936 large-net pins. Its error relative to its own value is
     8.6e-6.
   - A plain float evaluation of the same formula is 3.8× *worse* on that node, so this is float
     conditioning, not logic.
   - Options:
     - (a) accept it, and document that `max_rel` is normalised to the design's RMS, which a
       14 K-pin node outgrows;
     - (b) judge real designs by each node's error relative to its own magnitude;
     - (c) leave it as is.

   Tier 1 is unaffected. I left the tolerance unchanged.
2. **Should `large_nets` become the default?** Every consumer handles it now; the exception is
   chunked designs, which still drop large nets (rule C2).

## Next steps, ranked

### 1. Check the per-NMU bandwidth (cheap; could change the stream design)
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

### 2. Close timing (needs the build server)
- **Status:** post-route 4.335 ns vs 3.33 ns target (about 231 MHz), worst slack −1.0 ns, 20 K
  failing endpoints, measured on the **fused** loop, before the DATAFLOW split and large nets.
  Continue [[_NEW_HANDOFF_41_pnr_timing_20260923.md]]; don't restart.
- **Re-measure first** on today's design: the HLS-estimated worst path is now C's combiner (stage
  slack −0.39 ns, top −0.53 ns). A fresh post-route run comes before any fix.
- **First fix (from the fused run):** the top paths start at one float adder HLS shared between
  `refresh_macro_pins` and `fold_macro_pins`, fanning out to all 64 URAM banks. Unshare it and
  register its output before the bank broadcast. Then group the remaining failing endpoints
  (`path_groups.tcl`).
- **Watch, two interlocks:**
  - Deeper pipelines lengthen the read-add-write. If HLS raises II, raise `HAZARD_DISTANCE` (8
    costs <0.5% bubbles) and re-run the co-sim spacing sweep.
  - Retiming can also grow the beat-write → per-net-write **skew** in A and B. Depth 32 leaves 17
    states of margin; after any change, re-read the two write states in
    `*.verbose.sched.rpt` (see Facts).
- **Build-server rule:** the tunnel needs Mark to run `wsl ssh -fN build`; ask once, launch long
  jobs detached, don't poll.

### 3. Small fix: `hpwl_gradient_computer_v2` loses bursts on its two LUT loads
`offset_table` and `exp_lut` (32-bit) share gmem0, which is 1024 bits wide for `ChunkDesc`, so both
loads have no burst (pipeline depth 75). Give them their own 32-bit bundle, re-run C-synth, check
the *Inferred Burst Summary* in `csynth.rpt`. Low runtime cost today (small loads), so do it when v2
is next touched.

### 4. Chunking walkthrough + large nets in chunks + the resident-loop URAM budget (the Big Fix)
- **Why later:** chunking only matters once the resident loop decides what stays on chip.
- **Large nets are not chunked** (rule C2: `encode_chunked` homes only small nets), so the 8
  designs over 1 M slots, bigblue4 among them, still drop them. Homing a large net in one chunk
  needs its ghosts like any small net. The device side is ready: `hpwl_gradient_computer_v2`
  passes span counts equal to `desc.num_beats` today, which is the only change needed there.
- **The Big Fix:** in the resident `pl_algo` loop, positions and gradients stay in URAM, so the
  load and drain disappear (≥26 K of ~65 K cycles per axis on adaptec1 today).
- **Open constraints** (recorded in `vck5000/pl/src/pl_algo/DATAFLOW.md`, section "#41
  record-stream gradient inside the resident loop"):
  - one axis is 256 of 463 URAMs at 1 M slots, so both axes resident at once does not fit;
  - overlap belongs between modules via `axis` streams, not DATAFLOW between phases sharing a
    URAM array.
- **Reading list for the walkthrough:**
  - `vck5000/bring_up/hpwl_gradient_computer_v2/src/modules/hpwl_gradient_computer_v2.hpp` (its
    doc block explains the export / compute / fold passes);
  - `hpwl_computer_v3.hpp`;
  - `vck5000/bring_up/beat_packer/README.md` (protocol, `encode_chunked`);
  - Part 2 of [[_NEW_REPORT_41_record_datapath_20260922.md]].

### Later, optional
- **Fold `hpwl_computer_v2`'s beat loop onto `pin_bbox`.** They now carry the same large-net logic
  twice. It is a cleanup, so do it with the bit-identity driver.
- **newblue3's pads:** pins of one node with identical offsets have identical gradients, so a
  pin-multiplicity weight would remove most splits. That would remove most of newblue3's 4,002 pads
  (≈3% of its stream) and shrink its 16-beat extents. It would also ease decision 1: the 13,936-pin
  cell is exactly such a node.

## Facts established (don't re-derive)
- **A DATAFLOW stage that writes a beat stream and a later per-net stream needs downstream FIFO
  depth ≥ extent − 1 + skew**, where skew = (per-net write state) − (beat write state) in that
  stage's pipeline. A stalled pipeline stalls whole, so the in-flight iterations have already
  written. To find the states:
  `awk '/^State [0-9]+ </{s=$2} /_ssdm_op_Write.ap_fifo/{print s, $0}' <stage>.verbose.sched.rpt`
  under `.autopilot/db/`.
- **An HLS stream deeper than 16 goes to BRAM** (29 BRAM18 for a 2,132-bit beat). At ≤ 16 it was
  FF/LUT (3,095 per FIFO).
- **C simulation cannot deadlock, and RTL co-sim can.** XSIM's deadlock detector names the blocked
  FIFO pair.
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
- **DATAFLOW fits *inside* the beat loop, not *between* the module's phases.** Inside: each array is
  owned by one stage. Between phases: random-access dependencies, URAM ping-pong would need 512 of
  463, and `pos_URAM` has several writers.
- **Loop-carried arithmetic breaks II=1; feed-forward depth doesn't.** For a reduction across beats,
  use a shift-register window plus a tree, never a running accumulator. Masked tree inputs take the
  operator's identity (`Op::identity()`); `window[0]` is right for max/min but wrong for a sum.
- **Tier 1's tolerance cannot prove a refactor is exact** (it passes a 1-ulp change). Use the
  bit-identity driver.
- **`hls::stream` in tier 1** is a `std::deque` stand-in (`test/tier1_stub.hpp`). It has a default
  constructor only, so don't name streams. Module headers include `<hls_stream.h>` under
  `#ifndef PL_TIER1_STUB`.
- **The test Makefile tracks header dependencies** (since `4fadc25`): touching a module header
  rebuilds exactly the harnesses that include it.
- **Co-sim needs** `LIBRARY_PATH=/usr/lib/x86_64-linux-gnu` and takes about 13 min for synthesis plus
  two co-sims; several depths run fine in parallel on this box (8 cores, ~2 GB each).
