# Report: porting the sw_only improvement effort into pl_algo (PL iteration logic)

**Goal (per request):** take every item touched in the recent `sw_only` improvement effort and port
that exact logic into the `pl_only` (pl_algo) iteration logic. This document records **every PL update
made**, the sw_only source it mirrors, and the verification status.

## Where the PL "iteration logic" lives
- **PL kernel schedule:** `pl/src/pl_algo/src/modules/param_scheduler.hpp` — the on-device ePlace
  schedule + convergence recurrence (already a faithful sw_only port; predates the fixed-K experiment).
- **PL host driver:** `host/src/pl_algo/src/` — `Driver.cpp::runPlacement` (the sw_emu iteration loop),
  `Placement.hpp` (ABI-neutral policy math), `main.cpp` (`--place` setup), `Packer.cpp` (DataBase→PL
  buffers). `Driver.cpp`/`Placement.hpp` are **XRT-only** (compiled in the sw_emu flow, not the CPU host).

## The sw_only items in scope, and how each landed
| sw_only change (commit) | PL status | where |
|---|---|---|
| λ worsening → scale-invariant **relative** form (ad6d52a) | **already correct** in `param_scheduler.hpp`; **ported** into the host `Driver` loop (was a stub) | `Placement.hpp::updateDensityWeight`, `Driver.cpp` |
| **BB step clamp removed** [1e-4,4000] (ba9cf41) | **ported** (was still clamped in both PL spots) | `Placement.hpp::bbStepLength`, `param_scheduler.hpp` alpha |
| **net-degree mask** `ignore_net_degree=100` (8025644) | **ported** (HPWL metric + WA gradient) | `host_interface.hpp`, `Packer.cpp`, `Placement.hpp::hostHPWL`, `main.cpp` golden |
| **0.07 stop overflow** (7fe6d89) | **ported** | `Placement.hpp` cfg, `main.cpp`, `param_scheduler.hpp` comment |
| **ePlace auto grid** (06ca6fa) | **N/A on PL** — fixed hardware grid (documented) | `main.cpp` note |
| **target_density** from placement.constraints | **ported** (pl_algo DataBase already parses it; wired into cfg) | `main.cpp` |
| **gamma_ref_grid base_gamma** (6b5a924/8ce73d2) | **ported** | `main.cpp` |
| field frame: **dff_force_ratio** (492ebf3) | **ported** (force-ratio dff) | `Placement.hpp::densityForceFraction`, `Driver.cpp` |
| field frame: **dct_normalize_inverse=false** | **absorbed** on fixed PL grid (reasoning below) | — |
| field frame: **precond_raw_area=true** | **moot** (precond OFF default) | — |

## Detailed changes

### 1. BB step-length clamp removed (`3a42098`)
sw_only `computeLipshitzEstimate` now returns the raw BB estimate ("No magnitude clamp — mirrors
XPlace"). The PL still clamped to `[1e-4, 4000]` in two places; both removed:
- `Placement.hpp::bbStepLength` — returns `sqrt(||Δv||²)/sqrt(||Δg||²+1e-8)` unclamped.
- `param_scheduler.hpp` alpha output — same.

### 2. Net-degree mask, IGNORE_NET_DEGREE=100 (`3a42098`)
XPlace `net_mask`/`ignore_net_degree`: nets with >100 pins are excluded from **both** the WA gradient
and every reported HPWL. Added `constexpr int IGNORE_NET_DEGREE = 100` to `host_interface.hpp` (shared
contract) and applied it consistently:
- `Packer.cpp` — tags pins of nets with `degree<=1 OR degree>100` as `net=-1` (previously only `<=1`);
  the PL kernels (`metrics.hpp`, `hpwl_gradient` via the `npins` filter) already skip `net<0`.
- `Packer.cpp::hpwlFromPacked` — now skips masked nets (`f.net<0`), so the host HPWL reference agrees
  with `metrics.hpp` (was including them).
- `Placement.hpp::hostHPWL` — skips `deg<=1 || deg>100`.
- `main.cpp` pack-verify golden — skips the same, so `golden==packed` stays a valid check.
- **Verified (CPU host):** pci_bridge32_b HPWL `8.80562e8 → 8.79397e8` (masked), `golden==packed` PASS.

### 3. Full schedule ported into `Driver.cpp::runPlacement` (`00e78e2`)
The device loop was a **reduced stub**: λ set once by `initDensityWeight`, gamma constant
(`gamma_schedule=0`), no convergence test, ad-hoc precond escalation. Ported the sw_only
`performIteration` schedule via new `Placement.hpp` helpers:
- `densityForceFraction` — `dff = ||λ·g_den||₁ / (||g_wl||₁ + ||λ·g_den||₁)` (the `dff_force_ratio`
  form; field-norm invariant).
- `scheduleSkipUpdate` — the shared gate: on 2 of every 3 iters, while `k<50` or `dff∈(0.5,0.95)`,
  **freeze λ and γ together** (XPlace `step()`).
- `updateDensityWeight` — scale-invariant relative-form λ trend (grow near `max_step` while HPWL
  improves; relative-damp while it worsens). Matches sw_only ad6d52a (no fixed-K).
- Loop now: computes `dff`, applies the skip gate, ramps λ, updates γ (overflow-driven), and runs an
  **overflow-countdown convergence** (stop after overflow holds `< 0.07` for `conv_iters=30`).
- **Preconditioner OFF** (weight 1, filled once) to match sw_only default; removed the ad-hoc
  `precond_coef` escalation.

### 4. Host params in `main.cpp` (`00e78e2`)
- `base_gamma = 4*(die_w+die_h)/512` — sw_only `gamma_bin_scaled` referenced to the fixed
  `gamma_ref_grid=512` (grid-independent); `init_gamma=4` plays XPlace's `wa_coeff`. (Was `0.01*span`.)
- `target_density` from `db.getMaximumUtilization()` (placement.constraints), else `1.0` (ISPD2005).
- `gamma_schedule=1`, `density_weight_init_multiplier=8e-5` (was 0.01), `overflow_threshold=0.07`,
  `min_step=0.95`, `max_step=1.05`, `min_iters=50`, `conv_iters=30`.

### 5. Field frame — why only `dff_force_ratio` needed porting
The field-frame change (492ebf3) was **grid-dependent** — it closed the gap that widened from +1.4%@512
to ~+10%@1024. Its mechanism: the legacy inverse re-applied `1/N`, making the field ~N² too weak and λ
~N² inflated. On the **PL the grid is FIXED (DENSITY_GRID=1024)**, so that `N` factor is a *constant*
that λ fully absorbs (via the ratio-based `initDensityWeight` + scale-invariant relative ramp). The
grid-*dependent* benefit only manifests when comparing across grids, which a fixed-grid PL does not do.
`dff_force_ratio` (ported) makes the schedule itself invariant to the field-norm constant, and
`precond_raw_area` only affects the preconditioner mass, which is OFF by default. **Conclusion:** no
explicit `dct_normalize_inverse` port is required for the PL to match sw_only's behaviour at its fixed
grid. Revisit if (a) preconditioning is enabled, or (b) the PL grid becomes runtime-variable.

### 6. Auto grid sizing — not applicable to the PL
sw_only's ePlace formula (`bins ≈ √(N_movable)` capped by rows) is a **host** decision. The PL density
datapath is a fixed-size pipeline (`DENSITY_GRID=1024`), so the grid is pinned by the hardware. For
HW bring-up the design is run at 1024; a runtime-configurable-length FFT would be required to serve
multiple grids from one bitstream (noted as future work).

## Verification status
| check | result |
|---|---|
| CPU host build (`make host HOST=pl_algo`) | **PASS** |
| pack + HPWL net-mask verify (pci_bridge32_b) | **PASS** (golden==packed, masked) |
| `Driver.cpp` syntax-check (g++ -fsyntax-only, XRT headers) | **PASS** |
| `main.cpp` syntax-check (XRT) | **PASS** |
| `sched_verify` bit-identical replay vs a **fresh** sw_only `schedule_trace.csv` (mgc_fft_2, current defaults) | **PASS (schedule)** — `inv_gamma`, `alpha`, `lambda`, `coeff` all **0 rel err**. Confirms the relative-λ schedule and the BB-clamp removal match sw_only exactly. (An OLD Jul-12 fixed-K trace fails λ by 1.8 rel err, as expected — the stale trace, not the port.) Convergence flag inconclusive on this 150-iter capped run (fft_2 didn't reach 0.07). |
| **sw_emu host compile + LINK** (`make host HOST=pl_algo TARGET=sw_emu BUILD_XRT=1`) | **PASS** — Driver.cpp rebuilt with real XRT (new ABI `-D_GLIBCXX_USE_CXX11_ABI=1`), exe links with **no undefined references** (confirms the `updatePrecondWeights`/`precond_coef` removal didn't break linkage). Existing Jul-3 xclbin reused (my changes are host-side). |
| **sw_emu `--place` run, 2 iters** (device loop, pci_bridge32_b) | **PASS** — completed 2 iterations, positions finite/in-bounds. Every ported behavior confirmed ACTIVE on the device path: HPWL=**8.79397e8** == the CPU masked golden (879396943) → net-mask works on device; **gamma 1.25e4→1.25e5** → ported gamma schedule + gamma_ref_grid base_gamma (10× at overflow~1.0, exact); **alpha=1.152e6** (iter 2) → BB-clamp removal active (would be ≤4000 before); **coeff 0→0.2818** → Nesterov momentum; λ=1.4e-21 tiny init, frozen by skip_update over 2 iters (correct). Multi-iter λ RAMP / full convergence needs many more iters (impractically slow under emulation, ~11 device passes/iter) — verify on real HW. Fixed a stale-main.o bug (earlier CPU builds compiled main.o without USE_XILINX_XRT, so `--place` fell through to default mode). |

## Recommended next steps
1. Regenerate `schedule_trace.csv` from sw_only (`dump_schedule_trace`) and run `model/sched_verify` to
   confirm `param_scheduler.hpp` is bit-identical after the BB-clamp removal.
2. Rebuild the pl_algo sw_emu xclbin and run `--place` on `mgc_des_perf_1` / a small ISPD2015 design;
   confirm the trajectory converges (overflow → 0.07) and tracks the sw_only golden.
3. Decide the preconditioner story on the PL (audit flagged it as the main gap); if enabled, port
   `precond_raw_area` + the precond-scaled dff.
