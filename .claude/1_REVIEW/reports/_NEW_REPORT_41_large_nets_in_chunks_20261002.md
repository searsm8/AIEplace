# REPORT #41 — large nets (17..96 pins) in the chunked modules, and `large_nets` on by default

**Status: DONE (2026-10-02).** Large nets are carried by every module, chunked designs included,
and `Config::large_nets` is on by default. The plan (approved by Mark 2026-10-02, including both
default choices and the default flip) is kept below the results. Context:
[[_NEW_PLAN_41_large_net_gradient_fifo_20261002.md]] (large nets in the unchunked modules),
[[_NEW_HANDOFF_41_next_steps_20261002.md]] step 4. Data: `.claude/2_ARTIFACTS/large_nets_in_chunks/`.

## Results

**Verification, all passing:**
| check | result |
|---|---|
| tier 1 (`make test`) | all harnesses PASS. Both chunked tests now use the full large-net fixture (17..100 pins, split nodes, nets that must drop): v3 HPWL bit-exact at 9 chunks; gradient rel_rms 3.0–3.7e-7 at 1 / 9 / 36 chunks |
| coverage asserted | 70 large nets with external slots, all 7 span groups (v3); external nodes carry 16% of \|grad\|² (gradient) |
| mutants (`mutants.sh`) | 5/5 caught: span counts forced empty; each module reading chunk 0's counts (×2); large nets not homed; their foreign nodes not marked external |
| packer, 44 designs at 1 M slots | `check_chunked` ok on all 44; 614,117 large nets encoded, **0 dropped** |
| real designs, chunked | HPWL bit-exact on bigblue4 (ISPD + MMS), superblue12, newblue7. Gradient: ISPD bigblue4 and superblue12 PASS (rel_rms ~7e-7) |
| C-synthesis | every loop II=1 in both modules. `ChunkDesc` 416 → 512-bit port. The 32-bit small-table bundle bursts all three loads (`offset_table`, `exp_lut`, `group_counts`) |
| RTL co-sim (gradient, 10 chunks, 9 large nets all with external slots) | PASS, no deadlock: the large-net beat FIFOs drain across chunk restarts |

**The cost: external slots roughly double on the biggest designs** (44-design sweep, 1 M slots):
| design | K, small only → large nets | external slots, small only → large nets |
|---|---|---|
| ispd2005/bigblue4 | 3 → 4 | 18.8% → 40.0% |
| mms/bigblue4 | 3 → 4 | 19.4% → 39.2% |
| mms/newblue7 | 3 → 4 | 13.1% → 26.8% |
| mms/newblue5 | 2 | 10.1% → 16.0% |
| mms/newblue6 | 2 | 9.9% → 13.6% |
| bigblue3 (ISPD / MMS) | 2 | 6.7% / 7.9% → 10.7% / 11.5% |
| mgc_superblue12 | 2 | 4.3% → 5.1% |

A 96-pin net rarely has a majority of its nodes in one chunk, and `locality_order` ignores large
nets. Packer time also roughly doubles on bigblue4 (35 → 68 s). Both feed #42.

**Two MMS gradient `max_rel` failures are NOT chunking** (mms/bigblue4 1.2–1.6e-4, mms/newblue7
axis 0 1.5e-4, vs tol 1e-4; rel_rms ~6e-7). The same harness compiled with a 4 M-slot capacity
gives identical `max_rel` in **one** chunk (no mailbox) as in four (`maxrel_ab.txt`). It is the
open newblue3 "decision 1": float conditioning on MMS nodes with thousands of large-net pins,
judged against a design-RMS-normalised `max_rel`. These designs were simply unmeasurable before.

**Fixed along the way:**
- **The default flip silently re-enabled large nets in two "large nets off" runs** (`hpwl_computer_v2_test`,
  `hpwl_gradient_computer_test` used `packer::Config()`). Both now ask for off explicitly.
- **HLS widened the new 32-bit bundle to 64 bits** for `group_counts`' fixed-length read, which killed
  the `offset_table` / `exp_lut` bursts (a pointer narrower than its port gets none).
  `max_widen_bitwidth=32` on that bundle fixes it. Gradient-module top slack read −1.19 ns (was −1.46),
  **but not creditable to this work:** the synthesis ran with another session's uncommitted
  timing fix in the tree (an un-shared `refresh_macro_pins` adder, `BIND_OP` in
  `hpwl_computer_v2.hpp`), which targets exactly the old worst path. Tier 1, synthesis and co-sim
  here all included that change; it is not part of this commit.
- **A mutant I planned was wrong:** homing a net in a "wrong" chunk is not a bug. It still computes
  correctly and only costs external slots, so no test should catch it.

**Not updated:** `.claude/2_ARTIFACTS/grad_identity/grad_dump.cpp` (the bit-identity driver) still uses
the pre-rename names and the old signature, and relies on `Config()` meaning large nets off. Fix
it before relying on it.

---
# The plan, as approved

## The idea (nothing new to learn)
A large net is chunked exactly like a small one: it is **homed** in the chunk that owns most of its
nodes; its nodes owned elsewhere become **external** slots in that chunk; the home chunk computes
it; external gradients return through the **mailbox**. The only difference is scale: a 96-pin net
touches many more nodes, so it creates more external slots.

Inside a chunk, a large net is packed by the same code as an unchunked design (`pack_large_nets`:
span groups, hazard pads, extent ≤ `MAX_NET_EXTENT`). So the chunk stream follows the existing
L1–L8 rules with no new device logic.

## What changes
| where | today | after |
|---|---|---|
| `build_chunks` (packer) | votes on and homes 2..16-pin nets only (rule C2) | also homes 17..96-pin nets when `cfg.large_nets` |
| `check_chunked` | compares the decoded union with the small nets | small nets + every chunk's encoded large nets |
| per-chunk counts | `beat_count[15]` inside `ChunkDesc`; span groups hard-coded empty on the device | both arrays move to a separate int32 table, `group_counts` (15 degree + 7 span counts per chunk) |
| `ChunkDesc` | 28 int32 = 896 bits (1024-bit port) | 13 int32 = 416 bits (512-bit port) |
| both chunked modules | `span_count_REG[i] = desc.num_beats` | read from `group_counts` |
| `Config::large_nets` | `false` | `true` (Mark, 2026-10-02) |

**Why the separate table:** adding 7 span counts to `ChunkDesc` makes it 1120 bits, past the
1024-bit m_axi maximum. The 32-bit small tables (`offset_table`, `exp_lut`, `group_counts`) get
their own 32-bit bundle, which also fixes the known missing-burst issue on `offset_table` / `exp_lut`
(handoff step 3).

## Choices (Mark-approved defaults)
1. **Home large nets by majority vote**, like small nets. Not taken: splitting a large net across
   chunks and merging partial bboxes / sums (a cross-chunk reduction, gain unmeasured).
2. **Keep large nets out of `locality_order`.** High-fanout nets would pull unrelated nodes together
   and worsen the cut. Revisit under #42 if the external-slot numbers say so.

## Steps and checks
1. **Packer.** Home large nets; extend `check_chunked`.
   Check: the checker passes on all 44 designs at 1 M slots with 0 large nets dropped; external %
   and K are compared against the small-only baseline
   (`.claude/2_ARTIFACTS/large_nets_in_chunks/sweep_before_small_only.txt`).
2. **Per-chunk group counts.** Add the `group_counts` table and the 32-bit bundle; both modules read it.
3. **Tests.** Both chunked tests run with large nets (now the default), counting large nets in the
   golden's net total. New coverage check: large nets exist that carry external slots. Mutant: span
   counts forced empty must fail.
4. **Real designs.** Chunked HPWL on bigblue4 and superblue12 with large nets: bit-exact.
5. **Synthesis and co-sim.** C-synth both modules (II=1, bursts). Co-sim the gradient module chunked
   with large nets: the first run where the beat FIFOs (large-net bbox / sums) meet chunk restarts.
   They must be empty at every chunk boundary.
