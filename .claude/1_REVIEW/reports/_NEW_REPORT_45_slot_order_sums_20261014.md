# REPORT #45 step 5b: slot-order sums and the PL's exp table in `sw_flat_place`

*2026-10-14. Branch `pl_algo`. Follows [[REPORT_45_sw_flat_placer_20261009.md]] (step 5: bit-identical
to sw_only).*

## Result
Moving `sw_flat_place`'s sums to slot order, and its `exp` to the PL's lookup table, changes the run
**by no more than a 2-ULP perturbation of `init_gamma` does**: 28 of 30 variant runs, on five
designs, fall inside the noise band. Both outliers are on mms/adaptec1 and are ≤ 0.1% HPWL; see
below. bigblue4, the design whose slot order (3 chunks) is furthest from name order, is 5/5 inside,
all within ±0.011%.

So slot order hides no semantic difference (the plan's falsifier for step 5). sw_flat can sum in the
PL's order and still be trusted as the golden.

## What changed (`host/sw_flat/placer/`)
New `[sw_flat]` config table. Everything is off by default, so `make check-placer` stays
bit-identical to sw_only (re-run: PASS).

| key | effect |
|---|---|
| `slot_order = ["deposit"]` | the density deposit (solver map and overflow metric) walks movable slots ascending, not sw_only's name order |
| `slot_order = ["reductions"]` | the per-node sums (BB norms, combine L1s, precond a1/a2, initial λ) walk slots ascending |
| `wa_exp = "lut"` | the WA gradient's `exp` is the PL's table (`wa_sums.hpp lut_exp_pair`: 242 entries, step 0.05γ, linear interpolation, 0 past 12γ), not `std::exp` |

Also: phase timers in `final.txt` (`seconds_gradient / field / combine / metrics`); the loop moved from
`main.cpp` to `placer.cpp` (sw_only's AIEplace.cpp), so tools can link the placer;
`test/gradient_bench.cpp` (`make gradient-bench`); `test/sum_order_drift.py`, the measurement below.

**Not moved:** the fixed deposit (constant; the host computes it once), the force gather's per-node
bin order (step 6 decides it with `force_gather`), the gradient merge and the HPWL metric. On the PL
those are the record stream's scatter-add order and an integer (fixed-point) sum, so they belong to
"the PL's arithmetic", not to slot order. The initial placement keeps name order: it is the host's,
on either backend.

## Measurement
`test/sum_order_drift.py`: per design, the D6 config. Variants are compared with `base` (the
bit-identical default). The **noise band** is the spread of six runs with `init_gamma ×
(1 + k·2.5e-7)`, k = ±1..3, which is about 1-3 ULP. A placement run is chaotic: every variant,
noise included, differs from base at trace row 1, so a variant means something only against that
band. The band includes base's own 0, since base is one sample of the same noise: bigblue4's six
noise runs all fell below it. Final masked HPWL vs base; 2 threads per run, 4 at a time. Tables:
`host/sw_flat/build/sum_order_drift_*.md` (gitignored).

| design | noise band | deposit | reductions | slot (both) | lut | slot+lut |
|---|---|---|---|---|---|---|
| mgc_fft_a | −0.473..+0.056% | −0.001% | +0.021% | −0.013% | −0.444% | −0.026% |
| mgc_pci_bridge32_b | −0.501..+1.854% | +0.058% | +0.564% | +1.160% | +0.060% | +1.499% |
| adaptec1 | −0.018..+0.053% | +0.020% | −0.005% | +0.006% | +0.009% | −0.000% |
| mms/adaptec1 | −0.025..+0.022% | −0.006% | −0.008% | **−0.028%** | +0.001% | **+0.097%** |
| bigblue4 | −0.013..+0.000% | −0.011% | −0.005% | −0.005% | +0.001% | +0.001% |

- Every run converged, with iteration counts within ±5 of base.
- **The noise band is design-specific and can be wide.** pci_bridge32_b moves 2.4% end to end from a
  2-ULP γ change. Any future A/B on one seed of these small designs is noise at that scale.
- **The two outliers** sit on the design with the tightest band (±0.025%). One is −0.028%, at the
  band's edge; the other is +0.097%. Six noise samples underestimate the true spread, so this is not
  evidence of a systematic effect, and even taken at face value it is 0.1% HPWL. It would be worth a
  look only if step 6 shows mms/adaptec1 drifting further.
- **Within a run the drift stays small.** Relative HPWL difference, slot order vs base, at the same
  iteration:

  | iteration | 1 | 10 | 50 | 100 | 200 | 300 | 400 | 500 | 600 |
  |---|---|---|---|---|---|---|---|---|---|
  | adaptec1 | 8e-7 | 8e-7 | 2e-6 | 0 | 5e-7 | 4e-7 | 2e-5 | 5e-4 | 1e-4 |
  | mgc_fft_a | 2e-7 | 0 | 4e-7 | 1e-7 | 5e-7 | 5e-4 | 8e-4 | 2e-4 | 1e-4 |
  | pci_bridge32_b | 4e-7 | 4e-6 | 9e-7 | 0 | 4e-7 | 2e-7 | 9e-6 | 1e-5 | 8e-5 |

  It stays at float-rounding level (10⁻⁷ to 10⁻⁵) for 200-300 iterations and settles near 10⁻⁴ in the
  endgame, without running away. The 2-ULP noise runs behave the same, and start larger (adaptec1
  1e-4 at iteration 1). **The larger final differences are about when the run stops.** On
  pci_bridge32_b the stop fires at 714-721 instead of 715, and the best-solution rule can pick a
  different iteration; on a small design whose HPWL still moves in the endgame, a few iterations are
  worth about 1%.
- Exact overflow moves by up to ±14% relative (fft_a: 0.186 → 0.212 on the noise runs too). This is the
  sharp, unsmoothed number, and it is far noisier than HPWL at the same perturbation.

## The exp table, as a software speed-up
`make gradient-bench`: `compute_hpwl_partials` only, on identical positions (50 iterations in), 50 reps.

| design | threads | std::exp | table | table / exp |
|---|---|---|---|---|
| mgc_fft_a (100 K pins) | 1 | 1.81 ms | 1.51 ms | 0.83 |
| | 7 | 0.52 ms | 0.46 ms | 0.88 |
| adaptec1 (944 K pins) | 1 | 16.57 ms | 13.06 ms | 0.79 |
| | 4 | 6.27 ms | 5.01 ms | 0.80 |
| | 7 | 4.64 ms | 3.89 ms | 0.84 |

The table saves 12-21% of the gradient. But the gradient is only **15% of a sw_flat iteration**
(adaptec1, 7 threads, 654 iterations, 20.1 s total):

| phase | s | share |
|---|---|---|
| density field (deposit, serial + DCT) | 8.67 | 43% |
| metrics (HPWL + smoothed overflow, both serial) | 4.88 | 24% |
| WA gradient | 3.10 | 15% |
| combine (force gather) | 1.54 | 8% |
| rest (step, precond, snapshots) | ~1.9 | 10% |

So the table buys **~2-3% of software run time** (the `lut` rows above run 2-5% faster end to end,
consistent with that). Its value is fidelity to the PL, not speed. The software's real costs are the
two serial deposits (solver map and overflow metric) and the DCT. The overflow metric is a second
full deposit every iteration, whose job overlaps the solver's (the Improvements list's "operator
extraction").

The LUT gradient differs from `std::exp`'s by rel_rms 3e-4 (fft_a) to 2e-3 (adaptec1) at iteration 50
(`[info]`, not a verdict: the PL's own accuracy is held by `pl/test`).

## Next
- The rest of "the PL's arithmetic" (stock-take item 2): the gradient formula as `wa_gradient.hpp`
  computes it, the HPWL in fixed point, the merge in record-stream order. Then sw_flat's `slot+lut`
  mode plus these is the golden step 6's modules are checked against.
- Decision for Mark: the grid. sw_flat follows sw_only's per-design formula; the hardware grid is
  1024 (plan §10).
