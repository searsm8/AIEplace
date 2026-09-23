# #41 — the record-stream datapath: four device modules + chunking (2026-09-22, overnight)

Mark's brief (end of 2026-09-22): finish the beat packer, then build `hpwl_computer_v2` (positions
on chip, DDR streams records, random access only on chip), `hpwl_gradient_computer` (plus the
gradient and the on-chip scatter), `hpwl_computer_v3` (the chunking extension) and
`hpwl_gradient_computer_v2` (gradient plus chunking). The design this implements is in
[[_NEW_REPORT_41_beat_packer_20260922.md]] (Parts 1–2); the protocol is in
[[vck5000/bring_up/beat_packer/README.md]].

## Status at a glance
| module | tier 1 (offline golden) | mutation test | tier 2 (C-synthesis, 1 M slots) |
|---|---|---|---|
| `hpwl_computer_v2` | bit-exact, 3 packer configs × 2 axes | 4/5 caught (1 equivalent) | every loop II=1 |
| `hpwl_gradient_computer` | rel_rms 3.9e-7 (tol 1e-5); HPWL bit-exact | 10/11 caught (1 equivalent) | every loop II=1 |
| `hpwl_computer_v3` | bit-exact on 7 chunks and on 1 | 5/5 caught | every loop II=1 |
| `hpwl_gradient_computer_v2` | rel_rms 3.6e-7 on 7 chunks; HPWL bit-exact | 4/5 caught (1 equivalent) | every loop II=1 |

`make test` (tier 1) passes all 20 harnesses. Commits: `bc3d636` (packer library + macro pins),
`cac980f` (v2 + gradient), `0752b6f` (the landed #40 sources, see Housekeeping), `4e8fd0f`
(chunking), `2367363` (synthesis fixes).

**Real designs** (C simulation of the device modules, random positions; raw output
`.claude/2_ARTIFACTS/beat_packer/real_design_runs.txt`):
- **HPWL** (v2), bit-exact: adaptec1 (213 K nets), mgc_fft_1, MMS newblue2 (452 K nets, 74 K
  macro-pin slots).
- **Gradient**, rel_rms 3–8e-7: the same three designs.
- **Chunked** (v3 and gradient v2) on bigblue3, 2 chunks with 73 K ghosts: all 1.1 M nets
  bit-exact; gradient rel_rms 7e-7.
- ⚠️ newblue2's gradient max_rel is **9.5e-5 against the 1e-4 bound**. This is float cancellation
  in the absolute-coordinate combiner `(1 + x/γ)·B − C/γ` on macro-heavy nets (#15's known
  precision issue: net-local frames), not an encoding error, but the margin on real MMS data is thin.

## What was built
**Packer** (`bring_up/beat_packer/`), split into three files:
- `pin_record.hpp`: the protocol, shared by host and device.
- `beat_packer.hpp`: the host library.
- `beat_packer.cpp`: the CLI.

New this session:
- **Movable-macro pin slots** (Mark's call). With these, all 44 designs encode, at most 29 bits
  and at most 131 offsets.
- **Every movable node owns a slot.**
- **EMPTY is the all-ones record**, so `offset_bits` is the only split parameter.
- **Scheduled macro-pin list** (for the fold, below).
- **Chunking.**

**Device modules** (`bring_up/<module>/src/modules/`), one axis per call:
- **v2**: loads slot-major positions into 32 URAM banks at 16 slots per cycle, refreshes
  macro-pin slots on chip, gathers bank-major (each bank reads the one row addressed to it), and
  runs v1's Dhar trees. Output is per-net HPWL.
- **gradient**: v2 plus the rest of Dhar Method 1:
  - exp terms from packed-pair lookup tables, one copy per lookup,
  - four `dhar_tree<AddOp>` sum trees,
  - a per-net 1/B², the eq. 4 combiner,
  - a 4-step segmented scan merging a node's repeated lanes,
  - a bank-major scatter-add into grad URAM,
  - the macro-pin fold, and the drain.
- **v3**: an export pass (each chunk writes the positions others hold as ghosts), then a compute
  pass (import ghosts, then v2's beat loop). The export pass is skipped for one chunk.
- **gradient v2**: v3's passes, plus the ghost gradients written back into the chunk's own
  region, plus a fold pass (add the returned gradients, then the macro fold).

## Chunking (the extension)
- **Ownership and homes:** every movable node is owned by one chunk (a macro together with its
  pins), and every net is homed where most of its nodes live.
- **Ghosts:** a home net's foreign nodes become ghost slots, and fixed pins are copied.
- **Exchange buffer:** consumer-major, one region per consumer and one block per producer. All
  DDR traffic stays sequential; the random side is always on chip.
- **Chunk order:** chunks are contiguous runs of a breadth-first locality order.

`check_chunked` verifies:
- per-chunk decode, and that the union equals the netlist,
- ownership and capacity,
- that every exchange entry is written exactly once by the right producer and imported by the
  consumer expecting that node,
- that each producer's fold sequence keeps a slot `HAZARD_DISTANCE` apart.

At the real capacity (1 M slots/chunk) all 44 designs pass.
Raw table: `.claude/2_ARTIFACTS/beat_packer/run_chunked_cap1M.txt`.

| design | movable | K | ghosts | ghost % |
|---|---|---|---|---|
| bigblue3 (ISPD / MMS) | 1.10 M | 2 | 73 K / 86 K | 6.7 / 7.9 |
| bigblue4 (ISPD / MMS) | 2.17 M | 3 | 407 K / 420 K | 18.8 / 19.4 |
| superblue12 | 1.29 M | 2 | 55 K | 4.3 |
| newblue5 / 6 | 1.23 / 1.25 M | 2 | 124 K / 123 K | 10.1 / 9.9 |
| newblue7 | 2.48 M | 3 | 325 K | 13.1 |

The other 36 designs are one chunk. Ghost cost grows with K: adaptec1 forced to 3 / 6 chunks
gives 19% / 45%. A real partitioner (min-cut, FM refinement) is the lever if that matters; the
breadth-first cut was simply the first thing that verified.

## Verification detail
- **Goldens** come from the parsed netlist directly, not from any encoding:
  - v2 / v3: each net's float span, bit-exact. Macro-pin and ghost slots arrive as NaN and the
    exchange buffer starts as NaN, so a missed refresh, export or import can't pass.
  - gradient: `test/wa_gradient_golden.hpp`, sw_only's WA partial in double, with
    `hpwl_dhar_test`'s tolerances ([1] 1e-5 / 1e-4 vs the same-table golden, [2] 1e-4 / 1e-3 vs
    true exp). Observed [1] rel_rms is about 4e-7, 25× inside the bound.
- **Fixture** (`test/record_design.hpp`) exercises everything: cells on a small offset library,
  6 movable macros with ~1,260 pin slots, fixed nodes whose pins are shared across nets, repeated
  nodes on ~390 nets, every degree 2..16, and 17..40-pin nets that must be ignored. Coverage is
  asserted, not assumed.
- **Mutation survivors** are all output-equivalent:
  - An all-EMPTY net's span is 0 anyway.
  - Writes to fixed rows are never drained.
  - An extra macro fold in the compute pass is idempotent, because the fold assigns.

## C-synthesis (tier 2) — what it took
All four modules synthesize with **every loop at II=1**, at full capacity (1 M slots, 32 banks),
xcvc1902 at 3.33 ns (commit `2367363`). Beat-loop depths: v2 / v3 21, gradient 84, gradient v2 82.
Getting there needed three fixes, all found by synthesis and none visible to tier 1:

1. **The gather hung the HLS C front end** (10+ min, not finishing). Bisected over six variants
   (no gather / no refresh / no offsets / no trees / OR-row / masked readout). Only removing the
   gather, or replacing the lane readout `bank_pos[slot % BANKS]` with a masked loop over the
   banks, fixed it. The masked form compiles in about 6 s and schedules at II=1. `static` on the
   big arrays was suspected first and was not the cause.
2. **`MacroPinRef` was 12 bytes**, so entries straddled bus words and refresh ran at II=2.
   Padding it to 16 bytes gave II=1.
3. **The macro fold's register accumulator** carried a float add from entry to entry: II=3 with a
   4.9 ns path. The sum now lives in `pos_URAM` (dead after the beat loop) as a read-add-write,
   and the host orders the macro-pin list so a macro recurs only every `HAZARD_DISTANCE` entries
   (`schedule_macro_pins`: largest-first with cooldown, SKIP padding, FIRST / LAST flags). That
   gives II=1, the checker verifies the order, and three fold mutants are caught.

Also fixed: an explicit `s_axilite port=return bundle=control` split the control bundle (a hard
error once the front end got through).

**The hazard contract, confirmed in RTL co-simulation (2026-09-23).** The scatter-add, the
ghost-gradient add and the macro fold each declare a TRUE RAW dependence of distance
`HAZARD_DISTANCE`=4 on their URAM, and HLS scheduled all three at II=1. Tier-1 C simulation can't
check this, because it runs sequentially. So `bring_up/hpwl_gradient_computer/cosim/` runs the
synthesized RTL (xsim, 8 K-slot build, same pipeline) on a synthetic design packed at different
spacings, each in a fresh session:

| packer spacing | node updates closer than 4 | C simulation | RTL co-simulation |
|---|---|---|---|
| 4 (the contract) | 0 | pass | **pass**, rel_rms 5.5e-7 |
| 3 | 85 | pass | **FAIL**, rel_rms 48 |
| 2 | 124 | pass | **FAIL**, rel_rms 0.25 |
| 1 | 120 | pass | **FAIL**, rel_rms 48 |

So **4 is both sufficient and the minimum**, and **C simulation passes schedules the hardware
cannot execute**. That makes this co-simulation the only gate for any change to the RMW pipeline,
`HAZARD_DISTANCE` or the packer's scheduler. Hazards 3 and 1 fail identically because the error is
dominated by the macro fold: with 3 macros both schedules come out strict round-robin at spacing 3.
Run: `LIBRARY_PATH=/usr/lib/x86_64-linux-gnu vitis_hls -f cosim.tcl` (about 15 min), then
`COSIM_HAZARDS="…" vitis_hls -f cosim_rerun.tcl`. The chunked module's ghost-gradient add uses the
same mechanism but is not co-simulated.

| module | DSP | LUT | FF | BRAM | beat-loop depth |
|---|---|---|---|---|---|
| hpwl_computer_v2 | 24 (1%) | 64 K (7%) | 37 K (2%) | 32 (1%) | 21 |
| hpwl_gradient_computer | 675 (34%) | 198 K (21%) | 246 K (13%) | 160 (8%) | 84 |
| hpwl_computer_v3 | 24 (1%) | 103 K (11%) | 63 K (3%) | 32 (1%) | 21 |
| hpwl_gradient_computer_v2 | 675 (34%) | 255 K (28%) | 286 K (15%) | 160 (8%) | 82 |

⚠️ **Two numbers not to trust yet:**
- **URAM** reads 32 / 64, one per bank. Physically 1 M floats is 4 MB per array, which needs at
  least 116 URAMs at perfect packing (4K×72 each); the estimate counts cascaded instances. Only
  implementation gives the real figure, and the 2-floats-per-word packing from Part 2 is not done yet.
- **Timing:** HLS pre-route estimates exceed the effective budget (3.33 ns minus 0.9 ns
  uncertainty = 2.43 ns). Gradient beat loops are at 3.04 / 3.48 ns, folds at 2.97 / 3.89 ns;
  v2 / v3 have no warning. The 3.9 ns worst case is about 257 MHz, short of the 300 MHz target.
  This is pre-route, so place-and-route decides; the 2026-09-10 Dhar v1 hardware run failed
  timing on exactly this kind of dense float block.

## Open / next
- **Place-and-route** gives the real verdict on timing and URAM (the two warnings above). ⚠️ It
  **can't run on this box**: `export_design -flow impl` stops inside Vivado with no synthesis
  license for xcvc1902, while vitis_hls still exits 0. It needs the build server;
  `bring_up/hpwl_gradient_computer/impl_check.tcl` is ready to run there. The URAM word packing
  (2 floats per 72-bit word) is still to do.
- **Nets of 17..100 pins** are still out of scope (#40 plan). They carry 20–29% of ISPD2005 pins,
  so this is the largest remaining functional gap, and the plan now has to be designed on the
  record protocol.
- `resolve_beat` in v1 derives the net count from counts; v2 onward derives it from EMPTY lanes,
  so v1's contract is superseded rather than updated.
- **Partitioner:** replace the breadth-first cut with a min-cut partition if ghost cost matters on
  bigblue4 / newblue7.
- **URAM budget** against density's bin scatter is still deferred (Mark, 2026-09-22).

## Housekeeping
- `bring_up/hpwl_computer/` and `hpwl_gradient_dhar_v2/`, plus their two harnesses (#40, "landed
  and verified"), had never been committed, while the committed test Makefile already listed
  them and v2 reuses v1's trees. They are committed as-is in `0752b6f`, so a clean checkout builds.
