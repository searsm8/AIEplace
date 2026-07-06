# Checkpoint — markv1 ~XPlace on adaptec1+adaptec2 (divergence-guard fix); clamp reflected into PL (2026-07-06)

Branch `pl_algo`. The markv1 CPU golden has been brought most of the way to XPlace quality by
porting XPlace's control layer + density smoothing, and the key change (density-force clamping) is
reflected into the PL modules and passes HLS C-synth. **HPWL parity is real but design-dependent —
do not overclaim.**

## Current state (what's done, verified, committed)

Commits this session (all on `pl_algo`, markv1 golden unless noted):
- `3f4145a` XPlace per-iteration density-weight schedule + fixed filler sizing (was every-3rd; fillers
  were sized from the smallest macro → 0 on std-cell designs; now trimmed-mean movable-cell size).
- `0608864` **masked overflow** as the convergence metric — markv1 CONVERGES for the first time.
- `f3510ef` XPlace-style divergence guard (`check_divergence` + `life`), armed only in the
  near-converged band (`overflow < 5×stop`) so spreading isn't misread.
- `32ebea7` **clamp cell footprints in the density FORCE** (the −8% HPWL win).
- `0237e57` reflect the clamp into the PL modules.
- checkpoint/doc updates (`719fd63`, `4cb5245`).

**Two load-bearing ideas (both are XPlace/DREAMPlace *code*, not in their papers):**
1. **Masked overflow** — measure overflow on a *clamped-footprint* density map: each sub-bin cell is
   inflated to ≥√2 bins per dim with weight = real_area/clamped_area (area conserved), so it smears
   to grid resolution instead of spiking one bin. This is the smoothed field the optimizer actually
   minimizes, so it reaches `stop_overflow`. The *exact* (sharp-footprint) overflow floors ~0.12 even
   for a good placement — that was the whole "markv1 can't converge" symptom.
2. **Clamped density force** — apply the SAME clamp to ρ / the electrostatic gradient (XPlace
   `expand_ratio`). Removes sub-bin gradient spikes → stability → tighter placement → lower HPWL.

**Results (GP, best HPWL, masked-overflow convergence):**
- Clamp is a robust markv1-vs-markv1 win: adaptec1 7.71e7→7.10e7 (−8%), pci_bridge32_a −9.7%, fft_a −5.9%.
- Direct XPlace head-to-heads (we BUILT + ran XPlace on this box — see memory `xplace_build_and_run`):
  - **adaptec1**: markv1 7.10e7 vs XPlace GP 7.06e7 → matches (~1%).
  - **adaptec2**: markv1 9.53e7 vs XPlace 7.90e7 (**+21%**) AND markv1 did NOT converge (masked
    overflow stalled ~0.10, divergence guard stopped it at iter 332; XPlace ran 926 iters to 0.049).
- markv1 = 64-bin grid, CPU, no preconditioner. XPlace = GPU, finer grid, preconditioner on.

## RESOLVED 2026-07-06: adaptec2 "stall" was a divergence-guard false-fire (commit 73cbe36)

Hypothesis (a) was the binding constraint. The adaptec2 "floor at masked overflow ~0.10" was NOT a
spreading stall — the run was **killed mid-descent**. At iter 332 (where it stopped) masked overflow
was 0.101 and dropping ~2%/iter (0.120→0.106→0.104→0.101 over the prior 8 iters), HPWL still
improving, and max_iterations was 700. Root cause: `checkDivergence` referenced `best_fallback`
(lowest-overflow-so-far) when no converged solution existed. On a smooth monotonic descent
best_fallback ≈ the newest sample, so a trailing 3-iter mean always reads "worse than best" on BOTH
HPWL and overflow → guard false-fires, burns life, dies. XPlace never hits this: its `check_divergence`
returns False whenever `best_metric["hpwl"]==inf` (no below-threshold sol yet). **Fix:** key the guard
off `best_primary` only; skip when it's invalid. Post-convergence protection is unchanged.

Results after fix (masked-overflow convergence; both now stop via the normal 30-iter countdown):
- **adaptec2**: was killed iter 332 @ masked 0.101 / HPWL 9.53e7 (never converged). Now CONVERGES
  iter 380 @ masked **0.049** (== XPlace 0.049) / HPWL **9.01e7** (−5.5%). Gap to XPlace GP 7.90e7:
  +21% → **+14%**.
- **adaptec1** (regression check): still converges, HPWL 7.16e7 @ masked 0.064 (was 7.10e7 — within
  random-init noise). No regression.

## Spreading gap was mostly an artifact — markv1 ≈ XPlace at matched conditions (2026-07-06)

Ran adaptec1 to convergence at 1024² (DCT FFT + dct_normalize=true): 685 iters, 1.96 s/iter (~22 min).
Result vs the earlier 64-bin and XPlace:
| grid | HPWL | masked ovfw | exact ovfw | max_util | over-cap bins |
|------|------|-------------|------------|----------|---------------|
| markv1 64  | 7.09e7 | 0.041 | 0.221 | 2.87× | 18% |
| markv1 1024| 7.44e7 | 0.058 | **0.097** | **1.02×** | **0.4%** |
| XPlace 512 | 7.06e7 | 0.042 | 0.115 | 1.09× | 8.9% |

The 1024 numbers looked like a big win, BUT that compared markv1@1024 to XPlace@512 (finer vs coarser
grid) with the density maps using different cell sets. The proper apples-to-apples below revises this.

### Apples-to-apples: matched grid + matched cell set (the honest result)
Two corrections applied: (1) **markv1 now runs each design at XPlace's per-design grid** (adaptec1=512,
adaptec2=1024, bigblue3/4=2048 — from XPlace `utils/setup_dataset.py`); XPlace's grid = die bbox, NO
padding. (2) The earlier coverage confound was NOT grid extent — it was that **XPlace's density dump
excluded fixed macros while markv1's included them.** Added fixed macros to XPlace's dump
(`init_density_map` capped at target + movable; `run_placement_nesterov.py`, env-gated).

adaptec1, **both at 512, both fixed+movable**: mean_util 0.80 vs 0.76 (matched — confound gone), std
0.447 vs 0.451 (identical uniformity), over-cap bins 14.0% vs 16.8% (markv1 *fewer*), overflow_mass
0.061 vs 0.050, max_util 3.6× vs 2.4×. HPWL 7.34e7 vs 7.06e7 (+3.9%), exact overflow 0.164 vs 0.115.
Heatmap `tools/adaptec1_512_matched.png`: the maps are **nearly identical** (macros, central cluster,
column profiles all track).

**Corrected conclusion:** at matched grid + cell set, **markv1's placement ≈ XPlace's** — comparable
spread, slightly sharper single-bin peaks (max 3.6× vs 2.4×) and modestly higher overflow/HPWL (~+4%).
The dramatic "2.9× vs 1.1× hotspot gap" was an ARTIFACT of markv1@64-bin vs XPlace@512 + the fixed-macro
dump mismatch — NOT a real spreading defect. Residual gap is small (peaks + ~4% HPWL). (markv1 exact
overflow still drops with finer grid: 64→0.221, 512→0.164, 1024→0.097; but that's measured at the run's
own resolution, so cross-grid exact-overflow numbers aren't directly comparable.)

## THE remaining gap (exploratory — do with Mark steering)

Now that both converge at XPlace's *masked* overflow, the residual is the **exact-overflow /
density-distribution gap**: at matched masked overflow markv1 lands a more-clustered, less-legalizable
placement — adaptec2 exact 0.19, adaptec1 exact 0.24, vs XPlace ~0.115 — and HPWL is still ~+14% on
adaptec2. Ruled OUT already: **finer grid** (256-bin adaptec1 exact stayed 0.205 ≈ 64-bin) and the
**preconditioner** (clamp+precond adaptec1 HPWL 8.64e7 WORSE, exact 0.168; `enable_preconditioning`
stays FALSE).

**Full 1024² CPU grid — now practical, DCT FFT-accelerated (2026-07-06, commit a47aadc).** The naive
CPU DCT was O(N³)/iter (~4 min/iter @ 1024 → ~24 h/run). Implemented `DCT_fft`/`IDCT_fft`/`IDXST_fft`
(Makhoul, one length-N radix-2 FFT; `DCT.cpp`), verified ≡ naive to ~1e-6 for N=2..1024, and switched
`compute_a_uv_DCT`/`compute_eField_DCT` to them. **Now ~2 s/iter @ 1024 (~100× faster)** — a converged
1024 run is ~15 min. `"bins_per_row": 1024` works.

Also added **optional 1/N-per-DCT normalization** (`dct_normalize`, default false) to bound
intermediate magnitudes, and a `random_seed` config (default -1) for controlled A/B. **A/B on adaptec1
(seed 42):** iters 1-5 bit-identical (HPWL/ovfw/step/backtracks); `density_weight` differs by exactly
N⁴ (64⁴=1.68e7: 3.06e-10 vs 5.13e-3) → pure global scaling absorbed by λ, as expected. Both converge
~7.16-7.18e7 @ masked ~0.04. Normalized keeps λ O(1) and the field sane (unnormalized balloons ~N⁴,
which loses float precision at N=1024 — so **turn `dct_normalize` on when running 1024**). NOTE:
default is still false pending review — flipping it rescales λ (schedule is ratio-based so dynamics are
unchanged, but any absolute-λ expectations shift). **PAUSED here for review before the apples-to-apples
same-resolution density comparison.**

**Density-dump instrumentation DONE (2026-07-06, commit 61ad581).** markv1 `Placer::dumpBinDensity`
(config `dump_density:true`) writes masked+exact real-cell ρ CSVs (fillers excluded) at the restored
best; XPlace side is an env-gated dump (`XPLACE_DUMP_DENSITY=1`, `~/phd/Xplace/src/run_placement_nesterov.py`,
writes `~/aieplace_tmp/<bench>_density_exact.npy` + meta). Compare with `vck5000/tools/compare_density.py`.
**First adaptec1 result:** robust signal = markv1 forms local hotspots up to **2.9× capacity** (18% of
core bins over target) while XPlace caps at **~1.1×** (9% over) — matches markv1 exact 0.23 vs XPlace
0.115. CAVEAT: absolute utilization not yet apples-to-apples — markv1 is a 64-bin grid over the core
die, XPlace a 512-bin grid padded to power-of-2 (empty margins), so coverage differs. NEXT: rebin both
final placements over the identical die bbox at one resolution before drawing spatial conclusions;
then chase why markv1 permits >2× local pile-ups (candidate: density-force magnitude / gradient
clamping vs XPlace, not the stop criterion).

## PL port status + next gate

- New `src/modules/node_footprint.hpp` = shared clamped-footprint geometry. `density_bin.hpp`
  (bin_scatter) and `force_gather.hpp` (node_gather, the scatter's adjoint) both use it.
  `metrics.hpp` overflow is now masked for free (sums clamped ρ, fillers already excluded).
- `model/density_bin_model.cpp` updated → strip-tiled vs naive still **PASS bit-exact** (GRID=1024).
- **HLS C-synth `make PL=pl_algo TARGET=hw` → SYNCHK 0 errors, `top.xo` built** (Gate 1 pass). Density
  loops II=1; node_gather inner intersection II=5 (sub-bin cells now touch ~4 bins — an optimization
  target, not a correctness issue).
- **Next PL gate: sw_emu** — verify the clamped density/force numerically vs the *new* Grid golden on
  a real benchmark (long-running; not started). Then re-tune λ/γ for the 1024² grid; the PL's fine
  grid may or may not shrink the exact-overflow gap (64→256-bin CPU didn't, so don't assume it will).

## Build / run how-to
- **markv1 CPU golden:** `make host HOST=markv1` (no XRT). Run under a pty (the parser hangs on
  non-tty stdout): `script -qec './build/hw/host/markv1/aieplace_markv1.exe <cfg>' <log>`. Put temp
  configs in `~/aieplace_tmp/` (WSL `/tmp` is wiped between calls). Config template
  `host/src/markv1/run_config.json`; per-iter log `iterations.dat`, summary `run_summary.md`. Key new
  knobs: `enable_density_clamp` (true), `enable_preconditioning` (false), `enable_filler` (true).
  Details: memory `markv1_cpu_run_gotchas`.
- **PL C-synth:** `source /tools/Xilinx/Vitis/2022.2/settings64.sh`;
  `export PLATFORM_REPO_PATHS=$HOME/xilinx_local/opt/xilinx/platforms`; `cd pl && make PL=pl_algo TARGET=hw`.
- **XPlace reference:** memory `xplace_build_and_run` (system CUDA 12.3, conda base torch,
  `-DCMAKE_CXX_ABI=1`, PIC lefdef override, `pip install pulp igraph`, run with `< /dev/null`).
  `data/raw/ispd2005` is symlinked to the AIEplace benchmarks.

## Hardware deployment (deferred — needs Mark/Geert's card)
`make all TARGET=hw AIE=pl_algo PL=pl_algo HOST=pl_algo BUILD_XRT=1 AIE_DENSITY_INSTANCES=8` → hw
xclbin + host exe (hours-long P&R). Target needs: VCK5000 shell matching
`xilinx_vck5000_gen4x8_qdma_2_202220_1`, matching XRT, compatible libstdc++/glibc (host mixes ABIs),
and the benchmark input files at runtime. Limbo parser libs are static (travel in the exe). Risk
ranking: platform-shell > XRT version > libstdc++/glibc ABI; mitigate with `-static-libstdc++ -static-libgcc`.

## Key references
- **markv1 golden:** `host/src/markv1/src/{AIEplace,Density,Partials,Output,Grid}.cpp`, `include/{Node,AIEplace,Grid}.h`.
- **Source of truth:** XPlace `~/phd/Xplace/src/{param_scheduler,calculator,evaluator,database,nesterov_optimizer,initializer}.py`.
- **Auto-memory:** `clamped_density_force_milestone`, `markv1_nonconvergence_vs_xplace`,
  `xplace_build_and_run`, `markv1_cpu_run_gotchas`, `pl_algo_density_manager`, `pl_algo_stage5c`.

---

## Stage 5c reference (PL hardware draft — still current)
The full ePlace iteration runs on PL/AIE, sw_emu-verified end to end. PL modules:
`src/modules/{density_bin,dct_1d,dct_transpose,transpose,force_gather,hpwl_gradient,iteration_update,
memory_writer,metrics}.hpp`, `node_footprint.hpp` (new), `top.cpp`, `host_interface.hpp`, `formats.hpp`,
`DATAFLOW.md`. Host driver `host/src/pl_algo/src/{Placement.hpp,Driver.cpp,main.cpp}`; flags
`--iter-update`/`--metrics`/`--place <bench> <xclbin> [iters]`. Prior sw_emu verified commits:
f81212b, b1bfd92, b5c2e75, edcc81a. Stage 6 (PL optimization) when hardware work resumes: re-tune
λ/γ for 1024², fuse the 8-pass density solve, widen ports to 128-bit beats, BB α on PL.
