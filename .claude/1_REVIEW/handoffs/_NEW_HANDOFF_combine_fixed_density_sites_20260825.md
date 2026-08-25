# HANDOFF — collapse the fixed-density clamp to ONE shared formula (host sites 1 & 2)

*Written 2026-08-25, after #35 landed the `min(ρ,td)` cap. A cleanup follow-up, not a behaviour
change: the goal is that the formula exists in **one** place so the two host copies can never
disagree again — which is exactly the bug #34 was.*

## Why this exists

The fixed-density clamp — now `min(ρ, td)` (the cap; a Mark-authorized divergence from XPlace,
see `CLAUDE.md` → "Deliberate divergences from XPlace" and the canonical comment at
`Grid::clampFixedDensity`) — is written out **four times**. Twice in the host:

| # | site (current lines) | operates on | role |
|---|---|---|---|
| 1 | `host/src/common/src/Grid.cpp:158` `Grid::clampFixedDensity` | `grid.m_bins` (2-D `Bin`, clamps `.total_overlap`) | the **solver field** the DCT consumes |
| 2 | `host/src/sw_only/src/placer/Density.cpp:336` (loop; `cap` at :282) | a **local flat `std::vector<float>`** | the **overflow metric / convergence signal** |

…and twice in pl_algo (`density_bin.hpp`, `density_bin_model.cpp`) — **out of scope here**, they
are HLS + its reference model and are owned by **#20 step 3** ("include the real header; delete
`density_bin_model`'s own stale copy").

**#34 is the cautionary tale:** site 2 silently kept the old formula for four days while site 1
changed, so sw_only optimized one density map and decided when to stop from another. The comments
at both sites now shout "these must match" — this task removes the *need* to keep them in sync by
hand.

## Why you can't just call site 1 from site 2

They differ in **data structure, not arithmetic**. Site 1 walks `m_bins[col][row].total_overlap`
(a 2-D array of `Bin` structs); site 2 walks its own `std::vector<float> density` indexed
`col*ny + row`. So `computeOverflow` cannot call `clampFixedDensity` — there is no shared buffer.
The thing they share is the **formula**, and that is what to extract.

## The change

Add one free function (header `Grid.h` next to `clampFixedDensity`, or a small
`density_formula.hpp` if you prefer it provider-agnostic):

```cpp
// The fixed-macro density clamp, min(overlap, bin_area*td) == min(rho, td). ONE definition;
// every site that builds a density map calls it. Deliberate divergence from XPlace's scale
// (min(rho,1)*td) -- see CLAUDE.md's divergence registry. Assumes a uniform bin_area (all bins
// equal), which the grid already guarantees (getBinDensities uses m_bins[0][0] area, Grid.cpp:173).
inline void capFixedDensity(float* density, size_t n, float bin_area, float target_density) {
    const float cap = bin_area * target_density;
    for (size_t i = 0; i < n; ++i)
        density[i] = std::min(density[i], cap);
}
```

- **Site 2** (`Density.cpp`): replace the `#pragma omp parallel for` cap loop with
  `capFixedDensity(density.data(), density.size(), bin_area, target_density);`. (Keep or drop the
  `#pragma` — see "bit-identical" below.)
- **Site 1** (`Grid::clampFixedDensity`): the bins aren't contiguous floats, so either (a) leave
  `clampFixedDensity` as the canonical *entry point* but have it call `capFixedDensity` per row if
  a row's `total_overlap` is contiguous, or (b) simplest and safest — keep `clampFixedDensity`'s
  loop but make the **one line** that computes the clamp call a shared scalar helper
  `float capOne(float v, float bin_area, float td)`. Option (b) guarantees bit-identical and still
  gives you a single definition of the arithmetic. **Recommend (b).**

Net: the arithmetic `min(v, bin_area*td)` lives in exactly one place; the two loops stay where they
are (they must, they walk different containers).

## The hard constraint: bit-identical (sw_only is FROZEN)

Any host change must leave `make test-regress` **bit-identical** — that is the freeze contract, and
the two td<1 regress designs (`mgc_fft_a`, `mgc_pci_bridge32_b`) are the ones that exercise the
clamp. The arithmetic is `min(v, bin_area*target_density)` either way, same operands, same order,
so a faithful extraction **is** bit-exact — but *verify it*, don't assume:

```bash
cd vck5000 && make test-regress            # must say "bit-identical", 0 baselines changed
cd vck5000 && make test-regress-slow       # add the td=1 control (mms_adaptec1, ~140s)
```

Two float-reassociation traps to avoid:
- Compute `bin_area * target_density` the **same way** at each site (site 2 already precomputes
  `cap` once; match that). Don't reorder to `target_density * bin_area` — for IEEE floats
  `a*b == b*a`, so that one is safe, but a fused/rearranged expression may not be.
- Don't change the OpenMP scheduling of the site-2 loop in a way that alters reduction order. This
  loop is a pure elementwise map (no reduction), so threading is order-independent — safe to keep
  or drop the pragma. If you drop it, confirm regress still bit-identical (it will).

## Landmines

- **The formula is a deliberate divergence, not a bug — do not "fix" it to `min(ρ,1)·td`.**
  Mark-authorized 2026-08-25 (#35), worth −2.38 pp MMS. Registered in `CLAUDE.md`. The helper's
  comment must carry that so the single definition is also the single warning.
- **Four sites, not two.** This handoff collapses the two **host** copies. The two **pl_algo**
  copies are HLS (can't call a host helper) and belong to #20 step 3. If you touch the shared
  header, reference it from the pl_algo comment so the eventual `#include`-the-real-header work
  lands them on the same spec.
- **Uniform bin_area.** The helper takes a scalar `bin_area`; `clampFixedDensity` currently reads
  `m_bins[col][row].bb.getArea()` per bin. All bins are equal (the grid is uniform;
  `getBinDensities` already assumes it at `Grid.cpp:173`), so a scalar is correct — but assert it
  once rather than silently assuming, or keep option (b)'s per-bin read to sidestep the question.
- **Concurrent editors.** This tree has had two sessions in it (the garbage-param incident of
  2026-08-25 was one). `git status` before you stage; don't sweep up someone else's edit.

## Done when

`capFixedDensity` (or `capOne`) is the sole definition of `min(ρ,td)` in the host, both sites call
it, `make test-regress` + `-slow` are bit-identical with zero baseline changes, and the divergence
comment lives with the helper. Optional stretch: reference the helper from the two pl_algo comments
so #20 step 3 can converge them later.

## Related

`CLAUDE.md` "Deliberate divergences from XPlace" · `Grid::clampFixedDensity` (canonical) ·
history.md #34 (the four-site drift) / #35 (the landing) · tasks.md #20 step 3 (pl_algo copies) ·
`.claude/1_REVIEW/reports/_NEW_REPORT_35_density_rho_dct_and_td_20260825.md` (the full ρ/DCT/cap
explainer).
