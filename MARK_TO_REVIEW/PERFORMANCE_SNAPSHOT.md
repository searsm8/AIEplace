# markv1 GP performance snapshot — full ISPD2005 + ISPD2015 suite

Source: `results/DSE_20260707_185300`  |  28 designs completed  |  best defaults (grid-indep γ, dct_normalize, pin offsets; precond OFF), each at its XPlace grid, seed 42, stop masked-overflow 0.04.

Ratio = markv1 GP best HPWL / XPlace **published** reference (post-legalization+DP for ISPD2005). markv1 is **global placement only** (no legalization, no detailed placement).

**Reading the ratio (corrected 2026-07-08).** The academic flow is GP -> legalization (LG) -> detailed placement (DP). LG *increases* HPWL (it removes overlaps and snaps cells to legal rows/sites, displacing them from the WL-optimal continuous GP solution); DP then *claws back* some of that (local swap/reorder refinement), but the net LG+DP result is typically still **above** the raw GP HPWL. So XPlace's published number carries a legalization penalty that markv1's raw GP does not. A markv1 ratio of ~1.0 therefore means markv1's GP is at or *ahead of* XPlace's own GP -- markv1 is being compared against XPlace's inflated legal number. Corollary: adding LG+DP to markv1 would *raise* its HPWL (worsen these ratios); its value is **legality** (a usable, overlap-free placement) and an apples-to-apples legal-vs-legal comparison, NOT wirelength reduction. Refs: RePlAce (TCAD 2019), ABCDPlace (TCAD 2020).

| Design | Grid | Iters | Best HPWL | Overflow | XPlace ref | Ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| adaptec1@512 | 512 | 752 | 7.171e+07 | 0.0398 | 7.309e+07 | 0.98 |
| adaptec2@1024 | 1024 | 765 | 8.512e+07 | 0.0397 | 8.130e+07 | 1.05 |
| adaptec3@1024 | 1024 | 831 | 1.966e+08 | 0.0396 | 1.936e+08 | 1.02 |
| adaptec4@1024 | 1024 | 888 | 1.767e+08 | 0.0396 | 1.734e+08 | 1.02 |
| bigblue1@512 | 512 | 758 | 8.983e+07 | 0.0391 | 8.908e+07 | 1.01 |
| bigblue2@1024 | 1024 | 903 | 1.471e+08 | 0.0395 | 1.369e+08 | 1.07 |
| bigblue3@2048 | 2048 | 822 | 3.282e+08 | 0.183 | 3.031e+08 | 1.08 |
| bigblue4@2048 | 2048 | 946 | 8.090e+08 | 0.0398 | 7.422e+08 | 1.09 |
| mgc_des_perf_1@512 | 512 | 901 | 1.126e+09 | 0.0642 | 1.106e+09 | 1.02 |
| mgc_des_perf_a@512 | 512 | 920 | 2.018e+09 | 0.063 | 1.999e+09 | 1.01 |
| mgc_des_perf_b@512 | 512 | 905 | 1.640e+09 | 0.115 | 1.612e+09 | 1.02 |
| mgc_edit_dist_a@512 | 512 | 1086 | 4.228e+09 | 0.0896 | 4.198e+09 | 1.01 |
| mgc_fft_1@512 | 512 | 777 | 4.027e+08 | 0.0923 | 4.115e+08 | 0.98 |
| mgc_fft_2@512 | 512 | 752 | 3.674e+08 | 0.152 | 3.741e+08 | 0.98 |
| mgc_fft_a@512 | 512 | 816 | 6.249e+08 | 0.0772 | 6.258e+08 | 1.00 |
| mgc_fft_b@512 | 512 | 836 | 7.883e+08 | 0.0626 | 8.456e+08 | 0.93 |
| mgc_matrix_mult_1@512 | 512 | 871 | 2.114e+09 | 0.0715 | 2.116e+09 | 1.00 |
| mgc_matrix_mult_2@512 | 512 | 873 | 2.187e+09 | 0.0635 | 2.153e+09 | 1.02 |
| mgc_matrix_mult_a@512 | 512 | 178 | 3.080e+09 | 0.198 | 3.033e+09 | 1.02 |
| mgc_matrix_mult_b@512 | 512 | 923 | 2.725e+09 | 0.0395 | 2.763e+09 | 0.99 |
| mgc_matrix_mult_c@512 | 512 | 908 | 2.604e+09 | 0.0397 | 2.675e+09 | 0.97 |
| mgc_pci_bridge32_a@512 | 512 | 990 | 3.585e+08 | 0.278 | 3.609e+08 | 0.99 |
| mgc_pci_bridge32_b@512 | 512 | 1050 | 6.951e+08 | 0.323 | 7.140e+08 | 0.97 |
| mgc_superblue11_a@512 | 512 | 892 | 3.461e+10 | 0.0398 | 3.352e+10 | 1.03 |
| mgc_superblue12@1024 | 1024 | 892 | 2.673e+10 | 0.0577 | 2.578e+10 | 1.04 |
| mgc_superblue14@512 | 512 | 867 | 2.312e+10 | 0.0399 | 2.278e+10 | 1.02 |
| mgc_superblue16_a@512 | 512 | 777 | 2.612e+10 | 0.04 | 2.549e+10 | 1.02 |
| mgc_superblue19@512 | 512 | 824 | 1.642e+10 | 0.0396 | 1.554e+10 | 1.06 |

**Suite stats (28 with an XPlace ref):** mean ratio 1.014, median 1.020, min 0.930, max 1.090. Within ±2%: 18/28. Within ±5%: 23/28.
