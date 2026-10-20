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

## Recommendation
Strips of 4 rows (or 8: equal after read4), a FIFO of 128 per lane, and a dispatcher that reads the cell
stream and then the filler stream at 4 places each. Next: confirm the routing choice below in the model
(count the extra sends of the multiply-plus-margin route), then build it.

## Open questions for Mark
- Map: strips of 4 or 8 rows (equal after read4; 4 keeps clusters spread, 8 halves straddlers).
- Routing: exact rows at dispatch, or the multiply with a margin.
- Fillers are ~half the slots and their positions are spread: worth a separate filler path?
