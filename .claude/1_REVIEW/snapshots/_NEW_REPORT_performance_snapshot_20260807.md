# sw_only performance snapshot — full 44-design suite, legal-vs-legal

**Date:** 2026-08-07 · **Commit:** `11d8901` + the TODO #19 toggle retirement (branch `pl_algo`)
**Variant:** `HOST=sw_only` · seed 42, `deterministic = true`, XPlace's own per-design grid and
target density (`tools/benchmarks.py::_ROWS`).

| | |
|---|---|
| designs in the manifest | **44** (8 ISPD2005 + 20 ISPD2015 + 16 MMS) |
| **scored** (we have a placement *and* an XPlace reference) | **33** |
| **median ratio** (ours / XPlace, post-DP) | **1.0090** |
| mean, excluding the one broken design | **1.010** |
| within ±2% / ±5% | **25/33 · 30/33** |
| better than XPlace | **7** |

**Status: sw_only is "close enough" to XPlace and closed for now.** The run also surfaced two
real defects that nothing else was covering — see §5.

---

## 1. What is being compared

**Post-DP HPWL vs XPlace's post-DP HPWL** — legal against legal, both legalized and
detail-placed by *XPlace's own* LG+DP (`--global_placement False --given_solution`), so the back
end is identical and only the global placement differs. Both sides are the **exact, unmasked**
HPWL. Data: `2_ARTIFACTS/{full44,faithful}_suite_results.tsv` (GP) and
`2_ARTIFACTS/lgdp{44,_faithful}_results.tsv` (LG/DP); references in
`tools/benchmarks.py::_XPLACE_{ISPD,MMS}_FINAL`.

### ⚠️ The frame rule — the one thing to get right
XPlace divides ISPD2015 HPWL by `site_width` (`database.py:602`), so tier 2 lives in *site units*
while sw_only reports raw DBU. The two comparisons therefore differ:

- **post-GP** — ours is raw DBU, the reference is site units ⇒ convert the reference up
  (`benchmarks.xplace_hpwl_in_sw_frame`).
- **post-DP** — ours comes out of *XPlace's own log* (it legalized our placement), so it is
  **already site units**, same as the reference ⇒ **no conversion**. Converting here would
  inflate every tier-2 ratio by site_width.

**`site_width` is not uniform: 200 for the 15 `mgc_*`, 100 for the 5 `mgc_superblue*`.** A blanket
×200 — which every earlier note in the repo implied — is wrong by 2× on the superblues. Now
recorded per design in `benchmarks.py::SITE_WIDTH`; never hardcode it.

Verified end to end on `mgc_fft_a`: XPlace reads our handed-over placement as `3.125611E+06`
site units; ×200 = 6.2512e8 against our own 6.251e8 — the transfer and the factor are both exact.

## 2. Results by tier

| tier | suite | n scored | mean | median | min | max |
|---|---|---:|---:|---:|---:|---:|
| 1 | ISPD2005 | 8 | 1.0111 | 1.0065 | 0.993 | 1.057 |
| 2 | ISPD2015 | 9 | 1.2947 | 1.0113 | 0.984 | 3.543 |
| 3 | MMS (mixed-size) | 16 | 1.0074 | 1.0093 | 0.973 | 1.032 |
| — | **all** | **33** | 1.0866 | **1.0090** | 0.973 | 3.543 |

**Quote the median, not the mean.** Tier 2's mean and the overall mean are both dominated by a
single broken design (`mgc_matrix_mult_a`, 3.54 — §5.2). Without it the overall mean is **1.010**
and tier 2's is **1.014**.

MMS is the strongest tier, which is the expected payoff of the two-phase flow and TODO #19.

## 3. Full table

Ratio = our post-DP / XPlace post-DP. `-` in the ratio column means no comparison is possible;
the reason is in §4.

<!-- BEGIN TABLE (regenerate: python3 2_ARTIFACTS/analyze_full44.py --md) -->

| design | tier | td | grid | iters | stop | our post-DP | XPlace post-DP | ratio |
|---|---:|---:|---:|---:|---|---:|---:|---:|
| adaptec1 | 1 | 1 | 512 | 757 | converged | 7.2852e+07 | 7.3103e+07 | **0.9966** |
| adaptec2 | 1 | 1 | 1024 | 820 | converged | 8.1880e+07 | 8.1318e+07 | 1.0069 |
| adaptec3 | 1 | 1 | 1024 | 797 | converged | 1.9257e+08 | 1.9385e+08 | **0.9934** |
| adaptec4 | 1 | 1 | 1024 | 884 | converged | 1.7531e+08 | 1.7335e+08 | 1.0113 |
| bigblue1 | 1 | 1 | 512 | 817 | converged | 8.9478e+07 | 8.9081e+07 | 1.0045 |
| bigblue2 | 1 | 1 | 1024 | 849 | converged | 1.3781e+08 | 1.3697e+08 | 1.0062 |
| bigblue3 | 1 | 1 | 2048 | 678 | divergence_guard | 3.2007e+08 | 3.0294e+08 | 1.0565 |
| bigblue4 | 1 | 1 | 2048 | 900 | converged | 7.5229e+08 | 7.4255e+08 | 1.0131 |
| mgc_des_perf_1 | 2 | 0.91 | 512 | 893 | divergence_guard | 5.5450e+06 | 5.6326e+06 | **0.9845** |
| mgc_des_perf_a | 2 | 0.429 | 512 | 792 | divergence_guard | — | no ref | — |
| mgc_des_perf_b | 2 | 0.497 | 512 | 2173 | **nan_metrics** | — | no ref | — |
| mgc_edit_dist_a | 2 | 0.455 | 512 | 1371 | diverged_hpwl | — | no ref | — |
| mgc_fft_1 | 2 | 0.835 | 512 | 784 | divergence_guard | 2.0288e+06 | 2.0244e+06 | 1.0022 |
| mgc_fft_2 | 2 | 0.65 | 512 | 763 | divergence_guard | 1.8522e+06 | 1.8104e+06 | 1.0231 |
| mgc_fft_a | 2 | 0.5 | 512 | 832 | divergence_guard | 3.1419e+06 | 3.0618e+06 | 1.0262 |
| mgc_fft_b | 2 | 0.6 | 512 | 813 | converged | 4.1741e+06 | 4.1819e+06 | **0.9981** |
| mgc_matrix_mult_1 | 2 | 0.802 | 512 | 820 | converged | 1.0621e+07 | 1.0502e+07 | 1.0113 |
| mgc_matrix_mult_2 | 2 | 0.8 | 512 | 837 | converged | 1.0792e+07 | 1.0700e+07 | 1.0086 |
| mgc_matrix_mult_a | 2 | 0.6 | 512 | **176** | divergence_guard | 5.3740e+07 | 1.5170e+07 | **3.5426** |
| mgc_matrix_mult_b | 2 | 0.6 | 512 | 901 | converged | — | no ref | — |
| mgc_matrix_mult_c | 2 | 0.6 | 512 | 896 | converged | — | no ref | — |
| mgc_pci_bridge32_a | 2 | 0.384 | 512 | 743 | divergence_guard | — | no ref | — |
| mgc_pci_bridge32_b | 2 | 0.143 | 512 | 744 | divergence_guard | — | 3.4771e+06 | — |
| mgc_superblue11_a | 2 | 0.65 | 512 | 2135 | **nan_metrics** | — | no ref | — |
| mgc_superblue12 | 2 | 0.65 | 1024 | 2153 | **nan_metrics** | *(crashed)* | 2.5710e+08 | *excl.* |
| mgc_superblue14 | 2 | 0.56 | 512 | 2133 | **nan_metrics** | *(crashed)* | 2.2830e+08 | *excl.* |
| mgc_superblue16_a | 2 | 0.55 | 512 | 2133 | **nan_metrics** | — | no ref | — |
| mgc_superblue19 | 2 | 0.53 | 512 | 783 | converged | 1.6431e+08 | 1.5560e+08 | 1.0560 |
| adaptec1 | 3 | 1 | 512 | 1325 | converged | 6.7820e+07 | 6.8136e+07 | **0.9954** |
| adaptec2 | 3 | 1 | 1024 | 1377 | converged | 7.6278e+07 | 7.6177e+07 | 1.0013 |
| adaptec3 | 3 | 1 | 1024 | 1374 | converged | 1.5481e+08 | 1.5909e+08 | **0.9731** |
| adaptec4 | 3 | 1 | 1024 | 1409 | converged | 1.4264e+08 | 1.4136e+08 | 1.0090 |
| adaptec5 | 3 | 0.5 | 1024 | 1434 | converged | 3.2299e+08 | 3.1307e+08 | 1.0317 |
| bigblue1 | 3 | 1 | 512 | 1387 | converged | 8.5708e+07 | 8.5673e+07 | 1.0004 |
| bigblue2 | 3 | 1 | 1024 | 1593 | converged | 1.2518e+08 | 1.2566e+08 | **0.9962** |
| bigblue3 | 3 | 1 | 2048 | 1096 | converged | 2.8333e+08 | 2.7673e+08 | 1.0239 |
| bigblue4 | 3 | 1 | 2048 | 1454 | converged | 6.5704e+08 | 6.4644e+08 | 1.0164 |
| newblue1 | 3 | 0.8 | 512 | 1427 | converged | 6.0714e+07 | 6.0047e+07 | 1.0111 |
| newblue2 | 3 | 0.9 | 1024 | 1001 | converged | 1.5261e+08 | 1.5239e+08 | 1.0015 |
| newblue3 | 3 | 0.8 | 2048 | 782 | converged | 2.7525e+08 | 2.7265e+08 | 1.0095 |
| newblue4 | 3 | 0.5 | 1024 | 1604 | divergence_guard | 2.3290e+08 | 2.2985e+08 | 1.0133 |
| newblue5 | 3 | 0.5 | 1024 | 1497 | converged | 3.9501e+08 | 3.8989e+08 | 1.0131 |
| newblue6 | 3 | 0.8 | 2048 | 1561 | converged | 4.1371e+08 | 4.0834e+08 | 1.0132 |
| newblue7 | 3 | 0.8 | 2048 | 1499 | converged | 8.8819e+08 | 8.8032e+08 | 1.0089 |

<!-- END TABLE -->

Tiers 1 and 3 share design *names* (adaptec1–4, bigblue1–4) but are **different designs** —
tier 1 has fixed macros, tier 3 has movable ones. They are never interchangeable, and keying
anything on the bare name is a bug (one that was live in two places until 2026-08-07).

**Post-DP density** is at parity or better on all 8 MMS designs where it is measurable
(`td < 1.0`); at `td = 1.0` post-DP overflow is zero by construction and says nothing. Best case
newblue1 −23.5%, worst +0.7%. **The wirelength was not bought with density.**

## 4. Why 11 designs are not scored

| reason | n | designs |
|---|---:|---|
| **No XPlace reference** — fence regions (TODO #22) | 8 | des_perf_a, des_perf_b, edit_dist_a, matrix_mult_b, matrix_mult_c, pci_bridge32_a, superblue11_a, superblue16_a |
| **Our GP crashed**, no placement to score (TODO #23) | 2 | superblue12, superblue14 |
| Reference exists but XPlace cannot legalize *our* input (fence region) | 1 | pci_bridge32_b |

`--dataset ispd2015` is silently rewritten to `ispd2015_fix` by `Xplace/main.py:94-96` ("We
haven't yet support fence region"), and we hold one design in that variant. 12 of 20 were
recovered with `--custom_path`, which bypasses the rewrite — but **only** for designs with no
fence regions, because forcing the rest through a guard XPlace put there deliberately would
produce a number it cannot compute correctly. TODO #22 records the fix.

**The two crashed designs are excluded, not hidden:** scoring them would read 27.4× and 10.6×,
because the "placement" is the untouched random initial one.

## 5. What the run exposed — the real value of going to 44

Neither defect is visible from MMS or from `make test-regress`; both live in ISPD2015, which had
no current coverage at all.

### 5.1 Five designs never move a cell — `init_step_seed` underflow (TODO #23)
`mgc_superblue{11_a,12,14,16_a}` and `mgc_des_perf_b` stop `nan_metrics` at ~2133 iterations with
`step_length = 0` **from iteration 1**. `estimateInitialStep()` probes with `init_step_seed = 0.01`,
that displacement rounds to zero in float32 against these designs' coordinate scale, so
α = ‖Δpos‖/‖Δgrad‖ = 0 — and a zero step is self-sustaining. λ then ramps unopposed to 1e29 and
NaNs. Confirmed by probe: seed 0.01 → step 0; seed 1.0 → step 189647 and it spreads immediately.
**Not simply "the biggest designs"** — `des_perf_b` is 100× smaller than superblue and fails
identically.

### 5.2 `mgc_matrix_mult_a` quits at iteration 176 — and legalization exposes it
Its GP HPWL (3.113e9) is within 1% of the July 8 snapshot's 3.080e9, so **GP has not regressed**.
But it exits on the divergence guard at 176 iterations (peers take ~820) leaving a nearly
unspread placement — XPlace reads our handover at **exact overflow 0.9428** — and legalization
then costs **3.45×**. This is precisely the failure mode a GP-vs-legal comparison cannot see, and
July 8 scored this design 1.02.

## 6. Comparison to `PERFORMANCE_SNAPSHOT_July8.md`

### ⚠️ The two are not the same measurement. Do not subtract the headline numbers.

| | July 8 | today |
|---|---|---|
| designs | 28 (ISPD2005 + ISPD2015) | **44** (+ the 16 MMS mixed-size) |
| our side | **global placement only** | **post-GP → LG → DP**, legal |
| their side | XPlace **published** post-LG+DP | XPlace's **own local runs**, post-DP |
| basis | **GP vs legal — apples-to-oranges** | **legal vs legal** |
| headline | mean 1.014 (28) | median **1.0090** (33 scored) |

The July 8 snapshot says so itself: our raw GP was compared against a number carrying a
legalization penalty we had not paid, so *"a ratio of ~1.0 means markv1's GP is at or ahead of
XPlace's own GP"*, and *"adding LG+DP would raise its HPWL (worsen these ratios)."* That
prediction was correct, and paying the penalty is most of what the intervening month bought.

`mgc_matrix_mult_a` is the clean demonstration: unchanged GP, 1.02 under the old basis, 3.54
under this one.

**So the honest reading:** the July 8 figure was flattered by an unpaid legalization penalty on an
easier suite; today's has paid it, adds the harder mixed-size tier, and still lands at a median of
1.009 — with two genuine defects now visible that the old basis could not have shown.

### What changed in between — 86 commits
1. **Mixed-size phase 2** (TODO #13): LP macro legalization (CBC) + fixed-macro std-cell restart.
   XPlace's headline is post-phase-2; before this we could only match its phase-1 endpoint.
2. **XPlace-faithful filler sizing** (TODO #13 P1) — the macro-heavy designs had been running with
   *zero* fillers.
3. **Movable macros deposit at `target_density`** (TODO #11b) — removes a permanent overflow term
   that starved λ's feedback loop.
4. **TODO #19**, this week — every XPlace overflow metric excludes fillers, and the γ/λ throttle
   gates on the preconditioner ratio κ, not a gradient ratio. On MMS: post-DP +1.15% → +0.74%,
   converging runs 6/16 → 15/16.
5. Faithful field + preconditioner (TODO #2); OpenMP threading (TODO #12); `make test-regress`
   (TODO #17); the reference tables, `post_dp_density.py` and the LG/DP harnesses that make any of
   this measurable.

## 7. What sw_only does NOT do
- **No routability** — HPWL and density only.
- **No legalizer or detailed placer of its own.** Every legal number here is XPlace's LG+DP on our
  GP output. Right for apples-to-apples, but we do not own the back end.
- **No per-row site model** — `enforceDieBoundaries` clamps to the die *rectangle*.
- **`macro_legalization_xy`/`_ilp` and site/row alignment are not ported** (TODO #13).

## 8. Reproducing

```bash
cd vck5000 && make test-regress          # tripwire, ~12 s
cd vck5000 && make test-regress-slow     # + the mixed-size design, ~3 min
```

Full baseline (~2 h GP + ~15 min LG/DP for the 28; MMS is a separate ~5 h arm):

```bash
python3 2_ARTIFACTS/gen_suite_configs.py --suites ispd2005 ispd2015 \
    --outdir /tmp/full44/configs --results-root /tmp/full44/results
setsid nohup bash 2_ARTIFACTS/run_suite.sh > /tmp/full44/runner.log 2>&1 < /dev/null &
setsid nohup bash 2_ARTIFACTS/run_lgdp44.sh > /tmp/lgdp44/runner.log 2>&1 < /dev/null &
python3 2_ARTIFACTS/analyze_full44.py --md
```

XPlace references (only needed if the reference set changes):
`2_ARTIFACTS/run_xplace_ref.sh` (ispd2005) and `run_xplace_ref_2015.sh` (ispd2015, `--custom_path`).

**Gotchas that cost time here, so they do not cost it again:**
- Run `gen_*_inputs.py` to **completion in the foreground**; backgrounding a `gen && run` chain
  kills generation partway and silently produces a subset.
- The suite is **sequential on purpose** — the placer is already OpenMP-threaded across all cores,
  so concurrency just splits them and hits the master-spin cliff.
- `tools/def_patch_placement.py` must accept **scientific notation**: sw_only writes large
  coordinates as `1.26917e+06`, and a naive numeric regex silently dropped 63% of `superblue19`'s
  components, leaving them for XPlace to place. It now hard-errors instead of warning.

Before quoting any number here against XPlace, read `.claude/skills/xplace-compare` — there are
four independent ways to pick the wrong reference and each has produced a figure that later needed
retracting.
