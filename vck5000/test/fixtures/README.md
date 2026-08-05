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

The `closed-form dff max rel err` line is **informational, not part of the verdict**. This run
had preconditioning on, and the `sched_dff` closed form assumes it off, so a large value there
is expected and does not fail the harness.
