# `macro_deposits_target_density` (#11b): re-run and LOCKED — toggle retired

**Date:** 2026-08-02 · Handoff item 2 (`1_REVIEW/_NEW_HANDOFF_phase2_breadth_20260801.md`).
Mark's standing decision was "expect this to be locked `true` and the legacy branch deleted,
once the evidence is re-taken" (TODO #8, 2026-08-01) — the only thing holding it back was the
original A/B's mean +5.2% HPWL worse, run on zero-filler arms. That's now re-taken, and the
result supports locking it. **Done: the toggle is retired, `true` is now unconditional.**

---

## 1. Re-run

`2_ARTIFACTS/gen_footprint_ab_configs.py` (already fixed per the handoff) + `run_footprint_ab.sh`
(the runner had gone stale — still listed the old `base/proj/macro/both` arms after `#11a` was
retired and the generator switched to `off/on`; config files existed but nothing ran. Fixed as
part of this). 24 runs (8 designs x 2 arms + 8 td=1.0 control designs x 1 arm), seed 42, XPlace
per-design grid/td, current filler-fixed code. 05:43–13:21, all exit 0. Raw data:
`2_ARTIFACTS/footprint_ab_results.tsv` (rows tagged `off`/`on`; older `base`/`proj`/`macro`/`both`
rows in the same file are the pre-filler-fix run, left in place as history, not reused here).

## 2. Result

| design | off (HPWL) | on (HPWL) | Δ |
|---|---|---|---|
| newblue1 | 5.894e7 | 5.889e7 | −0.08% |
| newblue2 | 1.490e8 | 1.495e8 | +0.34% |
| newblue3 | 2.638e8 | 2.604e8 | −1.29% |
| newblue4 | 2.338e8 | 2.319e8 | −0.81% |
| adaptec5 | 3.020e8 | 3.248e8 | **+7.55%** |
| newblue5 | 3.965e8 | 3.922e8 | −1.08% |
| newblue6 | 4.216e8 | 4.208e8 | −0.19% |
| newblue7 | 8.911e8 | 8.953e8 | +0.47% |
| **mean (8)** | | | **+0.61%** |
| **mean (excl. adaptec5)** | | | **−0.38%** |

The confounded original verdict (+5.2% mean) does not replicate. With correct fillers, the flag
is HPWL-neutral to slightly positive on 7 of 8 designs. adaptec5 is the one outlier, and it isn't
a regression — it's the opposite:

```
adaptec5_off:  [STOP] reason=diverged_hpwl iteration=1163                  (never reaches phase 2)
adaptec5_on:   [PHASE] name=mixed_size end_iteration=649 reason=converged  (clean phase-1 exit)
               [STOP] reason=converged iteration=1436                      (phase 2 also converges)
```

`off` is adaptec5's known phase-1 divergence pathology (memory `mms-hard-spreading-three-diseases`).
`on` **fixes it**: phase 1 converges cleanly at iteration 649 instead of diverging at 1163, phase 2
proceeds normally, and the run ends `converged`. The "+7.55%" delta is comparing a genuine result
against a diverged one, not two comparable placements — if anything it undersells the flag, since
a diverged run isn't a real baseline. This is a second, independent confirmation of the TODO #8
finding that a movable macro emitting permanent unrecoverable overflow (the `off` behavior)
destabilizes exactly the designs it does this most on.

## 3. Decision: LOCKED — toggle retired

Per Mark's standing decision, criteria met (correct fillers, quality neutral-to-positive, one
design's divergence outright fixed): `macro_deposits_target_density` is no longer a config option.
`computeNodeFootprint` (`Grid.cpp`) always applies the XPlace-faithful branch now
(`target_density < 1.0 && isMovableMacro() -> weight = target_density`, per `database.py:921-923`).

Retired (config key + member + dead branch, per TODO #2's retire-settled-toggles pattern):
`AIEplace.h` (`macro_deposits_target_density` member + its stale confounded-verdict comment
block), `Setup.cpp` (config load + `Grid::setMacroDepositsTargetDensity` call), `Grid.h`
(`FootprintConfig::macro_deposits_target_density`, `Grid::m_macro_deposits_target_density`,
`setMacroDepositsTargetDensity()`), `Grid.cpp` (the `cfg.macro_deposits_target_density &&` guard),
`run_config.toml` (the key + its now-resolved pending-evidence comment).
`DataBase.cpp:373`'s comment keeps the retired name as a historical pointer (matches how `Grid.cpp`
still says "(TODO #11a)" after that flag's own retirement).

Rebuilt, `verify_swonly.sh` fast set bit-identical (ISPD2005/adaptec1 never triggers this branch —
`target_density < 1.0` structurally excludes it, so the retirement is a no-op there regardless of
the flag's prior default).

## 4. Not done

- **`2_ARTIFACTS/gen_footprint_ab_configs.py` / `run_footprint_ab.sh` now describe a retired flag**
  and can't produce a meaningful A/B any more (there's only one branch left). Left as historical
  record in the gitignored `2_ARTIFACTS/` dir, same as `mms_baseline_20260731.tsv` and the other
  frozen sweep artifacts — not deleted, not updated.
