# REPORT #48: XPlace density scatter + force gather share of runtime, all 44 designs

*2026-10-20. Measurement only; XPlace source untouched (monkeypatched driver), no AIEplace code changed.
Companion to [[REPORT_48_xplace_density_scatter_gather_20261020.md]] (how the two kernels work) and an
extension of [[REPORT_xplace_gpu_iteration_breakdown_20261013.md]] (which profiled a 100-iteration
window of adaptec1 and bigblue4 only).*

## Bottom line

1. **The share is set by the suite, and within a suite by size.** Scatter + gather (S+G) as a share of
   XPlace's GP wall time:

   | suite | n | S+G % of GPU kernel time (median, range) | **S+G % of GP wall (median, range)** | GPU busy % (median) |
   |---|---|---|---|---|
   | ISPD2005 | 8 | 27 % (11-47) | **20 % (5-43)** | 74 % |
   | ISPD2015 | 20 | 10 % (4-23) | **3.6 % (0.9-20)** | 35 % |
   | MMS (mixed-size) | 16 | 65 % (26-82) | **54 % (15-79)** | 86 % |

2. **Outside MMS, the share grows with design size** (Pearson r = 0.84 between log₁₀(nodes) and S+G % of wall,
   28 designs). Small designs are host/launch-bound: GPU busy is 18-50 % below ~300 K nodes, so no kernel matters
   much there. At 1.4 M+ nodes on a 1024² or 2048² grid, S+G is 19-43 % of wall,
   peaking at 42.7 % on bigblue4. mgc_superblue11_a has 1.8 M nodes but a 512² grid, and stays at 10 %, which
   is point 3.
3. **The cost per node rises with the grid, not with node count**: for non-MMS designs the median scatter
   cost is 0.20 / 0.36 / 0.61 ns per node at 512² / 1024² / 2048², and the gather 0.12 / 0.22 / 0.65.
   The work per node is the same at every grid (≈ 5.8 bins after the √2 stretch). Only the size of the map
   being hit at random changes (1 MB float at 512², 33.5 MB uint64 at 2048², against a 5 MB L2). This
   matches the cache-miss explanation in the companion report. 128² and 256² run slightly *higher* per node
   than 512² because those launches are too small to fill the GPU.
4. **MMS is different, and the gather is why.** In 15 of 16 MMS designs the gather costs more than the
   scatter, but in only 1 of 28 non-MMS designs. On MMS adaptec2 the gather takes 65.5 % of GPU time
   and 5.0 ms/iter for a design that costs 0.12 ms/iter as ISPD2005. **Cause, confirmed by trace:** in the mixed-size phase the movable
   macros are scattered and gathered like cells. The deterministic gather runs **one thread per node** over
   its whole footprint, and adaptec2's largest macro covers **150 400 bins**. One thread walks that serially
   and sets the kernel time: **11.9 ms per gather launch in phase 1 vs 0.097 ms in phase 2** (123×), once
   the macros are legalized and dropped (weight −1). Phase-1 gathers are 7.3 s of the run's 10.8 s of
   kernel time.
5. **The scatter is less exposed to the same tail** because it gives each node 4 threads and its atomics do
   not wait for a result. Phase-1 scatter launches are 1.13 ms against 0.064 ms in phase 2 (18×, not 123×).

## Basis (read before quoting any number)

| item | value |
|---|---|
| GPU | RTX 3080 10 GB, WSL2, also the display GPU (same box as the 10-13 report) |
| XPlace | `~/phd/Xplace` as built locally, defaults incl. **`--deterministic True`**, `--seed 42 --num_threads 8` |
| Data | ISPD2005 `--dataset ispd2005`; ISPD2015 `--custom_path` raw files, or `--dataset ispd2015_fix` for the 9 fence designs (same rule as `vck5000/tools/run_xplace_ref_2015.sh`); MMS `--dataset mms --mixed_size True` |
| Span | **The whole of `global_placement_main`**, every iteration from first to GP stop (MMS: both GP phases + the macro-LP between them). LG/DP not run. Iteration counts match the standing reference (`xplace_ref_ispd.tsv`) to ±1 |
| Two passes per design | **wall**: no profiler; denominator = XPlace's own `GP Time` (its iteration-loop timer). **prof**: `torch.profiler`, CUDA activity only, per-kernel GPU time summed over the run |
| Profiler overhead | prof-pass GP time / wall-pass GP time: median 1.05, range 0.98-1.23 (largest on the smallest designs). Kernel times are taken from the prof pass, wall times from the wall pass |
| Scatter | `density_map_cuda_deterministic_forward_kernel` + `copyFromFloatAuxMat` + `copyToFloatAuxMat` (the float↔uint64 round-trip belongs to the deterministic scatter). Includes the evaluator's own overflow scatters |
| Gather | `density_map_cuda_deterministic_backward_kernel` |
| Not counted in either | pre-normalize (`normalize_node_kernel`, 1-3 % of GPU), DCT/FFT transforms, WL kernels, torch glue |
| "GPU busy %" | Σ kernel time (prof) / GP wall (wall pass); one CUDA stream, so the sum is the busy time |
| "% GP wall" | S+G kernel time / GP wall. Because all work is on one stream with host syncs between, this is the time the GPU spends in these kernels out of the iteration loop's wall time; it is **not** a claim that removing them saves exactly that wall time on the host-bound designs |

Cross-check against the 10-13 window measurement: adaptec1 scatter 0.070 vs 0.068 ms/iter, gather 0.043 vs
0.045, wall 2.11 vs 2.09 ms/iter; bigblue4 whole-run S+G 46.7 % of GPU vs 50.3 % in the iteration 200-300
window (whole run includes the cheaper early iterations, where cells are still clumped).

## Per-design results

ns/node = per-iteration kernel time ÷ (movable nodes + fillers). "mov. macros" from XPlace's `#MovMacros`.

| suite | design | grid | nodes (cells+fillers) | mov. macros | iters | GP ms/iter | GPU busy % | scatter ms/iter | gather ms/iter | scatter % GPU | gather % GPU | **S+G % GPU** | **S+G % GP wall** | ns/node scatter | ns/node gather |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ispd2005 | adaptec1 | 512² | 370,971 | 0 | 847 | 2.11 | 47 | 0.0704 | 0.0425 | 7.1 | 4.3 | **11.4** | **5.4** | 0.19 | 0.11 |
| ispd2005 | adaptec2 | 1024² | 595,909 | 0 | 930 | 2.82 | 61 | 0.226 | 0.118 | 13.1 | 6.9 | **20.0** | **12.2** | 0.38 | 0.20 |
| ispd2005 | adaptec3 | 1024² | 1,387,961 | 0 | 922 | 3.86 | 75 | 0.448 | 0.275 | 15.4 | 9.5 | **24.9** | **18.8** | 0.32 | 0.20 |
| ispd2005 | adaptec4 | 1024² | 1,879,617 | 0 | 946 | 4.47 | 80 | 0.669 | 0.45 | 18.7 | 12.6 | **31.2** | **25.1** | 0.36 | 0.24 |
| ispd2005 | bigblue1 | 512² | 635,642 | 0 | 883 | 2.34 | 54 | 0.104 | 0.0766 | 8.2 | 6.0 | **14.2** | **7.7** | 0.16 | 0.12 |
| ispd2005 | bigblue2 | 1024² | 1,473,813 | 0 | 921 | 4.03 | 72 | 0.538 | 0.33 | 18.5 | 11.4 | **29.9** | **21.5** | 0.36 | 0.22 |
| ispd2005 | bigblue3 | 2048² | 2,040,914 | 1 | 1076 | 8.18 | 91 | 1.22 | 1.13 | 16.4 | 15.1 | **31.6** | **28.7** | 0.60 | 0.55 |
| ispd2005 | bigblue4 | 2048² | 5,054,761 | 0 | 1141 | 16.2 | 91 | 3.1 | 3.83 | 20.9 | 25.8 | **46.7** | **42.7** | 0.61 | 0.76 |
| ispd2015 | mgc_des_perf_1 | 128² | 113,173 | 0 | 708 | 1.52 | 28 | 0.0432 | 0.0085 | 10.1 | 2.0 | **12.1** | **3.4** | 0.38 | 0.08 |
| ispd2015 | mgc_des_perf_a | 256² | 108,377 | 0 | 726 | 1.83 | 42 | 0.0293 | 0.0102 | 3.8 | 1.3 | **5.1** | **2.2** | 0.27 | 0.09 |
| ispd2015 | mgc_des_perf_b | 256² | 112,644 | 0 | 742 | 1.48 | 27 | 0.0343 | 0.0131 | 8.6 | 3.3 | **11.9** | **3.2** | 0.30 | 0.12 |
| ispd2015 | mgc_edit_dist_a | 256² | 127,911 | 0 | 745 | 1.67 | 39 | 0.0286 | 0.014 | 4.4 | 2.2 | **6.6** | **2.6** | 0.22 | 0.11 |
| ispd2015 | mgc_fft_1 | 128² | 32,281 | 0 | 642 | 1.65 | 18 | 0.0116 | 0.00466 | 3.9 | 1.6 | **5.5** | **1.0** | 0.36 | 0.14 |
| ispd2015 | mgc_fft_2 | 128² | 42,047 | 0 | 645 | 1.61 | 19 | 0.0184 | 0.00525 | 5.9 | 1.7 | **7.6** | **1.5** | 0.44 | 0.12 |
| ispd2015 | mgc_fft_a | 256² | 61,525 | 0 | 756 | 1.63 | 27 | 0.0223 | 0.0107 | 5.1 | 2.5 | **7.6** | **2.0** | 0.36 | 0.17 |
| ispd2015 | mgc_fft_b | 256² | 66,856 | 0 | 772 | 1.61 | 26 | 0.0226 | 0.0117 | 5.4 | 2.8 | **8.2** | **2.1** | 0.34 | 0.17 |
| ispd2015 | mgc_matrix_mult_1 | 256² | 155,325 | 0 | 720 | 1.45 | 32 | 0.0339 | 0.022 | 7.4 | 4.8 | **12.2** | **3.9** | 0.22 | 0.14 |
| ispd2015 | mgc_matrix_mult_2 | 256² | 157,191 | 0 | 726 | 1.54 | 30 | 0.039 | 0.0224 | 8.4 | 4.8 | **13.2** | **4.0** | 0.25 | 0.14 |
| ispd2015 | mgc_matrix_mult_a | 512² | 216,641 | 0 | 838 | 1.88 | 49 | 0.0493 | 0.0314 | 5.4 | 3.4 | **8.8** | **4.3** | 0.23 | 0.14 |
| ispd2015 | mgc_matrix_mult_b | 512² | 290,865 | 0 | 804 | 1.95 | 50 | 0.057 | 0.043 | 5.8 | 4.4 | **10.2** | **5.1** | 0.20 | 0.15 |
| ispd2015 | mgc_matrix_mult_c | 512² | 290,864 | 0 | 822 | 2.03 | 49 | 0.0579 | 0.0419 | 5.8 | 4.2 | **9.9** | **4.9** | 0.20 | 0.14 |
| ispd2015 | mgc_pci_bridge32_a | 128² | 29,517 | 0 | 732 | 1.78 | 22 | 0.012 | 0.00406 | 3.1 | 1.0 | **4.1** | **0.9** | 0.41 | 0.14 |
| ispd2015 | mgc_pci_bridge32_b | 256² | 29,038 | 0 | 723 | 1.74 | 21 | 0.0179 | 0.0045 | 4.8 | 1.2 | **6.0** | **1.3** | 0.62 | 0.15 |
| ispd2015 | mgc_superblue11_a | 512² | 1,801,184 | 0 | 928 | 5.11 | 80 | 0.302 | 0.231 | 7.4 | 5.6 | **13.0** | **10.4** | 0.17 | 0.13 |
| ispd2015 | mgc_superblue12 | 1024² | 1,927,002 | 0 | 1306 | 5.23 | 86 | 0.624 | 0.424 | 13.8 | 9.4 | **23.2** | **20.0** | 0.32 | 0.22 |
| ispd2015 | mgc_superblue14 | 512² | 615,572 | 0 | 880 | 2.88 | 63 | 0.148 | 0.067 | 8.1 | 3.7 | **11.8** | **7.5** | 0.24 | 0.11 |
| ispd2015 | mgc_superblue16_a | 512² | 792,242 | 0 | 916 | 3.13 | 69 | 0.182 | 0.0958 | 8.4 | 4.4 | **12.8** | **8.9** | 0.23 | 0.12 |
| ispd2015 | mgc_superblue19 | 512² | 513,595 | 0 | 900 | 2.66 | 67 | 0.111 | 0.0545 | 6.2 | 3.1 | **9.3** | **6.2** | 0.22 | 0.11 |
| mms | adaptec1 | 512² | 371,033 | 62 | 1345 | 2.95 | 48 | 0.134 | 0.373 | 9.5 | 26.6 | **36.1** | **17.2** | 0.36 | 1.01 |
| mms | adaptec2 | 1024² | 605,914 | 127 | 1333 | 9.28 | 83 | 1.2 | 5.02 | 15.7 | 65.5 | **81.1** | **67.1** | 1.98 | 8.29 |
| mms | adaptec3 | 1024² | 2,261,951 | 58 | 1555 | 10.3 | 87 | 1.7 | 4.31 | 18.9 | 47.8 | **66.7** | **58.2** | 0.75 | 1.90 |
| mms | adaptec4 | 1024² | 3,079,690 | 69 | 1640 | 7.88 | 85 | 1.51 | 1.76 | 22.6 | 26.4 | **49.0** | **41.6** | 0.49 | 0.57 |
| mms | adaptec5 | 1024² | 1,152,631 | 76 | 2016 | 9.8 | 89 | 1.28 | 4.8 | 14.7 | 55.2 | **69.9** | **62.0** | 1.11 | 4.16 |
| mms | bigblue1 | 512² | 635,672 | 31 | 1441 | 2.64 | 58 | 0.138 | 0.261 | 9.0 | 17.2 | **26.2** | **15.1** | 0.22 | 0.41 |
| mms | bigblue2 | 1024² | 1,621,848 | 959 | 1464 | 8.8 | 42 | 0.737 | 0.657 | 19.9 | 17.7 | **37.6** | **15.8** | 0.45 | 0.40 |
| mms | bigblue3 | 2048² | 2,673,397 | 69 | 1884 | 32.1 | 96 | 4.28 | 21 | 13.9 | 68.1 | **82.0** | **78.7** | 1.60 | 7.85 |
| mms | bigblue4 | 2048² | 5,554,831 | 199 | 2281 | 26.6 | 91 | 4.59 | 11 | 18.9 | 45.5 | **64.5** | **58.8** | 0.83 | 1.99 |
| mms | newblue1 | 512² | 511,861 | 53 | 1366 | 4.4 | 70 | 0.478 | 1.52 | 15.6 | 49.7 | **65.3** | **45.4** | 0.93 | 2.98 |
| mms | newblue2 | 1024² | 943,308 | 25 | 1538 | 7.68 | 86 | 0.724 | 3.79 | 11.0 | 57.6 | **68.7** | **58.7** | 0.77 | 4.01 |
| mms | newblue3 | 2048² | 1,950,362 | 51 | 1498 | 20.8 | 93 | 2.41 | 10.5 | 12.4 | 54.0 | **66.4** | **62.0** | 1.23 | 5.39 |
| mms | newblue4 | 1024² | 848,480 | 81 | 1895 | 4.61 | 73 | 0.412 | 0.851 | 12.2 | 25.3 | **37.5** | **27.4** | 0.49 | 1.00 |
| mms | newblue5 | 1024² | 1,860,758 | 91 | 2011 | 8.88 | 86 | 0.977 | 3.15 | 12.7 | 41.1 | **53.8** | **46.5** | 0.53 | 1.70 |
| mms | newblue6 | 2048² | 3,614,219 | 74 | 1923 | 15.2 | 91 | 2.9 | 4.6 | 20.9 | 33.2 | **54.1** | **49.2** | 0.80 | 1.27 |
| mms | newblue7 | 2048² | 5,833,183 | 161 | 2122 | 29.2 | 93 | 4.74 | 13.2 | 17.4 | 48.5 | **65.9** | **61.3** | 0.81 | 2.26 |

## Trends

### T1. Non-MMS: S+G share tracks design size, because GPU busy tracks size

| (28 non-MMS designs) | Pearson r vs log₁₀(nodes) |
|---|---|
| GPU busy % | 0.97 |
| S+G % of GP wall | 0.84 |
| S+G % of GPU kernel time | 0.82 |

The first row explains most of the second. Below ~300 K nodes XPlace spends most of each 1.5-2 ms iteration
on the host (launches, ~9 stream syncs per iteration, per the 10-13 report). No kernel exceeds a few percent
of wall there: S+G is ≤ 4.0 % of wall on 12 of the 20 ISPD2015 designs, 4.3-5.1 % on matrix_mult_a/b/c, and
above that only on the five superblues (6.2-20 %).
Once the GPU is busy (≥ 1.4 M nodes) S+G reaches 19-43 % of wall on 1024²/2048² grids, but only 10 % on
mgc_superblue11_a (1.8 M nodes, 512²). Size makes the GPU busy, and the grid sets how much of that is S+G (T2). The rest of the GPU time is mostly WL
(12-25 % of GPU on the large ISPD2005 designs) and torch elementwise/reduction glue.

### T2. Per-node cost rises with the grid, at constant per-node work

| non-MMS grid | n | scatter ns/node (median) | gather ns/node (median) | S+G % GPU (median) |
|---|---|---|---|---|
| 128² | 4 | 0.39 | 0.13 | 6.5 |
| 256² | 8 | 0.29 | 0.14 | 7.9 |
| 512² | 9 | 0.20 | 0.12 | 11.4 |
| 1024² | 5 | 0.36 | 0.22 | 24.9 |
| 2048² | 2 | 0.61 | 0.65 | 39.1 |

Every cell and filler smaller than √2 bins is stretched to a ≤ 3×3, ~5.8-bin footprint regardless of
grid (companion report §3a), so the arithmetic per node is constant. From 512² up, the cost per node still
rises 3× (scatter) and 5× (gather). The map grows from 1 MB to 33.5 MB against a 5 MB L2, and the access
stream is random (fillers start uniform over the die; nodes are sorted by area, not position). The rise
below 512² is the opposite effect: 30-150 K-node launches don't fill the GPU, so fixed per-launch cost
dominates. *Inference: L2 hit rates were not measured (no `ncu` on this box).*

The gather overtakes the scatter only at 2048² (bigblue4: 0.76 vs 0.61 ns/node). At smaller grids the
gather is the cheaper kernel even with 1 thread per node. Its per-node loads hit in L2 when the 2-plane
field (2 MB at 512², 8 MB at 1024²) mostly fits, and its cost grows fastest with the map.

### T3. MMS: gather-dominated, through the single-thread macro tail

- In 15/16 MMS designs gather > scatter (vs 1/28 otherwise); gather is 17-68 % of GPU time (median 47 %).
- **Trace evidence, MMS adaptec2** (`trace_mms_adaptec2/`, same driver with a chrome trace, phase boundary =
  the 0.74 s kernel gap of the macro-LP):

  | | phase 1 (macros movable) | phase 2 (macros fixed, weight −1) |
  |---|---|---|
  | gather launches | 616, median **11 919 µs**, max 16 054 | 794, median **97 µs** |
  | scatter launches | 1 232, median 1 133 µs | 1 588, median 64 µs |
  | all kernels | 9 547 ms | 1 283 ms |

  adaptec2's largest node is **150 400 bins** (436 × 345 bins at 1024²); its 127 macros cover 638 759 bins
  (61 % of the die). The deterministic gather (`density_map_cuda_kernel.cu:192-224`) loops one thread over
  all of it, i.e. ~80 ns per bin-iteration on that one thread. The scatter splits each node over 2×2 threads
  and does not wait on its atomics, so its tail is ~10× shorter.
- **Macro size matters, macro count doesn't**: MMS bigblue2 has the most macros (959) but some of the lowest
  MMS gather costs (0.40 ns/node, 17.7 % GPU). Its macros are many and small. bigblue3 (69 macros) and adaptec2
  (127) are the worst. *Largest-macro footprint was checked only for adaptec2; the per-design ranking by
  largest macro is the obvious next check.*
- Because phase 2 behaves like the ISPD2005 runs, the MMS whole-run S+G share is really "phase-1 share ×
  phase-1 fraction of the run". MMS iteration counts are 1.3-2.3 K, and phase 1 is roughly half (adaptec2:
  616 of 1410 gather launches).

### T4. Where the 10-13 window number sits

The 10-13 report's "52 % of bigblue4 kernel time" (iterations 200-300) is a representative mid-run figure.
The whole run gives 46.7 %. The ISPD2005 suite median is 27 % of GPU time and 20 % of wall.

## Implications for `bin_density_CU` (and the force gather)

- **Whether scatter/gather is worth accelerating depends on the suite.** On ISPD2015 it is ≤ 5.1 % of
  XPlace's wall for 15 of 20 designs, so the GPU there is limited by everything except density. On large ISPD2005 it is
  20-43 %, and on MMS up to 79 %. A per-iteration comparison of the VCK against XPlace will look very
  different depending on which suite it quotes.
- **Grid size is what makes it expensive on the GPU, through cache misses.** An on-chip URAM grid has
  uniform access cost, so the GPU's 1024²→2048² per-node slowdown has no counterpart on the PL. Our cost
  per node at a fixed lane count is flat in grid size.
- **Large movable macros need their own path in the gather too, not only the scatter.** XPlace's MMS
  numbers come from one thread walking a 150 K-bin macro. In a 1-node-per-cycle banked CU the same macro is
  ~9 400 cycles at 16 bins/cycle (≈ 31 µs at 300 MHz). That is negligible if it streams through the banks,
  but it stalls the cell pipe if it is handled inline. Plan the macro path once and use it for both scatter
  and gather.
- **The gather deserves the same attention as the scatter.** On MMS it is the larger of the two, and at
  2048² on ISPD2005 too. The 4×4 banked layout serves read-only gathers without forwarding (companion report §6).

## Reproduce

Scripts and raw data in `.claude/2_ARTIFACTS/xplace_profile_suite/` (untracked, 171 MB):

```bash
cd ~/phd/AIEplace/.claude/2_ARTIFACTS/xplace_profile_suite
./run_suite.sh                          # 44 designs x {wall, prof}, ~40 min on this box; resumable
./run_suite.sh mms:adaptec2             # one design (skips if prof.json exists)
python3 collate.py                      # -> results.csv, results.md
~/anaconda3/bin/python summarize.py     # -> the tables + trend statistics above (needs py >= 3.10)
```

`profile_full_gp.py` wraps `global_placement_main`; set `PROF_TRACE=1` for a chrome trace
(how `trace_mms_adaptec2/` was made). Per design: `<suite>/<design>/{wall,prof}.json` (prof.json holds every
kernel's total µs and launch count) and `{wall,prof}.log` (XPlace's log).
