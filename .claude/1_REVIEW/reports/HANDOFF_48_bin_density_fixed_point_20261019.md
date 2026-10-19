# HANDOFF #48: `bin_density_CU`'s fixed-point accumulator, modeled in sw_flat

*2026-10-19. Branch `pl_algo`. Draft report: becomes `REPORT_48_...` when the CU side lands.*

**Mark's frame (2026-10-19):** `bin_density_CU` (renamed from `density_map_CU`) gets the whole chip for
now, 1024 × 1024 only, budgeted at about half the URAMs; it accumulates in **fixed point**. Its inputs
are the slot-ordered positions (one long stream over the whole design, no chunking), a per-slot
footprint index into a small table, and the host's fixed base map. Its outputs are ρ and the overflow.

## The format: 18.18 unsigned in a 36-bit URAM word

URAM288 runs as 8K × 36 (AM007; [[PLAN_48_density_onchip_20261014.md]]'s 10-16 correction), so one bin
per 36-bit word puts the 1024² accumulator in **128 URAMs (28% of 463)**, within the half-chip budget.
`densrec::RHO_WORD_BITS = 36` (`pl/src/format/density_footprint.hpp`).

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
| bigblue4 | −0.480..+0.001% | −0.460% | *(pending)* | | |

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

## Open choice for Mark: a per-iteration exponent
The 18 integer bits are bought for iterations 0–7 only; from about iteration 20 the peak is < 6 K
(13 bits), and on the small designs it is < 12 (4 bits). A per-iteration F (the host picks F_k from the
previous evaluation's peak, with 2 bits of margin; the device returns the peak from its drain pass)
costs the device only a runtime scale and shift, keeps bit-exactness (the host's choice is
deterministic integer data), and would put the small designs below float's error too. Not built:
q18's drift is already in noise.

## Next
1. `bin_density_CU`: the integer accumulator (`ap_uint<36>` per bin), w' in the footprint table, the
   fixed base sent as integers, excess returned as int64, drain `rho = acc * 2^-F`.
2. `density_inputs.hpp`: the table carries w'; the base goes over as `base_q`.
3. `bin_density_check`: rho and excess bit-equal to sw_flat with `deposit = "fixed"`, in any order (a
   shuffled-order mutant must still match: that is the point of the format).
4. Still open in the model, not touched here: the bin-index divide (`(int)(xl / bin_w)`) wants a
   reciprocal multiply on the device, which can move a boundary bin; the 4×4 banking and lanes change
   no arithmetic.
