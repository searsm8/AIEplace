# Summary — project status at a glance
*Updated 2026-08-08 14:08 EDT. Branch `pl_algo`. If this file and the code disagree, the code wins — say so.*

## Two threads
- **sw_only** — CPU golden reference; goal is to match XPlace. The active thread.
- **pl_algo** — move the whole placement iteration onto the PL. Blocked behind #20, deliberately.

## Where sw_only stands
- **Median HPWL ratio 1.0090 vs XPlace** over 33 scored designs (44-design suite, legal-vs-legal,
  2026-08-07). 25/33 within ±2%, better than XPlace on 7. **Quote the median** — the mean (1.087)
  is one broken design. → [[_NEW_REPORT_performance_snapshot_20260807.md]]
- Landed: two-phase mixed-size flow + LP macro legalization; #19's two XPlace faithfulness fixes
  (overflow excludes fillers; the γ/λ throttle gates on preconditioner κ). Both toggles retired
  2026-08-07 — faithful behaviour is now unconditional.
- **#23 — 5 ISPD2015 designs never move a cell.** `init_step_seed = 0.01` underflows to a zero
  initial BB step; λ ramps to 1e29 and NaNs. Root-caused, confirmed by probe, **not yet fixed**.
- **#22 — 8 ISPD2015 designs have no XPlace reference** (fence regions; `--dataset ispd2015` is
  silently rewritten to `ispd2015_fix`). Blocked on obtaining that dataset.

## Where pl_algo stands
- All datapath modules written, HLS C-synthesis clean, each verified against the sw_only golden.
- **#20 — do NOT compose Stage 5 first.** pl_algo's algorithm is frozen at the **2026-07-14**
  sw_only, and `dumpScheduleTrace()` — the mechanism that would catch the drift — was deleted from
  sw_only as dead code on 07-28. So `make test`'s green `sched_verify` checks a **07-18 golden and
  always will**. Restore the trace and the tier-1 coverage (**3 of 17 modules today**) first.
  → [[_NEW_REPORT_pl_algo_stage5_assessment_20260806.md]]
- `top.cpp` is still a mode-switch bring-up scaffold; the host owns the γ/λ schedule, one
  round-trip per iteration.
- `make host HOST=pl_algo` needs one `make clean HOST=pl_algo` first (stale `.d`, not a source break).

## Also open
- **#21 — repo restructure** (host to top level, one host binary). Proposal only, nothing started.
  **Merge `origin/geert` before anything else** — one `.gitignore` conflict today, 25 hand-moved
  files after.

## Verify anything
```bash
cd vck5000 && make test          # pl_algo tier-1, seconds, no Vitis
cd vck5000 && make test-regress   # sw_only vs committed baselines, ~12 s
```

## Deeper
`.claude/tasks.md` (open items)
`.claude/1_REVIEW/reports/` (evidence) 
`vck5000/pl/src/pl_algo/DATAFLOW.md` (authoritative for pl_algo)
