# REPORT #49: sw_only's HPWL metric now sums in double

*2026-10-16. Branch `pl_algo`. Mark's call: un-freeze sw_only for this one fix. Found in #45 step 5b
([[REPORT_45_pl_arithmetic_20261016.md]]).*

## Result
- **The metric is now accurate.** The float accumulator drops low bits once the total gets large.
  At iteration 30 it read low by up to 0.39% (bigblue4); it now matches a double reference to 2e-8.
- **Placement quality is unchanged.** Across 13 large designs, legal-vs-legal post-DP HPWL moves by a
  mean of **+0.015%** (median +0.013%, range −0.24..+0.27%). That is placement-noise scale: a 2-ULP
  `init_gamma` nudge alone moves small designs by 0.5–2%.
- **What does move is the GP HPWL sw_only reports.** On the largest designs it is now 0.1–0.5% higher,
  because it is now correct. So GP-vs-XPlace ratios on those designs read slightly worse than in the
  archived goldens (bigblue4 GP ratio ~1.017 → ~1.022). The DP ratio, the headline, is unchanged.

## The defect
`DataBase::computeTotalWirelength` summed per-net float HPWLs, in name order, into a **float**
(`OrderedReduce::sum`). The total reaches 1e8–1e10. At 7e8 one float ulp is 64 DBU, which is larger
than many nets' HPWL, so each add rounds. Over ~1–2 M nets the error biases low.

Measured at iteration 30 (sw_flat, bit-identical to sw_only), against a double sum over the same nets:

| design | float metric error |
|---|---|
| mgc_fft_a, adaptec1, bigblue1, mms/bigblue1 | ≤ 1.3e-5 |
| bigblue2, mgc_superblue16_a, superblue19, mms/bigblue2 | 6e-5 – 1.5e-4 |
| mgc_superblue11_a, superblue14 | −5e-4 – −8e-4 |
| mms/bigblue3, bigblue3, mgc_superblue12 | −1.7e-3 – −2.5e-3 |
| bigblue4 | **−3.9e-3** |

The metric feeds sw_only's λ schedule (Δhpwl), its best-solution choice, and every GP HPWL it prints
(`run_summary.md`, `gp_only.csv`). **The scored goldens were never affected:** legal-vs-legal HPWL comes
from XPlace's evaluator.

## The fix
- `host/src/common/include/Common.h`: `OrderedReduce::sum_in_double`, the same as `sum` (terms in
  parallel, added in index order) but accumulated in double and rounded to float once.
- `host/src/common/src/DataBase.cpp`: `computeTotalWirelength` uses it. Per-net terms are still float,
  and the order is still name order. Only the accumulator changed.
- `host/sw_flat/placer/wirelength.cpp`: the same change in `total_wirelength`, so sw_flat stays
  bit-identical to sw_only. `make -C host/sw_flat check-placer`: PASS.
- Double rather than int64: one line, no unit choice. The PL route already has its int64
  (`[sw_flat] hpwl = "fixed"`); both agree with a double reference to ~2e-8.
- Every other `OrderedReduce` sum (L1 norms, BB norms, precond) is untouched. They were not measured;
  totals over ~1 M nodes might show the same effect.

## Regress baselines
`make test-regress-slow` passed at HEAD before the change. After it, all three designs drift (first
printed difference at row ~36 on fft_a). Regenerated with
`run_regress.sh --slow --update-baselines --reason "HPWL metric accumulates in double ..."`, then
re-verified PASS. Iterations: mgc_pci_bridge32_b 693 → 701, mms_adaptec1 1274 → 1283.

## Measurement: before/after on the large designs
**Before** = the archived goldens (`GOLDEN_sw_only_frozen_20260825`, `GOLDEN_mms_sw_only_frozen_20260825`).
They are valid as "before" because (a) the regress baselines had not moved since the golden commit
`271d024`, and (b) a control run reproduced the golden's bigblue1 GP row exactly (697 iterations, overflow
0.0452, GP HPWL 8.759e7) with the fix stashed (`control_prefix_bigblue1.csv`). **After** = `make dse` on
the 13 designs, default seed and XPlace grids.

| design | iterations | reported GP HPWL | post-DP HPWL | DP ratio vs XPlace |
|---|---|---|---|---|
| ispd2005/bigblue1 | 697 → 697 | −0.011% | **−0.006%** | 1.0042 → 1.0041 |
| ispd2005/bigblue2 | 711 → 708 | +0.000% | **−0.019%** | 1.0051 → 1.0049 |
| ispd2005/bigblue3 | 741 → 739 | +0.168% | **+0.015%** | 1.0127 → 1.0129 |
| ispd2005/bigblue4 | 753 → 752 | +0.462% | **+0.013%** | 1.0096 → 1.0097 |
| ispd2015/mgc_superblue11_a | 669 → 668 | −0.060% | **−0.092%** | 1.0203 → 1.0194 |
| ispd2015/mgc_superblue12 | 799 → 799 | +0.081% | **+0.124%** | 1.0465 → 1.0478 |
| ispd2015/mgc_superblue14 | 648 → 655 | −0.044% | **−0.133%** | 1.0273 → 1.0260 |
| ispd2015/mgc_superblue16_a | 678 → 679 | +0.117% | **+0.270%** | 1.0303 → 1.0330 |
| ispd2015/mgc_superblue19 | 658 → 658 | −0.124% | **−0.046%** | 1.0793 → 1.0788 |
| mms/bigblue1 | 1361 → 1363 | −0.024% | **+0.021%** | 1.0036 → 1.0038 |
| mms/bigblue2 | 1454 → 1469 | +0.165% | **+0.206%** | 0.9965 → 0.9986 |
| mms/bigblue3 | 1324 → 1322 | +0.037% | **+0.086%** | 0.9984 → 0.9993 |
| mms/bigblue4 | 1453 → 1469 | +0.248% | **−0.239%** | 1.0188 → 1.0164 |

| | Δ post-DP mean | median | range | mean DP ratio |
|---|---|---|---|---|
| ISPD (9) | +0.014% | −0.006% | −0.133..+0.270% | 1.0262 → 1.0263 |
| MMS (4) | +0.019% | +0.054% | −0.239..+0.206% | 1.0043 → 1.0045 |

"Reported GP HPWL" is sw_only's own number, printed to 4 significant figures, so it resolves only
~0.05%. On bigblue3/4 it rises by about the size of the old metric error: the same placement, now
measured correctly.

Evidence: `.claude/2_ARTIFACTS/results/HPWL_double_sum_20261016/` (run dir
`vck5000/results/DSE_20261016_145550/`).

## ⚠️ dse.py bug found: same-named designs in two suites overwrite each other's LG+DP
`lgdp.json` is keyed `name@grid`, so `ispd2005/bigblueN` and `mms/bigblueN` in one sweep collide. The
later LG+DP result wins, and `dse_results.csv` gives it to **both** rows (here, 4 of 8 bigblue rows were
wrong by up to 14%). The table above takes those 8 values from `dse.log`, which prints each run's DP in
order; run order was confirmed against `configs/run_NNN.toml`. **Any earlier sweep that mixed ISPD2005
and MMS designs has the same corruption.** The archived goldens ran the two suites separately, so they
are unaffected. Needs a fix: key on `suite/name@grid`.

> **Fixed 2026-10-19.** Run labels now carry the suite (`ispd2005_bigblue1@512`), and dse.py refuses to
> launch (dry-run included) when two runs share a label. The guard was checked by restoring the old
> label: it refuses `adaptec1@512`. A mixed sweep (`DSE_20261019_103057`: ispd2005/adaptec1 +
> mms/adaptec1) keeps separate lgdp.json entries and the right DP on each row. An audit of the 34
> surviving sweeps under `vck5000/results/` and `.claude/2_ARTIFACTS/` found duplicate labels **only in
> this run** (and its archived copy, now flagged by a README). `--resume` on an old sweep reuses the
> labels in that sweep's `sweep.json`, so old sweeps resume unchanged.

## What's next
- The archived goldens are pre-fix. On small designs the fix moves results by noise only. A full
  re-score would fold naturally into #46's re-score, which needs both suites anyway.
- Mark: is "frozen" still the rule for sw_only otherwise? This change is recorded as a one-off.
