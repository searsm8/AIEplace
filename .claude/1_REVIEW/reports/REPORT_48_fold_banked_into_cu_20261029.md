# REPORT #48: `bin_density_CU` is now the 8-producer, 64-bank module, bit-exact on 44/44

*2026-10-29. Branch `pl_algo`. Mark: "Skip the model; fold the banked design into bin_density_CU now."
This was the handoff of the same name; its brief is kept below the result. Evidence for the banked design:
[[REPORT_48_bank_address_multiplies_20261028.md]] (656 DSP), [[REPORT_48_drain_timing_20261028.md]] (Vivado synth
2.928 ns at 3.33), [[REPORT_48_banked_cosim_20261029.md]] (RTL bit-exact, forwarding distance 4, depth 6).
Overview: [[EXPLAINER_48_bin_density_CU_overview_20261022.md]].*

## Result

`pl/src/modules/bin_density_CU.hpp` keeps its interface, its phases and its golden. Its fast deposit is now
`banked_top`'s: 8 producers, 8 × 64 private FIFOs and 64 URAM banks with forwarding. Its other phases moved
from 16 to 64 banks.

| check | result |
|---|---|
| `make -C pl test` (tier 1, plain g++, no Vitis) | **PASS**, all 4 harnesses; `bin_density_CU_test` at 128 and 1024, shuffled order bit-identical |
| `make -C host/sw_flat check-bin-density` | **44 of 44 PASS**: rho, excess and overflow bit-equal, shuffled slot order too. `.claude/2_ARTIFACTS/scratch_48/bin_density_check_44_banked_cu_20261029.txt` |
| C-synthesis (`pl/synth/bin_density_CU/synth_check.tcl`, grid 1024, 3.33 ns) | met, **2.571 ns** estimated; II = 1 everywhere it matters (table below) |
| RTL co-sim of the folded CU (`cosim/run_depths.sh`, grid 64 and 48) | **depths 1-3 FAIL, 4-6 PASS**: minimum forwarding depth **4**, as in `banked_top`; `BIN_FORWARD_DEPTH` stays 6 |

### C-synthesis (Vitis HLS 2022.2, xcvc1902, 3.33 ns)

| | DSP | FF | LUT | URAM |
|---|---|---|---|---|
| **`bin_density_CU_top` total** | **689 (35 %)** | 258 K (14 %) | **517 K (57 %)** | **128 (27 %)** |
| `deposit_region` (deal + 8 produce + drain_banks + 512 FIFOs) | 657 | 212 K | 433 K | (the banks) |
| ↳ `produce` × 8 | 82 each | 19.1 K each | 42.8 K each | |
| ↳ `drain_banks` | 0 | 38.1 K | 58.5 K | |
| ↳ `deal_slots` | 1 | 12.6 K | 4.5 K | |
| ↳ FIFOs | | 8.5 K | 27.3 K | |
| `deposit_slow` (64-way dynamic bank) | 8 | 9.8 K | 16.1 K | |
| `overflow_excess_sum` (64 → 1 tree) | 4 | 3.7 K | 8.7 K | |
| `drain` to rho beats (16 int→float, ×rho_unit) | 16 | 2.8 K | 2.5 K | |
| `load_base` | 0 | 0.7 K | 0.3 K | |
| *for comparison:* `banked_top` (deposit only) | 656 | 263 K | 454 K | 128 |
| *for comparison:* previous `bin_density_CU` (16 banks, 1 cell/cycle) | 102 | 79 K | 100 K | (37 in csynth) |

| loop | II | depth | cycles at grid 1024 |
|---|---|---|---|
| `load_base` | 1 | 5 | 65.5 K (one beat per cycle; was 262 K at 4 bins/cycle) |
| `deal_beats` | 1 | 4 | one beat per cycle, so 8 slots per cycle feed the producers |
| `produce_cells` × 8 | 1 | 33 | one cell per cycle each |
| `drain_banks`' `drain` | 1 | 6 | (same depth-6 schedule as `banked_top`) |
| `reduce` | 1 | 10 | 16.4 K (64 bins/cycle; was 65.5 K) |
| `drain` to rho beats | 1 | 14 | 65.5 K (one beat per cycle; was 262 K) |
| `deal_table` | **2** | 77 | ≤ 256 per region: a carried m_axi read dependence; not worth a fix |
| `slow_rows` | **7** | 21 | the slow path, one bin per 7 cycles: the macro-tile item replaces it |

So one call at grid 1024 now takes about 65.5 K (base) + cell beats × 2 + 16.4 K + filler beats × 2 + slow + 65.5 K
cycles, against 262 K + cell slots + 65.5 K + filler slots + slow + 262 K before. LUT is the resource to watch:
57 % for this CU alone, beside `hpwl_gradient_CU`. A per-producer LUT diet (the 16:1 fill select × 64 banks)
is the lever if place-and-route needs it; out of scope here.

## Decisions

1. **Feeding the producers (as proposed).** `deal_slots` reads one beat of x, y and footprint index per cycle
   (three m_axi ports, as before) and gives lanes 2p and 2p+1 to producer p as one `SlotPair` FIFO word (depth 4).
   A producer reads its FIFO every other cycle, so one reader keeps all 8 busy. NO_DEPOSIT slots (slow cells,
   padding, borrowed copies, pins) go to the producer and deposit nothing, as before. **The dealing is by lane**,
   so how many real cells each producer gets depends on the slot order. Accepted, to be measured on the device
   rather than modelled.
2. **Footprint table: one BRAM per producer, fed by stream.** Each `deposit_region` call begins with `deal_slots`
   reading the table from DDR, scaling the weight (`fp.weight *= fixed_scale`, the same single float multiply)
   and writing every entry to 8 table FIFOs. Each producer loads its own 128-entry `table_BRAM` from its FIFO.
   That costs ≤ 2 × 128 cycles per call. **There is no shared table and no array read by two dataflow
   processes.** `FOOTPRINT_TABLE_MAX` drops 4096 → **128** in the contract (measured 4..105 on the 44
   designs; `check-bin-density` [2] asserts the fit, max 128 in its log). `deposit_slow` reads its entry
   straight from DDR and scales it the same way, so the CU-level `footprint_BRAM` is gone.
3. **Phases around the region, at 64 banks.** The URAM is `rho_URAM[64][16K]`, bank `8·(col%8) + row%8`, address
   `(col/8)·128 + row/8` (fixed `GRID_MAX/8` pitch, a shift). It is bound **`ram_t2p`**: URAM is natively true dual
   port, so `load_base` and `drain` move two words per bank per cycle, and `drain_banks` reads on one port and
   writes on the other. Order: `load_base` → region (cell beats) → `deposit_slow` (cells) →
   `overflow_excess_sum` → region (filler beats) → `deposit_slow` (fillers) → `drain`. **One owner per phase.**
   Inside the region only `drain_banks` touches the URAM. The region returns once every bank has taken every
   producer's `last`, and that return is the B' barrier. `overflow_excess_sum` reads one address in all 64
   banks: an 8×8 tile per cycle.
4. **The drain to rho beats, at full rate.** A col-major beat is rows 16k..16k+15 of one column. Under 8×8 those are
   the 8 banks of bank column `col % 8`, at addresses 2k (lanes 0..7) and 2k+1 (lanes 8..15): **two words in each
   of 8 banks**, which `ram_t2p` reads in one cycle. To keep the bank indices constant (else HLS sees 16 dynamic
   reads per bank), all 64 banks are read at both addresses and the bank column is selected after. `load_base` is
   the mirror image: writes enabled only in the selected bank column. Both run at **one beat per cycle**, 4× the
   old 16-bank loops.
5. **`hls::stream` in host g++ builds: a shim, not the include path.** `bin_density_CU.hpp` includes
   `<hls_stream.h>` when `__SYNTHESIS__` is defined or the header is on the path (`__has_include`). That covers
   HLS, co-sim and `banked_check`. Otherwise it defines a minimal `hls::stream` over a `std::deque` (write, read,
   empty; a read of an empty stream aborts). Off-synthesis the region's processes run one after another, so an
   unbounded queue is exactly a stream. **Tier 1 and `check-bin-density` still need no Vitis**, and no Makefile
   changed.

## Co-sim

`pl/synth/bin_density_CU/cosim/` (Vitis HLS 2022.2, `PL_DENSITY_GRID_MAX=64`, two calls: grid 64 and grid 48).
The bench is new. It lays out `banked_top`'s per-producer cell lists so that producer p's j-th cell sits at beat j/2,
lane 2p + j%2. Bank words are therefore re-hit **across producers** at distances 1..8, and the slices finish at very
different times (700 / 440 / 550 / 401 / 620 / 490 / 0 / 3 cells). One macro takes the slow path. The filler
region repeats the distance-1 pile-up on every producer, and a fixed base sits under all of it. The golden is a
plain uint64 deposit in slot order, with the excess taken at the barrier, compared bit for bit.

| `BIN_FORWARD_DEPTH` | 0 | 1 | 2 | 3 | **4** | 5 | 6 (code) |
|---|---|---|---|---|---|---|---|
| C simulation | FAIL (77 / 143 bins) | PASS | PASS | PASS | PASS | PASS | PASS |
| RTL, grid 64 / 48: bins that differ | not run | **FAIL** 159 / 176 | **FAIL** 129 / 133 | **FAIL** 105 / 105 | **PASS** 0 / 0 | **PASS** 0 / 0 | **PASS** 0 / 0 |

- **Distance 4, the same as `banked_top`.** `drain_banks` has the same depth-6 schedule in the CU. Folding it in,
  and binding `ram_t2p`, did not move the read-to-write distance. The excess fails along with rho at depths 1-3,
  so the barrier reduce sees the hazards too.
- **Termination, the two regions and the barrier are clean.** Both transactions pass: 4,616 and 5,034 cycles
  (Verilog), with no deadlock report. The second region starts after the reduce, and the filler pile-up on all
  8 producers is bit-exact.
- One run is ~28 min wall (csynth ~13 + co-sim ~12), three in parallel.

## Vivado synthesis (2026-10-30): timing NOT met, one path

`pl/synth/bin_density_CU/impl_check.tcl` on the build server (Vitis 2022.2, out of context, 3.33 ns, default
strategy), run from a scratch copy `~/impl48_cu`. Started 02:33, done 04:04: 91 min. Reports:
`.claude/2_ARTIFACTS/fold_48_vivado/run1/`.

| Vivado synth, 3.33 ns | folded `bin_density_CU_top` | *`banked_top` final (10-29)* |
|---|---|---|
| post-synth period | **4.062 ns, NOT met** | 2.928 ns, met |
| WNS / TNS / failing endpoints | **−0.732 ns / −277.5 ns / 504** of 500,158 | +0.402 ns / 0 / 0 |
| LUT / FF / DSP / URAM / BRAM | **294,106 / 194,409 / 687 / 128 / 86** | 287,689 / 182,695 / 656 / 128 |
| ↳ `deposit_region` LUT / FF / DSP | 260,249 / 160,587 / 657 | |
| ↳ `deposit_slow` / `drain` / `overflow_excess_sum` / `load_base` LUT | 7,151 / 6,409 / 3,360 / 665 | |

Vivado's LUT count is **57 % of csynth's 517 K**; the CU adds only ~6.4 K LUT to the banked deposit.

**All 504 failing endpoints are one path family** (`fail_buckets.tcl` on the synth checkpoint): source
`drain_banks`' drain-loop `done_reg`, destination the `EN_A` pins of the `rho_URAM` cascades (496 at −0.732,
8 at −0.668, bank 0). The path is 8 LUT levels, 80 % routing: `done_reg` (fanout 588) → bank-FIFO empty / the
8-way priority OR (`or_ln304`, bin_density_CU.hpp:304) → the drain pipeline's enable (**fanout 2,771**) → the
**CU-level port mux** that shares each URAM between `drain_banks`, `load_base`, `drain`, `overflow_excess_sum`
and `deposit_slow` → a 769-fanout net (1.1 ns) to every URAM enable. Each bank is 16 K words = two cascaded URAMs, so
the address top bit becomes `EN_A`. In `banked_top` the URAMs had one owner and this chain met with slack to spare.
Folding added the shared port mux and its fanout, and that is enough to fail.

**`style=frp` cannot work on this loop (tried 2026-10-30, pragma removed).** HLS drops it: warning 200-1964, "Disabling
free running pipeline (frp) architecture on pipeline 'drain' ... because it has non-blocking accesses on an input
port", once per `empty()`. The generated RTL has no FRP logic and `rho_URAM_*_ce*` is still gated by
`ap_block_pp0_stage0_11001`. So the Vivado run was stopped: it was the same design as run 1. RTL co-sim of that
build matched the earlier sweep (depth 3 FAIL, 4 and 6 PASS), which proves nothing about FRP. The `banked_top` FRP run
in [[REPORT_48_drain_timing_20261028.md]] may have been dropped the same way; not checked.

**The stall is one that never fires.** `ap_block_pp0_stage0_11001` is an OR over `(take predicate) & (empty_n == 0)`
for every FIFO, from the blocking `read()` at bin_density_CU.hpp:303. `take` already includes `!empty()`, so the
term is never true, but HLS cannot prove that and builds the logic.

Possible fixes, not tried:
- **`read_nb` in place of `empty()` + `read()`.** A non-blocking read has no stall term, so the enable should stop
  depending on the FIFOs. Changes the C (and the `hls::stream` shim needs `read_nb`); the schedule may move, so
  sweep the forwarding depth again.
- **Register the priority pick.** Feed the URAM the pick from one cycle earlier, which cuts the path at the OR tree.
  This changes the C schedule, so the forwarding depth must be swept again in co-sim.

Either fix then needs another Vivado run (~90 min).

## Not done / follow-ups

- **Vivado timing** of the folded CU fails at 3.33 ns (section above). The next step is to close it.
- `deposit_slow` at II = 7 and its 64-way dynamic bank select (16 K LUT): superseded by the macro tiles,
  [[HANDOFF_48_macro_tiles_20261022.md]].
- `host/sw_flat/model/{lane_model,bank_fifo_model}.cpp` call `footprint_window(..., bin_w, bin_h)` where the
  signature takes `inv_bin_w, inv_bin_h` (since the multiply change, 2026-10-27). Pre-existing; they still compile,
  but their windows are wrong. Not touched here.

---

# The brief, as handed off

## Two designs today

- **`bin_density_CU`** (`pl/src/modules/bin_density_CU.hpp`) is the real module: complete, verified, slow.
  One cell per cycle (`deposit_fast`), 16 banks (4×4), no FIFOs. Phases: `load_base` (fixed base into the
  banks), cells, `overflow_excess_sum` (the B' barrier), fillers, `deposit_slow` (wide footprints, 1 bin per
  cycle), drain to `rho` beats. Checked bit-exact by `check-bin-density` 44/44, `bin_density_CU_test`, and
  RTL co-sim (`pl/synth/bin_density_CU/cosim/`).
- **`banked_top`** (`pl/synth/bin_density_banked/banked_top.cpp`) is the fast deposit, a costing harness.
  8 producers (`pl/synth/bin_density_producer/producer.hpp`), 8 × 64 private FIFOs, 64 banks (8×8), the
  drain with forwarding. It has none of the phases above: no base, no overflow barrier, no fillers phase, no
  slow path. It takes 8 pre-split `SlotIn` slices on 8 m_axi ports, clears the URAM and writes raw words.

**The fold:** `bin_density_CU` keeps its interface, phases and golden, and its fast deposit becomes the
banked one. After it, `bin_density_CU` is the 8-producer, 64-bank module, and `check-bin-density` is the
proof.

## What has to be designed (decide, then write the decision in the report)

1. **Feeding the producers from the slot beats.** The CU reads `pos_x`/`pos_y`/`footprint_idx` as 16-slot
   beats. Proposal: one reader process takes a beat (3 bursts) and deals its 16 slots to the 8 producers,
   2 each (lanes 2p, 2p+1), into small per-producer input FIFOs. 16 slots per beat against 8 per cycle
   consumed, so one beat every 2 cycles is enough. NO_DEPOSIT slots (slow cells, empty) are dropped by the
   producer as today. Producer imbalance from the dealing pattern is accepted; measure it later, not now.
2. **Footprint table:** one BRAM copy per producer (≤ 128 entries, loaded once, scaled by `fixed_scale`).
3. **Phases around the dataflow region:** `load_base` → deposit region (cell beats) → `overflow_excess_sum`
   → deposit region again (filler beats) → `deposit_slow` → drain. The region ends when every bank has seen
   every producer's `last` (as `banked_top`), which is exactly the barrier B' needs. All of these move from
   16 to 64 banks: bank `8·(col%8) + row%8`, address at the fixed pitch `GRID_MAX/8`.
   `deposit_slow` stays one bin per cycle (macro tiles are the next item). Mind the URAM: the banks are
   written by the drain inside the dataflow region and by the phases outside it. Keep one owner per phase.
4. **The drain to `rho` beats** (16 bins per beat, column-major as today): which banks a beat touches under
   8×8, and keep it at full rate.
5. **`hls::stream` in host-compiled code.** `check-bin-density` and the tier-1 harness compile the CU with
   plain g++. `hls_stream.h` is header-only (`-I$XILINX_HLS/include`, as `banked_check.cpp` uses).
   Either add that include path to both builds, or keep a tiny shim. Tier 1 must still run without Vitis
   sourced, or the report must say clearly that it now needs the header path.

## Steps

1. Restructure `bin_density_CU.hpp` (reuse `producer.hpp`'s `produce_cell`, `banked_top`'s drain).
   → `make -C pl test` PASS (`bin_density_CU_test`, grids 128 and 1024).
2. → `make -C host/sw_flat check-bin-density` **44/44 bit-equal** rho and excess, shuffled order included.
3. C-synthesis (`pl/synth/bin_density_CU/`): II=1 in producers and drain, timing estimate met, DSP ≈ 656 +
   the CU's base/reduce/slow/drain; URAM 128. Record the numbers.
4. RTL co-sim of the folded CU: adapt `pl/synth/bin_density_CU/cosim/` (its bench must now collide across
   producers; reuse `pl/synth/bin_density_banked/cosim/cosim_tb.cpp`'s sections). Depths 1..6: 1-3 FAIL,
   4 or more PASS expected; the code keeps 6.
5. Vivado synthesis on the build server (`impl_check.tcl` pattern). The portal is Mark's to open: ask once,
   don't retry. Optional for done.

Keep `pl/synth/bin_density_banked/` as the costing record (it is cited by three reports). Don't delete it.
Out of scope: macro tiles ([[HANDOFF_48_macro_tiles_20261022.md]]), the `(ow·w)·oh` reorder, the force
gather, place-and-route.

## Done means

`make -C pl test` and `check-bin-density` 44/44 bit-equal; csynth numbers recorded; RTL co-sim of the
folded CU passes at depth 6, with the minimum known. tasks.md #48 and the Active Task updated, a
milestones.md line if it lands, this file `git mv`'d to `REPORT_48_fold_banked_into_cu_20261029.md`.
Commit file by file (never `git add -A`); push `pl_algo`.
