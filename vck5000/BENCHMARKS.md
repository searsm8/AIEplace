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

| design | suite | XPlace grid | target density |
|---|---|---|---|
| adaptec1 | mms | 512 | 1 |
| adaptec2 | mms | 1024 | 1 |
| adaptec3 | mms | 1024 | 1 |
| adaptec4 | mms | 1024 | 1 |
| adaptec5 | mms | 1024 | 0.5 |
| bigblue1 | mms | 512 | 1 |
| bigblue2 | mms | 1024 | 1 |
| bigblue3 | mms | 2048 | 1 |
| bigblue4 | mms | 2048 | 1 |
| newblue1 | mms | 512 | 0.8 |
| newblue2 | mms | 1024 | 0.9 |
| newblue3 | mms | 2048 | 0.8 |
| newblue4 | mms | 1024 | 0.5 |
| newblue5 | mms | 1024 | 0.5 |
| newblue6 | mms | 2048 | 0.8 |
| newblue7 | mms | 2048 | 0.8 |

**Tier 1 + Tier 2 = 28 designs = the XPlace-paper suite (= dse.py `_full_suite`).**  Tier 3 = 16 mixed-size designs.

**Out of scope** (do not add): ISPD2019 (local only, no XPlace data); classic superblue1–18 (XPlace-tuned but no data present).

## Screening picks (sensitivity analysis)
- `ispd2015/mgc_fft_a` — fast default (~60s/run)
- `ispd2005/adaptec1` — Tier 1 clean XPlace ratio anchor (slow, ~8–15 min/run)
- `ispd2015/mgc_des_perf_1` — convergence-control stress (stalling std-cell design)

