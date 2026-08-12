# REPORT — `mgc_matrix_mult_a`: one stray space, 3.27× → 1.02 (2026-08-11)

*TODO #27, closed same day. The worst design in the suite was never an algorithm failure.*

## 1. Result

| | before | after |
|---|---|---|
| post-DP HPWL | 4.9557e+07 | **1.5430e+07** |
| ratio vs XPlace (1.5170e+07) | **3.2669** | **1.0171** |
| stop reason | `divergence_guard`, iter 271 | **`converged`**, iter 715 |
| exact overflow at handoff | 0.2338 | **0.1642** |
| fillers | **29,779,040** | **144,904** |
| effective target density | **60** | **0.6** |

Suite effect over the same 19 scored ISPD designs:

| | median | mean | within ±2% |
|---|---|---|---|
| 2026-08-10 baseline | 1.0113 | 1.1218 | 12/19 |
| after the #26 preconditioner fix | 1.0095 | 1.1335 | 13/19 |
| **after this fix** | **1.0095** | **1.0151** | **14/19** |

**The mean is quotable for the first time.** It had been meaningless in every prior snapshot, and
in each one it was this single design carrying it. ISPD2015 alone: median 1.0232 → 1.0171, mean
1.2023 → **1.0223**. `divergence_guard` 8/28 → **7/28**.

## 2. Cause

`host/benchmarks/ispd2015/mgc_matrix_mult_a/placement.constraints` was **25 bytes**:

```
maximum_utilization=60% \n      <- a space between the '%' and the newline
```

Every other ISPD2015 design's file is 24 bytes. `mgc_fft_b`, `mgc_matrix_mult_b` and
`mgc_matrix_mult_c` carry the *same* 60% value and were always fine — the design is not special,
its input file was.

`DataBase::readPlacementConstraints` (`common/src/DataBase.cpp:79`) decides whether to divide by 100
by testing `value_str.back() == '%'`. The space makes that false, so it takes the else branch —
and `std::stof("60% ")` stops at the `%`, reports success, and returns **60.0**. The design ran at
target density 60 instead of 0.6.

`addFillers` budgets whitespace as `target_density * placeable_area - stdcell_area`, so a 100×
target bought a ~205× filler population: **29,779,040 fillers for 149,650 movable cells**, ~199 per
cell, a total filler area of 4.5e+13 against a die of 2.25e+12 — **20× the die**. The density
objective was meaningless from iteration 1 and GP died at 271.

## 3. Why it survived so long

It was never silent. The run had been printing its own diagnosis for weeks:

```
INFO  Read placement constraint: maximum_utilization=6000%
INFO  Fillers: 29779040 at (758, 2e+03), effective target density 60
```

Nobody had cause to re-read an `INFO` line on a design already written off as "the broken one". It
surfaced only because #26's re-score made `matrix_mult_a` the sole obstacle to quoting a mean, which
sent someone to its log for the first time in weeks.

Two things follow that are worth keeping:

- **A design labelled "broken" stops being read.** `matrix_mult_a` appeared as a known outlier in
  three consecutive snapshots (08-07, 08-10, 08-11) and its ratio was quoted each time as evidence
  the mean should be discarded — which is exactly the reasoning that stopped anyone from asking why.
- **Its response to #26 was noise, not signal.** #26 apparently made it 7.71% *worse* (3.0331 →
  3.2669). With 29.8M bogus fillers that number meant nothing; it was reshuffling a broken
  landscape. Any future "design X got worse" needs its inputs checked before its algorithm.

## 4. The fix, and its one weakness

**The trailing space was deleted from the benchmark file.** It is now byte-identical to
`mgc_matrix_mult_b/placement.constraints` (verified with `cmp`). The original is preserved at
`/tmp/matrix_mult_a.constraints.orig` for this session only.

⚠️ **This fix is not tracked and will not survive.** `vck5000/host/benchmarks/.gitignore` line 2
ignores `ispd2015`, so the corrected file is **not in git**, is not shared with Geert's checkout,
and is silently undone by any re-download or fresh clone of the benchmarks. The symptom on a
machine that has not had this repair is a `matrix_mult_a` ratio of ~3.27 and a suite mean near 1.13.

**How to check in one command, on any machine:**

```bash
wc -c vck5000/host/benchmarks/ispd2015/mgc_matrix_mult_a/placement.constraints   # must be 24, not 25
```

A parser-side fix was written and then withdrawn at Mark's direction (2026-08-11) as too much
machinery for the problem — it added a `PlacementConstraints.h` and a tier-1 harness. That call is
recorded here rather than argued: the standing risk is that `readPlacementConstraints` still accepts
any `stof`-parsable prefix, so a differently-malformed contest file will fail the same way, and the
plausibility check (`> 1.0` is impossible by construction) does not exist. If this bites a second
time, the withdrawn approach is in this session's history.

## 5. Verification

- Re-run under the **identical** `full44_v3` suite config, changing only `results_dir`.
- `Fillers: 144904 ... effective target density 0.6` — the predicted count was ~144,900 before the
  re-run, computed from `0.6 × placeable − stdcell` over the filler footprint. Predicting the number
  and then hitting it is what confirms the mechanism rather than merely correlating with it.
- Post-DP through XPlace's own LG+DP, same two-stage method as the suite:
  `Input solution, exact HPWL: 1.464871E+07 exact Overflow: 0.2071` → `After DP, HPWL: 1.543012E+07`.
- All 20 ISPD2015 `placement.constraints` files re-checked byte-wise: **every one is now 24 bytes**
  except `mgc_superblue19`, which is 25 — a trailing *blank line*, not a trailing space, and it
  parses correctly because the read loop `break`s on the first matching line.

⚠️ The v3 TSVs (`full44_v3_suite_results.tsv`, `lgdp44_v3_results.tsv`) still hold the **broken**
`matrix_mult_a` rows. They were not edited: mixing a design re-run on a different input into a
suite TSV would destroy its provenance, which is the same failure mode the 08-10 snapshot was
written to correct. The corrected numbers live in this report; the next full suite run will fold
them in naturally.

## 6. What this does not fix

`matrix_mult_a` at 1.0171 is now unremarkable, but three designs still track badly and are
untouched by this: `mgc_superblue19` (1.0797), `mgc_superblue12` (1.0572), `mgc_superblue14`
(1.0313). Those remain the largest real quality gap in the suite, and the superblue group is now
the whole story of the ISPD2015 tier's 1.0223 mean.
