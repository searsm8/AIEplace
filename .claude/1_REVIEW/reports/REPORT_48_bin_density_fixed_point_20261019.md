# REPORT #48: `bin_density_CU`'s fixed-point accumulator, in sw_flat and on the CU

*2026-10-19 (sw_flat model) and 2026-10-20 (CU side). Branch `pl_algo`. Was
`HANDOFF_48_bin_density_fixed_point_20261019.md`.*

**Result:** `bin_density_CU` accumulates each bin as an unsigned fixed-point count in one 36-bit URAM
word, with the fraction bits F set per design from a proven bound, so no bin can overflow. It deposits one
cell per cycle (II = 1) through a 4 × 4-banked grid with forwarding, and equals sw_flat's model **bit for
bit on 44/44 designs** (ρ, the overflow excess and the overflow), in the device's slot order and in a
shuffled one. RTL co-simulation shows the forwarding working: without it, or one register short, the
RTL is wrong; with it, bit-exact.

> **Superseded 2026-10-20:** the format below started as a fixed 18.18 (Mark kept F = 18 on 10-20 a.m.),
> then became the per-design total-area bound the same day (Mark, after asking what happens on much larger
> designs). The 18.18 sections are kept as the measurement record.

**Mark's frame (2026-10-19):** `bin_density_CU` (renamed from `density_map_CU`) gets the whole chip for
now, 1024 × 1024 only, budgeted at about half the URAMs; it accumulates in **fixed point**. Its inputs
are the slot-ordered positions (one long stream over the whole design, no chunking), a per-slot
footprint index into a small table, and the host's fixed base map. Its outputs are ρ and the overflow.

## The format: 18.18 unsigned in a 36-bit URAM word

URAM288 runs as 8K × 36 (AM007; [[PLAN_48_density_onchip_20261014.md]]'s 10-16 correction), so one bin
per 36-bit word puts the 1024² accumulator in **128 URAMs (28% of 463)**, within the half-chip budget.
`densrec::URAM_WORD_WIDTH = 36` (`pl/src/format/density_footprint.hpp`).

The bin unit is one bin area. A bin holds an unsigned count of 2^-F bin areas, F = `deposit_frac_bits`.

**The integer part is set by the densest bin, and that bin is at the very start of the run.** The initial
placement is a Gaussian of σ = 0.001 × die around the centre (`setup.cpp`, XPlace's), about one bin at
1024, and it keeps tightening for a few steps before it spreads. Peak ρ over the first 100 iterations,
D6 configs, grid capped at 1024, all 44 designs (`.claude/2_ARTIFACTS/scratch_48/peak_100.txt`,
`peak_12.txt` with design names, probe `peak_density.cpp`):

| | peak ρ (bins) | iteration |
|---|---|---|
| worst, mms/newblue4 | **95,408** | 4 |
| mms/adaptec5, newblue5, newblue2, bigblue4 | 59–67 K | 1–2 |
| ispd2005/bigblue4 | 54,630 | 1 |
| every design at iteration 100 | ≤ 5,896 | |

Every peak is at iteration 0–7. 95,408 needs 17 integer bits (2^17 = 131,072, 1.37×). **F = 18 leaves
18 integer bits: 262,144, 2.7× headroom**, at a resolution of 2^-18 = 3.8e-6 bin per deposit.

⚠️ The peak is the initial pile, not placement. It scales with cells per unit die area at the start, so a
larger design or a 2048 grid (less clamp smoothing) can push it up. sw_flat exits on any bin that would
not fit the word (`deposit_fixed_point`), so a breach is loud in software; the device word wraps silently.

## The math (sw_flat `placer/density.cpp`, `[sw_flat] deposit = "fixed"`, `deposit_frac_bits = F`)

    s       = 2^F / bin_area[0]                       (float; fixed_scale)
    w'      = weight * s                              (float; the host's footprint table entry)
    q(o)    = (uint64)(o + 0.5f)                      (float add, then truncate: nearest integer; the add
                                                       itself rounds, identically on CPU and device)
    base_q  = q(fixed_base[b] * s)                    (once, after the min(rho, td) cap)
    acc[b]  = base_q[b] + Σ q(overlap_w * overlap_h * w')   over the cells, then the fillers
    cap_q   = q(bin_w * bin_h * td * s)
    excess  = Σ_b max(0, acc[b] - cap_q)              (exact integer, before the fillers: #48 B')
    overflow = (float)(excess * 2^-F) * bin_area[0] / (total_movable_area + 1e-8f)
    rho[b]  = (float)acc[b] * 2^-F                    (round to nearest, then an exact power of two)

The overlap geometry (`overlap_w`, `overlap_h`) is the float deposit's, unchanged. Each overlap is
rounded once; the sums are integer, so **ρ and the excess do not depend on the deposit order**: the CU
can run any number of lanes, in any order, and still equal sw_flat bit for bit. That replaces today's
"bit-equal only in slot order" and the 2e-5 overflow tolerance with exact equality. The device returns
the int64 `excess`, as `hpwl_CU` returns its int64 HPWL; the host forms the ratio.

## Precision at fixed positions: q18 vs float, both against a double sum

Same positions, same per-bin overlaps; only the accumulation differs. The reference is a double sum of
the float deposit's own overlap terms (`.claude/2_ARTIFACTS/scratch_48/quant_error.cpp`, `pl+Bprime`
configs, `quant_30.txt` / `quant_300.txt`). rel_rms is over all bins; overflow is #48 B'.

| design (grid, iteration) | peak ρ | float ρ rel_rms | q18 ρ rel_rms | float overflow rel | q18 overflow rel |
|---|---|---|---|---|---|
| bigblue4 (1024, 30) | 37,950 | 2.3e-5 (max 1.0 bin) | **3.0e-7** | 3.3e-6 | 1.8e-7 |
| superblue12 (512, 30) | 15,760 | 8.4e-5 (max 1.75 bin) | **2.3e-7** | 4.8e-5 | 4.5e-7 |
| mms/newblue4 (1024, 30) | 839 | 1.8e-6 | 1.5e-6 | 4.8e-5 | 3.7e-7 |
| adaptec1 / mms/adaptec1 (512, 30) | 2,529 / 9,268 | 1.1e-6 / 2.9e-6 | 3.8e-7 / 4.5e-7 | 1.9e-6 / 2.2e-5 | 5e-8 / 4e-8 |
| mms/newblue4 (1024, 300) | 554 | 7.2e-7 | 6.9e-7 | 2.6e-5 | 2.1e-7 |
| adaptec1 (512, 300) | 102 | 3.0e-7 | 1.4e-6 | 1.7e-7 | 1.5e-8 |
| mgc_fft_a (256, 30 / 300) | 5.6 / 11.5 | 6.9e-8 / 9.3e-8 | 4.3e-6 / 3.9e-6 | 3.7e-7 / 8.6e-7 | 2.0e-7 / 7.6e-8 |
| mgc_pci_bridge32_b (128, 30) | 9.5 | 3.6e-7 | 9.7e-6 | 1.5e-6 | 9.3e-7 |

- **The overflow is better in q18 on every row** (≤ 9.3e-7; float up to 4.8e-5): the excess is summed exactly.
- **ρ: q18 wins where bins are dense and loses where they are sparse.** Float keeps relative precision and
  loses absolute precision as a bin fills: at 3.8e4 bin areas one float ulp is 0.004 of a bin, and a
  serial sum of thousands drifts a whole bin. q18 keeps 2^-18 absolute everywhere, which is coarse
  only next to small values: the small ISPD2015 designs at coarse grids (pci_bridge32_b 9.7e-6, fft_a 4e-6).
- q14 and q16 are 4× and 16× coarser, as expected; nothing argues for them over q18.

## Drift vs the float deposit (final quality)

`sum_order_drift.py --variants pl+Bprime pl+Bprime+q14 pl+Bprime+q16 pl+Bprime+q18` (`drift_fixed.md`):

| design | noise band (vs base) | pl+Bprime | q18 vs base | q18 vs pl+Bprime | in noise |
|---|---|---|---|---|---|
| mgc_fft_a | −0.451..+0.038% | −0.448% | −0.423% | +0.025% | yes |
| mgc_pci_bridge32_b | −0.312..+1.760% | +0.138% | −0.129% | −0.266% | yes |
| adaptec1 | −0.026..+0.023% | +0.001% | −0.005% | −0.007% | yes |
| mms/adaptec1 | −0.024..+0.011% | −0.493% | −0.359% | +0.135% | no (as pl+Bprime) |
| bigblue4 | −0.010..+0.007% | −0.004% | −0.002% | +0.002% | yes |

(bigblue4's q18 was run by hand from the same config; 758 iterations, converged, no bin left the word.
⚠️ A first read of bigblue4 here showed −0.480..+0.001% / −0.460%: those were 10-16's files, not yet
overwritten by this run. The row above is from today's files.)

mms/adaptec1's `pl+Bprime` is already outside base's band (5b's best-solution flip, primary vs aux);
q18 sits next to it, so the band to read it against is `pl+Bprime`'s, not base's. Signs are mixed, so
nothing systematic.

## Order independence, checked

With `deposit = "fixed"`, mgc_fft_a's whole schedule trace is **byte-identical** whether the deposit runs
in slot order or in name order (`slot_order = ["reductions"]` vs `["deposit", "reductions"]`). In float the
two orders diverge (#45 step 5b). This is what lets the CU use lanes and any banking.

## Verification so far
- `deposit = "float"` (default) is untouched: `make -C host/sw_flat check-placer` PASS bit-identical.
- `deposit = "fixed"`, F = 18: no bin left the 36-bit word on any full run above (sw_flat would exit).

## The CU side (2026-10-20)

F is now a contract constant: `densrec::RHO_FRAC_BITS = 18`, `RHO_UNIT = 2^-18`, and the rounding itself,
`densrec::to_fixed`, sits in `pl/src/format/density_footprint.hpp`, so host and device cannot round
differently. sw_flat's `deposit_frac_bits` knob is gone (the q14/q16 rows above were measured with it).

`bin_density_CU` (`pl/src/modules/bin_density_CU.hpp`):
- `rho_URAM` is `ap_uint<36>` in synthesis (a `uint64_t` off-synthesis, so a bin past 36 bits is visible
  to the harnesses instead of wrapping).
- `load_table` scales each footprint's weight by `fixed_scale`: sw_flat's `fp.weight *= fixed_scale`, once
  per table entry, not per overlap.
- Deposit: `rho += to_fixed(overlap_w * overlap_h * weight)`, the float geometry unchanged.
- The base arrives as `densrec::FixedBeat` (16 × uint32 per beat), from sw_flat's `fixed_base_q`.
- Overflow: `excess = Σ max(0, rho − overflow_cap)` as a uint64 tree per beat into one accumulator. It is
  exact, so the 8 rotating partials and the 2e-5 tolerance are gone. The device returns the integer; the
  host forms the ratio with `Placer::overflow_from_excess`.
- Drain: `(float)acc * RHO_UNIT`, exact after the conversion's rounding.

Verification:

| check | result |
|---|---|
| `make -C pl test`, `bin_density_CU_test` vs a double golden | rel_rms 4.9e-6 / 3.0e-6 (128 / 1024 bins), overflow 5.7e-7 / 9.1e-7 |
| tier-1 [6] ORDER: slots shuffled, cells and fillers separately | ρ and excess bit-identical; a float accumulator over the same two orders differs in 5,219 / 1,153 bins |
| `make -C host/sw_flat check-bin-density`, 44 designs, grid ≤ 1024, iteration 30 | **44/44 PASS**: ρ bit-equal, excess equal, overflow bit-equal, shuffled order bit-identical, mutant caught (`.claude/2_ARTIFACTS/scratch_48/bin_density_check_44_fixed_20261020.txt`) |
| `check-placer` (float default) | PASS, bit-identical to sw_only |
| C-synth (`pl/synth/bin_density_CU`) | `reduce` II=1, `drain` II=1, `load_base` II=1; `rows` (the deposit RMW) II=4; DSP 58 → 24 |

⚠️ csynth reports 37 URAMs (2 per bank). A 64K × 36-bit bank needs 8 URAMs in 8K × 36 mode, so the
physical count should be **128**. csynth under-counts deep URAM cascades (the float version read 21).
**Not yet confirmed:** read it from a `v++ -l` utilization report.

## The total-area bound: F per design (2026-10-20)

18 integer bits was sized from the 44 designs' measured peak, not guaranteed. A guarantee costs nothing
to compute: every bin holds a share of the total deposit, so no bin exceeds

    Σ = (fixed base + Σ_slots weight · cw · ch) / bin_area       (fillers top up to td; the clamp keeps area)

The host computes Σ once (sw_flat `deposit_fixed_base`, the tier-1 and co-sim benches likewise) and sets
`integer bits = ⌊log2 Σ⌋ + 2` (the ceiling plus one guard bit, which absorbs the ≤ ½-unit round-up of
each deposit), `F = min(36 − integer bits, 31)` (`densrec::rho_frac_bits`). The CU takes `fixed_scale =
2^F / bin_area` and `rho_unit = 2^-F` as arguments, so F costs it nothing.

Σ ≈ td · bins², so F follows the grid more than the design: on the 44 at grid ≤ 1024, **F = 14 on 7
designs** (the large ones at 1024), 15 on 11, 16–17 on 14, 19–23 on the small ISPD2015 designs at coarse
grids (`bin_density_check_44_banked_20261020.txt`). Bigger industrial designs at a finer grid get fewer
fraction bits, never an overflow (2048: Σ ≲ 2^22, F ≈ 12).

Precision vs the double golden at fixed positions (tier-1, 128 bins): rel_rms 6.3e-5 at F = 14, 1.6e-5 at
16, 4.9e-6 at 18, then **a floor of 2.8e-6 from F ≈ 20 up**: what remains is the float geometry, which sw_flat
shares. The overflow floors at ~2e-6 the same way (F = 18's 5.7e-7 was a lucky cancellation).

Drift, `pl+Bprime+fixed` (per-design F) against `pl+Bprime` and base's noise band (`drift_totalarea_F.md`):

| design | F | vs base | vs pl+Bprime | in base's noise |
|---|---|---|---|---|
| mgc_fft_a (256) | 19 | +0.011% | +0.462% | yes |
| mgc_pci_bridge32_b (128) | 23 | −0.017% | −0.155% | yes |
| adaptec1 (512) | 17 | +0.004% | +0.002% | yes |
| mms/adaptec1 (512) | 17 | +0.115% | +0.611% | no: just outside, on base's side of the design's known two outcomes (overflow 0.076 vs 0.089) |
| bigblue4 (2048: its D6 grid, past the CU's 1024; the model runs at any grid) | ~12 | −0.013% | −0.009% | no: 0.003 pp past a ±0.01% band (q14 at 2048 read −0.011%); 759 iterations, converged |

## The banked deposit and its forwarding (2026-10-20)

**Banks.** The grid is split over 16 URAM banks, `bank = 4·(col mod 4) + (row mod 4)`, address
`(col / 4, row / 4)`. Any window of up to 4 × 4 bins touches 16 different banks, so a footprint that spans
≤ 4 bins per axis is deposited in one cycle, every bank doing at most one read-add-write. Powers of two
make the mod and divide free (the low and high address bits). Each bank is 256 × 256 words at 1024:
64K × 36 b = 8 URAMs, 128 in all. (Bank count does not change the total; capacity does.)

**The fast path** (`deposit_fast`) takes one slot per cycle. For each of the 16 banks, unrolled: which
window column/row it owns is `(bank_col − col_lo) mod 4` and `(bank_row − row_lo) mod 4`, so the 4 column
overlaps and 4 row overlaps are computed once and each bank multiplies its pair. Footprints wider than
2.99 bins (`densrec::fits_window`) leave the stream for a slow list, deposited bin by bin after the fast
pass: ≤ 4,933 slots (0.5%) on any of the 44, none on 19 designs, never a filler.

**Forwarding.** A bank's read-add-write is spread over the pipeline: csynth issues the URAM read at stage
112 and the write at stage 117. Cell t writes at absolute cycle t + 116; cell t + k reads at t + k + 111.
So any cell within 5 cycles of the previous cell that touched the same bin reads a stale value from the
URAM. Each bank keeps its last `BIN_FORWARD_DEPTH` writes (address, new value) in registers; a read whose
address matches takes the newest such value instead. `#pragma HLS DEPENDENCE variable=rho_URAM inter
false` tells HLS not to stall for the URAM, which is only correct because of those registers.

C simulation runs the pipeline one iteration at a time, so it is correct at any depth and cannot test
this. RTL co-simulation can (`pl/synth/bin_density_CU/cosim/`, `run_depths.sh`), on a stream built to
collide: 128 cells at one position (distance 1), then cells cycling through k far-apart positions for
k = 2..5, then random cells, NO_DEPOSIT slots, a macro on the slow path and a filler tail. Golden: the
plain deposit in the bench, one bin at a time, no banks, no forwarding. Grid shrunk to 64 for RTL speed:

| BIN_FORWARD_DEPTH | C simulation | RTL co-simulation |
|---|---|---|
| 0 (no forwarding) | PASS | **FAIL**: 79 of 4096 bins wrong, excess 2.95e9 vs 7.82e9 |
| 4 | PASS | **FAIL**: 32 bins wrong, all in the distance-5 section |
| 5 | PASS | **PASS**, bit-exact |
| 6 (default) | PASS | **PASS**, bit-exact |

The predicted distance was 5 and the RTL agrees: depth 4 loses exactly the cells 5 apart. The default is 6
(one spare). The depth is tied to the schedule, so `run_depths.sh` must be re-run whenever `deposit_fast`
changes.

**Two co-sim traps hit on the way:** a C parameter named `bins` becomes an RTL port named `bins`, a
SystemVerilog keyword, and the simulator wrapper does not compile (the CU's parameter is `grid_bins`
now); and HLS could not prove `load_base`'s four stores hit four banks while the bank was computed as
`bank_of(col, row + k)` (II = 4), so it is written as `4·(col mod 4) + k`.

**Synthesis** (full size, 1024, 3.33 ns): `deposit_fast_loop` **II = 1** (depth 117), `load_base`, `reduce`
and `drain` II = 1 (4, 16 and 4 bins per cycle), `slow_rows` II = 4. LUT 100,608 (11%), FF 79 K, DSP 114.
csynth reports 37 URAMs; the arrays need 128 (see above), confirm at link. One evaluation at 1024 is
about load 262 K + deposit (one cycle per slot) + reduce 65 K + drain 262 K cycles: bigblue4's 5.8 M
slots make it ~6.4 M cycles, ~21 ms at 300 MHz. XPlace's scatter on bigblue4 is 4 ms on a 3090: one cell
per cycle is not enough (Next, 1).

## Not taken: a per-iteration exponent
The 18 integer bits are bought for iterations 0–7 only; from about iteration 20 the peak is < 6 K
(13 bits), and on the small designs it is < 12 (4 bits). A per-iteration F (the host picks F_k from the
previous evaluation's peak, with 2 bits of margin; the device returns the peak from its drain pass)
costs the device only a runtime scale and shift, keeps bit-exactness (the host's choice is
deterministic integer data), and would put the small designs below float's error too. Not built:
q18's drift is already in noise.

## Next
1. **Lanes.** One cell per cycle is ~21 ms on bigblue4. Row-band ownership: L lanes, each owning
   1024 / L rows with its own 16 banks, cells routed by row (a straddler goes to both lanes). At L = 8 each
   band's banks are 8K words, exactly one URAM in 8K × 36 mode, so **8 lanes cost no extra URAM** (still
   128); 16 would double it. Costs: a routing network from the 16-slot beats, boundary duplicates, and load
   imbalance in the early clustered iterations. Integer adds make lane order irrelevant (check [5]).
2. **Bin-index divide:** `(int)(xl / bin_w)` wants a reciprocal multiply on the device, which can move a
   boundary bin. That would be a change to the model (sw_flat first), not just the CU.
3. **URAM count:** confirm 128 at link. **DDR format:** 16 bins per cycle for load/drain needs 4 × 4
   tiles per beat (parked by Mark).
4. **Step 6's golden is `pl+Bprime+fixed`** (`sum_order_drift.py`); `pl+Bprime` stays as the float reference.
5. XPlace's own scatter/gather structure: [[REPORT_48_xplace_density_scatter_gather_20261020.md]] (in progress).
