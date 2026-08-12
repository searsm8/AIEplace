# REPORT — preconditioner always on + unthrottled escalation: diagnosis, fix, 28-design re-score (2026-08-11)

*Commit `3c70b38`. Supersedes the headline in [[_NEW_REPORT_performance_snapshot_20260810.md]] for
the 19 scored ISPD designs; that report's method section still governs and is not restated here.*

## 1. Headline

**Median post-DP HPWL ratio 1.0113 → 1.0095 over the same 19 scored ISPD designs**, and two designs
that previously died on `divergence_guard` now converge. The tiers disagree and must be quoted
separately:

| | median before → after | mean before → after |
|---|---|---|
| **ISPD2005 (8)** | 1.0065 → **1.0053** | 1.0111 → **1.0052** |
| **ISPD2015 (11)** | 1.0232 → 1.0273 | 1.2023 → 1.2268 |
| ISPD2015 excl. `matrix_mult_a` | 1.0184 → 1.0190 | 1.0192 → 1.0228 |
| **all 19** | 1.0113 → **1.0095** | 1.1218 → 1.1335 |
| all 19 excl. `matrix_mult_a` | 1.0091 → 1.0086 | 1.0156 → 1.0150 |

`divergence_guard` **10/28 → 8/28**. Within ±2%: 12/19 → 13/19. Beat XPlace: 4/19 → 3/19.

**Quote the median, and say which tier.** The mean is one design (§5).

## 2. What was wrong

`updateSchedule()` freezes λ, γ and `precond_coef` on 2 of every 3 iterations while
`precond_kappa` (XPlace's `weighted_weight`) sits in (0.5, 0.95). κ rises with λ, so it is meant to
cross that window once and leave. Two defects kept it from leaving.

**(A) Our escalator was inside the throttle.** `step_precond_coef` is the one member of XPlace's
`step()` trio with **no** `if self.skip_update: return` guard (`param_scheduler.py:359-366`, against
`:299` and `:353` which both have one). That is deliberate: `precond_coef` is precisely what carries
κ out of the window, so throttling it makes the throttle self-sustaining. Ours lived at the bottom
of `updateDensityWeight()`, called only under `perform_update`, so the `%20` grid could fire only
where it met `%3` — **every 60 iterations instead of 20**.

**(B) The preconditioner was off on all 28 ISPD designs.** `enable_preconditioning` was a flat
`false` until `638b9a8` (2026-07-17), which made it auto-ON iff `num_movable_macros > 0` on the
evidence that preconditioning is "a wash on fixed-macro designs". True of the preconditioner; false
of the flag, because `precond_coef` also feeds κ. Zero movable macros ⇒ `escalation_enabled` false
⇒ `precond_coef` frozen at 1.0 for the whole run ⇒ the throttle had no release mechanism at all.

### The evidence that settled it

XPlace's own `bigblue3` GP, instrumented via its `PRECOND_TRACE` hook:

| | XPlace | ours (before) |
|---|---|---|
| κ enters (0.5, 0.95) | iter 626 | iter ~595 |
| κ leaves | **iter 901** | **never** |
| iterations throttled | 275 | 83, then killed |
| `precond_coef` at end | **512** | **1.0** |
| λ at end | **29.7** | 0.040 |
| final overflow | 0.051 | 0.180 |

**Both placers hit the same throttle for a comparable stretch.** XPlace escapes because
`precond_coef` starts doubling the moment overflow crosses 0.3 (iteration 901); κ goes
0.9313 → 0.9909 within 25 iterations, λ then ramps unthrottled 0.203 → 29.7 (**146×**), and overflow
collapses 0.2885 → 0.0514. That is the entire endgame, and it is driven by `precond_coef`, not λ.

Ours, from `iterations.dat` — the throttle is visible directly in the λ-update density:

```
iters      λ updates   λ growth/iter   overflow at end of window
481-511      30/30       1.02900          0.3537
541-571      30/30       1.02900          0.2341
571-601      24/30       1.02314          0.1981   <- throttle engages
601-631      10/30       1.00958          0.1855
631-661      10/30       1.01325          0.1787
661-678       5/17       1.01260          0.1798   <- killed
```

30/30 → 10/30 is the `%3` gate exactly. 1.029/iter is `1.05 × 0.98`, the schedule's **maximum**, so
there was no headroom to absorb the cut; overflow descent went from −20% per 30 iterations to −4%,
and `checkOverflowPlateau(50, 0.05)` fired at iteration 678 as designed. The guard was not wrong —
the run genuinely stalled.

## 3. The fix

`updatePrecondCoef()` extracted from `updateDensityWeight()` and called from `updateSchedule()`
outside the `perform_update` gate; `auto_enable_preconditioning` removed entirely, with
`enable_preconditioning` defaulting true to match XPlace's `--use_precond` (`main.py:35`).
`precond_explicitly_set` went with it. The key is gone from the header, `Setup.cpp`,
`default_config.toml` and all three frozen regress configs (where the value is now pinned
explicitly, so the tripwire is immune to a future default change).

### Fix (A) in isolation

`mms_adaptec1` already ran preconditioner-ON, so it isolates the escalation change:

| | before | after |
|---|---|---|
| iterations | 1288 | 1259 |
| final HPWL | 6.382e+07 | **6.370e+07** (−0.19%) |
| final overflow | 0.04115 | **0.0405** |

Small, clean, right direction on all three. **(A) stands on its own merits**, independent of the
bigblue3 story.

### Fix (A) is provably a no-op when the preconditioner is off

Running the frozen `mgc_pci_bridge32_b` regress config with `enable_preconditioning = false` on the
**new** binary reproduced the old baseline's final trajectory row bit-for-bit
(`752, 7.196e+08, 6.086e-02, 1.413e+01, 1.426e-02, 0`). This is why (A) alone could never have been
observed on ISPD2005 — `escalation_enabled` was already false there — and it is a fact, not an
inference.

## 4. Results

Post-DP through XPlace's own LG+DP, both sides. Raw data:
`.claude/2_ARTIFACTS/full44_v3_suite_results.tsv` (stage 1),
`.claude/2_ARTIFACTS/lgdp44_v3_results.tsv` (stage 2).

| design | before | after | Δ | ratio before → after |
|---|---|---|---|---|
| adaptec1 | 7.2852e+07 | 7.3179e+07 | +0.45% | 0.9966 → 1.0010 |
| adaptec2 | 8.1880e+07 | 8.1602e+07 | −0.34% | 1.0069 → 1.0035 |
| adaptec3 | 1.9257e+08 | 1.9287e+08 | +0.16% | 0.9934 → 0.9950 |
| adaptec4 | 1.7531e+08 | 1.7519e+08 | −0.07% | 1.0113 → 1.0106 |
| bigblue1 | 8.9478e+07 | 8.9563e+07 | +0.09% | 1.0045 → 1.0054 |
| bigblue2 | 1.3781e+08 | 1.3767e+08 | −0.10% | 1.0062 → 1.0052 |
| **bigblue3** | 3.2007e+08 | **3.0631e+08** | **−4.30%** | 1.0565 → **1.0111** |
| bigblue4 | 7.5229e+08 | 7.4957e+08 | −0.36% | 1.0131 → 1.0095 |
| **mgc_des_perf_1** | 5.5461e+06 | **5.5265e+06** | −0.35% | 0.9847 → **0.9812** |
| mgc_fft_1 | 2.0353e+06 | 2.0400e+06 | +0.23% | 1.0054 → 1.0077 |
| mgc_fft_2 | 1.8593e+06 | 1.8608e+06 | +0.08% | 1.0270 → 1.0278 |
| mgc_fft_a | 3.1361e+06 | 3.1455e+06 | +0.30% | 1.0243 → 1.0273 |
| mgc_fft_b | 4.1639e+06 | 4.1731e+06 | +0.22% | 0.9957 → 0.9979 |
| mgc_matrix_mult_1 | 1.0644e+07 | 1.0614e+07 | −0.29% | 1.0135 → 1.0106 |
| mgc_matrix_mult_2 | 1.0768e+07 | 1.0778e+07 | +0.09% | 1.0063 → 1.0072 |
| **mgc_matrix_mult_a** | 4.6011e+07 | 4.9557e+07 | **+7.71%** | 3.0331 → **3.2669** |
| mgc_superblue12 | 2.7153e+08 | 2.7182e+08 | +0.11% | 1.0561 → 1.0572 |
| mgc_superblue14 | 2.3360e+08 | 2.3544e+08 | +0.79% | 1.0232 → 1.0313 |
| **mgc_superblue19** | 1.6429e+08 | 1.6800e+08 | **+2.26%** | 1.0559 → **1.0797** |

Two recoveries, both from `divergence_guard` to `converged`: **bigblue3** (predicted) and
**mgc_des_perf_1** (not predicted — it now converges *and* beats XPlace by 1.9%).

**The honest shape of this change:** it is a clear win on ISPD2005 and a ~0.4% loss on ISPD2015
once the outlier is set aside. It converts two broken runs into converged ones and costs ≤0.45% on
most healthy designs. It is also the XPlace-faithful behaviour, which is why it was done.

### Controls

- The "before" median reproduces the published 1.0113 exactly.
- All four designs from the pre-suite standalone study reproduced bit-for-bit inside the suite
  (`adaptec1` 7.3179e+07, `adaptec3` 1.9287e+08, `bigblue1` 8.9563e+07, `bigblue3` 3.0631e+08).
- `make test`, `make test-regress`, `make test-regress-slow` all green; three baselines regenerated
  with `--reason`.

⚠️ **A scoring error was made and corrected during this analysis.** The first pass multiplied only
the XPlace reference by `site_width`, producing nonsense ~0.005 ratios on ISPD2015. Both sides come
out of XPlace's own evaluator and are already in the same frame — **no site-width conversion belongs
in a TSV-vs-`benchmarks.py` comparison.** The reproduced 1.0113 "before" median is what confirms the
corrected version.

## 5. `mgc_matrix_mult_a` is a parser bug — see [[#27]]

The design that owns the entire mean is not an algorithm failure. Its
`placement.constraints` is **25 bytes**, one trailing space longer than every other design's:
`maximum_utilization=60% \n`. `readPlacementConstraints` tests `value_str.back() == '%'` to decide
whether to divide by 100; the space defeats it, `std::stof("60% ")` returns **60.0**, and the design
runs at `target_density = 60` — 100× intended. It generates **29,779,040 fillers** for 149,650
movable cells (~199 per cell, against ~144,900 expected), a filler area ~20× the die.

`mgc_fft_b`, `mgc_matrix_mult_b` and `mgc_matrix_mult_c` carry the same `60%` value in 24-byte files
and parse correctly. All 20 ISPD2015 constraint files were checked byte-wise: **exactly one design
is affected.** ISPD2005 and MMS are Bookshelf and have no such file.

This is also why `matrix_mult_a` got *worse* under this change: with 29.8M bogus fillers the density
landscape is meaningless, so a schedule change only reshuffles the failure. **Its response to any
algorithm change is not signal until #27 is fixed.**

## 6. Open

1. **[[#27]]** — the parser fix. Cheapest large win available; would make the suite **mean**
   quotable for the first time (1.0150 over the other 18 today).
2. **`mgc_superblue19` +2.26%** and **`mgc_superblue14` +0.79%** — unexplained, and the superblue
   designs were already the worst-tracking group (`superblue12` 1.057, `19` 1.080).
3. **8 designs still stop on `divergence_guard`**, down from 10. Unexplained, and predates this work.
4. **[[#26]]** — 9 unscored designs (fence regions), and the finding that our own parser discards
   `REGIONS`/`GROUPS`, so we place those designs unconstrained.
5. **[[#25]]** — ISPD2015 `target_density` mismatch, which this re-run does not address and which
   plausibly contributes to the ISPD2015 tier tracking worse than ISPD2005.

## 7. Reproduce

```bash
cd /home/msears/phd/AIEplace/vck5000
$HOME/anaconda3/bin/python ../.claude/2_ARTIFACTS/gen_suite_configs.py \
    --suites ispd2005 ispd2015 --outdir /tmp/full44_v3/configs --results-root /tmp/full44_v3/results
SUITE_CFG=/tmp/full44_v3/configs SUITE_LOG=/tmp/full44_v3/logs \
SUITE_RES=$PWD/../.claude/2_ARTIFACTS/full44_v3_suite_results.tsv \
SUITE_PROG=/tmp/full44_v3/progress.txt bash ../.claude/2_ARTIFACTS/run_suite.sh

LGDP44_GP=/tmp/full44_v3/results LGDP44_OUT=/tmp/lgdp44_v3 \
LGDP44_RES=$PWD/../.claude/2_ARTIFACTS/lgdp44_v3_results.tsv \
    bash ../.claude/2_ARTIFACTS/run_lgdp44.sh
```

Stage 1 ~1 h 40 m, stage 2 ~8 min; both resumable. Pass `SUITE_RES` / `LGDP44_RES` explicitly — the
scripts still default to a path that no longer exists.

Artifacts: `.claude/2_ARTIFACTS/bigblue3_stall_20260811/` holds the before/after placement GIFs, the
two `iterations.dat` traces, and XPlace's own κ trace for bigblue3
(`xplace_bigblue3_kappa_trace_20260811.csv`).
