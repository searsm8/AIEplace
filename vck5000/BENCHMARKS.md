# Benchmark manifest

Auto-generated from `tools/benchmarks.py` (`python3 tools/benchmarks.py --write-md`).
The master list of designs we work on: present in the XPlace dataset, tuned in XPlace's `setup_dataset.py`, and present locally under `host/benchmarks/`. Grid / target-density are XPlace's own per-design tuned values.

## Tier 1 — ISPD2005 (fixed macros; frame matches sw_only → honest XPlace ratio; the anchor set)

| design | suite | XPlace grid | target density |
|---|---|---|---|
| adaptec1 | ispd2005 | 512 | 1 |
| adaptec2 | ispd2005 | 1024 | 1 |
| adaptec3 | ispd2005 | 1024 | 1 |
| adaptec4 | ispd2005 | 1024 | 1 |
| bigblue1 | ispd2005 | 512 | 1 |
| bigblue2 | ispd2005 | 1024 | 1 |
| bigblue3 | ispd2005 | 2048 | 1 |
| bigblue4 | ispd2005 | 2048 | 1 |

## Tier 2 — ISPD2015 mgc_* (site-width frame; XPlace HPWL needs ×site_width conversion)

| design | suite | XPlace grid | target density |
|---|---|---|---|
| mgc_des_perf_1 | ispd2015 | 512 | 0.91 |
| mgc_des_perf_a | ispd2015 | 512 | 0.429 |
| mgc_des_perf_b | ispd2015 | 512 | 0.497 |
| mgc_edit_dist_a | ispd2015 | 512 | 0.455 |
| mgc_fft_1 | ispd2015 | 512 | 0.835 |
| mgc_fft_2 | ispd2015 | 512 | 0.65 |
| mgc_fft_a | ispd2015 | 512 | 0.5 |
| mgc_fft_b | ispd2015 | 512 | 0.6 |
| mgc_matrix_mult_1 | ispd2015 | 512 | 0.802 |
| mgc_matrix_mult_2 | ispd2015 | 512 | 0.8 |
| mgc_matrix_mult_a | ispd2015 | 512 | 0.6 |
| mgc_matrix_mult_b | ispd2015 | 512 | 0.6 |
| mgc_matrix_mult_c | ispd2015 | 512 | 0.6 |
| mgc_pci_bridge32_a | ispd2015 | 512 | 0.384 |
| mgc_pci_bridge32_b | ispd2015 | 512 | 0.143 |
| mgc_superblue11_a | ispd2015 | 512 | 0.65 |
| mgc_superblue12 | ispd2015 | 1024 | 0.65 |
| mgc_superblue14 | ispd2015 | 512 | 0.56 |
| mgc_superblue16_a | ispd2015 | 512 | 0.55 |
| mgc_superblue19 | ispd2015 | 512 | 0.53 |

## Tier 3 — MMS (mixed-size / movable macros; separate regime, preconditioner matters)

Two XPlace reference points, both from the same 2026-07-17 local runs:

- **Mixed-GP** = phase 1, macros movable — NOT the `GP Stop!` line. Its overflow EXCLUDES both fillers and movable macros (XPlace's zero_macro_grad at this checkpoint); compare against sw_only's macro-excluded overflow, not "Final Overflow (exact, +fillers)". See `_XPLACE_MMS_MIXED_GP`.
- **post-GP / post-LG / post-DP** = the end of the flow (phase 2, then legalization, then detailed placement). **post-DP HPWL is the headline quality metric** — legalization costs 1-8% HPWL and an under-spread GP pays more of it, so a GP-vs-GP comparison flatters whichever placer spread less. See `_XPLACE_MMS_FINAL`.

Both HPWL columns are **unmasked (all nets)** — XPlace's `get_obj_hpwl` calls `hpwl_cuda.hpwl` with no `net_mask`; only the per-iteration `masked_hpwl:` line (including the one inside `GP Stop!`) is masked. Compare against sw_only's "Final HPWL (exact, all nets)", not "Final HPWL".

| design | suite | XPlace grid | target density | XPlace Mixed-GP HPWL | XPlace Mixed-GP overflow | XPlace post-GP HPWL | XPlace post-LG HPWL | XPlace post-DP HPWL |
|---|---|---|---|---|---|---|---|---|
| adaptec1 | mms | 512 | 1 | 6.238e+07 | 0.1306 | 6.457e+07 | 7.013e+07 | 6.814e+07 |
| adaptec2 | mms | 1024 | 1 | 7.129e+07 | 0.0963 | 7.270e+07 | 7.774e+07 | 7.618e+07 |
| adaptec3 | mms | 1024 | 1 | 1.535e+08 | 0.1247 | 1.544e+08 | 1.614e+08 | 1.591e+08 |
| adaptec4 | mms | 1024 | 1 | 1.363e+08 | 0.1352 | 1.370e+08 | 1.437e+08 | 1.414e+08 |
| adaptec5 | mms | 1024 | 0.5 | 3.035e+08 | 0.1485 | 3.098e+08 | 3.147e+08 | 3.131e+08 |
| bigblue1 | mms | 512 | 1 | 8.295e+07 | 0.1741 | 8.333e+07 | 8.651e+07 | 8.567e+07 |
| bigblue2 | mms | 1024 | 1 | 1.212e+08 | 0.1052 | 1.217e+08 | 1.270e+08 | 1.257e+08 |
| bigblue3 | mms | 2048 | 1 | 2.706e+08 | 0.1235 | 2.628e+08 | 2.830e+08 | 2.767e+08 |
| bigblue4 | mms | 2048 | 1 | 6.241e+08 | 0.1295 | 6.271e+08 | 6.536e+08 | 6.464e+08 |
| newblue1 | mms | 512 | 0.8 | 5.946e+07 | 0.1361 | 5.837e+07 | 6.089e+07 | 6.005e+07 |
| newblue2 | mms | 1024 | 0.9 | 1.516e+08 | 0.1426 | 1.487e+08 | 1.538e+08 | 1.524e+08 |
| newblue3 | mms | 2048 | 0.8 | 2.828e+08 | 0.0400 | 2.692e+08 | 2.736e+08 | 2.727e+08 |
| newblue4 | mms | 1024 | 0.5 | 2.238e+08 | 0.1818 | 2.299e+08 | 2.322e+08 | 2.298e+08 |
| newblue5 | mms | 1024 | 0.5 | 3.792e+08 | 0.1697 | 3.846e+08 | 3.923e+08 | 3.899e+08 |
| newblue6 | mms | 2048 | 0.8 | 4.029e+08 | 0.1419 | 4.032e+08 | 4.107e+08 | 4.083e+08 |
| newblue7 | mms | 2048 | 0.8 | 8.638e+08 | 0.1517 | 8.657e+08 | 8.856e+08 | 8.803e+08 |

**Tier 1 + Tier 2 = 28 designs = the XPlace-paper suite (= dse.py `_full_suite`).**  Tier 3 = 16 mixed-size designs.

**Out of scope** (do not add): ISPD2019 (local only, no XPlace data); classic superblue1–18 (XPlace-tuned but no data present).

## Screening picks (sensitivity analysis)
- `ispd2015/mgc_fft_a` — fast default (~60s/run)
- `ispd2005/adaptec1` — Tier 1 clean XPlace ratio anchor (slow, ~8–15 min/run)
- `ispd2015/mgc_des_perf_1` — convergence-control stress (stalling std-cell design)

