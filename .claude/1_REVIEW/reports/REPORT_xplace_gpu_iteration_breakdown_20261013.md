# XPlace GPU: per-iteration time breakdown (adaptec1, bigblue4)

Measurement only. No change to AIEplace or XPlace source (the driver monkeypatches XPlace at run time).
Context: supplies the GPU side of the "critical path per iteration" framing (VCK ≈ max(WL grad, density+eField); GPU ≈ their sum) and tests whether eField dominates.

## Bottom line

1. **The DCT/eField transform is NOT the heavy part on the GPU** — 4 transforms total **0.034 ms/iter (adaptec1, 1.6 % of wall)** and **0.65 ms/iter (bigblue4, 3.2 %)**. What dominates density is **scatter + gather** (the per-node splat/force kernels): bigblue4 9.4 of 18.2 ms kernel time (52 %).
2. **WL and density/eField do not overlap at all on the GPU**: everything launches on one CUDA stream (stream 7, 11 k of 11 k kernels); measured temporal overlap between WL kernels and density-pipeline kernels = **0.000 ms/iter** on both designs. GPU iteration ≈ strict sum of the parts.
3. **adaptec1 is launch/host-bound, not compute-bound**: wall 2.09 ms/iter but kernels are busy only 0.96 ms (46 %); 9 `cudaStreamSynchronize` per iteration. **bigblue4 is compute-bound**: wall 20.1 ms, kernels busy 18.2 ms (90 %).
4. Which of WL vs density is bigger **flips with design size**: adaptec1 (512², 211 k nodes) WL kernel 0.42 ms > density+eField 0.22 ms; bigblue4 (2048², 2.2 M nodes) WL 2.5 ms ≪ density+eField 11.1 ms. For the VCK critical-path model this means "eField dominates" is a statement about *scatter/gather + the transform on the AIE*, not about the transform alone; on the GPU the transform is nearly free.

## Basis (read before quoting any number)

| item | value |
|---|---|
| GPU | NVIDIA GeForce RTX 3080, 10 GB (compute 8.6), driven from WSL2; also the display GPU (≈1.3 GB used by Windows) |
| Software | torch 2.8.0+cu128, CUDA toolkit 12.3, XPlace @ `88ae004` (local instrumentation only: `PRECOND_TRACE` no-op, `--use_precond False` fix) |
| Flags | defaults of `main.py` incl. **`--deterministic True`** (XPlace default → deterministic scatter/gather/node-grad kernels, slower than atomics), `--use_precond True`, `enable_sample_force` ends by iter ~100 so density runs every iter in-window. Not mixed-size, no timing/route |
| adaptec1 | ispd2005, **512×512 bins** (setup_dataset.py), target_density 1.0, 211 447 nodes / 221 142 nets / 944 053 pins; converges ≈840 iters |
| bigblue4 | ispd2005, **2048×2048 bins**, target_density 1.0 |
| Window | skip 200 optimizer steps (warm-up), measure steps 200–500 (wall, sync passes: N=300) and 200–300 (profiler pass: N=100). Sensitivity: window 600–700 below. Overflow in main window: adaptec1 0.82→0.66, bigblue4 0.81→0.76, i.e. **early/mid GP, not the converged tail** |
| Iteration | one `NesterovOptimizer.step` = 1.00 closure (WL+density) call/iter in every window (no backtracking observed) + `fast_evaluator` + `ParamScheduler.step`. GP loop only; LG/DP excluded |
| Tooling | `torch.profiler` (CPU+CUDA); **`nsys` is not installed on this box**. Kernels mapped to categories via `record_function` ranges on the launching CPU thread |

Three independent passes so the numbers cross-check (profiler inflates wall by 0.5–1 ms/iter on small designs, so wall is taken from the un-instrumented pass):

- **wall** — no instrumentation, one sync at window edges → true ms/iter.
- **sync** — `cuda.synchronize()` at every section boundary → serialized wall per category *including launch overhead and syncs* (sums to > wall; it removes any async hiding).
- **prof** — `torch.profiler` → GPU kernel time per category, stream ids, overlap.

## Results

### True per-iteration time (wall pass)

| design | ms/iter | GPU-busy (kernel union, prof pass) | busy / wall |
|---|---|---|---|
| adaptec1 | **2.085** (300 iters) | 0.955 ms | 46 % |
| bigblue4 | **20.11** (300 iters) | 18.16 ms | 90 % |

### GPU kernel time per category (prof pass, 100 iters, ms/iter; % of GPU-busy)

| category (what it covers) | adaptec1 ms | % | bigblue4 ms | % |
|---|---|---|---|---|
| **WL gradient** — `merged_wl_loss_grad` fwd+bwd (pin pos, WA kernel, node-grad, sum) | 0.423 | 44.3 | 2.523 | 13.9 |
| density scatter — `density_map_cuda.forward` (mov + filler maps) | 0.068 | 7.2 | 4.161 | 22.9 |
| density pre-normalize (node info prep) | 0.027 | 2.8 | 0.376 | 2.1 |
| **DCT/eField transforms** — dct2 fwd + idxst_idct(Ex) + idct_idxst(Ey) + idct2(potential) | 0.034 | 3.5 | 0.656 | 3.6 |
| ↳ forward dct2 | 0.0100 | | 0.182 | |
| ↳ inverse Ex / Ey / potential | 0.0074 / 0.0081 / 0.0081 | | 0.157 / 0.161 / 0.157 | |
| electric force gather — `density_map_cuda.backward` | 0.045 | 4.7 | 5.288 | 29.1 |
| density glue (spectral scale mults, vstack, clone, energy sum, DtoD copies) | 0.044 | 4.6 | 0.655 | 3.6 |
| closure glue (zero grad, cat, `+=`, precond divide) | 0.058 | 6.0 | 1.026 | 5.7 |
| optimizer update (Nesterov/BB: clones, norms, axpy) | 0.138 | 14.4 | 1.917 | 10.6 |
| evaluator (masked HPWL + overflow, reuses cached density map) | 0.044 | 4.6 | 0.579 | 3.2 |
| param scheduler (precond weight elementwise) | 0.039 | 4.1 | 0.371 | 2.0 |
| unannotated | 0.036 | 3.7 | 0.610 | 3.4 |
| **total GPU-busy** | **0.955** | | **18.161** | |

Density pipeline total (scatter + prenorm + transforms + gather + glue): adaptec1 **0.218 ms**, bigblue4 **11.14 ms**. WL: 0.423 / 2.523 ms. Largest single kernels: bigblue4 `density_map_cuda_deterministic_backward_kernel` 5.29 ms (29 %) and `..._forward_kernel` 3.85 ms (21 %); adaptec1 `calc_node_grad_deterministic_cuda_kernel` 0.256 ms (27 %).

### Serialized wall per category (sync pass, ms/iter, includes launch + sync overhead)

| section | adaptec1 | bigblue4 |
|---|---|---|
| WL grad (`wl_grad_fwd_bwd`) | 0.543 | 2.626 |
| density_total (all of the below + glue) | 1.245 | 12.932 |
| ↳ prenormalize | 0.075 | 0.420 |
| ↳ scatter (2 calls: mov + filler) | 0.165 | 4.126 |
| ↳ dct_forward | 0.125 | 0.487 |
| ↳ Ex / Ey / potential inverse | 0.103 / 0.092 / 0.089 | 0.476 / 0.457 / 0.462 |
| ↳ force gather | 0.091 | 5.019 |
| closure total | 2.123 | 17.013 |
| optimizer_step total | 2.611 | 19.530 |
| evaluator | 0.199 | 0.787 |
| param scheduler | 0.604 | 1.174 |
| **iteration (sum)** | **3.523** | **21.685** |

Read this table as "what each stage costs if you cannot hide it", not as a decomposition of wall: the sync pass is 1.4 ms (adaptec1) / 1.6 ms (bigblue4) slower than the wall pass because every boundary forces a drain. On adaptec1 the transform stages cost ~0.1 ms each here versus ~0.008 ms of kernel time → they are almost entirely host launch cost (several torch ops per transform).

### Overlap / streams

- All 11 204 (adaptec1) and 11 504 (bigblue4) kernels/memcpys over 100 iters run on **one stream (id 7)**; no multi-stream code in XPlace's GP path.
- WL-kernel vs density-kernel temporal overlap: **0.000 ms/iter** (both designs). WL and density are independent in the math (both only read `mov_node_pos`), but the code serializes them in one stream.
- Host syncs: 9 `cudaStreamSynchronize` + 18 `cudaMemcpyAsync` per iteration. CPU-blocked in `cudaStreamSynchronize` 0.58 ms/iter (adaptec1) and 17.5 ms/iter (bigblue4; mostly just waiting for the kernels). Call sites of the 9 syncs not attributed (unverified; the BB/backtracking scalar compares in `NesterovOptimizer.step` and the `.item()`/scalar reads in the loop are the likely sources).

### Window sensitivity (steps 600–700, `*_late/`)

| design | wall ms/iter | GPU-busy | WL | scatter | gather | transforms |
|---|---|---|---|---|---|---|
| adaptec1 (overflow 0.47) | 2.161 | 0.973 | 0.434 | 0.065 | 0.047 | 0.034 |
| bigblue4 (overflow 0.64) | 20.107 | 17.852 | 2.486 | 3.776 | 5.093 | 0.644 |

Within ≈5 % of the early window; the breakdown does not shift materially as cells spread (within the range reached; bigblue4 had not converged by iter 700).

## What this does and does not support

- Supports: no WL/density overlap on GPU; GPU transform cost is negligible; density scatter/gather dominates large designs; small designs are host-bound.
- Does **not** show: the VCK-side numbers. Whether "eField dominates on VCK" holds depends on AIE FFT + PL pre/post-processing + DDR streaming time at 1024², which this measurement cannot say. Also note XPlace's `density_map_cuda` scatter/gather here corresponds to AIEplace's density scatter and force gather — if those stay on the PL, they belong on the *density side* of the VCK max().
- Caveats: single GPU, WSL2 launch overhead (inflates the host-bound adaptec1 case — a native-Linux or faster-CPU box could be < 2 ms/iter); deterministic kernels (XPlace default) are slower than XPlace's non-deterministic atomic mode, which was not measured; early/mid-GP windows, not converged tail; one run per pass (run-to-run noise not quantified, but wall and late-window numbers agree to ~4 %).

## Commands (reproduce)

```bash
cd ~/phd/AIEplace/.claude/2_ARTIFACTS/xplace_profile
bash run_all.sh adaptec1 bigblue4     # wall (N=300), sync (N=300), prof (N=100); warm=200
bash run_late.sh                      # warm=600 sensitivity (wall + prof)
bash analyze_all.sh                   # trace -> category tables
```
Underlying invocation (per pass):
```bash
cd ~/phd/Xplace && PROF_MODE={wall|sync|prof} PROF_N=<n> PROF_WARM=200 PROF_OUT=<dir> \
  ~/anaconda3/bin/python <artifacts>/profile_gp.py --dataset ispd2005 --design_name <design> \
  --load_from_raw True --result_dir /tmp/xplace_prof --write_placement False < /dev/null
```

## Raw data

`.claude/2_ARTIFACTS/xplace_profile/` (untracked): `profile_gp.py` (driver), `analyze.py`, `run_*.sh`;
`<design>/{wall,sync,prof}.json`, `<design>/trace.json` (chrome trace, ~20 MB), `<design>/analysis_prof.txt`; `<design>_late/…` for the 600–700 window; `<design>.<pass>.log` run logs.
