# `vck5000/tools/`

Every file here carries a status. **If you add a tool, add its row** — an unlisted tool is
indistinguishable from a stale one six weeks later, which is what this file exists to prevent.

| status | meaning |
|---|---|
| **live** | on the path to a number we quote, or run regularly. Fix bugs here. |
| **dormant** | works, answered a real question, nothing calls it today. Kept because it would cost more to rewrite than to keep. Read the note before reviving. |

The rule that decides whether a file belongs here at all: **anything that produces a number we
quote is tracked here; one-off experiment runners stay with their output** in `.claude/2_ARTIFACTS/`.

---

## The manifest — read this first

| file | status | what |
|---|---|---|
| `benchmarks.py` | **live** | **Master benchmark manifest, and the single source of truth for per-design grid + target_density** (XPlace's own tuned values from `setup_dataset.py`), plus the XPlace HPWL references. Never hardcode a design list or a grid in another tool — query this. |

## Scoring pipeline — produces the numbers we quote

**`dse.py` IS the scoring pipeline** (`make dse`, TODO #30, collapsed 2026-08-26): one command
generates configs, runs GP, and legalizes + detailed-places each result through `lgdp.py`
(XPlace's own legalizer), emitting `Our LG HPWL` / `Our DP HPWL` / `DP Ratio` — the legal-vs-legal
headline. The seven-script pipeline it replaced (`gen_suite_configs.py` → `run_suite.sh` →
`run_lgdp44.sh` / `run_lgdp_suite.sh` → `analyze_full44.py` / `analyze_lgdp_suite.py`) is retired;
see *Removed 2026-08-26*. The tier1+tier2 cross-check reproduced its committed numbers exactly
(TODO #30), and the tier3/MMS path was spot-checked against `_XPLACE_MMS_FINAL` (DP ratios
0.98–1.02, bookshelf `--mixed_size` arm).

Code is tracked; **results are not**. `dse.py` writes to `results/DSE_<ts>/` (gitignored); the
XPlace reference runners default `$ARTIFACTS` to the repo-root `.claude/2_ARTIFACTS/`.

The one piece still run **by hand**, feeding `benchmarks.py`:

| file | status | output |
|---|---|---|
| XPlace ref | `run_xplace_ref.sh` (ispd2005), `run_xplace_ref_2015.sh` (ispd2015) | **live** | `xplace_ref_ispd.tsv` → populates `benchmarks.py::_XPLACE_ISPD_FINAL` by hand |

Legalization runs through **XPlace's own legalizer** (`main.py --global_placement False`), not
OpenROAD — see *Dormant* for the opendp island.

Supporting converters, called by `lgdp.py`:

| file | status | what |
|---|---|---|
| `def_to_bookshelf_pl.py` | **live** | sw_only DEF → bookshelf `.pl` (bookshelf tiers). Frame is bit-perfect. |
| `def_patch_placement.py` | **live** | the DEF analogue, for the LEF/DEF ISPD2015 tier. Patches placements into the **original** `floorplan.def` — sw_only writes no ROW statements and the legalizer needs the site rows. |
| `post_dp_density.py` | **live** | density metrics on a legalized placement, both sides. Post-DP HPWL alone flatters an under-spread GP (TODO #3). Only discriminates below target_density 1.0. |
| `fence_check.py` | **live** | how badly a placement violates DEF fence regions (TODO #26). We solve unconstrained; so does XPlace. |
| `analyze_fence_cost.py` | **dormant** | separates "our placer is better" from "we ignore the fence" (#26, closed). Takes two `<suite>-schema` TSVs as args; its generator `run_lgdp44.sh` was retired 2026-08-26, so feed it hand-built TSVs (or re-derive from `dse_results.csv`'s `variant`/`DP` columns) to revive it. |

## Sweeps and sensitivity

| file | status | what |
|---|---|---|
| `dse.py` | **live** | **The launch point for running many benchmarks / many configs** (`make dse`). `--designs tier1+tier2`, `--set K=v1,v2`, `--grid`, `--runset`, `--resume`, `--dry-run`; `--help` is the reference. Grid + target_density come from `benchmarks.py`. **Legalizes + detailed-places each GP result through XPlace by default** (TODO #30) so the headline is legal-vs-legal; `--gp-only` skips it. Every sweep writes `sweep.json` — the manifest of exactly what was launched — and (unless `--gp-only`) `lgdp.json`. |
| `lgdp.py` | **live** | Legalize + detailed-place ONE sw_only GP `.def` through XPlace's own LG+DP, returning `{lg, dp, variant, status}`. Called per-run by `dse.py`; also runnable standalone (`lgdp.py <suite/design> <gp.def> <workdir>`). Handles the three format paths (bookshelf, ispd2015 custom_path, ispd2015_fix fence). Needs XPlace's CUDA env. The reusable core the monolithic `run_lgdp44.sh` / `run_lgdp_suite.sh` folded into (both retired 2026-08-26, TODO #30). |
| `analyze_dse.py` | **live** | Re-renders a finished sweep's table (`analyze_dse.py results/DSE_<ts>`). A 10-line wrapper around `dse.py::summarize`, so there is one renderer, not two that drift. |
| `morris.py`, `morris_factors.py`, `analyze_morris.py` | **live** | Morris elementary-effects screen. `morris_factors.py` is the editable source of truth for factor ranges; the other two import it. |
| `sobol.py`, `analyze_sobol.py` | **live** | Sobol variance decomposition, same runner path. |

The important-parameter set is **regime-dependent** — screen a converging *and* a stalling design,
never just one.

## Visualization

| file | status | what |
|---|---|---|
| `config_keys.py` | **live** | derives the config keys sw_only actually reads, straight from the sources, and asserts nothing sets one it doesn't. Run by `make test` (`--check-configs`) and by `dse.py` on every `--set` (a misspelled key would otherwise sweep nothing and report success). |
| `generate_viz.py` | **live** | full-die / zoomed placement frames from a run's node-position dump (TODO #16). Driven by the `viz-gif` skill. |
| `make_viz_gifs.py`, `gif_builder.py` | **live** | frame → GIF assembly. |
| `plot_histories.py` | **live** | the 5 convergence charts from `iterations.dat` (written every iteration, ungated). |

## sw_only diagnostics

`make test-regress` is the actual tripwire (`vck5000/test/regress/`). These cover axes it does not.

| file | status | what |
|---|---|---|
| `verify_swonly.sh` | **live** | fixed design set, pinned seed, collects the artifacts that pin numerical behavior. Covers the **thread-count / atomics** axis (`DETERMINISTIC=false`) that `test-regress` deliberately pins away. Documented in `host/src/sw_only/README.md`. |
| `compare_swonly.sh` | **live** | diffs two `verify_swonly.sh` trees for numerical equality. |
| `profile_swonly.sh` | **live** | per-function split across designs and grids. |
| `compare_density.py` | **live** | sw_only vs XPlace bin-density maps. Input still producible: `params.dump_density` is read in `Output.cpp`. |
| `check_row_spans.py` | **live** | cells outside the legal site span of their row. The die is a rectangle to us but a staircase on 11 MMS designs, so "inside the die box" is weaker than "inside a row". |

## Housekeeping

| file | status | what |
|---|---|---|
| `bootstrap_third_party.sh` | **live** | builds Limbo for a fresh clone. Called by the Makefile. |
| `prune_run_artifacts.sh` | **live** | reclaims disk from `results/` sweep dirs. Dry run by default; `--go` to delete. |

## Dormant

Working, but nothing on a current path calls them. Listed so they read as *parked*, not *unknown*.

| file | why it's parked |
|---|---|
| `run_opendp.sh`, `opendp_legalize.tcl`, `merge_gp_into_floorplan.py` | The **OpenROAD opendp** legal-vs-legal island (2026-07-10, recorded +7.2% on `mgc_des_perf_1`). Superseded for every quoted number by the XPlace-legalizer pipeline above. Kept because `/usr/local/bin/openroad` is installed and opendp is a genuinely **independent** legalizer — a second opinion that does not share XPlace's code. ⚠️ `merge_gp_into_floorplan.py` does the same job as `def_patch_placement.py`, worse; if you revive this island, rewire it onto that and delete this. |
| `eval_overflow_xplace.sh` | Measures XPlace's exact overflow/HPWL on a sw_only DEF **without legalizing** — deliberately avoids the fragile legalizer. MMS only. |
| `vcd_to_svg.py` | pl_algo hw_emu waveform rendering. Parked with the pl_algo thread, not stale. |

## Known issues

**A `GP Ratio` next to an unconverged `Best OVFW` is not a result.** `mgc_pci_bridge32_a/b` stop at
0.27/0.32 overflow; an under-spread placement flatters its own HPWL (TODO #3). The summary footer
says so, but the per-row number does not (per-row gate deferred to #3).

<details><summary>Fixed 2026-08-14 (#29): XPlace GP HPWL was N/A on 22 of 28</summary>

`Placer::lookupXplaceReferenceHPWL` was a hardcoded 6-entry map. Now the exe writes raw columns
only and dse.py enriches dse_results.csv with `XPlace GP HPWL` + `GP Ratio` from
`benchmarks._XPLACE_GP_MASKED` (all 28), masked-paired and site-width-correct. The DP comparison
lands in the same file the same way.
</details>

<details><summary>Fixed 2026-08-12 by the #28 refactor (846 → 371 lines)</summary>

`dse.py` selected result columns with a hardcoded `fixed_cols` **denylist** that no longer matched
`Output.cpp`, so `results.md` had a blank `Best HPWL` column on every row, no HPWL-range footer,
and result columns listed as *swept parameters*. Column selection is now a positive list
(`RESULT_COLS`) that warns loudly on a schema change, swept parameters come from `sweep.json`
rather than being inferred from the CSV header, and `analyze_dse.py` is a wrapper around the same
renderer. `_full_suite()`'s duplicate 28-design grid table is gone — grid and `target_density` now
come from `benchmarks.py`, closing the silent-`target_density` trap on MMS designs.
</details>

## Removed 2026-08-26

The seven-script scoring pipeline, retired once `dse.py` subsumed it (TODO #30). The cross-check
against it passed exactly on tier1+tier2, and the tier3/MMS path was spot-checked against
`_XPLACE_MMS_FINAL` before deletion. Recoverable from git history; recorded here so nobody
re-derives them.

| file | folded into |
|---|---|
| `gen_suite_configs.py` | `dse.py::prepare` / `write_config` (grid + target_density + seed + `deterministic` from the template; the MMS movable-macros come from the bookshelf data, not a flag) |
| `run_suite.sh` | `dse.py::run_all` (smallest-first, sequential GP, resumable) |
| `run_lgdp44.sh` (ispd2005+2015), `run_lgdp_suite.sh` + `gen_lgdp_inputs.py` (mms) | `lgdp.py::legalize` — all three format paths (bookshelf, ispd2015 `--custom_path`, ispd2015_fix fence) plus the MMS `--mixed_size` arm; `dse.py` pipelines it behind the next GP |
| `analyze_full44.py`, `analyze_lgdp_suite.py` | `dse.py::summarize` (rewrites `dse_results.csv` + the aggregate footer; `analyze_dse.py` re-renders a finished sweep) |

## Removed 2026-08-12

Deleted as stale; recoverable from git history. Recorded here so nobody re-derives them.

| file | why |
|---|---|
| `xplace_gp_ref.py` | Cached 2026-07-10 XPlace GP-HPWL snapshot + scraper. Fully superseded: `run_xplace_ref.sh` scrapes the same `GP Stop! … masked_hpwl` line into `xplace_ref_ispd.tsv`, which feeds `benchmarks.py`. Zero references. |
| `collate_mms.py` | Hardcoded to one run dir (`results/mms_suite_precondON`) and carried its **own** XPlace reference dict duplicating `benchmarks.py::_XPLACE_MMS_FINAL`. Superseded by `analyze_lgdp_suite.py`. Zero references. |
| `make_scorecard.py` | Superseded by `analyze_full44.py`. Only surviving mention was in `docs/archive/`. |
| `legalize_swonly_mms.sh` | Required an unmerged XPlace branch edit; the 2026-08-04 LG/DP report documented its header claim as wrong. Superseded by `run_lgdp_suite.sh`. |
| `bench_swonly.sh` | TODO #12 (closed, archived 2026-08-07) pre/post-threading speedup A/B. Needs a **pre-threading reference binary** that no longer exists, so it cannot be re-run. Its measurement is preserved in `.claude/history.md`. |
| `adaptec1_*.png` (4 files, 2.1 MB) | Run output committed into a code directory. **Moved**, not destroyed, to `.claude/2_ARTIFACTS/legacy_density_heatmaps/` — where the repo rule says artifacts live, and untracked. `docs/archive/pl_algo_CHECKPOINT_history.md` cites two of them. |
