# PLAN #41 — large nets in `hpwl_gradient_computer`: three stages joined by FIFOs (2026-10-02)

*Proposal for Mark, not built. It supersedes the mechanics of L3 in
[[_NEW_PLAN_41_large_nets_on_records_20260923.md]] (three passes that re-read a group's chunk
stream) and keeps L3's exact arithmetic. The stream it consumes already exists and is verified on
the HPWL path. Protocol: `vck5000/bring_up/beat_packer/README.md`, "Large nets". Status:
[[_NEW_HANDOFF_41_next_steps_20261002.md]] step 1.*

## The problem
In today's fused `gradient_beat_loop`, every value a pin needs is ready in the same beat: the
net's bbox, its four sums B±/C±, and the combine. A large net spans `span` ≤ 8 consecutive beats,
which creates two dependencies that cross beats:

1. **bbox before exps.** `a± = exp(±(x − max/min)/γ)` needs the bbox of *all* of the net's beats.
2. **sums before combine.** Each pin's partial needs B±, C± summed over *all* of the net's beats.

Each wait is bounded by `MAX_SPAN` = 8 beats. So the buffer is a delay of at most 8 beats per
stage boundary, not a store for a whole net group.

## Proposal: three stages, two pairs of FIFOs, one DATAFLOW region

```
          beats FIFO A→B (depth 16)                beats FIFO B→C (depth 16)
 ┌───────────────┐ ───────────────▶ ┌──────────────────┐ ───────────────▶ ┌────────────────────┐
 │ A  pin_bbox    │                  │ B  wa_sums        │                  │ C  wa_gradient      │
 │ gather, bbox   │ ──net bbox FIFO─▶│ exps, sum trees   │ ──net sums FIFO─▶│ combine, merge,     │
 │ trees, HPWL out│   (large nets)   │                   │   (large nets)   │ scatter-add (RMW)   │
 └───────────────┘                  └──────────────────┘                  └────────────────────┘
   reads pos_URAM, offset_BRAM        reads lut_BRAM                         owns grad_URAM
```

- **A:** today's gather and bbox trees. This is `hpwl_computer_v2`'s beat loop, including its
  running large-net bbox. Per beat it pushes x[16], the decoded lanes, the beat flags
  (small/large, first/last, degree) and, for small nets, each lane's own max/min. On a large
  net's last beat it also pushes `(max, min)` to the bbox FIFO. The HPWL by-product leaves from A.
- **B:** exps and sum trees. A small net takes its bbox from the beat. On the first beat of a
  large net, B pops the bbox FIFO; this blocking read is the "wait until the bbox is known".
  The beat FIFO's depth lets A run up to 8 beats ahead to finish that net. Per beat, B pushes x,
  the lanes, a±[16], and for small nets the per-net sums. On a large net's last beat it pushes
  the net's B±, C± to the sums FIFO.
- **C:** the combiner, lane merge and scatter-add. A large net pops its sums on its first beat;
  everything else is today's code. The `HAZARD_DISTANCE` dependence pragma moves into C with the
  read-modify-write.

**Throughput:** every stage is II=1, so each beat costs one cycle once, and large nets add only
their own beats. Re-streaming (L3) would cost large beats ×3, about 1.5× total on adaptec1 (51.5 K
small + 3 × 17.1 K large vs 68.6 K). The price is about 2 × 16 beats of extra latency, paid once
per call.

**Ownership:** each on-chip array has one stage that touches it: `pos_URAM` / `offset_BRAM` in A,
`lut_BRAM` in B, `grad_URAM` in C. The handoff's fact "DATAFLOW does not fit" was about the
*phases* (load / beat loop / fold / drain), which share `pos_URAM` and `grad_URAM`. That still
holds. This region contains only the beat loop, and the phases around it stay sequential.

## How B and C know when to read the per-net FIFOs
No stage ever asks "is this FIFO ready?". Each read is decided by flags that travel *inside* the
beat FIFO, and it is a blocking read, so it stalls until the data exists.

- **Producer:** A writes the bbox FIFO exactly once per large net, on the net's last beat.
- **Consumer:** B reads it exactly once per large net, on the first beat it receives with
  `large && first_beat`. A computed those flags (A owns the beat counter and `span_beat_count`)
  and passed them along with the beat.
- **Balance:** one write and one read per large net, in stream order, so the FIFO entries pair up
  with nets automatically. It ends each call empty, and every stage runs exactly `num_beats`
  iterations.
- **Ordering:** B can reach a net's first beat before A has finished that net, because A is at
  most `span` − 1 beats ahead in that net. B then stalls on the read until A pushes. Meanwhile A
  can keep going only if the A→B beat FIFO has room for the rest of the net, which is where the
  depth bound comes from.
- **B → C:** the same pattern with the sums FIFO. B writes on the net's last beat, C reads on the
  net's first beat, and the flags are forwarded unchanged.

## Accumulating sums across a net's beats: no feedback arithmetic
A running sum `S += beat_sum` is a feedback loop through a float adder. An fadd takes several
cycles, so at II=1 it would raise II; this is the same class of problem as the bbox compare, and
worse. And the integer-key trick that fixed the bbox doesn't apply to addition.

**Instead, B keeps a shift register of the last 8 beats' per-beat sums.** Those are the degree-16
tree outputs `[14][0]`, 4 values per beat. On a large net's last beat, a masked 8-input adder
tree reduces the newest `span` entries. A shift register is just registers, with no arithmetic in
the feedback path, so the reduction pipelines freely.

| cost | amount |
|---|---|
| state | 8 beats × 4 floats |
| adders | 4 trees × 7 adders |

A uses the same window for its bbox (a masked 8-input max/min tree). That has been in
`hpwl_computer_v2` since 2026-10-02 and replaced its integer-key running bbox, so A and B share
one pattern.

## The scatter-add hazard on large-net beats (packer work)
The `HAZARD_DISTANCE` contract (a node's read-modify-writes ≥ 4 beats apart) has two new ways to
be broken. A net's beats must be consecutive, because A finds net boundaries with a counter.

1. **Split node:** a node with more than 16 pins on one net has runs in two beats of that net,
   fewer than 4 apart when the span is < 5. It is rare (about 400 nets on MMS newblue3, 0 on
   ISPD2005).
2. **Shared node between nets:** a node on two large nets that are adjacent in the stream.

**Proposed fix: let a large net contain all-EMPTY padding beats.**
- **Device:** A ignores an all-EMPTY beat's tree output. That is one more condition next to the
  EMPTY-lane mask, because lane 0 holds no pin in such a beat.
- **Packer:** reorders nets within a span group, the same list-scheduling as `schedule_beats`.
  It pads only when reordering fails; a pad extends its net's span, still ≤ `MAX_SPAN`.
- **Checker:** enforces the hazard rule on the large-net section too.

**Measure first:** how many pads each design needs.

## Should the three stages be separate modules?
**Recommendation: yes, as three HLS functions in a DATAFLOW region inside the one kernel, each in
its own header. Not three kernels.**

- **For separate functions:**
  - Each stage gets its own pipeline schedule and depth, and its own timing problem. The current
    critical paths start at one HLS-shared adder fanning out to all 64 URAM banks, and smaller
    scheduled units make that kind of sharing visible and fixable.
  - Each stage is tier-1 testable on its own against intermediate goldens: per-net bbox, per-net
    sums, final gradient.
  - A is literally `hpwl_computer_v2`'s beat loop, so v2 and the gradient computer can share it
    instead of keeping two copies.
- **Against separate kernels (CUs):** stream connectivity would have to go through the `v++` link,
  with three control interfaces and three host launches, and nothing would gain from it. In one
  kernel, DATAFLOW gives the same overlap.
- **Files**, per the module-naming rule (one major function per file, named for its output),
  under `bring_up/hpwl_gradient_computer/src/modules/`:
  - `pin_bbox.hpp`
  - `wa_sums.hpp`
  - `wa_gradient.hpp`
  - `hpwl_gradient_computer.hpp`, which becomes the wrapper: load / DATAFLOW { A; B; C } / fold /
    drain.
- **Risks:**
  - **DATAFLOW deadlock** if a FIFO is too shallow. The bbox FIFO wait needs the A→B beat FIFO
    ≥ `MAX_SPAN` − 1 plus A's pipeline slack. C simulation cannot catch this; RTL co-sim can. Sweep
    the depth in co-sim, as the hazard spacing was, and expect a deadlock below the bound.
  - **FIFO area.** The B→C beat payload is about 3–4 Kbit wide (x, lanes, a±, per-net sums),
    which is too wide to put in BRAM efficiently; it would be SRL/LUTRAM, roughly 4 K LUTs per
    16-deep FIFO. That is an estimate; C-synth gives the real number. Passing per-net sums
    instead of per-lane sums keeps it down.

## Build order (easier environment first, as with the HPWL path)
1. **Split the existing small-net beat loop into A/B/C** with no large nets.
   *Done when:* tier 1 gives output bit-identical to today (same arithmetic), every stage is II=1
   in C-synth, and the co-sim hazard sweep still passes at spacing 4.
   **DONE 2026-10-02.** `pin_bbox.hpp` / `wa_sums.hpp` / `wa_gradient.hpp`, plus
   `gradient_beat_loop` as the DATAFLOW region, with the same signature, so the chunked
   `hpwl_gradient_computer_v2` inherits it.
   - **Bit-identity:** every gradient and HPWL output is bit-identical to the pre-split code over
     synthetic (3 packer configs), chunked synthetic (capacity 2048 / 768), adaptec1 and newblue2
     (driver `/tmp/grad_dump.cpp`, 15.7 MB dump). A 1-ulp perturbation is detected, and tier 1's
     tolerance would not catch one.
   - **C-synth (plain):**

     | | II | depth | slack |
     |---|---|---|---|
     | before (fused) | 1 | 84 | −0.61 ns |
     | A | 1 | 21 | −0.00 ns |
     | B | 1 | 35 | −0.00 ns |
     | C | 1 | 40 | −0.28 ns |
     | top | | | −0.53 ns |

     The combiner's fmul chain is now the worst path. Cost: LUT +11.8 K, FF +7.5 K, DSP +3,
     mostly the two depth-2 streams.
   - **C-synth (chunked):** II=1 in every stage, top slack −1.46 → −1.19 ns.
   - **RTL co-sim:** spacing 4 PASS; spacing 1 passes C sim and FAILS RTL (rel_rms 48), as
     before. No deadlock.
2. **Packer:** hazard-schedule the large-net section, with padding beats, and add the checker
   rule. Report pads per design over the 44-design manifest.
3. **Large-net paths:** A's from v2, B's window sums, C's sums pop.
   *Done when:* a tier-1 gradient golden that includes 17..96-pin nets holds today's tolerance
   (rel_rms ~4e-7, tol 1e-5).
4. **Synthesis and co-sim:** C-synth, co-sim of the hazard spacing and the FIFO depths, then the
   bit-exact HPWL by-product on the real designs.

## Decisions (Mark, 2026-10-02)
1. **Separate headers per stage.**
2. **Small steps:** step 1, the pure refactor, lands and is verified before any large-net code.
3. **Padding from the packer is acceptable.** The packer's full contract is now the header of
   `beat_packer.hpp` (rules R/S/B/L/M/C). This large-net hazard rule is listed there as **L6, not
   yet guaranteed**.
4. **Window form for both A and B:** one pattern, no integer-key trick. Landed in
   `hpwl_computer_v2` (`reduce_window`).
