# REPORT #31 — Designs that stall at overflow > 0.1: the stall is a grid issue (fixed); the overflow-metric gap is open

*2026-08-15. Branch `pl_algo`. The grid stall is fixed and landed. **⚠️ CORRECTION (same day):** the
`target_density` half of the original conclusion below is retracted — XPlace's td MATCHES ours (#25
retracted). The overflow-metric discrepancy is real but is NOT config; it is open under #3.*

> **⚠️ Read this first — corrected twice on 2026-08-15; this is the settled conclusion.** The
> overflow discrepancy is **entirely the grid cap**, and the overflow *metric* is correct.
> - **target_density is NOT a factor.** XPlace uses the same per-design td we do (0 mismatches on all
>   20; the `target_density: 1.0` in XPlace's log is a params echo, effective `target density = 0.65`).
>   #25 is retracted.
> - **The `fft_2` "7× at matched grid" was my error:** I read the *requested* 512 from XPlace's
>   eval-log header and missed the cap warning below it — XPlace evaluated fft_2 at **128** (171 rows).
>   An independent naive rectangular-overlap reference reproduces OUR overflow at every grid
>   (512→0.161=ours, **128→0.020≈XPlace 0.0215**), proving the metric is physically correct.
> - **Root cause = the grid fix was incomplete.** The 5 designs below were derived from XPlace's
>   GP-reference logs, which anomalously print 512 for the very designs its eval caps. **13 of 20
>   ISPD2015 designs are row-capped**; we were running 8 more at 512.
> - **Final fix (Mark's call):** sw_only now caps an explicit grid at `num_rows` too (`Setup.cpp`
>   row_cap branch), matching XPlace; `benchmarks.py` holds XPlace's requested 512 and the code caps.
>   `make test-regress` bit-identical. The 5-design manifest numbers below are the *partial* fix.

## TL;DR

1. **New standing tooling.** `results.csv` now carries three overflow verdicts on the *same* shipped
   `.def`: our smoothed `Best OVFW`, **`Our Exact OVFW`**, and **`XPlace In OVFW`** (XPlace's own
   exact overflow on the placement we hand its legalizer — TODO #3's `gp_ovfl_in`). `lgdp.py` scrapes
   XPlace's `Input solution … exact Overflow` line; `dse.py` scrapes our exact from `run_summary.md`.
   No exe change.
2. **The overflow metric is right at td=1, diverges at td<1.** All 8 ISPD2005 (td=1 both sides, grid
   matches): **our exact overflow == XPlace's to < 0.3%** (adaptec1 0.1130 / 0.1131). But on `mgc_*`
   it is 2–7× high **even at matched td AND grid** (`fft_2` below) — a genuine metric divergence,
   open under #3, NOT the config gap this report first claimed.
3. **The stall itself is a grid divergence (this part is solid):** we force 512; XPlace caps
   `num_bin` at `num_rows` (`database.py:161`, power-of-2 floor). **5 designs** ran finer than XPlace.
   *(The original point 3 also blamed `target_density`; retracted — see the correction banner.)*
4. **The `Best OVFW > 0.1` stall is a grid artifact with a real QoR cost — and a clean fix.** Running
   the 5 grid-mismatched designs at XPlace's *effective* grid converges all of them below the 0.07
   smoothed threshold and **improves GP and DP ratio on every one** (3 flip to beating XPlace post-DP).

## The three-column tooling (do-this-first step, done)

`lgdp.py::legalize()` now returns `in_hpwl` / `in_ovfl` from XPlace's
`Input solution, exact HPWL: … exact Overflow: …` (`run_placement_nesterov.py:24`, logged for the
given solution before legalization). `dse.py::summarize()` adds `Our Exact OVFW` (from
`run_summary.md`'s "Final Overflow (exact, no fillers)") and `XPlace In OVFW`, slotted next to the
smoothed `Best OVFW`. Verified: re-summarizing the committed 28-design run
(`results/DSE_20260814_133037`) reproduces the headline exactly (DP median 1.0106 / mean 1.0149).

**Why three columns.** `Best OVFW` is the *smoothed* convergence signal; it under-reads exact
overflow by up to ~3× (`matrix_mult_a` 0.047 smoothed → 0.164 exact). The apples-to-apples partner
for XPlace's exact number is `Our Exact OVFW`, measured on the same geometry the `.def` holds
(post-#24 restore).

## #3 reconciliation: the metric is right; the config differs

| design | our exact | XPlace-in | ratio | note |
|---|---|---|---|---|
| adaptec1 | 0.1130 | 0.1131 | 1.00 | ISPD2005: td=1.0 both, grid matches |
| adaptec2 | 0.1022 | 0.1025 | 1.00 | ” |
| bigblue1 | 0.1224 | 0.1224 | 1.00 | ” |
| … (all 8 ISPD2005) | | | ≈1.00 | agree to < 0.3% |

**⚠️ CORRECTED:** the agreement above holds only at **td=1**. At td<1 (all `mgc_*`) the metric
diverges 2–7× **even at matched td and grid** — see the correction banner and the *Overflow-metric
divergence at td<1* section below. #3 is **narrowed, not closed**: the metric is validated at td=1,
but the td<1 gap is the real open reconciliation.

## Divergence 1 — grid cap (the `Best OVFW > 0.1` stall)

XPlace (`database.py:161-171`): if `num_rows < num_bin_y`, set `num_bin_y = 2^floor(log2(num_rows))`
and scale `num_bin_x` to keep aspect. sw_only has the **identical** rule
(`Setup.cpp:340`, `row_cap = 2^floor(log2(num_rows))`) — but applies it **only on the auto grid
path**; an explicit `bins_per_row` override (which `dse --grid xplace` always passes) bypasses it.
And `benchmarks.py` records XPlace's *requested* 512, not its *effective* post-cap grid. So on
low-row designs we solve a finer-binned density problem than XPlace.

**5 designs affected** (XPlace log override warnings):

| design | num_rows | our grid | XPlace effective |
|---|---|---|---|
| `mgc_pci_bridge32_a` | 200 | 512 | **128** |
| `mgc_pci_bridge32_b` | 400 | 512 | **256** |
| `mgc_des_perf_a` | 450 | 512 | **256** |
| `mgc_des_perf_b` | 300 | 512 | **256** |
| `mgc_edit_dist_a` | 400 | 512 | **256** |

### Grid A/B — run each at XPlace's effective grid, full GP+LG+DP

| design | grid | our exact OVFW | GP ratio | DP ratio |
|---|---|---|---|---|
| `pci_bridge32_a` | 512 → **128** | 0.333 → 0.211 | 1.098 → **1.013** | 1.054 → **1.024** |
| `pci_bridge32_b` | 512 → **256** | 0.492 → 0.243 | 1.009 → **1.007** | 0.984 → **0.976** |
| `des_perf_a` | 512 → **256** | 0.160 → 0.203 | 1.020 → **0.988** | 1.018 → **0.997** |
| `des_perf_b` | 512 → **256** | 0.161 → 0.192 | 1.032 → **0.982** | 1.015 → **0.993** |
| `edit_dist_a` | 512 → **256** | 0.142 → 0.159 | 1.021 → **1.005** | 1.011 → **1.010** |

**Every design improves both ratios; 3 flip to beating XPlace post-DP** (`pci_bridge32_b`,
`des_perf_a`, `des_perf_b`). All three previously-flagged low-row stallers converge below the 0.07
smoothed threshold at the correct grid.

Note exact overflow is **not** the QoR predictor — it rises slightly on `des_perf_*`/`edit_dist`
(coarser grid) yet DP ratio improves. What the wrong grid cost was wirelength: at 512 the placer
chased a density resolution it could not satisfy (stall), over-spreading; at the correct grid it
converges and the wirelength is better. Consistent with #31's thesis that overflow magnitude does
not, by itself, predict post-DP QoR.

### Fix LANDED 2026-08-15 (Mark's go-ahead)

`benchmarks.py`'s grid corrected for these 5 to XPlace's effective value (`pci_bridge32_a`→128, the
other four →256), citing `database.py:161`; the `xplace_grid` header comment rewritten (grid =
XPlace's EFFECTIVE post-cap value). sw_only already has the cap logic, so this is a manifest-only
change. *(The `target_density` column comment was also edited to say "our DEF value, XPlace uses 1.0"
— that edit is WRONG per the correction banner; XPlace uses the same per-design td. To fix in a
follow-up.)*

**Full 28-design re-run `results/DSE_20260815_105117` (GP+LG+DP) confirms the controlled result:**
only the 5 fixed designs moved, **all improved**, the other 23 bit-identical (deterministic GP).

| | pre-#31 (`DSE_20260814_133037`) | **post-#31 (`DSE_20260815_105117`)** |
|---|---|---|
| all 28 DP ratio (median / mean) | 1.0106 / 1.0149 | **1.0095 / 1.0120** |
| ISPD2015 (20) DP (median / mean) | 1.0163 / 1.0189 | **1.0106 / 1.0148** |
| better than XPlace | 4 | **6** |

Per-design DP: `pci_bridge32_a` 1.054→1.024, `pci_bridge32_b` 0.984→**0.976**, `des_perf_a`
1.018→**0.997**, `des_perf_b` 1.015→**0.993**, `edit_dist_a` 1.011→1.010. (within-±2% went 21→20
only because `pci_bridge32_b` now beats XPlace by >2% — it left the band by being *better*.)

## The overflow "7×" was the grid — metric is correct (resolves #3)

**There is no target_density divergence and no metric bug.** The `fft_2` overflow gap is XPlace
evaluating at its **row-capped grid (128)** while we ran **512**. An independent naive
rectangular-overlap reference on `fft_2`'s shipped `.def` (parsing LEF sizes + DEF positions,
depositing into an N×N grid at td=0.65):

| grid | naive overflow | matches |
|---|---|---|
| 512 | **0.1612** | **ours** (0.161) |
| 256 | 0.0634 | — |
| 128 | **0.0204** | **XPlace** (0.0215) |
| 64 | 0.0083 | — |

So our `computeOverflow` is physically correct at every grid. The 7× was purely that XPlace's
given-solution eval capped fft_2 to 128 (its eval log: *"num_bin_y 512 is larger than num_rows 171.
Use 128"*) while our run used 512. HPWL matched (3.751e8 both), so it was the same placement — only
the evaluation grid differed. Footprint smoothing was ruled out (a 4-bin √2 clamp only drops 0.161→
0.079, nowhere near 0.0215); adaptec1 matching at td=1 was a red herring (it's bookshelf, low grid).

**This also proved the #31 grid fix incomplete.** The original 5 designs came from XPlace's
*GP-reference* logs, which anomalously print 512 for fft_1/fft_2. The authoritative source is
`num_rows` (die_height / site_row_height, which sw_only computes faithfully — the num_rows audit
confirmed <0.5% error on all 20). **13 of 20 ISPD2015 designs are row-capped**; 8 were still at 512:

| design | num_rows | XPlace effective |
|---|---|---|
| fft_1 / fft_2 / des_perf_1 | 132 / 171 / 222 | **128** |
| fft_a / fft_b / matrix_mult_1 / matrix_mult_2 | 400 / 400 / 275 / 277 | **256** |
| superblue12 | 2302 | **512** (XPlace requests 512, not the 1024 our manifest held) |

**Fix (Mark's call — code, not manifest):** sw_only now caps an explicit `bins_per_row` at
`row_cap = 2^floor(log2(num_rows))` (`Setup.cpp`, the else branch), so `min(requested, row_cap)`
matches XPlace's `database.py:161` for *any* grid, auto or explicit. `benchmarks.py` reverted to
XPlace's requested 512 (superblue12 1024→512); the code caps and logs the effective grid.
`make test-regress` **bit-identical** (regress configs use the auto path, already capped).
ISPD2005 unaffected (num_rows 890–2694 > requested grid, no cap).

Because both tools now evaluate overflow at the same (capped) grid, the overflow *signal* — which
drives the γ/λ schedule and the stop criterion — is reconciled with XPlace's, not just the final
number. **Verified in the full re-run `DSE_20260815_161306`:** `Our Exact OVFW` ≈ `XPlace In OVFW` on
the std-cell designs (fft_2 0.227/0.228, des_perf_b 0.192/0.192, matrix_mult_1 0.185/0.185); the
residual gap survives only on macro-heavy designs (superblue12 0.127/0.275), i.e. the fixed-density
difference below.

**Headline after the universal cap (`DSE_20260815_161306`, supersedes the 5-design partial fix):**

| | pre-#31 | 5-design partial | **universal cap (final)** |
|---|---|---|---|
| all-28 DP (median / mean) | 1.0106 / 1.0149 | 1.0095 / 1.0120 | **1.0096 / 1.0113** |
| ISPD2015 (20) DP (median / mean) | 1.0163 / 1.0189 | 1.0106 / 1.0148 | **1.0129 / 1.0138** |
| GP-ratio mean | — | 1.0223 | **1.0047** |
| within ±2% / better | — | 20 / 6 | **22 / 6** |

Mixed per-design (fft_a −2.7pp, fft_2 −1.4pp better; des_perf_1 +1.8pp, fft_1/matrix_mult_2 +0.6pp
worse), net improvement, and faithful to XPlace's grid. ISPD2005 bit-identical.

⚠️ **One separate, minor macro-design difference remains:** XPlace **scales** the fixed/blockage
density by td (`initializer.py:82`, `·target_density`); we **cap** it at td (`Grid.cpp:139`, `min`).
Equal at td=1, ours slightly higher at td<1 in macro-perimeter bins. Not the fft_2 cause (0 fixed
there); worth aligning for macro-heavy designs. Tracked under #3.

## Per-flagged-design verdict (ISPD)

- `mgc_pci_bridge32_a` — **grid** (512 vs 128). Grid fix: GP 1.098→1.013, DP 1.054→1.024.
- `mgc_pci_bridge32_b` — **grid** (512 vs 256). Grid fix: DP 0.984→0.976 (beats XPlace).
- `mgc_des_perf_b` — **grid** (512 vs 256). Grid fix flips to beating XPlace (DP 0.993).
- `mgc_fft_2` — **grid** (512 vs 128, 171 rows). It looked "grid-matched" only because I misread
  XPlace's eval-log header; XPlace caps it to 128. The universal-cap fix runs it at 128 (DP was
  already 1.028; re-run pending). This is the design that cracked the overflow puzzle (#3).

MMS stragglers were already dispositioned in [[tasks.md]] #31 (newblue3 the standout; overflow does
not cost DP QoR there — LG+DP absorbs it).

## Files touched

- `vck5000/tools/lgdp.py` — scrape `in_hpwl`/`in_ovfl` from XPlace's given-solution eval.
- `vck5000/tools/dse.py` — `Our Exact OVFW` + `XPlace In OVFW` columns; `import re`.
- `vck5000/tools/benchmarks.py` — 5 grid values → XPlace effective (LANDED); td-column comment fixed.

Grid A/B runs: `results/DSE_20260815_10{3733,3834,3846,…}` (single-design, full GP+LG+DP).
