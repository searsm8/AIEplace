# PLAN #48: `bin_density_CU` with 8 lanes (row ownership)

*2026-10-20. Branch `pl_algo`. Builds on [[REPORT_48_bin_density_fixed_point_20261019.md]] (one lane: 16
banks, forwarding, II = 1, bit-exact 44/44). Mark: begin the 8-lane row-band design.*

## Why

One cell per cycle is one cycle per slot: bigblue4 streams ~5.8 M slots, ~21 ms per evaluation at
300 MHz. XPlace's whole scatter on bigblue4 is ~4 ms on a 3090
([[REPORT_48_xplace_density_scatter_gather_20261020.md]]). We need several cells per cycle.

## The idea

Give each lane its own rows of the grid. A lane owns 16 banks covering only its rows, deposits only into
them, and runs the one-lane pipeline we already have (banks + forwarding). A slot goes to every lane whose
rows its footprint touches; a fast footprint spans ≤ 4 rows, so that is one lane or two.

**URAM is free at 8 lanes.** At 1024, a lane owns 128 rows; its 16 banks are (1024 / 4) × (128 / 4) = 8K
words each, exactly one URAM in 8K × 36 mode: 128 URAMs total, as today. 16 lanes would make each bank
4K words, half a URAM, so the count doubles.

## Which rows a lane owns

| map | lane of row r | spreads a cluster | footprints in two lanes |
|---|---|---|---|
| bands | r / 128 (contiguous) | no: a pile in one band runs on one lane | rarely (only at 7 band edges) |
| strips of N rows | (r / N) mod 8 | yes, for clusters taller than 8N rows | often for small N: a ~2-bin-tall footprint straddles a 4-row strip ~35% of the time |

Within a lane, the 4 × 4 bank rule is unchanged (bank = 4·(col mod 4) + row mod 4), so any N that is a
multiple of 4 keeps the one-cycle window.

## The dispatcher

Slots arrive 16 per DDR beat. A **beat-synchronous** dispatcher waits until every lane has taken its share
of a beat; a **FIFO** dispatcher writes each lane's share into that lane's FIFO (at most W entries per
FIFO per cycle) and moves on, so a busy lane does not stall the others until its FIFO fills.

⚠️ **Routing needs each slot's row range first.** Today `row_lo = (int)(yl / bin_h)` is computed inside
the deposit pipeline, with a float divide. The dispatcher needs it for up to 16 slots per cycle, before
the lanes. Options:
- the dispatcher computes the exact row range (float divides at dispatch rate; lanes reuse it);
- route on `y * inv_bin_h` (a multiply) with a one-row margin, and let each lane clip to its rows
  exactly; a slot sent to a lane it does not touch costs that lane one empty cycle;
- the host sends a per-slot row hint... no: positions change every iteration on the device.
The second keeps the arithmetic bit-exact (lanes still compute the golden's window) and costs only
dispatch slack; measure the extra sends in the model before choosing.

## Cycle model (`host/sw_flat/model/lane_model.cpp`, `make -C host/sw_flat lane-model`)

On sw_flat's own positions at every iteration of a run (`pl+Bprime+fixed` arithmetic): per beat, each
lane's share; cycles for beat-synchronous and FIFO dispatch (depth 32, 4 writes per cycle) and the
perfect-buffering bound, against today's one slot per cycle. Whole-run speedup is the number that matters.

Whole run, speedup over one slot per cycle (`.claude/2_ARTIFACTS/scratch_48/lane_model_*.txt`; remedy
columns sampled every 4th iteration):

| design (grid) | bands FIFO | strip4 FIFO | strip4 deep | strip4 read4 | **strip4 both** | strip8 both | bound strip4 / strip8 |
|---|---|---|---|---|---|---|---|
| mgc_fft_a (256) | 5.06 | 4.75 | 4.90 | 4.95 | **5.03** | 4.50 | 5.19 / 4.62 |
| adaptec1 (512) | 1.94 | 3.53 | 3.81 | 4.18 | **4.37** | 4.31 | 5.35 / 5.52 |
| mms/newblue4 (1024) | 1.48 | 2.84 | 3.06 | 3.65 | **3.82** | 3.72 | 4.68 / 4.81 |
| mgc_superblue12 (512) | 1.75 | 2.81 | 2.96 | 3.54 | **3.63** | 3.61 | 4.52 / 4.77 |
| bigblue4 (1024) | 2.61 | 4.38 | 4.61 | 5.00 | **5.17** | 5.29 | 6.02 / 6.62 |

(deep = FIFO depth 128; read4 = the dispatcher reads the stream at 4 places, one beat from each in turn;
both = the two together. Base FIFO: depth 32, 4 writes per cycle.)

What limits it, in order:
1. **Spatial order in the stream.** Neighbouring slots are neighbours on the die, so consecutive beats load
   the same lanes. Reading at 4 places fixes most of it (+15-30%); a deeper FIFO helps less (+5-8%).
2. **Straddlers.** With 4-row strips ~35% of fast slots touch two lanes and cost both a cycle, so the bound
   is at most 8 / 1.35 ≈ 5.9. 8-row strips halve that (~18%) but spread clusters less: a wash after read4.
3. **The early pile-up.** For the first ~10 iterations the cells sit in a few rows and even the bound is
   ~2.5x (bigblue4); nothing in the dispatcher fixes that.

bigblue4 at 5.2x: ~5.5 M slots / 5.2 ≈ 1.06 M cycles ≈ 3.5 ms per deposit at 300 MHz, against XPlace's
~4 ms scatter (3090). The load, reduce and drain add ~0.6 M cycles until the DDR format moves to 4 x 4 tiles.

## Round 2 (2026-10-20): centre-weighted maps and two cells per cycle

Mark asked for (a) maps that put more lanes on the busy middle and (b) a model of a lane depositing two
cells in one cycle when their banks do not overlap ("pairing": the lane looks one item ahead in its queue).
Whole run, read4 + FIFO 128 + pairing, speedup over one slot per cycle; iterations 0-15 alone in brackets
(`.claude/2_ARTIFACTS/scratch_48/lane_model_*.txt`, every 4th iteration simulated):

| map | URAMs | mgc_fft_a | adaptec1 | mms/newblue4 | superblue12 | bigblue4 |
|---|---|---|---|---|---|---|
| bands | 128 | 6.15 | 3.43 | 2.39 | 2.74 | 4.59 |
| **strip4** | 128 | **7.02** (4.3) | **5.72** (2.2) | **4.79** (1.6) | **4.58** (1.6) | **7.16** (3.2) |
| strip8 | 128 | 5.67 | 5.23 | 4.39 | 4.27 | 6.66 |
| graded (thin middle) | 192 | 5.38 | 3.47 | 2.54 | 2.66 | 4.16 |
| mid-strip | 128 | 6.57 | 4.13 | 3.35 | 3.40 | 5.40 |

- **Pairing is the biggest single gain**: strip4 on bigblue4 5.17 → 7.16, past the one-item-per-cycle
  bound (6.0). Straddler halves use one or two bank rows, so they pair easily: the strip4 straddle cost
  turns into an advantage.
- **The centre-weighted maps did not help.** Graded bands cost 192 URAMs and land with plain bands; the
  striped middle sits between bands and strips. Their problem is the same as bands': any contiguous block
  of rows sees long runs of the stream.
- **The first 16 iterations stay at 1.5-3x on every map.** The pile is a few rows tall, smaller than any
  lane's share however it is cut. It is ~2% of the run's iterations, but costs more cycles per iteration.
- bigblue4 at 7.2x: ~0.77 M cycles ≈ 2.6 ms per deposit at 300 MHz.

## Round 3 (2026-10-20): a FIFO in front of every bank (Mark)

No row ownership at all. P producers each compute one cell's footprint per cycle and push one update per
covered bin into that bin's bank FIFO; every bank pops one update per cycle and does its read-add-write
(with forwarding, as today). A FIFO accepts at most W pushes per cycle; the first cell that does not fit
stalls the producers. The 4 × 4 (or 8 × 8) interleave spreads any cluster wider than the interleave evenly
over the banks, so balance no longer depends on the stream order. 64 banks (8 × 8 interleave) are 16K
words each, 2 URAMs: still 128. A cell averages ~5.8 updates, so 64 banks could take ~11 cells per cycle.

Whole run, speedup over one slot per cycle (iterations 0-15 alone in brackets;
`.claude/2_ARTIFACTS/scratch_48/bank_model_*.txt`, `make -C host/sw_flat bank-fifo-model`), FIFO depth 32,
stream read at 4 places:

| design | lanes best (strip4, read4, pair) | 64 banks P=8 W=2 | **P=8 W=4** | **P=16 W=4** |
|---|---|---|---|---|
| mgc_fft_a | 7.02 (4.3) | 6.21 | **8.26** | **9.72** (4.9) |
| adaptec1 | 5.72 (2.2) | 5.37 | **6.49** | **7.20** (2.4) |
| mms/newblue4 | 4.79 (1.6) | 4.79 | **5.53** | **5.94** (1.8) |
| mgc_superblue12 | 4.58 (1.6) | 4.56 | **5.18** | **5.49** (1.7) |
| bigblue4 | 7.16 (3.2) | 6.25 | **7.99** | **9.33** (4.2) |

- **W, the pushes a FIFO takes per cycle, is the key knob.** W = 1 serializes (two cells in one cycle
  nearly always share a bank): 2.0-2.6x however many producers. W = 2 matches the lanes; W = 4 beats them.
- **16 banks are too few** (2.2-2.7x at best: 16 banks / 5.8 updates = 2.75 cells per cycle ceiling).
- bigblue4 at 9.3x: ~0.59 M cycles ≈ 2.0 ms per deposit at 300 MHz.
- **Cost moves to the producers and the fill network:** P copies of the footprint arithmetic, and a
  network steering up to 16 updates per producer into 64 FIFOs that each take W per cycle (e.g. W
  sub-FIFOs per bank, drained round-robin). Not yet estimated: synthesize one producer before choosing P.

## What one producer costs (2026-10-22)

`pl/synth/bin_density_producer/producer_top.cpp`: one producer alone (streams in and out, so no m_axi in
the price): per cycle one slot -> window -> 4 column + 4 row overlaps -> 16 (bank, address, increment)
updates for the 64-bank grid. `producer_check.cpp` holds it to bin_density_CU's own deposit: 0 of 262,144
bins differ over 200 K cells (a third placed on bin edges), with the bin index by divide (bit-exact, the
check's verdict) and by multiply (also 0 differing bins, at die 10000 and 10007.3: the two only disagree
when a quotient lands within a rounding step of an integer, and then only on a sliver of overlap).

C-synthesis at 3.33 ns, II = 1 both ways:

| bin index | DSP | LUT | FF | depth |
|---|---|---|---|---|
| multiply | 95 (4.8%) | 17.1 K (1.9%) | 11.3 K | 30 |
| divide | 91 (4.6%) | 17.2 K (1.9%) | 11.3 K | 43 |

- **The dividers were not the cost.** Four float divides price like four multiplies here; they only add
  13 pipeline stages. The DSPs are the 16 increments, `(ow · oh) · weight`, ~32 float multiplies.
- **Budget** (VC1902: 1,968 DSP, 900 K LUT; `hpwl_gradient_CU` alone takes 708 DSP / 279 K LUT):
  P = 8 is ~760 DSP (39%) and ~137 K LUT (15%): with the gradient, 1,468 DSP (75%). **P = 16 is ~1,520 DSP
  (77%) alone: does not fit beside the gradient.**
- **To make P = 16 fit:** compute `ow · weight` once per column (4 multiplies) and then `· oh` (16): 20
  multiplies instead of 32, ~40% fewer DSPs. That changes the rounding order, so it is a golden change too
  (sw_flat first, drift measured), like the multiply bin index.
- Not yet priced: the fill network (P × 16 updates steered into 64 FIFOs, W = 4 pushes each), and the 64
  banks' read-add-write with forwarding (bin_density_CU today: 16 banks, 114 DSP, 101 K LUT in all).

**Choice: P = 8** (5.2-8.3x over one cell per cycle on the five designs; bigblue4 8.0x, ~2.3 ms), with
P = 16 (bigblue4 9.3x) only after the product reorder.

## Recommendation
**Superseded by round 3** (a FIFO per bank, 64 banks, W = 4, P = 8). The row-lane recommendation below
stands as the record:
**Strips of 4 rows, a FIFO of 128 per lane, a dispatcher reading at 4 places, and pairing in each lane.**
No divide at dispatch: change the golden's row index to a multiply by `1/bin_h` (sw_flat first, drift
measured), then the dispatcher and lanes copy it exactly. Explainer: [[EXPLAINER_48_lane_dispatcher_20261020.md]].

## Open questions for Mark
- Map: strips of 4 or 8 rows (equal after read4; 4 keeps clusters spread, 8 halves straddlers).
- Routing: exact rows at dispatch, or the multiply with a margin.
- Fillers are ~half the slots and their positions are spread: worth a separate filler path?
