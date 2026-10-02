# HANDOFF #41 — what to work on next (rewritten end of 2026-10-02)

Where `hpwl_gradient_computer` (the WA-gradient PL module on the static pin-record stream) stands,
and the ranked next steps. Background:
[[_NEW_EXPLAINER_41_hpwl_gradient_computer_20261001.md]] (what the module is),
[[_NEW_REPORT_41_ddr_bundles_20261001.md]] (DDR / NoC / URAM findings),
[[_NEW_REPORT_41_record_datapath_20260922.md]] (how it was built and verified),
[[_NEW_PLAN_41_large_net_gradient_fifo_20261002.md]] (**the large-net plan; steps 1–2 done**).

## State (everything committed; HEAD `7c6ec41`, tree clean)
- **Large nets (17..96 pins), HPWL path:** done in `hpwl_computer_v2`, bit-exact on all 36
  fitting designs. The packer emits them opt-in (`Config::large_nets`) as span groups 2..8, at
  their minimum span: 48 excess beats in 1.58 M, 0 dropped.
- **Large-net bbox:** **window form** (Mark): a shift register of the last 8 beats' max/min, reduced
  by a masked tree on a net's last beat. No loop-carried arithmetic.
- **Step 1 (done):** the gradient beat loop is three DATAFLOW stages, `pin_bbox` → `wa_sums` →
  `wa_gradient` (one header each). Outputs are bit-identical to the fused loop, II=1 per stage, and
  the co-sim hazard sweep still gives spacing 4 PASS / 1 FAIL.
- **Step 2 (done):** the large-net section is hazard-scheduled with **pads**: all-EMPTY beats with
  lane 0 EMPTY, invisible to net accounting. 4,035 pads over 44 designs; 4,002 are on newblue3,
  from split nodes.
- **Packer contract:** fully written in the header of `beat_packer.hpp` (rules R/S/B/L/M/C, each
  with what verifies it). **Read it before touching the packer or a consumer.**
- **Tooling:**
  - `.claude/2_ARTIFACTS/grad_identity/` — the bit-identity driver; use it for any refactor.
  - `.claude/2_ARTIFACTS/large_net_tools/` — the 44-design sweep, design list, drop diagnosis, and
    mutant scripts.

## Next steps, ranked

### 1. Step 3 of the plan: large nets in the three gradient stages — designed, NOT started
Nothing is coded. The design below is worked out; check it against the plan, then build.

**Per stage:**
- **A, `pin_bbox`:** port `hpwl_computer_v2`'s large-net path:
  - `large` / `pad` / beat counter, and EMPTY lanes take lane 0's position;
  - the max/min window, skipped on pads;
  - the HPWL in `v[0]` on the last real beat.

  Add flags to `PinBeat`: `large`, `pad`, `first` (first real beat of a net), `last`. On a net's
  last real beat, also push `(max, min)` to a new **bbox stream**.
- **B, `wa_sums`:**
  - On `large && first`, pop the bbox stream into two registers. They are only assigned, never
    computed on, so there is no feedback arithmetic.
  - Large lanes use those registers; small lanes keep `pb.net_max/min`.
  - Each beat's four sums are the degree-16 tree outputs `[14][0]`; EMPTY lanes contribute 0
    (a±=0).
  - Shift them (pads excluded) into a 4×8 window. On the last real beat, reduce the newest `span`
    entries and push `(B+, C+, B−, C−)` to a new **sums stream**.
- **C, `wa_gradient`:** on `large && first`, pop the sums stream into registers, and use them for
  `k = 0` on large beats. The merge and scatter are unchanged; packer rule L6 covers the hazard.

**Masking — use the identity, not `window[0]`:** generalise `reduce_window<Op>` (in
`hpwl_computer_v2.hpp`) to fill masked entries with `Op`'s identity: −∞ for max, +∞ for min, 0 for
add. `window[0]` is right for max/min but wrong for sums. Add `identity()` to `MaxOp`/`MinOp`
(`hpwl_computer.hpp`) and `AddOp` (`wa_sums.hpp`). v2 must stay bit-exact through that change.

**FIFO depth and deadlock — the open correctness item:**
- B reads a net's first beat, then blocks on the bbox stream until A finishes the net's last real
  beat. So `pin_beats` must hold the net's whole **extent**: first to last real beat, pads inside
  included. The same applies to `sum_beats`.
- Span ≤ 8, but split-node pads stretch the extent. **Add contract rule L8:** the extent is at most
  `MAX_NET_EXTENT`. The packer must enforce it (drop or count, as with `MAX_SPAN`) and the checker
  must check it.
- **Measure the real maximum extent over the 44 designs first** (newblue3 is the worst), then pick
  `MAX_NET_EXTENT` and set both stream depths ≥ that.
- The bbox and sums streams only need to absorb how many nets A runs ahead (≈ depth/2). Give them 16.
- C simulation cannot deadlock; RTL co-sim can. **Sweep the stream depth in co-sim** and expect a
  deadlock below the bound. That is the proof the bound is right.

**Plumbing:**
- `gradient_beat_loop` and `pin_bbox` need `span_count_REG`.
- `hpwl_gradient_computer`, `hpwl_gradient_computer_top.cpp` and `cosim/cosim_top.cpp` need a
  `span_count` pointer on `gmem1` (as in `hpwl_computer_v2_top.cpp`).
- The chunked `hpwl_gradient_computer_v2` passes span counts all equal to `desc.num_beats`, as
  `hpwl_computer_v3.hpp` does. Chunks carry no large nets.

**Verification:**
- **Golden:** `test/wa_gradient_golden.hpp` `wa_gradient()` skips nets that are not `in_scope`. Give
  it the set of encoded large nets (`enc.large_nets`) to include. The `[3]` "touched" sets in
  `hpwl_gradient_computer_test.cpp` must include them too.
- **Harness:** run the large-net configs on `SyntheticSpec::max_large_degree = 100` plus the
  hand-built nets `hpwl_computer_v2_test.cpp` adds (forced extra spans, split node, forced drop).
  Keep a large-nets-off config.
- **Tolerances:** the same (rel_rms < 1e-5, max_rel < 1e-4). The HPWL by-product stays bit-exact.
- **Mutants:**
  - B pops on the wrong beat;
  - sums window ignores span, or uses `window[0]` instead of 0;
  - C uses the beat's own sums instead of the net's;
  - a pad shifts the sums window.
- **Bit-identity:** with large nets off, outputs must stay bit-identical to HEAD (identity driver).
- **Synthesis and co-sim:** C-synth (II=1 per stage, note the FIFO area); co-sim hazard sweep plus
  depth sweep; then the real-design gradient runs (adaptec1, newblue2, newblue3).

**Known duplication:** `pin_bbox` and `hpwl_computer_v2`'s beat loop will carry the same large-net
logic. Folding v2 onto `pin_bbox` is a later cleanup; mention it rather than doing it mid-step.

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
  failing endpoints, measured on the **fused** loop. Continue
  [[_NEW_HANDOFF_41_pnr_timing_20260923.md]]; don't restart.
- **Re-measure first:** the DATAFLOW split moved the HLS-estimated worst path into C's combiner
  fmul chain (top −0.61 → −0.53 ns; A and B −0.00). A fresh post-route run on the split design
  comes before any fix.
- **First fix (from the fused run):** the top paths start at one float adder HLS shared between
  `refresh_macro_pins` and `fold_macro_pins`, fanning out to all 64 URAM banks. Unshare it and
  register its output before the bank broadcast. Then group the remaining failing endpoints
  (`path_groups.tcl`).
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
  - the 8 of 44 designs over 1 M slots keep per-chunk DDR traffic, and **chunks carry no large
    nets yet** (`encode_chunked` homes only small nets);
  - overlap belongs between modules via `axis` streams, not DATAFLOW between phases sharing a
    URAM array.
- **Reading list for the walkthrough:**
  - `vck5000/bring_up/hpwl_gradient_computer_v2/src/modules/hpwl_gradient_computer_v2.hpp` (its
    doc block explains the export / compute / fold passes);
  - `hpwl_computer_v3.hpp`;
  - `vck5000/bring_up/beat_packer/README.md` (protocol, `encode_chunked`);
  - Part 2 of [[_NEW_REPORT_41_record_datapath_20260922.md]].

### Later, optional
- **newblue3's pads:** pins of one node with identical offsets have identical gradients, so a
  pin-multiplicity weight would remove most splits, and with them most of newblue3's 4,002 pads
  (≈3% of its stream). Not worth it yet.

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
- **DATAFLOW fits *inside* the beat loop, not *between* the module's phases.** Inside: each array is
  owned by one stage. Between phases: random-access dependencies, URAM ping-pong would need 512 of
  463, and `pos_URAM` has several writers.
- **Loop-carried arithmetic breaks II=1; feed-forward depth doesn't.** For a reduction across beats,
  use a shift-register window plus a tree, never a running accumulator.
- **Tier 1's tolerance cannot prove a refactor is exact** (it passes a 1-ulp change). Use the
  bit-identity driver.
- **`hls::stream` in tier 1** is a `std::deque` stand-in (`test/tier1_stub.hpp`). It has a default
  constructor only, so don't name streams. Module headers include `<hls_stream.h>` under
  `#ifndef PL_TIER1_STUB`.
- **The test Makefile tracks header dependencies** (since `4fadc25`): touching a module header
  rebuilds exactly the harnesses that include it.
- **Co-sim needs** `LIBRARY_PATH=/usr/lib/x86_64-linux-gnu` and takes about 13 min for the hazard
  sweep (synthesis plus spacing 4 and 1).
