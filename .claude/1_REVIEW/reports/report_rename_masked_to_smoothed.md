# Report: rename "masked overflow" → "smoothed overflow"

**Task:** `OVERNIGHT_WORK/rename_masked_overflow.md`
**Status:** ✅ complete, committed as its own commit.
**Behavior:** pure rename, no logic change. Rebuilt binary runs clean and produces the
expected numbers.

## What changed
Renamed markv1's confusingly-named "masked overflow" metric (the √2-inflated /
area-conserving smoothed density field that drives GP convergence) to **"smoothed
overflow"**, disambiguating it from XPlace's unrelated net-mask `masked_hpwl`. The paired
**exact overflow** was left untouched (smoothed vs exact reads cleanly).

### Code (all in `vck5000/host/src/markv1/`)
| File | Change |
|---|---|
| `src/Density.cpp:409-410` | doc: "XPlace's *masked* overflow" → "*smoothed* overflow (… equivalent to XPlace's expand_ratio-inflated density field)" |
| `src/Density.cpp:420` | doc "Why masked matters" → "Why smoothed matters" |
| `src/Density.cpp:488` | doc "two CSVs — masked …" → "smoothed …" |
| `src/Density.cpp:505` | **CSV filename** `_rho_masked.csv` → `_rho_smoothed.csv` |
| `src/Density.cpp:512` | log string `"masked"` → `"smoothed"` |
| `src/Grid.cpp:56` | comment "the masked overflow metric" → "the smoothed overflow metric" |
| `src/AIEplace.cpp:809` | comment "masked overflow first crosses" → "smoothed overflow …" |
| `src/Output.cpp:401` | comment "the masked overflow that drove convergence" → "smoothed" |
| `src/Output.cpp:403` | **local var** `final_masked_overflow` → `final_smoothed_overflow` |
| `src/Output.cpp:405` | comment "(masked + exact)" → "(smoothed + exact)" |
| `src/Output.cpp:438` | **results label** `"Final Overflow (masked)"` → `"Final Overflow (smoothed)"` (user-visible) |
| `src/Output.cpp:681-684` | convergence-signal comments + inline `// masked (clamped)` → `// smoothed (clamped)` |
| `include/AIEplace.h:229-233` | `computeOverflow`/`dumpBinDensity` decl comments |

### Downstream (docstrings / consumer)
| File | Change |
|---|---|
| `tools/compare_density.py:4` | docstring CSV name `_rho_masked.csv` → `_rho_smoothed.csv` (the tool takes the path as an arg, so only the doc needed updating) |
| `tools/dse.py:112` | docstring "stop masked-overflow 0.04" → "smoothed-overflow" |
| `tools/make_scorecard.py:26` | docstring "stop masked-overflow 0.04" → "smoothed-overflow" |

## Decisions
- **CSV rename (option a in the handoff):** chose to rename the dumped CSV to
  `_rho_smoothed.csv` and update `compare_density.py`'s docstring, for full consistency.
  `compare_density.py` reads the path from a positional arg (no hardcoded name), so no code
  logic changed there.
- **`clamp` bool param → `smoothed`:** NOT done. The handoff marked this cosmetic and
  "keep separate/optional." Left as-is to keep the diff surgical.
- **Out of scope, intentionally left:** pl_algo docs (`pl/src/pl_algo/CHECKPOINT.md`,
  `param_scheduler.hpp:62`) still say "masked" — that's a separate design and the task
  scoped only to markv1. AIE kernels' "masked" comments mean vector-lane masking, an
  unrelated meaning.

## Verification
1. `make host` — builds clean (only pre-existing unrelated warnings). Exe at
   `build/hw/host/markv1/aieplace_markv1.exe`.
2. Ran `mgc_matrix_mult_b` @64 (seed 42, headless) end-to-end: EXIT=0, converged in 468
   iters. Output now prints **"Final Overflow (smoothed) 4.194e-02"** alongside
   **"Final Overflow (exact) 1.645e-01"** — the expected smoothed(~0.04)/exact(~0.16)
   split. HPWL 2.806e9.
3. Every changed line is a comment, log/label string, a local variable, or the output CSV
   filename — none touch the overflow/HPWL computation, so the result is bit-identical by
   construction.

## Commit hygiene note
The working tree already contained **unrelated uncommitted work** that I did NOT touch or
commit: deleted `tools/*.png` files, an untracked `tools/analyze_dse.py`, pl_algo build
artifacts (`_x/`, `model/*`), and a `_gamma_ab()` addition in `dse.py`. My `dse.py`
docstring change is a separate hunk from `_gamma_ab`; I staged only my hunk (via targeted
`git apply --cached`) so the rename commit contains just the rename.
