# #42 — Partition quality vs mailbox widening, judged end to end (2026-10-02)

**Question (Mark):** would a better chunk partitioner pay off, judged on end-to-end runtime (host
start-up + device)? Mark then asked to explore **widening the mailbox loops** first. Assumptions:
300 MHz (Mark; v2 C-synth does not yet meet 3.33 ns, see "Slack" below), 1000 iterations, 2 gradient
axes per iteration. Density / field-solve device time is **not** included anywhere below.

## Answer
1. **The falsifier fails, by a lot.** On today's v2 module the mailbox is **49–85%** of a chunked
   gradient evaluation, not "well under 10%". It is worse than the back-of-envelope figure, because
   the 1-float **send write and collect read are not bursts** (4.26 cycles per entry in RTL co-sim,
   not 1).
2. **Widening fixes it, partition-independently.** At 16 entries per beat the mailbox drops to
   **2.4–12.2%**. The host layout reaches **99.3–99.8% lane use** on all 8 chunked designs, with 0
   rule violations and 0 hazard beats. End to end, bigblue4 goes from **86.7 s to 28.7 s**.
   Prototype: tier 1 bit-identical, tier 2 II=1 on all four loops.
3. **The price is LUTs:** ~32 K LUT per widened loop, so **~128 K for all four (~14% of the
   VC1902)**. The four 1-float loops cost ~2.6 K. This is the decision for Mark (options below).
4. **Host start-up was dominated by one O(n²) line, now fixed.** `build_chunks` K=4 on bigblue4
   took 57 s, all of it in a `vector::erase` inside parcel ordering. A `deque` cuts it to 3.8 s, with
   output bit-identical. bigblue4 start-up: **69.9 s → 15.0 s**.
5. **Partition, after widening: only the K=4 designs, and mostly through K.** A cut that fits K=3
   would save ~7% (bigblue4) to ~14% (newblue7) of end-to-end. The BFS cut misses K=3 by only 3.9 K
   slots on bigblue4 and 33 K on newblue7, so a capacity-aware or balanced cut is the lever, not a
   min-cut library. Not done; recommendation only.

## Method
- **`vck5000/bring_up/beat_packer/chunk_profile.cpp`** (`make profile`, new) gives three things
  per design:
  - host start-up per stage, with each `build_chunks` K attempt timed separately;
  - a per-phase cycle model of `hpwl_gradient_computer_v2`, from the descriptor trip counts;
  - the same model with the real widened layout.
- **Cycle model calibration.** Two RTL co-sims of v2:
  - the existing bench: capacity 256, K=10, 953 external, **62,573** cycles per axis;
  - a new medium run: 3,000 cells, capacity 4,096, K=2, 2,275 external, **37,373** cycles per
    axis. It passed numerically. Recipe (tb, top, tcl, latency report) is in
    `.claude/2_ARTIFACTS/chunk_profile_42/medium_cosim/`, and the fit is `calib2.cpp` / `check_fit.cpp` next to it.

  The first version of the model (1 cycle per entry) under-predicted the medium run by 37%. The
  synthesis log shows why: `send_parcel`'s mailbox write sits behind `if (slot < 0) continue` and
  is not inferred as a burst, and neither is `collect_parcel`'s mailbox read. Each access then waits
  on the co-sim AXI model's 64-cycle latency with 16 requests in flight, about 4 cycles each.

  Fitting two unknowns to the two runs gives **165 cycles per loop invocation** and **4.26 cycles
  per send/collect entry**. With two points and two unknowns the fit is exact by construction; the
  independent check is that 4.26 ≈ 64/16. Real DDR latency at 300 MHz is likely higher, so 4.26 is
  a **floor**, and today's 1-float numbers below are optimistic.

  On the real designs, loop invocations are <1% of cycles, so the model is essentially trip counts.
- **Unchunked cross-check:** the hpwl_gradient_computer co-sim (294 beats, 2,103 cycles) is
  over-predicted by ~600 cycles. That error is all invocation overhead and is irrelevant at scale.

## Results — the 8 chunked designs (per axis, one gradient evaluation)
"bursts fixed" = the 1-float loops with the send/collect burst defect removed (`--unburst 1`).
The last column is start-up + 1000 × 2 axes × cycles / 300 MHz; the widened figure also includes the
widened layout's own build time (≤1.5 s).

| design | K | ext% | start-up before → after (s) | 1-float cyc/axis (mailbox %) | 1-float, bursts fixed | widened (mailbox %) | lane use | end-to-end s: now → bursts fixed → widened |
|---|---|---|---|---|---|---|---|---|
| ispd2005/bigblue3 | 2 | 10.7 | 9.1 → 5.2 | 2.03 M (61%) | 1.26 M (37%) | 0.82 M (3.7%) | 99.56% | 18.7 → 13.6 → 10.9 |
| ispd2005/bigblue4 | 4 | 40.0 | 69.9 → 15.0 | 10.75 M (85%) | 5.09 M (68%) | 1.83 M (12.2%) | 99.72% | 86.7 → 48.9 → 28.7 |
| mgc_superblue12 | 2 | 5.1 | 8.8 → 6.4 | 1.41 M (49%) | 0.99 M (27%) | 0.74 M (2.4%) | 99.27% | 15.8 → 13.0 → 11.4 |
| mms/bigblue3 | 2 | 11.5 | 10.6 → 5.5 | 2.19 M (61%) | 1.37 M (37%) | 0.89 M (3.7%) | 99.64% | 20.1 → 14.6 → 11.7 |
| mms/bigblue4 | 4 | 39.2 | 67.3 → 15.9 | 10.72 M (84%) | 5.17 M (66%) | 1.99 M (11.0%) | 99.72% | 87.3 → 50.4 → 30.6 |
| mms/newblue5 | 2 | 16.0 | 16.8 → 7.3 | 2.94 M (70%) | 1.66 M (48%) | 0.92 M (5.5%) | 99.76% | 26.9 → 18.3 → 13.8 |
| mms/newblue6 | 2 | 13.6 | 14.8 → 7.6 | 2.69 M (66%) | 1.58 M (43%) | 0.94 M (4.6%) | 99.72% | 25.5 → 18.1 → 14.2 |
| mms/newblue7 | 4 | 26.8 | 51.4 → 18.6 | 8.91 M (78%) | 4.58 M (58%) | 2.09 M (8.2%) | 99.64% | 78.0 → 49.1 → 33.7 |

**After widening, what is left of the chunked overhead is the chunk reload / fold / drain passes**
(bigblue4: send-pass reload 0.20 M + load 0.20 M + fold 0.38 M + drain 0.19 M, against a 0.63 M beat
loop). Those scale with K and with total slots, not with the mailbox. They are the resident loop's
problem (#41 "Big Fix"), and K is the partition's only remaining lever.

**K=3 what-if** (`--force-k 3`, capacity unchecked, `whatif_k3.txt`): bigblue4 externals drop from
40% to 28%, and widened end-to-end goes from 28.7 to 26.7 s (−7%). newblue7: 33.7 → 29.1 s (−14%).
The BFS cut at K=3 overshoots the 1,048,576-slot capacity by 3,872 slots (bigblue4) and 32,768
(newblue7). So it is close, but **not a valid partition as it stands**.

**Unchunked designs (36/44):** start-up is 0.1–5.2 s, and **parse is ~55–70% of it**. The gradient
device time is 0.08–2.6 s for 1000 iterations. On every unchunked design, host start-up is the same
size as the whole gradient run or larger, so a faster parser would be the next host-side lever.
Full table: `all44_after_deque_fix.txt`.

## Start-up: the O(n²) erase
gprof on bigblue4 accounted for only 17.6 s of 82 s; the rest was in libc (memmove). The per-stage
timer pinned it to `build_chunks` K=4 (57.0 s), while the failed K=3 attempt cost only 3.1 s. The
cause was the parcel-ordering loop: it picks from a sorted `std::vector` and `erase`s mid-vector.
Every pick lies within the first `hazard` entries, so a `std::deque` gives the same order at O(1).
- **Fix:** one line in `beat_packer.hpp` (`build_chunks`).
- **Proof:** an FNV hash over every `chunked_device_arrays` output is identical before and after on
  bigblue3, mms/newblue5 and bigblue4, and `check_chunked` passes.
- `make test`: PASS.

## The widening prototype — `vck5000/bring_up/mailbox_widened/`
- **`mailbox_layout.hpp`** (host) groups each parcel into 16-lane beats under three rules:
  - **W1:** the owner slots in a beat are in distinct banks.
  - **W2:** the consumer slots in a beat are in distinct banks.
  - **W3:** an owner slot is not repeated within HAZARD_DISTANCE beats across its parcels (collect
    is a read-add-write).

  Parcels are whole beats, laid out consumer-major as today. The greedy grouping takes the fullest
  (owner bank, consumer bank) buckets first. `check_wide_mailbox` verifies W1–W3 and checks that
  every external entry travels exactly once, with both sides naming the same node.
- **`src/modules/mailbox_widened.hpp`** (HLS) holds `send_wide`, `receive_wide`, `return_wide` and
  `collect_wide`. Each loop reads one lane-list beat plus one mailbox beat per cycle; the URAM side
  is bank-major, the same idiom as `gather_pin_positions` and the `wa_gradient` scatter-add.
- **Tier 1:** `test/mailbox_widened_test.cpp` (in `make test`) compares against the 1-float loops on
  the same `Chunked` design. Positions and gradients are **bit-identical** (memcmp, NaN external
  slots included): collect adds each slot's contributions in the same parcel order, so the float
  sums do not even reorder. Mutants:

  | mutant | caught by |
  |---|---|
  | send skips its lane-list advance | [2] positions |
  | collect overwrites instead of adding | [3] gradients |
  | layout ignores W3 | [1] layout (6 violations); the C model cannot see a hazard |
  | layout ignores W2 | [1] layout |
  | scatter drops lane 15 | [2]/[3] |

  The lane-use bound (60%) comes from the observed 73% on the synthetic K=7 design: small parcels
  round up to whole beats.
- **Tier 2** (`synth_check.tcl`, top `mailbox_widened_top`): all four loops reach **II=1** at an
  estimated 2.43 ns, and every mailbox and lane-list access is a 512-bit burst. The first attempt
  had real II violations, because a DDR beat passed by reference makes HLS issue 16 word reads;
  copying each beat into a local fixed it.

  | loop | LUT | FF | depth |
  |---|---|---|---|
  | receive | 31.4 K | 8.9 K | 4 |
  | send | 31.8 K | 5.6 K | 147 |
  | return | 31.7 K | 5.9 K | 9 |
  | collect | 32.3 K | 120.5 K | 78 |

  collect also uses 32 DSPs (the 32 parallel fadds). The top-level total is 177.9 K LUT (19.8%) and
  168.5 K FF (9%), including the URAM port muxing across six loops.
- **Not done:** integrating it into `hpwl_gradient_computer_v2` (that module belongs to the active
  #41 work), and RTL co-sim of the widened loops.

## Options for Mark (the LUT question)
| option | mailbox cycles | extra LUT | bigblue4 end-to-end |
|---|---|---|---|
| A. Fix the two 1-float bursts only: skip padding segments in the outer parcel loop, so the inner write/read is unconditional | 4E | ~0 | 86.7 → 48.9 s |
| B. Widen all four loops as prototyped | 4E/16 | ~128 K (four crossbars) | → 28.7 s |
| C. Widen, but route mailbox beats through the beat loop's EXISTING gather (pin_bbox) and scatter-add (wa_gradient) | 4E/16 | ~0 new crossbar (est.) | ≈ B |
| D. Widen with one shared gather and one shared scatter unit (send+return, receive+collect) | 4E/16 | ~64 K (est.) | ≈ B |

A is nearly free and worth doing whatever else is chosen; it is a v2 change, so it belongs to #41.
C is the most attractive and the most invasive. D is the conservative middle.

## Slack (recorded per Mark)
The 300 MHz assumption is optimistic for the chunked module today:
- `hpwl_gradient_computer_v2` C-synth estimate: −1.46 ns at 3.33 ns (#41), later read as −1.19 ns,
  with other uncommitted edits in the tree;
- the `gradient_beat_loop` region reads −1.05 ns in the v2 co-sim project's synthesis.

The widened loops by themselves estimate 2.43 ns. At the v2 estimate (~4.5–4.8 ns), every device
second above scales by ~1.4–1.45.

## What would falsify this
- **The 4.26 cycles per entry** comes from the co-sim AXI model. Real-hardware send/collect timing
  (Geert's card) would replace it; if real DDR latency is higher, the "now" column gets worse.
- **The model** has been checked against RTL only for 1-float v2, not for the widened loops. A
  widened co-sim, or integrating into v2 and re-running the medium co-sim, closes that gap.
- **The partition savings** come from an invalid (over-capacity) K=3 cut. They hold only if a
  capacity-respecting K=3 cut has similar externals.

## Artifacts / files
- `.claude/2_ARTIFACTS/chunk_profile_42/` (not tracked):
  - `all44_after_deque_fix.txt`: all 44 designs, fitted model;
  - `chunked8_bursts_fixed.txt`;
  - `whatif_k3.txt`;
  - `all44_before_deque_fix_oldmodel.txt`: before-fix start-up only, since its cycle columns use
    the pre-calibration model;
  - `tabulate.py` and `apply_unburst.py` (scratch).
- **New, tracked:**
  - `vck5000/bring_up/beat_packer/chunk_profile.cpp`, plus `Makefile` targets `chunk_profile` /
    `profile`;
  - `vck5000/bring_up/mailbox_widened/` (layout, module, top, `synth_check.tcl`);
  - `vck5000/test/mailbox_widened_test.cpp`, added to `HARNESSES` and `INCLUDES` in `test/Makefile`.
- **Changed:** `beat_packer.hpp`, the one-line deque fix.
- **Nothing committed.** The new files build on another session's uncommitted #41 edits in
  `beat_packer.hpp`, `pin_record.hpp` and `test/Makefile` (the vocabulary rename, large nets), so
  they cannot be committed on their own.
