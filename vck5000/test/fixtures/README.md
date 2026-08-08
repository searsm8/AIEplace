# Test fixtures

Committed input data for the tier-1 harnesses. Everything here is an *input to a test*, not a
result -- that is why it lives next to the harnesses instead of under `vck5000/results/`, which
is gitignored and therefore cannot be depended on by an automated test.

## `schedule_trace_adaptec1.csv`

A full sw_only run of **adaptec1** (ISPD-2005), 692 iterations, produced with
`"dump_schedule_trace": true`. `sched_verify` replays it through the PL
`modules/param_scheduler.hpp` and asserts the four schedule scalars match row-for-row.

Copied from `results/regress_ispd2005/adaptec1/20260718_015453_141_cpu_cpu/`.
`schedule_trace_adaptec1.config.json` is that run's `config_used.json`, committed alongside
because the harness's convergence settings **must** match it.

Two things to know before swapping in a different trace:

- **The trace must be a complete run that stopped on its own.** `sched_verify` checks that the
  scheduler's stop flag first fires on exactly the last recorded iteration. A truncated trace,
  or one from a run killed early, fails that check no matter how correct the scheduler is.
- **`convergence_overflow_threshold` must match `p.overflow_threshold` in `sched_verify.cpp`.**
  This run used 0.07. A mismatch shows up as `schedule ok, convergence FAIL` -- the scalars
  still verify bit-exact while the stop check compares against the wrong threshold.

- **The trace needs all 16 columns**, including `precond_coef`, `precond_a1_norm` and
  `precond_a2_norm`. `sched_verify` derives κ (XPlace's `weighted_weight`) from the last two and
  asserts the closed form against it; a 13-column trace is rejected rather than silently skipped.

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
