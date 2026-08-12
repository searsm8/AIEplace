# `vck5000/tools/`

Helper scripts. Two kinds live here, and the difference is what earns a file a place in git:

- **The scoring pipeline** — everything that produces a number we quote. Tracked, because a fix to
  one of these (a wrong template, a stale path, a silently-skipped design) is a fix to the numbers,
  and it must survive a fresh clone. Moved here from the gitignored `.claude/2_ARTIFACTS/` on
  2026-08-12; see *What stayed behind*.
- **Everything else** — `benchmarks.py`, the DEF/bookshelf patchers, `dse.py`, the sensitivity
  tooling, the visualiser, `fence_check.py`.

## The scoring pipeline

Code is tracked here; **results are not** — every runner writes to `$ARTIFACTS`, which defaults to
the repo-root `.claude/2_ARTIFACTS/` (gitignored, and large). Override it to keep a throwaway run
away from the standing tables:

```bash
ARTIFACTS=/tmp/myrun bash tools/run_lgdp44.sh
```

Run order, and what each step leaves behind:

| step | script | output |
|---|---|---|
| 1. configs | `gen_suite_configs.py` | per-design `.toml` under a run dir |
| 2. our GP | `run_suite.sh` | `full44_suite_results.tsv` |
| 3. XPlace reference | `run_xplace_ref.sh` (ispd2005), `run_xplace_ref_2015.sh` (ispd2015) | `xplace_ref_ispd.tsv` |
| 4. legalize + DP ours | `run_lgdp44.sh` (ispd2005+2015), `run_lgdp_suite.sh` + `gen_lgdp_inputs.py` (mms) | `lgdp44_results.tsv`, `lgdp_suite_results.tsv` |
| 5. scorecard | `analyze_full44.py`, `analyze_lgdp_suite.py` | the table you quote |

Step 3 populates `benchmarks.py::_XPLACE_ISPD_FINAL` by hand — the TSV is the evidence, the dict is
what the analyzers read.

`analyze_fence_cost.py` is a one-question tool kept with the pipeline because it re-answers a
decision: it separates "our placer is better" from "we ignore the fence" (TODO #26).

### Two traps these scripts are now defended against

- **A stale hardcoded artifacts path reads as an empty suite.** `analyze_full44.py` pointed at
  `vck5000/2_ARTIFACTS` for five days after the artifacts moved under `.claude/`, and printed a
  table of dashes rather than an error. It now exits loudly if the directory is missing.
- **Missing `ispd2015_fix` silently drops 9 designs.** Both ISPD2015 runners fail with the
  regeneration command instead, and warn when the raw `floorplan.def` is newer than the derived
  `_fix` DEF. See the fence-region notes in the repo-root `CLAUDE.md`.

## What stayed behind

`.claude/2_ARTIFACTS/` keeps the **one-off experiment runners** — `run_footprint_ab.sh`,
`run_mms_ab.sh`, `run_mms_fillconv.sh`, `run_mms_viz.sh`, `run_phase2_suite.sh`,
`run_thread_throughput_ab.sh`, `run_xplace_overflow.sh` and their `gen_*`/`analyze_*` partners, plus
the viz debugging scripts. Each answered one question, was reported on, and is not on the path to
any number we quote today. They are output, and they live with the output.

If you revive one and it starts feeding a headline, move it here.
