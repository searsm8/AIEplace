# REPORT #23 — `init_step_seed` is now in site widths, not raw DBU

*2026-08-10. Closes the "decide the fix" bullet of TODO #23. Code: [[Step.cpp]], [[DataBase.cpp]],
[[DataBase.h]], [[Setup.cpp]], [[default_config.toml]], and the pl_algo mirror in [[Driver.cpp]] /
[[Placement.hpp]] / [[main.cpp]].*

## The one-line change

```cpp
step_length = init_step_seed * site_width;   // was: init_step_seed
```

in `Placer::estimateInitialStep()`. Everything else in this report is why that line is right, and
why the larger change it appears to imply is not.

## What was broken

`estimateInitialStep()` takes one trial step `x' = x0 − seed·P·g0`, then sets
α = ‖Δx‖ / ‖P·Δg‖. On `mgc_superblue{11_a,12,14,16_a}` and `mgc_des_perf_b` the trial displacement
landed below one float32 ULP of the coordinates, so **Δx was exactly 0 ⇒ α = 0**, and a zero step is
self-sustaining: the run no-opped to max_iterations, λ ramped to 1e29, and it reported the untouched
initial placement as its HPWL. Since 2026-08-10 it aborts loudly instead, but it still did not place.

## Root cause: a unit mismatch with XPlace, not a precision problem

XPlace's estimator is character-for-character ours, **including the absence of any guard**
(`Xplace/src/initializer.py:171-177`):

```python
def estimate_initial_learning_rate(obj_and_grad_fn, constraint_fn, x_k, lr):
    x_k   = constraint_fn(x_k).clone().detach().requires_grad_(True)
    obj_k, g_k     = obj_and_grad_fn(x_k)
    x_k_1 = (constraint_fn(x_k - lr * g_k)).clone().detach().requires_grad_(True)
    obj_k_1, g_k_1 = obj_and_grad_fn(x_k_1)
    return (x_k - x_k_1).norm(p=2) / (g_k - g_k_1).norm(p=2)
```

It never trips because of what happens **upstream**, in `database.py:850-856`:

```python
def preprocess(self):
    self.preshift()                  # die -> origin
    self.prescale_by_site_width()    # every coordinate / site_width
    # if args.scale_design:
    #     self.prescale()            # the [0,1] unit-box one -- COMMENTED OUT
```

So XPlace places in **site-width units**, and its `args.lr = 0.01` is a displacement of 0.01 *site
widths*. Ours was 0.01 **raw DBU** — on an ISPD-2015 die whose site is 100–200 DBU wide, a step
100–200× smaller in physical terms, and small enough to vanish into the rounding of a ~3.5e6 DBU
coordinate.

### The correction that matters for future work

**Normalizing coordinates by a constant buys no precision at all.** float32's relative epsilon is
scale-invariant, so dividing every coordinate by 200 divides the ULP by 200 with it. The physical
resolution is identical in both frames:

| frame | coordinate (superblue11_a) | ULP | ULP in DBU |
|---|---|---|---|
| ours (raw DBU) | 3.5e6 | 0.25 | **0.25** |
| XPlace (sites, ÷200) | 1.8e4 | 1.9e-3 | **0.38** |

What normalization changes is **what absolute constants mean** — nothing else. That is the whole of
the divergence here, and it is why the fix is a multiplication rather than a refactor.

The corollary is worth keeping: **shifts buy precision, scales do not.** XPlace's `preshift()` is
the shift (a no-op for these designs — all three DEFs checked carry `DIEAREA ( 0 0 )`), and TODO #15
(net-local coordinate frames) is a shift, which is why it measured **301×** on adaptec1 late where
scaling would measure 1×.

## Why not adopt XPlace's normalization globally

It was considered and rejected. A normalization refactor buys exactly one thing: hyperparameters
carrying coordinate units become design-relative in one stroke. Counting the active parameters in
`default_config.toml`, there is **one** such parameter — `init_step_seed`. `init_gamma` used to be
the second and was already handled the same targeted way, via `gamma_bin_scaled` /
`gamma_ref_grid = 512`; everything else is a ratio, a count, or a threshold.

Against that: normalization touches every geometric quantity (grid sizing, bins, footprints, HPWL
reporting, legalization, DEF writeback), re-tunes the whole suite, invalidates every baseline and
the 44-design snapshot, and desynchronizes pl_algo's frozen algorithm. Not worth it for one config
line.

⚠️ **The exception is fixed-point.** `ap_fixed` has an absolute resolution, so scale genuinely does
matter there in a way it does not in float. If pl_algo ever narrows to `ap_fixed`, normalization
stops being cosmetic. That is already the stated motivation behind #15 and belongs there.

## Where site width comes from

Mirrors `m_row_height` exactly — same two sources, no new parsing:

| input | source | value seen |
|---|---|---|
| LEF/DEF | `lefiSite::sizeX()` in `lef_site_cbk`, microns, scaled to DBU in `readDesignFiles()` | 200 (`mgc_fft_a`, `mgc_pci_bridge32_b`, `mgc_des_perf_b`), 100 (`mgc_superblue11_a`) |
| Bookshelf | `row.site_width` in `add_bookshelf_row` (.scl `Sitewidth`) | **1** on every ISPD2005/MMS design |

**Bookshelf `Sitewidth = 1` is the load-bearing fact**: it means ISPD2005/MMS coordinates are already
effectively site units, `seed · 1 == seed`, and the entire tuned MMS suite is bit-unchanged. It is
also why the bug only ever appeared on ISPD-2015 — and it is the same reason XPlace's `lr = 0.01`
works on both suites unmodified, which makes this change *faithful*, not a workaround.

Fallback if a design names no site: row height, then a hard error. Both are the design's own length
scale; neither is raw DBU.

## Verification

| check | result |
|---|---|
| `make test-regress` (mgc_fft_a, mgc_pci_bridge32_b) | **red before, as intended**; baselines regenerated with `--reason`; green after |
| `make test-regress-slow` (mms/adaptec1, bookshelf) | **PASS bit-identical, baseline untouched** — the predicted no-op, confirmed rather than assumed |
| `make test` (pl_algo tier 1) | PASS, unaffected |
| `make host HOST=pl_algo` | builds (mirror compiles; not sw_emu-verified — needs the card) |

**Trajectory impact on the two regenerated designs** (chaotic reordering, not a quality claim):

| design | iters | final HPWL | Δ |
|---|---|---|---|
| `mgc_fft_a` | 731 → 727 | 5.903e8 → 5.906e8 | +0.05% |
| `mgc_pci_bridge32_b` | 751 → 752 | 7.248e8 → 7.196e8 | −0.72% |

**The designs the fix was for**, previously 0 iterations of actual movement:

| design | site width | α₁ (was 0) | outcome |
|---|---|---|---|
| `mgc_des_perf_b` | 200 | 43918.6 | **converged, 825 iters**, smoothed overflow 0.996 → 0.046 |
| `mgc_superblue11_a` | 100 | 288331 | **converged, 849 iters**, overflow 0.972 → 0.047, HPWL 7.443e10 → 3.300e10 |

`mgc_superblue11_a` is the clean demonstration: HPWL **improves 56%** over the initial placement
while overflow falls by 20×. (`mgc_des_perf_b`'s HPWL *rises* 3.7e8 → 1.54e9 over its run — that is
the usual artifact of measuring against a degenerate all-cells-stacked start, not a regression; its
overflow tells the real story.) Both previously reported the untouched initial placement after 2133
no-op iterations and a NaN.

## Follow-ups (carried in tasks.md)

1. **Re-run the 4 `nan_metrics` designs** in the 44-design snapshot and re-score. The median 1.0090
   is over 33 scored designs; these were excluded.
2. **`make test-regress` still cannot see this class of bug** — both fast designs are small, and the
   slow one is bookshelf where site width is 1. Adding one large LEF/DEF design remains open.
3. **The pl_algo mirror is compile-verified only.** `Driver.cpp::estimate_initial_step` now scales by
   `cfg.site_width`; it has never run against the golden.
4. Three comments claiming `init_step_seed` *is* XPlace's `args.lr` were false on units and are now
   correct — `Setup.cpp`, `default_config.toml`, `Placement.hpp`.
