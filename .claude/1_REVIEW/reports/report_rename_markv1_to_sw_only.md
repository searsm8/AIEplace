# Report: rename `markv1` → `sw_only`

**Task:** `OVERNIGHT_WORK/rename_markv1_to_sw_only.md`
**Status:** ✅ complete, committed as `a88588a` (its own commit, per the task note).
**Behavior:** provably neutral — bit-identical output with a fixed seed (see Verification).

## Key scoping decision (READ THIS)
`markv1` names **three** things in the tree: `host/src/markv1` (the CPU software golden),
`aie/src/markv1`, and `pl/src/markv1` (the partial-offload **hardware** kernel variants,
selected by `AIE=`/`PL=`). The task scoped the rename to the **host software golden only**
(`host/src/markv1` → `host/src/sw_only`, `HOST` var). So:

- **Renamed → `sw_only`:** the host design, the `HOST` default, all build/exe/config paths
  derived from it, tooling that targets it, and every doc/comment that cites the **CPU
  golden** (its functions: `computeHpwlPartials_CPU`, `Grid::computeBinOverlaps`,
  `compute_eField_DCT`, `computeLipshitzEstimate`, the parser, `DCT.cpp`, `Common.h`, …).
- **Kept `markv1` (intentional):** `aie/src/markv1`, `pl/src/markv1`, `common.mk`
  `AIE ?= markv1` / `PL ?= markv1`, and comments citing the **AIE hardware kernels/graphs**
  (partials/FFT graphs, the Makhoul pre/post recipe). A blanket replace would have corrupted
  these references to still-existing hardware variants.

This split is now documented at the top of `CLAUDE.md` and `pl_algo/README.md`.

## What changed
**Functional (required for the build):**
- `git mv host/src/markv1 host/src/sw_only` — history preserved (git shows `R`/`RM`), including
  the prebuilt parser libs under `lib/`.
- `common.mk`: `HOST ?= sw_only`. Everything downstream is derived: `HOST_DIR`,
  `BUILD_DIR_HOST` (`build/hw/host/sw_only`), `HOST_EXE` (`aieplace_sw_only.exe`),
  `HOST_RUN_CONFIG`. No hardcoded paths needed changing in `host/Makefile` or the design's own
  `makeflags.mk` (both already use `$(HOST_DIR)`).
- `host/src/pl_algo/makeflags.mk`: `MARKV1_DIR` → `SW_ONLY_DIR` (the `-L .../sw_only/lib` parser
  libs the pl_algo host reuses).
- `tools/dse.py`: `EXE_PATH`/`CONFIG_PATH` → sw_only paths.

**Docs / comments / tooling:**
- `CLAUDE.md` — rewrote the variant description to reflect the host/hardware split.
- `tools/{compare_density,make_scorecard,make_viz_gifs}.py` — descriptive strings, labels,
  and the `load_sw_only`/`sw_only_exact_csv` identifiers.
- `pl_algo` source comments (`host/src/pl_algo/src/*`, `pl/src/pl_algo/src/*`, `modules/*`,
  `model/{density_bin_model,sched_verify}.cpp`) — CPU-golden citations → sw_only.
- `pl/src/pl_algo/model/density_model.cpp` — **mixed file**, hand-classified: CPU-golden lines
  → sw_only, AIE-kernel/graph/recipe lines kept as markv1.
- `pl/src/pl_algo/{DATAFLOW.md,README.md,CHECKPOINT.md}`, `build_reports/stage5c.md` — updated;
  in the two historical docs this also fixed now-broken `HOST=markv1` commands and
  `host/src/markv1/...` / `aieplace_markv1.exe` paths. The two **memory-slug tokens**
  (`markv1_cpu_run_gotchas`, `markv1_nonconvergence_vs_xplace`) were preserved.

**Memory (Claude auto-memory):**
- New `sw_only_rename.md` documenting the rename and the read-older-memories-as-sw_only note.
- `MEMORY.md` — prominent naming banner + index pointer; build command note.
- `markv1_cpu_run_gotchas.md` — fixed the broken build/run commands and exe/config paths;
  added the unseeded-random-init determinism note.

## Verification
1. `make host` (HOST=sw_only) — builds clean; exe at `build/hw/host/sw_only/aieplace_sw_only.exe`.
2. `make host HOST=pl_algo` — builds clean; links `-L .../host/src/sw_only/lib`.
3. **Bit-identical proof:** ran `mgc_matrix_mult_b` @64 with `"random_seed": 42` using the
   old on-disk `aieplace_markv1.exe` and the new `aieplace_sw_only.exe`. Both: **460 iters,
   HPWL 2.823e9, smoothed overflow 0.04206, exact 0.1611** — identical. (Without a seed the
   default config's random init gives ~0.4% run-to-run variation, which is why an unseeded
   A/B looks different; the seed is required for the check.)
4. `git grep markv1` over `vck5000`/`CLAUDE.md` (excluding the intentional
   `aie/src/markv1`, `pl/src/markv1`, AIE-kernel comments, and memory slugs) returns only the
   deliberate `AIE=markv1`/`PL=markv1` split descriptions.

## Commit hygiene note
As in the previous task, the working tree carried unrelated pre-existing work I did **not**
commit: deleted `tools/*.png`, untracked `tools/analyze_dse.py` + pl_algo build artifacts
(`_x/`, `model/density_*` binaries), and the `_gamma_ab()` addition in `dse.py`. I staged only
my `dse.py` path hunk (targeted `git apply --cached`) and added files by explicit path so the
PNG deletions and untracked artifacts stayed out of the commit.
