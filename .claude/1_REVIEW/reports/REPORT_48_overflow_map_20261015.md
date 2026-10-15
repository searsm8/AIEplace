# REPORT #48 phase 2: the overflow map is B' (XPlace's one deposit), measured

*2026-10-15. Branch `pl_algo`. Was `_NEW_HANDOFF_48_overflow_map_20261014.md`; the decision and the
options table are kept below.*

## Result
**Decision (Mark, 2026-10-14): B'.** "We treat XPlace as gospel, and our URAM is precious." The device
gets the per-iteration overflow from its own solver map with no second accumulator. The map is reduced
after the cells are deposited and before the fillers, over a fixed base built at **exact** size, as
XPlace's `init_density_map` is.

Measured in sw_flat:
- **B' gives the same number as sw_only's separate metric.** The two are bit-equal on 38 of 44
  designs at the stop iteration. On the 6 others the relative difference is at most 2.5e-4, which is
  rounding (see below).
- **B alone, which keeps sw_only's clamped fixed base, is biased.** At the stop it reads 1.3%, 4.5%
  and 6.7% low on bigblue3, bigblue2 and bigblue4. So the exact base is required, not optional.
- **On trajectories, B' is at the noise level.** Over seven designs, the stop iteration moves by ±2,
  final HPWL by at most 0.03%, and every run converges. Two small values sit just outside a
  six-sample noise band (bigblue2 −0.032%, bigblue4 +0.006%); see the table.

This makes sw_flat's B' mode (and, later, the device) **diverge from frozen sw_only, toward XPlace**:
the solver no longer stretches small fixed nodes. It is not an entry for CLAUDE.md's "divergences
from XPlace" registry, because it removes one. sw_only keeps its clamped fixed base (frozen).

## What changed (`host/sw_flat/`)
Two `[sw_flat]` keys, both off by default, so `make check-placer` is still bit-identical to sw_only
(re-run: PASS 2/2).

| key | effect |
|---|---|
| `fixed_base = "exact"` | `deposit_fixed_base` deposits fixed nodes at exact size: XPlace `initializer.py:29`, `forward_naive(..., -1.0, -1.0, ...)`. The cap `min(ρ, td)` is unchanged (that is #35's deliberate divergence). |
| `overflow_map = "snapshot"` | `compute_electric_fields` deposits the components, reduces `Σ max(0, d − bin_w·bin_h·td) / total_movable_area`, then deposits the fillers. `record_iteration_results` uses that value instead of `compute_overflow(true)`. This is XPlace's `electronic_density_layer.py:113-131` and `overflow_fn` (`run_placement_nesterov.py:41`). |

**B' = both keys.** The final report's overflow (`final.txt`) still uses the separate metric. It is a
one-off host measurement, not a per-iteration device value.

New tooling:
- `test/overflow_map_check.cpp` (`make check-overflow-map`, `JOBS=`, `ITERATIONS=`). On one
  placement it computes separate, B' and B. Verdicts: **[1]** \|B'/separate − 1\| ≤ 1e-3; **[2]** the
  full map with fillers never reads lower than the snapshot. `[info]`: B's bias, the number of
  stretched fixed nodes, and the base bins that differ.
- `test/sum_order_drift.py`: adds the variants `snapshot`, `exact_base` and `Bprime`, and a
  `--variants` filter. Pass `--variants` **after** the design list, or `nargs="+"` swallows the
  designs.

## Measurement 1: same placement, three overflows (44 designs, grid capped at 1024)
`.claude/2_ARTIFACTS/scratch_48/overflow_map_check_it30_20261014.txt` (30 iterations) and
`overflow_map_check_stop_20261014.txt` (run to the stop).

**B' vs separate.** Bit-equal on 38/44 at both points. The 6 others are all ISPD2015 designs with
non-dyadic bin widths: matrix_mult_2/a/b/c, superblue11_a and superblue14. At iteration 30 they
differ by at most 6e-6; at the stop, by 3e-5 to 2.5e-4, which is ≤1.2e-5 in absolute overflow.
The two overlap forms (`bin_xl + bin_w` vs `(c+1)·bin_w`) round apart, and `d − cap` cancels more
as the overflow falls, so the relative difference grows. This is not a bias: its sign varies by
design.

The bound was 1e-5, set from the 30-iteration run. It failed those 6 at the stop and is now 1e-3,
4× above the worst rounding seen and 13× below B's smallest real bias.

**B vs separate, at the stop.** Only 6 designs have fixed nodes the clamp actually stretches
(nonzero area, under √2 bins):

| design | stretched fixed nodes | base bins changed | B's bias at the stop |
|---|---|---|---|
| bigblue2 | 19,425 / 23,084 | 73,186 | **−4.5%** |
| bigblue4 | 7,503 / 8,170 | 72,521 | **−6.7%** |
| bigblue3 | 623 / 1,298 | 6,781 | **−1.3%** |
| adaptec1 | 1 / 543 | 224 | −1e-4 |
| bigblue1 | 1 / 560 | 222 | −1.1e-4 |
| mgc_superblue11_a | 15 / 1,458 | 82 | −2.5e-4 |

At iteration 30 the same biases were only 1e-4 to 8e-4. They grow as the cells settle against the
fixed nodes. That is the systematic effect the handoff warned about: B would read low exactly when
the 0.07 stop is being tested. The ISPD2015 DEF fixed nodes and the MMS terminals are either large
or zero-area, so they are unaffected.

## Measurement 2: trajectories (D6 config, native grid)
`.claude/2_ARTIFACTS/scratch_48/overflow_map_drift_20261015.md`. Method as in #45 step 5b: the noise
band is six runs with `init_gamma × (1 ± k·2.5e-7)`, plus base's own 0. 2 threads per run, 4 runs
at a time.

| design (grid) | noise band | snapshot (B) | exact_base | **B'** | B' iterations (base) |
|---|---|---|---|---|---|
| mgc_fft_a (256) | −0.473..+0.056% | identical | identical | **identical** | 628 (628) |
| adaptec1 (512) | −0.018..+0.053% | +0.007% | +0.008% | +0.008% | 654 (654) |
| bigblue1 (512) | −0.009..+0.026% | +0.000% | +0.011% | +0.011% | 698 (698) |
| mgc_superblue11_a (512) | −0.042..+0.048% | +0.036% | +0.042% | −0.023% | 669 (668) |
| bigblue3 (2048) | −0.022..+0.004% | −0.010% | identical | −0.010% | 753 (753) |
| bigblue2 (1024) | −0.027..+0.010% | **−0.083%** | **−0.032%** | **−0.032%** | 715 (713) |
| bigblue4 (2048) | −0.013..+0.000% | −0.010% | −0.007% | **+0.006%** | 758 (759) |

All figures are final masked HPWL relative to base. Bold means outside the band.

- **mgc_fft_a is bit-identical over the whole run under B'.** It has no stretched fixed nodes, and
  the snapshot equals the separate metric bit for bit. This is the clean control.
- **bigblue2 is the only design B' visibly moves**, by −0.032% HPWL, with the stop 2 iterations
  later. B alone moves it 2.6× further (−0.083%), and its exact overflow is +5.5% higher than base
  against +0.45% for B'. B's low reading is visible on a real run here.
- bigblue4's +0.006% is outside a band whose upper edge is base's own 0. Six samples underestimate
  the spread (the same caveat as in 5b), so this is not a signal.
- bigblue3 and bigblue4 run at grid 2048 here, against 1024 in Measurement 1. At 2048, √2 bins is
  narrower, so fewer fixed nodes are stretched: bigblue3's exact base changes nothing at all.
  **The device grid is 1024**, where measurement 1 shows the larger base change. A 1024-grid
  trajectory check on bigblue3/4 is worth running before step 6 quotes numbers.
- Exact (unsmoothed) overflow moves by −1.8% to +1.6% on B'. That is within what 5b found the noise
  runs do (up to ±14%).

**Falsifier (from the handoff): a systematic stop or HPWL shift outside noise on the affected
designs.** Not seen for B'. The one excursion beyond noise is 0.03% HPWL and 2 iterations, and it
has no consistent sign across designs.

## Device cost (for #48 phase 2's CU, not built yet)
`density_map_CU` (`pl/src/modules/density_map_CU.hpp`) has to split its deposit loop at
`layout.first_filler_slot`, run one read-only reduction pass over `rho_URAM` (1M bins / 16 lanes ≈
64K cycles ≈ 0.2 ms at 300 MHz, ~2% of the bigblue4 bar), output one float, and then deposit the
fillers. The host builds `fixed_base_DDR` exact-size (`fixed_base = "exact"`). No extra URAM is
needed.

Open detail: `first_filler_slot` is a chunk boundary, and the reduction needs it beat-aligned (a
multiple of 16) or a partial-beat mask. `make_density_inputs` already takes `placer.fixed_base` as it
stands, so `check-density-map` follows the switch automatically.

## Next
1. `density_map_CU`: the filler barrier plus the overflow reduction, with a tier-1 check against
   sw_flat's `snapshot_overflow` (bit-exact: the same serial sum order is achievable, or state a
   tolerance if the CU sums as a tree).
2. Turn B' on in the step-6 golden config (sw_flat with `slot_order`, `wa_exp = "lut"`,
   `overflow_map = "snapshot"`, `fixed_base = "exact"`).
3. Optional: the bigblue3/4 trajectory at grid 1024 (see above).

<details><summary>The handoff's decision text (2026-10-14), kept verbatim in substance</summary>

The stop test, the γ schedule, the λ schedule and the precond escalation all read the smoothed
overflow every iteration. In sw_only it is not the solver's map: the solver's fixed base is
√2-clamped, while the metric's fixed base is exact size; the cap area is per-bin in one and
`bin_w·bin_h` in the other; fillers are in one and not the other; and the overlap rounding differs.
XPlace does it with ONE deposit (`mov_density_map = init_density_map.clone()` + cells → overflow;
fillers into a separate map, then added). Options were A (a second accumulator, +128 of 463 URAM),
B (reduce before the fillers, no storage) and B' (B with an exact-size fixed base). Recommendation:
B', or B if B' moved results; decide by measurement.
</details>
