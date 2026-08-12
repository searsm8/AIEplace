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

Code is tracked; **results are not**. Every runner writes to `$ARTIFACTS`, defaulting to the
repo-root `.claude/2_ARTIFACTS/` (gitignored, large). Override it to keep a throwaway run off the
standing tables:

```bash
ARTIFACTS=/tmp/myrun bash tools/run_lgdp44.sh
```

Run order:

| step | file | status | output |
|---|---|---|---|
| 1. configs | `gen_suite_configs.py` | **live** | per-design `.toml` under a run dir |
| 2. our GP | `run_suite.sh` | **live** | `full44_suite_results.tsv` |
| 3. XPlace ref | `run_xplace_ref.sh` (ispd2005), `run_xplace_ref_2015.sh` (ispd2015) | **live** | `xplace_ref_ispd.tsv` |
| 4. LG+DP ours | `run_lgdp44.sh` (ispd2005+2015), `run_lgdp_suite.sh` + `gen_lgdp_inputs.py` (mms) | **live** | `lgdp44_results.tsv`, `lgdp_suite_results.tsv` |
| 5. scorecard | `analyze_full44.py`, `analyze_lgdp_suite.py` | **live** | the table you quote |

Step 3 populates `benchmarks.py::_XPLACE_ISPD_FINAL` **by hand** — the TSV is the evidence, the
dict is what the analyzers read.

Legalization runs through **XPlace's own legalizer** (`main.py --global_placement False`), not
OpenROAD — see *Dormant* for the opendp island.

Supporting converters, all **live** (called by step 4):

| file | what |
|---|---|
| `def_to_bookshelf_pl.py` | sw_only DEF → bookshelf `.pl` (bookshelf tiers). Frame is bit-perfect. |
| `def_patch_placement.py` | the DEF analogue, for the LEF/DEF ISPD2015 tier. Patches placements into the **original** `floorplan.def` — sw_only writes no ROW statements and the legalizer needs the site rows. |
| `post_dp_density.py` | density metrics on a legalized placement, both sides. Post-DP HPWL alone flatters an under-spread GP (TODO #3). Only discriminates below target_density 1.0. |
| `fence_check.py` | how badly a placement violates DEF fence regions (TODO #26). We solve unconstrained; so does XPlace. |
| `analyze_fence_cost.py` | separates "our placer is better" from "we ignore the fence". Kept with the pipeline because it re-answers a decision. |

## Sweeps and sensitivity

| file | status | what |
|---|---|---|
| `dse.py` | **live** | The sweep runner. `DSE_RUN_SET=<name>`, `--resume`. ⚠️ **see Known issues.** |
| `analyze_dse.py` | **live** | Reads a sweep's `results.csv`. Currently the **only** correct reader of that CSV — see Known issues. |
| `morris.py`, `morris_factors.py`, `analyze_morris.py` | **live** | Morris elementary-effects screen. `morris_factors.py` is the editable source of truth for factor ranges; the other two import it. |
| `sobol.py`, `analyze_sobol.py` | **live** | Sobol variance decomposition, same runner path. |

The important-parameter set is **regime-dependent** — screen a converging *and* a stalling design,
never just one.

## Visualization

| file | status | what |
|---|---|---|
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

**`dse.py`'s results table is stale against the exe's CSV schema** (found 2026-08-12, not yet
fixed — a `dse.py` refactor is queued separately). It filters result columns with a hardcoded
`fixed_cols` **denylist** that no longer matches `Output.cpp`, which now emits `Best GP HPWL`,
`XPlace GP HPWL`, `Final HPWL Exact` and the `Phase1 *` columns. dse.py still looks for
`Best HPWL` / `XPlace HPWL`. It does not crash — it degrades silently:

- the `Best HPWL` column is **blank on every row** of `results.md` and the console table;
- the HPWL-range footer silently disappears;
- result columns are listed as *swept parameters* (`Swept parameters: Best GP HPWL, Phase1 Iters, …`).

Confirmed in `results/DSE_20260810_173906/results.md`. **Until it is fixed, read a sweep with
`analyze_dse.py <results.csv>`, not `results.md`.** The fix is to select result columns with a
positive list, so a new `Output.cpp` column can never again be silently reinterpreted.

**`dse.py::_full_suite()` duplicates `benchmarks.py`.** Its 28-design grid table is an exact
duplicate of the manifest's `grid` column (verified 2026-08-12). It also never sets
`target_density`, which the manifest carries — harmless today (ISPD2015 takes it from
`placement.constraints`, ISPD2005 is 1.0) but the exact trap `gen_suite_configs.py` was written to
close, and it bites silently the moment an MMS design enters a sweep.

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
