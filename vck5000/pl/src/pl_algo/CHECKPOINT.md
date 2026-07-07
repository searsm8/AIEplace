# Checkpoint — markv1 GP now ≈ XPlace across the suite (6/7 within ~2%); grid-tied γ was the lever (2026-07-07)

## 2026-07-07 session — closed most of the HPWL gap
Two wirelength fixes landed in the markv1 golden, both verified via `tools/dse.py` and committed on
`pl_algo`, taking adaptec1 from +5% to +1.6% vs local XPlace GP and the whole suite to near-parity:
- **`6b5a924` grid-tied WA γ (the big lever).** XPlace sets `base_gamma = wa_coeff·(bin_w+bin_h)`;
  markv1 used a bare `init_gamma=4` constant → WA ~42× too sharp (near winner-take-all) and mis-scaled
  with grid. New `gamma_bin_scaled` flag (default true): `base_gamma = init_gamma·(bin_w+bin_h)`,
  finalized after the grid is built. adaptec1@512 7.322e7→**7.171e7 (−2.1%)**; adaptec2@1024 legacy
  γ *couldn't even spread* (stalled exact 0.25) while grid-tied converged (exact 0.089). See
  auto-memory `gamma_bin_scaled_milestone`.
- **`3121b58` pin offsets** (bookshelf center-relative→LL; `enable_pin_offsets` flag). ~0.2% — correct
  to have, NOT the gap. See `pin_architecture`.
- **`c9c8a41` dse.py `explicit_runs`** — per-run configs alongside the Cartesian product (solves
  per-design grids in one sweep). See `dse_sweep_tool`.
- **init_gamma multiplier + BB α clamp both ruled out** (flat optimum at wa_coeff≈4; clamp not binding).
- **Best defaults baked into `run_config.json`:** gamma_bin_scaled, dct_normalize, enable_pin_offsets.

**Suite scorecard (each design @ its XPlace grid, best defaults, ratio vs XPlace published HPWL):**
adaptec1 0.98 · bigblue1 1.01 · matmult_b 0.99 · des_perf_a 1.01 · fft_b 0.93 · pci_bridge32_a 0.99 ·
**adaptec2@1024 1.20 (outlier).** The converged designs (adaptec1/bigblue1/matmult_b @ ovfw~0.04) are
within 1-2%.

**adaptec2 outlier = a 1024-GRID effect, not the design.** 2×2 (design×grid @ masked 0.04): adaptec2
**@512 = 8.327e7 (ratio 1.02)** but @1024 = 9.729e7 (1.20); adaptec1 is grid-robust (0.98→0.99). At the
SAME exact spread adaptec2 loses ~17% HPWL going 512→1024. **ROOT-CAUSED:** grid-tied γ (∝1/N) over-sharpens at 1024 for γ-sensitive adaptec2. adaptec2@1024
init_gamma sweep: ig4→9.729e7 (1.20), **ig8→8.512e7 (1.05), ig16→8.519e7** — doubling init_gamma
(restoring the 512 absolute γ≈16) recovers −12.5% HPWL. adaptec1 is γ-flat so grid-robust. init_gamma=8
suite re-sweep: fixes adaptec2@1024 (1.20→**1.05**) but REGRESSES @512 designs (matmult_b +1.2%, pci
+1.8%) → NOT a safe default; **kept init_gamma=4 (no code change).** **TOP NEXT ITEM (gates the PL
@1024): fix the γ SCALING, not the constant** — base_gamma ∝ 1/N (grid-tied) over-sharpens at high N;
make it scale gentler (∝ die_span/REF_N or tie to site pitch, ~grid-independent) so one config is right
at 512 AND 1024, then re-verify the suite. (XPlace uses ∝1/N yet runs adaptec2@1024 fine — likely its
always-on preconditioner compensates; markv1 precond is OFF. Confirm before changing scaling.)
Details: `gamma_bin_scaled_milestone`.

---

# (prior) Checkpoint — markv1 GP ≈ XPlace at matched conditions; only ~5% HPWL residual left (2026-07-06)

Branch `pl_algo`. The markv1 CPU golden now matches XPlace quality closely once compared fairly. This
session: fixed the adaptec2 non-convergence, FFT-accelerated the DCT (1024 grid now practical), added
density-map instrumentation, and — through a careful apples-to-apples comparison — showed the
"markv1 is more clustered than XPlace" narrative was largely a **measurement artifact**. The one real
remaining gap is a modest ~5% wirelength (HPWL) difference.

## The bottom line (what to tell the next session)
At **matched grid resolution + matched cell set + matched overflow**, markv1's global placement is
essentially on par with XPlace on physical spread. The prior "2.9× hotspots vs 1.1×" gap was an
artifact of (1) markv1 running at 64-bin while XPlace ran at 512, (2) the density dumps using different
cell sets (XPlace's excluded fixed macros), and (3) comparing at each placer's own stopping point.
Fix all three and the density maps are nearly identical. **The genuine residual is ~5% HPWL** (adaptec1:
markv1 7.42e7 vs XPlace 7.06e7 at matched spread) — a wirelength-efficiency gap, NOT a spreading or
density-force defect. **Next: chase the ~5% HPWL** — candidates are the WA-γ (wirelength-smoothing)
schedule, the WL-gradient formulation (`Partials.cpp`), or the optimizer settling at a WL-worse local
optimum. Do NOT chase the electrostatic force (Tests A/B below cleared it).

## Done, verified, committed this session (all on `pl_algo`)
- `73cbe36` **Divergence-guard fix.** adaptec2's "stall at masked overflow ~0.10" was a false-fire:
  `checkDivergence` keyed off `best_fallback` (newest lowest-overflow point), so on a smooth descent a
  trailing 3-iter mean always read "worse than best" → guard burned life and killed the run mid-descent
  (iter 332, overflow still dropping ~2%/iter, max_iter was 700). Fixed to key off `best_primary` only
  (mirrors XPlace `check_divergence` returning False while no converged sol). Result: **adaptec2 now
  CONVERGES** (iter 380, masked 0.049 == XPlace 0.049, HPWL 9.53e7→9.01e7). adaptec1 unaffected.
- `a47aadc` **FFT DCT.** `DCT_fft`/`IDCT_fft`/`IDXST_fft` (Makhoul, one length-N radix-2 FFT; `DCT.cpp`),
  verified ≡ the naive transforms to ~1e-6 for N=2..1024. `compute_a_uv_DCT`/`compute_eField_DCT` now use
  them. Naive O(N³)/iter (~4 min/iter @1024) → **~2 s/iter @1024 (~100×)**; a converged 1024 run is ~15-22
  min. `"bins_per_row": 1024` works (CPU path is grid-agnostic).
- `972a726` **`dct_normalize` default true.** 1/N-per-DCT normalization; A/B proved it's a pure global
  scale absorbed by λ (iters 1-5 bit-identical; density_weight differs by exactly N⁴). Keeps λ O(1) and
  the field at a sane magnitude — critical at 1024 (unnormalized field balloons ~N⁴, loses float precision).
- `61ad581` **Density-dump instrumentation** + `tools/compare_density.py` (see Tooling below).
- Checkpoint/finding commits: `442d66b`, `adefcc5`, + this rewrite. `random_seed` config added (default
  -1 = time-based) for controlled A/B.

## The apples-to-apples comparison (the honest result)
Three corrections made the markv1-vs-XPlace density comparison fair:
1. **Match XPlace's per-design grid.** XPlace hard-codes grid per design in `Xplace/utils/setup_dataset.py`
   (adaptec1=512, adaptec2=1024, bigblue3/4=2048); its grid spans exactly the die bbox, no padding. markv1
   now runs each design at that resolution.
2. **Match the cell set.** The mean-level confound was NOT grid extent — markv1's `computeOverflow`/
   `dumpBinDensity` deposits a capped fixed-macro baseline + movable, while XPlace's dump was movable-only
   (adaptec1 is 43% fixed area). Added fixed macros to XPlace's dump (`init_density_map`.clamp(0,target) +
   movable; `~/phd/Xplace/src/run_placement_nesterov.py`, env-gated `XPLACE_DUMP_DENSITY=1`).
3. **Match overflow (Test A).** Compare at the same spreading level, not each placer's stopping point.

**adaptec1, both @512, both fixed+movable, matched overflow** (markv1 stop 0.07→0.04, masked ovfw 0.040
≈ XPlace 0.042): max_util 2.68× vs 2.37×, overflow_mass 0.042 vs 0.050 (markv1 lower), over-cap 14% vs
17%, std 0.41 vs 0.45, exact overflow 0.111 vs 0.115 — **spread matches**. HPWL 7.42e7 vs 7.06e7 (+5%).
Heatmaps: `tools/adaptec1_512_matched.png` (at each's own stop) and `tools/adaptec1_512_matched_overflow.png`.

### Tests A & B (ruling out hypotheses for the peak/HPWL residual)
- **Test A (stopping point) — CONFIRMED.** Lowering markv1's stop 0.07→0.04 so it spreads to XPlace's
  overflow dropped max_util 3.56×→2.68× and exact overflow 0.162→0.111 (≈ XPlace) for only +1.1% HPWL. So
  the sharper-peaks residual was mostly markv1 stopping earlier, not a force defect.
- **Test B (preconditioner) — RULED OUT (harmful).** `enable_preconditioning:true` @512 never converges
  (overflow floors ~0.51, HPWL 1.11e8): the large normalized-field λ makes `precond_weight=max(1,pins+λ·area)`
  crush the density force. Stays FALSE.
- **⇒ Neither implicates the density-force code; the force-diff harness is NOT the priority.** The real
  residual is the ~5% HPWL (wirelength efficiency).

## Open next step
**Chase the ~5% HPWL gap** (markv1 vs XPlace at matched spread). Start by diffing markv1's wirelength
path against XPlace: the WA (weighted-average) γ schedule (`updateGamma`), the WL-gradient
(`Partials.cpp` `computeHpwlPartials_CPU`), and the Nesterov/BB step settings. A matched-overflow HPWL
comparison across a few benchmarks (adaptec2 @1024, pci_bridge32_a) would confirm the gap is universal
and its size, before deciding where to intervene.

## Tooling added this session
- **markv1 density dump:** config `"dump_density": true` → `Placer::dumpBinDensity` (Density.cpp) writes
  `<results_dir>/<bench>/<ts>_cpu_cpu/<bench>_density_rho_{masked,exact}.csv` (ρ = area/bin_area, fixed
  baseline capped + movable, fillers excluded) at the restored best. Reuses `computeOverflow(clamp, out)`.
- **XPlace density dump:** `XPLACE_DUMP_DENSITY=1` env → `~/aieplace_tmp/<bench>_density_exact.npy` + `_meta.json`.
- **compare:** `python tools/compare_density.py MARKV1_EXACT_CSV XPLACE_NPY --meta META --out PNG --grid N`
  → side-by-side heatmaps, over-cap masks, difference, marginal profiles, printed stats.
- **`random_seed`** (params, default -1) for identical init across A/B arms.

## PL port status + next gate (unchanged this session)
- `src/modules/node_footprint.hpp` = shared clamped-footprint geometry; `density_bin.hpp` (bin_scatter)
  and `force_gather.hpp` (node_gather, the adjoint) both use it. `metrics.hpp` overflow is masked for free.
- `model/density_bin_model.cpp`: strip-tiled vs naive PASS bit-exact (GRID=1024).
- **HLS C-synth `make PL=pl_algo TARGET=hw` → SYNCHK 0 errors, `top.xo` built** (Gate 1 pass). Density
  loops II=1; node_gather inner intersection II=5 (sub-bin cells touch ~4 bins — an opt target).
- **Next PL gate: sw_emu** — verify clamped density/force vs the Grid golden on a real benchmark
  (long-running; not started). Then re-tune λ/γ for 1024². Note the FFT DCT is a *software golden*
  acceleration; the PL/AIE still uses the AIE FFT for the transform.

## Build / run how-to
- **markv1 CPU golden:** `make host HOST=markv1` (no XRT). Run under a pty (parser hangs on non-tty
  stdout): `script -qec './build/hw/host/markv1/aieplace_markv1.exe <cfg>' <log>`. Temp configs in
  `~/aieplace_tmp/` (WSL `/tmp` is wiped between calls). Template `host/src/markv1/run_config.json`.
  Key knobs: `bins_per_row` (per-design, match XPlace), `dct_normalize` (true), `enable_density_clamp`
  (true), `enable_preconditioning` (false), `enable_filler` (true), `dump_density`, `random_seed`.
  Details: memory `markv1_cpu_run_gotchas`. When launching watch-loops, `pgrep -x aieplace_markv1`.
- **PL C-synth:** `source /tools/Xilinx/Vitis/2022.2/settings64.sh`;
  `export PLATFORM_REPO_PATHS=$HOME/xilinx_local/opt/xilinx/platforms`; `cd pl && make PL=pl_algo TARGET=hw`.
- **XPlace reference:** memory `xplace_build_and_run` (system CUDA 12.3, conda base torch,
  `-DCMAKE_CXX_ABI=1`, PIC lefdef override, `pip install pulp igraph`, run with `< /dev/null`; density
  dump via `XPLACE_DUMP_DENSITY=1`). `data/raw/ispd2005` symlinked to the AIEplace benchmarks.

## Hardware deployment (deferred — needs Mark/Geert's card)
`make all TARGET=hw AIE=pl_algo PL=pl_algo HOST=pl_algo BUILD_XRT=1 AIE_DENSITY_INSTANCES=8`. Needs a
VCK5000 shell matching `xilinx_vck5000_gen4x8_qdma_2_202220_1`, matching XRT, compatible libstdc++/glibc.
Risk: platform-shell > XRT version > libstdc++/glibc ABI; mitigate `-static-libstdc++ -static-libgcc`.

## Key references
- **markv1 golden:** `host/src/markv1/src/{AIEplace,Density,Partials,Output,Grid,DCT}.cpp`,
  `include/{Node,AIEplace,Grid,DCT}.h`.
- **Source of truth:** XPlace `~/phd/Xplace/src/{param_scheduler,calculator,evaluator,database,nesterov_optimizer,initializer}.py`, grid sizes `utils/setup_dataset.py`.
- **Auto-memory:** `dct_fft_acceleration`, `density_map_comparison`, `markv1_nonconvergence_vs_xplace`,
  `clamped_density_force_milestone`, `xplace_build_and_run`, `markv1_cpu_run_gotchas`, `pl_algo_stage5c`.

---

## Stage 5c reference (PL hardware draft — still current)
Full ePlace iteration runs on PL/AIE, sw_emu-verified end to end. PL modules:
`src/modules/{density_bin,dct_1d,dct_transpose,transpose,force_gather,hpwl_gradient,iteration_update,
memory_writer,metrics}.hpp`, `node_footprint.hpp`, `top.cpp`, `host_interface.hpp`, `formats.hpp`,
`DATAFLOW.md`. Host driver `host/src/pl_algo/src/{Placement.hpp,Driver.cpp,main.cpp}`; flags
`--iter-update`/`--metrics`/`--place <bench> <xclbin> [iters]`. Prior sw_emu verified commits:
f81212b, b1bfd92, b5c2e75, edcc81a. Stage 6 (PL optimization): re-tune λ/γ for 1024², fuse the 8-pass
density solve, widen ports to 128-bit beats, BB α on PL.
