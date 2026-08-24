# HANDOFF #35 — the MMS regression is in `#3`'s density field, and we don't know why

*Written 2026-08-21 at commit `0e712e8`. Start here for #35; `tasks.md` #35 is the checklist, this
is the reasoning behind it. #34 is the closed half of the same story.*

## Orientation in one paragraph

`#3` (2026-08-17) changed how a bin's **fixed-component occupancy** is computed, from a cap
`min(ρ, td)` to a scale `min(ρ, 1)·td`, matching XPlace (`initializer.py:82`). It landed in one of
four places that compute that quantity. `#34` (2026-08-21) found the other three and made them
agree. **On ISPD that fix was a clear win** — mean 1.0126 → 1.0112, and `pci_bridge32_a`'s overflow
came into agreement with XPlace's own report of the same placement (gap 0.0151 → 0.0001). **On MMS
it changed nothing**, which killed the hypothesis that a lagging metric was the cause. The MMS tier
is still ~1.9 pp worse than before `#3`, and the cause is now known to be the density field itself.
That is #35.

## The numbers

MMS, 16 designs, post-DP HPWL ratio vs XPlace (legal-vs-legal):

| run | median | mean | what it is |
|---|---|---|---|
| `DSE_20260814_152306` | 1.0137 | **1.0161** | pre-`#3`, pre-`#32`/7a-7b — last known good |
| (2026-08-19, pruned) | 1.0192 | 1.0351 | `#3` in the field, metric stale |
| `MMS_sw_only_frozen_20260821` | 1.0188 | **1.0347** | `#34` fixed — all four copies agree |

**Fixing the metric moved the mean by 0.04 pp.** That is the falsification.

Per design, sorted by `target_density` (`#3` is algebraically a no-op at td=1, so this split is a
real discriminator, not a correlation):

| design | td | pre-`#3` | now | Δ pp |
|---|---|---|---|---|
| adaptec5 | 0.50 | 1.0456 | 1.1958 | **+15.02** |
| newblue4 | 0.50 | 1.0138 | 1.0639 | **+5.01** |
| newblue5 | 0.50 | 1.0153 | 1.0673 | **+5.20** |
| newblue1 | 0.80 | 1.0121 | 1.1089 | **+9.68** |
| newblue3 | 0.80 | 1.0144 | 1.0188 | +0.44 |
| newblue6 | 0.80 | 1.0141 | 1.0194 | +0.53 |
| newblue7 | 0.80 | 1.0113 | 1.0371 | +2.58 |
| newblue2 | 0.90 | 1.0305 | 1.0378 | +0.73 |
| adaptec1–4, bigblue1–4 | 1.00 | — | — | flat; `bigblue3` −8.13 is `#32`/7a-7b |

Every td<1 design regressed. Every td=1 design is flat or improved. Magnitude roughly tracks
(1−td), with `newblue1` the outlier that does not fit that trend cleanly.

## Established, with evidence

1. **`#3`'s field change carries it.** The td split above. `#3` is provably identical at td=1
   (`min(ρ,1)·1 == min(ρ,1)`), and the td=1 half is flat.
2. **It is not the metric.** `#34` made `computeOverflow` (the convergence signal), `density_bin.hpp`
   and `density_bin_model.cpp` all match `Grid::clampFixedDensity`; MMS moved 0.04 pp.
3. **The same formula HELPS low-td ISPD.** `mgc_pci_bridge32_b` (td=0.143) GP HPWL −8.67%,
   `_a` (td=0.384) −4.78%. So the formula is not simply wrong, and **td alone is not the
   discriminator** — ISPD's lowest-td design improved while MMS's did not.
4. **The new formula is XPlace-faithful.** `initializer.py:82`:
   `init_density_map.clamp_(min=0.0, max=1.0).mul_(args.target_density)`, and `init_density_map` is
   normalized to (0,1) per `database.py:671`. Overflow thresholds at
   `(density_map - target_density) * bin_area` (`evaluator.py:48`), which pairs with it.
5. **`make test` and `make test-regress` are green at HEAD.** `mms_adaptec1` (td=1.0) is
   bit-identical across `#34`, which is the control proving `#34` changed only what it intended.

## Ruled out — do not re-derive these

- ❌ **"The stale `computeOverflow` was feeding the schedule a wrong stop signal."** Plausible,
  mechanically real (it *is* the convergence signal — `Output.cpp:568`, `Phase2.cpp:216-217`), and
  **measured to be worth 0.04 pp on MMS.** Fixed anyway because three copies disagreeing is a bug
  on its own terms.
- ❌ **"td is misconfigured on these designs."** Our `benchmarks.py` values match XPlace's
  `utils/setup_dataset.py` hardcoded table exactly, checked design by design. `pci_bridge32_b`'s
  td=0.143 was verified against XPlace source *and* visually (see the GIF referenced below) — the
  whitespace is real.
- ❌ **"`#31`'s grid cap affects MMS."** It fires on zero of the 16; every MMS design's requested
  grid is at or below its `num_rows` cap.

## The constraint any hypothesis must satisfy

This is the part that makes #35 interesting, and it argues against the obvious guess.

The obvious guess is *"the phase-1 → phase-2 macro transition got less consistent."* **It got MORE
consistent.** Work it through at td=0.5, a bin half-covered by a macro:

| | rule | deposit |
|---|---|---|
| **phase 1** (macro is movable) | `weight = target_density` — `Grid.cpp:31-32`, `#11b`, matching `database.py:921-923` | 0.25·bin_area |
| **phase 2, OLD** (macro frozen → fixed) | `min(ρ, td)` | 0.50·bin_area |
| **phase 2, NEW** (macro frozen → fixed) | `min(ρ, 1)·td` | 0.25·bin_area |

So `#3` made the macro's density **continuous across the phase boundary** where it used to jump 2×,
and MMS got *worse*. Any explanation has to account for that.

A physical reading that fits: a frozen macro **actually occupies** that area. Scaling its
contribution by td tells the optimizer there is room where there physically is none, cells crowd
the macro perimeter, and the legalizer pays for it in displacement. Under that reading the old cap
was *wrong but usefully conservative* on macro-heavy designs. **This is a hypothesis, not a
finding** — it is untested and it does not explain why ISPD's fixed macros behave differently.

## Leads, ranked

### 1. Does our phase-2 frozen macro deposit match XPlace's? (best lead — a real divergence may be here)

XPlace **rebuilds `init_density_map` at the mixed-size transition** and folds the now-frozen
macros in explicitly, at **weight 1.0**:

```python
# initializer.py:21-24
if ps is not None and ps.zero_macro_grad:
    # compute the mov + fixed macro density map
    node_pos  = torch.cat([data.node_pos[data.is_mov_macro].contiguous(),  node_pos])
    node_size = torch.cat([data.node_size[data.is_mov_macro].contiguous(), node_size])
    node_weight = node_size.new_ones(node_size.shape[0])   # <-- weight 1.0, NOT target_density
```
…then that whole map goes through `clamp_(0,1).mul_(target_density)` at `initializer.py:82`.
The transition is triggered at `run_placement_nesterov.py:172-173` (`ps.zero_macro_grad = True`).

Ours: `Phase2.cpp:81` `db.freezeMovableMacros()` (defined `DataBase.cpp:412`) moves the macros
into `getFixedComponents()`. From then on `isMovableMacro()` is **false** for them
(`Grid.cpp:23` says so explicitly), so `#11b`'s `weight = target_density` override stops applying
and they deposit at the area-conserving weight, then go through `clampFixedDensity`.

**Check:** are those two the same function? Weight-1.0-then-scale vs area-conserving-weight-then-
scale coincide for a fully covered bin but *not* for a partially covered one, which is exactly
where `#3` bites. Confirm against `Grid.cpp:14-35` (`computeNodeFootprint`'s weight logic).

### 2. `rebuildFillers()` can raise `target_density` mid-run

`Phase2.cpp:92`: `target_density = db.rebuildFillers(target_density);` with the comment *"filler
sizing may have raised it"*, then `grid.setTargetDensity(target_density)`. So the `td` that
`clampFixedDensity` multiplies by is **not necessarily the configured td** after the phase
boundary. Under the old cap a raised td loosened the ceiling; under the new scale it *scales the
whole fixed baseline up*. Worth logging the before/after td on `adaptec5` and `newblue1` — the two
worst designs — before theorising further.

### 3. Two stale doc comments in `Density.cpp` (small, but this exact class of thing caused #34)

- `Density.cpp:267` — *"Fixed macros form a per-bin-capped baseline (mirrors clampFixedDensity)"*.
  "per-bin-capped" is the **old** formula's name.
- `Density.cpp:244-245` — *"their density is clamped so bins fully covered by fixed macros register
  as 'at capacity'"*. Still true for fully-covered bins, but imprecise now.

Fix these while you are in the file. `rules.md`: a comment that names a function is a claim.

### 4. `DensityVerify.cpp:59` is correct by construction — note it and move on

`pl_algo/src/DensityVerify.cpp:59` **calls** `grid.clampFixedDensity(TARGET_DENSITY)` rather than
reproducing it, so it inherited `#34`'s fix automatically. It is a fifth *site* but not a fifth
*copy*. No action; recorded so nobody re-audits it.

## The experiment to run first

**We have never measured `#3` in isolation.** Three states exist; only two have been run:

| state | field formula | all copies agree? | `#32` 7a/7b? | MMS mean |
|---|---|---|---|---|
| **A** | old `min(ρ,td)` | yes | **no** | 1.0161 (2026-08-14) |
| **B** | new `min(ρ,1)·td` | no (metric stale) | yes | 1.0351 |
| **C** | new `min(ρ,1)·td` | yes | yes | 1.0347 (HEAD) |
| **D** | old `min(ρ,td)` | yes | **yes** | ❓ **never run** |

The 2026-08-14 baseline confounds `#3` with `#32`'s 7a/7b because both landed in the same run.
**D is the missing cell** and it isolates `#3` exactly: D vs C is `#3` alone, with everything else
held at HEAD.

To build D, revert the formula in **all four** places (reverting one recreates the `#34` bug):

| file | revert to |
|---|---|
| `host/src/common/src/Grid.cpp:149-157` | `min(overlap, bin_area * td)` |
| `host/src/sw_only/src/placer/Density.cpp:~324-330` | same |
| `pl/src/pl_algo/src/modules/density_bin.hpp:~75,99` | same |
| `test/density_bin_model.cpp` (**two** sites) | same |

```bash
cd vck5000
# build D, then:
make test && make test-regress          # test-regress WILL go red -- expected, do NOT re-baseline
make dse DSE_ARGS="--designs tier3"     # ~90 min unattended
```

⚠️ **Do not regenerate regress baselines for the A/B.** D is a throwaway experiment, not a
direction. Revert the working tree afterwards.

If D ≈ 1.0161, `#3` alone is the whole regression and the decision is a clean
accept-the-faithful-cost vs fix-macro-handling call. If D is between 1.0161 and 1.0347, `#32`'s
7a/7b carries part of it too and the picture is more tangled than the td split suggests.

## Commands and where things are

```bash
cd vck5000 && make test                              # pl_algo tier-1, seconds
cd vck5000 && make test-regress                      # sw_only, ~12s (add -slow for the td=1 control)
cd vck5000 && make dse DSE_ARGS="--designs tier3"    # MMS, ~90 min
```

| artifact | what |
|---|---|
| `.claude/2_ARTIFACTS/results/MMS_sw_only_frozen_20260821/` | the regression, at HEAD. Has its own README |
| `.claude/2_ARTIFACTS/results/DSE_20260814_152306/` | **pre-`#3` reference, 2.0 GB, do NOT delete** — the only surviving "before" half |
| `.claude/2_ARTIFACTS/results/GOLDEN_sw_only_frozen_20260821/` | the ISPD golden `#34` produced |
| `.claude/2_ARTIFACTS/results/SUPERSEDED_sw_only_20260817_pre34/` | 2.2 GB, prunable — numbers recorded, banner added |
| `vck5000/results/single_runs/mgc_pci_bridge32_b/20260821_170044_435_cpu_cpu/viz_render/full/` | the GIF confirming td=0.143 sparsity is real |

⚠️ **Disk was at 96% (47 GB free) when this was written.** An MMS run is ~2 GB. Prune
`SUPERSEDED_sw_only_20260817_pre34/` first if you need headroom:
`bash tools/prune_run_artifacts.sh --go <dir>` keeps the CSVs and drops the payload.

## Landmines

- **sw_only is FROZEN.** Any behaviour change needs Mark's explicit decision. #35's A/B is a
  throwaway experiment, not a landing — revert it.
- **`prune_run_artifacts.sh` prints `(dry run - nothing deleted)` even with `--go`.** It does
  delete. Verify with `du`, don't run it twice.
- **`results.csv` was renamed `dse_results.csv`** (`4c9120f`). Archived runs keep the old name;
  new runs use the new one. Both hold the same thing.
- **Don't quote the regress-tier deltas as suite numbers.** `test/regress` configs auto-size their
  grid via the ePlace formula, so they run `pci_bridge32_b` at a different grid than the manifest's
  256. That is how a −20.67% regress figure and a −8.67% suite figure both came from one change.
- **Two sessions have been editing this tree concurrently.** Check `git status` before `git add -A`;
  an in-progress `rules.md` edit from another session got swept into a commit this way.

## Related

`tasks.md` #35 (the checklist) · #34 (the closed half) · #3 (where the formula change originated) ·
`#11b` (`Grid.cpp:31-32`, the movable-macro deposit rule that lead 1 turns on) ·
`journal.md` 2026-08-21 (the superseded narration and how the hypothesis died)
