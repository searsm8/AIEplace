# Test fixtures

Committed input data for the tier-1 harnesses. Everything here is an *input to a test*, not a
result -- that is why it lives next to the harnesses instead of under `vck5000/results/`, which
is gitignored and therefore cannot be depended on by an automated test.

## `schedule_trace_adaptec1.csv`

A full sw_only run of **adaptec1** (ISPD-2005), 652 iterations, produced with
`dump_schedule_trace = true`. `sched_verify` replays it through the PL
`modules/param_scheduler.hpp` and asserts the four schedule scalars match row-for-row.

**Regenerated 2026-08-28 (TODO #20 step 1)** from post-#19 sw_only, replacing the 2026-07-18
trace. The old trace predated #19: sw_only gated on `density_force_fraction`, so `sched_verify`
had to feed dff into the scheduler's `kappa` parameter to reproduce it. Current sw_only gates on
`precond_kappa`, which this trace now carries as its own column, so the replay feeds κ directly.
`schedule_trace_adaptec1.config.toml` is that run's `config_used.toml` (config is TOML now, not
JSON), committed alongside because the harness's convergence settings **must** match it. Grid 512,
target_density 1.0, seed 42, from `tools/benchmarks.py`.

Two things to know before swapping in a different trace:

- **The trace must be a complete run that stopped on its own.** `sched_verify` checks that the
  scheduler's stop flag first fires on exactly the last recorded iteration. A truncated trace,
  or one from a run killed early, fails that check no matter how correct the scheduler is.
- **`convergence_overflow_threshold` must match `p.overflow_threshold` in `sched_verify.cpp`.**
  This run used 0.07. A mismatch shows up as `schedule ok, convergence FAIL` -- the scalars
  still verify bit-exact while the stop check compares against the wrong threshold.

- **The trace needs all 21 columns.** The first 16 are the original set (through `precond_coef`,
  `precond_a1_norm`, `precond_a2_norm`); the regeneration appended `precond_kappa`, `phase`,
  `phase_iteration`, `stop_reason`, `backtrack_steps`. `sched_verify` feeds `precond_kappa` into the
  scheduler's gate and cross-checks it against κ derived from a1/a2; a short trace is rejected, not
  silently skipped. `stop_reason` is the per-iteration in-progress reason (RUNNING=0 for a clean
  converged run — the terminal CONVERGED is a run-level fact set after the last dump, so it is not
  on any row; a divergence guard that trips mid-run *would* show).
- **`precond_coef` escalation is now exercised (pc 1→256).** κ is produced by the pc in force when
  its a2 was formed — `precond_coef[i-1]`, not the pc logged on the row. At each x2 boundary the row
  logs the new pc while its κ still reflects the old one, so `sched_verify` groups by the *producing*
  pc; grouping by the logged pc puts each boundary row at half its plateau's `c` and blows the spread
  to ~51%. The old trace held `precond_coef` at 1.0 throughout and never tested this.

> ### ⛔ CORRECTED 2026-08-07 — the old note here was wrong, and it hid TODO #19b
>
> This file used to say: *"The `closed-form dff max rel err` line is informational, not part of the
> verdict. This run had preconditioning on, and the `sched_dff` closed form assumes it off, so a
> large value there is expected and does not fail the harness."*
>
> **It was not a preconditioning artifact.** That line read 1.608 (161% error) because it was
> comparing `sched_dff` — which computes **κ** — against the trace's `density_force_fraction`
> column, a *different quantity*: a gradient-norm ratio that is not monotone in λ. Same for the
> `dff_coef` constancy line, which reported a **633,000× spread**, took the median anyway, and
> exited 0 for weeks.
>
> Both are now derived from κ and **asserted** (`kappa_coef` plateau spread < 5%, closed form
> < 2e-2). On this fixture they read 1.12% and 5.33e-3. The `density_force_fraction` fit is still
> printed, tagged `[info]`, so the divergence stays visible — it is not a verdict.
>
> The underlying defect was sw_only's, not the harness's: see TODO #19b. The harness's own fault
> was printing a correct check instead of asserting it.
