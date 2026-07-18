# AIEplace `sw_only` vs. XPlace — comparison report

**Purpose.** `sw_only` is the CPU software golden for AIEplace: a from-scratch reimplementation of
the ePlace/DREAMPlace analytical global placer, tuned to track the **XPlace** reference
(`~/phd/Xplace`, a PyTorch ePlace) as faithfully as possible. This document collates everything we
learned matching the two during the `sw_only` improvement effort.

**Where it stands** (full-suite snapshot `DSE_20260716_161321`, seed 42, auto-grid). On ISPD2005 (the
designs with a directly comparable XPlace GP reference) `sw_only` GP wirelength matches XPlace GP at
ratios **1.00 / 1.01 / 1.02 / 1.03 / 1.01 / 1.02** (adaptec1/2/3/4, bigblue1/2; mean ~1.015, all ≥1.00).
**26 of 28** designs converge to the 0.07 overflow stop; the two holdouts are `bigblue3` (0.112,
λ-overshoot / divergence-guard, not a die/moat issue) and `mgc_matrix_mult_a` (0.203, mixed-size,
set aside). The remaining known gaps and deliberate divergences are in Section 3.

> Reference: XPlace GP exact HPWL (this box) — adaptec1 7.064e7, adaptec2 7.903e7, bigblue1 8.726e7,
> bigblue2 1.310e8, adaptec3 1.859e8, adaptec4 1.677e8. Compare only against XPlace **GP** (not its
> legalized number), and only on ISPD2005 (XPlace ISPD2015 HPWL is site-width-normalized ~/200).

---

## Section 1 — XPlace details incorporated that improved results

Core analytical-placement algorithm (all matched to XPlace/ePlace):
- **WA (weighted-average) wirelength** with a `γ` smoothing parameter; gradients evaluated at the
  Nesterov probe point; wirelength and density gradients are **added** (`+=`, not subtracted).
- **Nesterov accelerated gradient** with momentum, and a **Barzilai–Borwein / Lipschitz** step-length
  estimate as the base step.
- **Backtracking line search** (`enable_backtracking`) bounding the step — with **no magnitude
  clamp** on the step (XPlace bounds via backtracking alone; the old `[1e-4, 4000]` clamp was removed).
- **Electrostatic density** via DCT/IDCT/IDXST, accelerated with an **FFT** (O(N log N)); the
  density→field round-trip normalization matches DREAMPlace/XPlace (see field-faithful frame below).

Levers that measurably closed the gap to XPlace:
- **Clamped density force (`expand_ratio`)** — inflate each cell footprint to ≥ √2 bins/dim with an
  area-conserving weight, smoothing sub-bin density spikes. This is in the XPlace/DREAMPlace *code*
  but not the papers. Robust improvement: adaptec1 7.71e7→7.10e7 (−8%), pci_bridge32_a −9.7%,
  fft_a −5.9%. (`enable_density_clamp`, default on.)
- **Field-faithful field-solve frame** (`dct_normalize_inverse=false`, `dff_force_ratio=true`,
  `precond_raw_area=true`). The legacy inverse re-applied the forward 1/N, making the field ~N² too
  weak and inflating λ ~N²; the distortion grew with grid resolution and was the **grid-dependent**
  gap lever. Faithful inverse lands λ within ~50× of XPlace. adaptec1@512 −1.0%, adaptec2@1024 −3.0%.
- **High-degree net masking** (`ignore_net_degree = 100`) — drop >100-pin nets from the WL gradient
  and the reported HPWL, matching XPlace `--ignore_net_degree`. Makes the HPWL comparison
  apples-to-apples and removes power/clock-rail force distortion.
- **ePlace auto-grid + row cap** — `bins ≈ 2^round(log2(√N_movable))`, then capped so
  `bins ≤ num_rows` (`bin_height ≥ cell row height`), matching XPlace's `num_bin ≤ num_rows` guard.
  This cleared the entire "grid finer than the row structure" stall cluster (e.g. pci_bridge32_b
  0.32→0.07). `bins_per_row` is left unset so the formula runs.
- **Scale-invariant λ schedule** — the density-weight worsening-branch damping uses the **relative**
  form (`μ ∝ 1.05^(−Δhpwl/prev_hpwl·100)`), which fixed the severe std-cell stalls (des_perf_1
  0.82→0.066). (XPlace's fixed constant is a coordinate-frame difference — see Section 3.)
- **Die area = core rows + `die_shift` + fixed-area clip** (2026-07-15). Bookshelf die is now the
  `.scl` core-row bbox (was terminal-inferred, +9% too large); all coords are shifted so the die LL
  is the origin (XPlace `die_shift`) and un-shifted on DEF output; FIXED area is clipped to the die.
  adaptec1: die 1.245e8→1.1419e8, fixed 6.41e7→4.916e7, fillers 133K→**160,067** — all bit-exact to
  XPlace.
- **Fixed-terminal density clip / "moat" fix** (2026-07-16). 480/543 adaptec1 IO terminals sit
  outside the core-row die; the density deposit was shift-clamping them onto the edge bins, forming a
  false fixed-density moat that repelled cells/fillers from the die periphery. FIXED nodes are now
  geometrically clipped to the die (movable/filler keep the area-conserving edge shift), matching
  XPlace's `init_density_map`. adaptec1: floored 0.107 → **converges 0.07 (primary)**, HPWL
  6.913e7→**7.048e7 = XPlace 7.060e7** (0.98→0.998).
- **Filler generation** (`compute_filler_without_fence`) — filler size, formula, target-density and
  movable-area accounting all match XPlace exactly (adaptec1 size 14.44×12, 160,067 fillers).
- **Random-center init** (`init_method="random_center"`) — tight Gaussian cluster at die center
  (σ = 0.001·die span), fillers seeded uniformly across the die, matching XPlace.
- **Convergence** — 0.07 overflow stop (`= --stop_overflow`) plus a 30-iteration countdown after the
  first crossing (XPlace's `convergence_iterations`), and a `density_jolt` (enlarge_density) that
  doubles λ once on an early high-overflow plateau.
- **Preconditioner BB-clamp fix** — the BB step upper clamp is scaled by the mean preconditioner
  weight (exactly 1.0 when precond is off ⇒ no-op), so the step doesn't collapse as λ grows.
- **PLACED→FIXED status rule** — DEF components with PLACED status and non-CORE/BLOCK class are
  treated as FIXED (faithful defensive match; a no-op on the current benchmarks).
- **Pin offsets** — pin positions are offset from the node origin (bookshelf center-relative → LL).

---

## Section 2 — XPlace details incorporated with no noticeable difference

- **Preconditioner** (diagonal Jacobi, `#pins + precond_coef·λ·area`) — **a wash on fixed-macro
  ISPD2005** (it only damps *movable* macros, and those benchmarks fix all macros), which is why it
  looked pointless for a long time. It is **essential on mixed-size (MMS)** — see Section 3 (now
  auto-enabled by movable-macro presence).
- **Bin-tied γ** — XPlace sets `γ = 4·(bin_w+bin_h)` (halves at 1024 grid). A/B vs our grid-independent
  γ: matching XPlace's bin-tied γ did **not** help and hurt some designs (adaptec2@1024 +27% vs +8%).
  Neutral-to-negative; not adopted.
- **λ ramp schedule tuning** — matching XPlace's worsening-branch schedule exactly was quality-neutral;
  the schedule is not the residual-gap lever.
- **Pin offsets A/B** — enabling real pin offsets moves HPWL only ~0.2% (not the ~5% gap once
  suspected).
- **`init_method` A/B** — uniform-box vs XPlace-style random-center is a wash on quality; random-center
  kept only to mirror XPlace.
- **Density jolt interval A/B** — `density_jolt_interval` 1000 vs 50 vs 25 on adaptec1: negligible
  (best-solution iter 766→748, HPWL +0.04%). The jolt fires ~once regardless; it is an early-escape,
  not a plateau-breaker.
- **PLACED→FIXED rule** — correct and faithful, but a no-op on every current benchmark (their
  COMPONENTS blocks have no PLACED entries).

---

## Section 3 — Known differences we keep (and why)

- **Convergence overflow metric — filler-EXCLUDED.** XPlace's GP **stop** signal (`overflow_fn`) uses
  movable **+ filler** density; `sw_only` stops on the filler-**excluded** overflow (which equals
  XPlace's *exact* overflow, ~0.11 on adaptec1). **Full-suite A/B done (2026-07-17,
  DSE_20260717_005948 vs _20260716_161321):** filler-inclusive converged on adaptec1/2 and the mgc
  std-cell designs (moat fix; adaptec1 iter 961, 1.003× XPlace), but did NOT finish on the larger
  ISPD2005 designs (adaptec3/4, bigblue2/4) within the 1200-iter cap. Those were **still descending
  at iter 1200** (min overflow = last iteration) — a **budget** effect, because the filler-inclusive
  metric starts ~2× higher (fillers add density) and descends slower, not overshoot. We keep
  filler-excluded as the default (it converges 26/28 within budget); filler-inclusive is a viable but
  slower faithful alternative that would need a higher iteration cap. (`convergence_include_fillers`,
  default false.)
- **λ-overshoot near convergence — the real holdout mechanism (bigblue3, mm_a).** Separate from the
  filler question. On bigblue3 (default metric) sw_only spreads healthily to iter ~700 where it is
  essentially at XPlace's answer (overflow 0.113, HPWL 3.17e8 vs XPlace 0.051 / 2.91e8), but λ keeps
  ramping and the placement over-spreads — HPWL explodes 3.17e8→5.01e8 (+58%) and overflow *rises* to
  0.139; the divergence guard keeps the iter-700 solution, so it floors at 0.11 and never reaches
  0.07. XPlace descends monotonically through the same region. Diagnostic: at matched overflow
  sw_only runs a much lower λ than XPlace (overflow 0.30 at λ≈0.003 vs XPlace λ≈0.20), so λ lags then
  catches up abruptly in the critical zone and the density force overpowers wirelength. Fixing this
  (smoother/step-bounded λ near convergence) is the next lever for the remaining holdouts AND would
  let the faithful filler-inclusive stop finish.
- **γ scaling — grid-independent.** `sw_only` uses `base_gamma = init_gamma·die_span/gamma_ref_grid`
  with `gamma_ref_grid = 512` (grid-independent), vs XPlace's bin-tied γ. A/B showed our optimizer
  descends the grid-independent (softer at 1024) WA landscape better than the bin-tied one. Deliberate,
  A/B-justified divergence.
- **λ worsening-branch — relative, not fixed-K.** XPlace uses a fixed constant `K = 350000` in the
  density-weight damping; `sw_only` uses the scale-invariant relative form. XPlace's K assumes its
  site-width-normalized HPWL frame; `sw_only` works in raw bookshelf DBU, where the fixed K mis-scales
  std-cell designs (HPWL 5e8–1e9) and crushes the λ ramp. (`density_weight_worsening_hpwl_norm` can
  select the fixed-K form if the HPWL is normalized first.)
- **Grid sizing — the formula reproduces XPlace's hand table (15/16 on MMS).** XPlace picks `num_bin`
  from a hand-tuned per-design lookup (`setup_dataset.py:setup_design_args`); `sw_only` uses the ePlace
  formula (√N_movable → pow2) + the row cap. **The grid genuinely matters** — that is *why* XPlace
  hand-tunes it (a too-coarse grid under-reads density → premature stop → legalization blows up, see the
  MMS section below). With the **macro-excluded divisor** (2026-07-18, commit 7aa22d9 — the auto-grid
  divides placeable area by the average *std-cell* area, not the all-movable average that big macros
  inflate), the formula now matches XPlace's hand table **exactly on 15 of 16 MMS designs**:

  | design | mov macros | sw_only formula grid | XPlace tuned grid | match |
  |---|---|---|---|---|
  | adaptec1 | 63 | 512 | 512 | ✓ |
  | adaptec2 | 127 | 1024 | 1024 | ✓ |
  | adaptec3 | 58 | 1024 | 1024 | ✓ |
  | adaptec4 | 69 | 1024 | 1024 | ✓ |
  | adaptec5 | 76 | 1024 | 1024 | ✓ |
  | bigblue1 | 32 | 512 | 512 | ✓ |
  | bigblue2 | 924 | 1024 | 1024 | ✓ |
  | bigblue3 | 65 | 2048 | 2048 | ✓ |
  | bigblue4 | 195 | 2048 | 2048 | ✓ |
  | newblue1 | 64 | 512 | 512 | ✓ |
  | newblue2 | 25 | 1024 | 1024 | ✓ |
  | newblue3 | 51 | 2048 | 2048 | ✓ |
  | newblue4 | 81 | 1024 | 1024 | ✓ |
  | newblue5 | 91 | **2048** | **1024** | ✗ |
  | newblue6 | 74 | 2048 | 2048 | ✓ |
  | newblue7 | 161 | 2048 | 2048 | ✓ |

  The lone miss (newblue5) is a **target_density** difference, not a grid-formula failure: XPlace lowers
  `target_density` to 0.5 for newblue5 (also adaptec5, newblue4/6), and the formula's bin count scales
  with `placeable_area·target_density/avg_cell` — halving td there would cross the pow2 boundary to 1024.
  `sw_only` keeps td = 1.0 (= `maximum_utilization`) uniformly. **Empirical confirmation** (grid sweep,
  adaptec1 = formula 512): HPWL is a flat 6.366e7 / 6.378e7 / 6.371e7 at 512 / 1024 / 2048 — the formula
  grid is the HPWL sweet spot; finer grids only lower the overflow *reading* (honester density) without
  improving wirelength. We keep the principled formula (with the macro-excluded divisor): it now
  *derives* XPlace's expert grid choices instead of needing a table.
- **Preconditioner — auto-ON for movable macros, off for fixed-macro (2026-07-17/18).** The old "keep
  it off, it's a wash" verdict was a **wrong-benchmark artifact** — it was only ever tested on
  fixed-macro ISPD2005 (all macros FIXED → nothing for the preconditioner to damp). On **MMS**
  (movable-macro) it is **decisive**: MMS adaptec1 with precond OFF does NOT converge (overflow plateaus
  0.126, HPWL 1.05e8), ON (scale=1) converges cleanly (0.038, 6.36e7, ~40% lower). `sw_only` now
  **auto-enables** the preconditioner iff the design has movable macros (commit 638b9a8, die-relative
  0.02% area threshold; explicit `enable_preconditioning` always wins; fixed-macro path bit-identical).
  `precond_density_scale=1` beats the ~50× field-norm "basis match" (macros self-damp via their own huge
  area term). The site-width-normalization concern is a fixed-macro λ-magnitude issue, orthogonal to this.
- **Coordinate frame — raw bookshelf DBU.** `sw_only` optimizes in raw DBU; XPlace divides coords by
  `site_width`. For ISPD2005 (`site_width` = 1, GCD scale 100) the frames are numerically equivalent,
  but the λ-magnitude, fixed-K, and preconditioner differences above all trace back to this choice.
- **Mixed-size designs — not fully supported.** `mgc_matrix_mult_a` (macros 484–782 µm among 2 µm
  std cells) early-stops without converging. XPlace handles it with `include_macros` and a 2×
  (0.14) stop overflow; `sw_only` lacks dedicated mixed-size support. Set aside. (Superseded in part by
  the MMS work below — movable-macro convergence now works with the auto preconditioner.)

---

## Section 4 — MMS (Modern Mixed-Size, movable macros) — 2026-07-17/18

The first benchmark set with **movable** macros (ISPD2005 with the macros freed; adaptec1-5, bigblue1-4,
newblue1-7, bookshelf). Acquired from DREAMPlace's Dropbox → `host/benchmarks/mms/`. This is where the
preconditioner and grid-sizing findings above were established. Data: `MARK_TO_REVIEW/mms_data_20260717.md`.

**XPlace MMS reference** (`main.py --dataset mms --mixed_size True --num_threads 8`, seed 42; GP + LG + DP).
`sw_only` precond-ON GP is **+0.2% to +3.6% vs XPlace GP** on the 8 designs where its density grid resolves
overflow comparably to XPlace (adaptec1-4, bigblue1-2, bigblue4, newblue2). Legal-vs-legal (run `sw_only`'s
GP through XPlace's own legalizer, `tools/legalize_swonly_mms.sh`): `sw_only` **beats XPlace** on adaptec1
(6.68e7 vs 6.81e7, −2.0%), ties bigblue1/2 and adaptec2 (grid-fixed, −0.3%), and adaptec4 +1.9%.

**Overflow under-read (the reason grid size matters).** On macro-heavy designs a too-coarse grid makes
`sw_only`'s own overflow read far below XPlace's exact overflow on the *same* placement (adaptec2 at the
old 512 grid: self 0.060 vs XPlace 0.087) → it stops too early → the std-cell legalizer must spread cells
far → HPWL blows up (+17.5%). The **macro-excluded grid divisor** (Section 3) fixes this (adaptec2 → 1024,
self 0.057, legalizes −0.3%). A *second, separate* under-read affects the newblue family (self ~0.05 vs
XPlace ~0.24, grid-independent) — an unresolved density/overflow-metric mismatch that makes their raw GP
"wins" (−6 to −24%) untrustworthy (under-spread placements). Open item.
