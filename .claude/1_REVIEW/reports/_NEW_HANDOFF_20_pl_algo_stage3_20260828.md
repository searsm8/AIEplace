# HANDOFF #20 — pl_algo tier-1 coverage → v1 resident loop

**Date:** 2026-08-28
**Branch:** `pl_algo`
**Related:** tasks.md #20, [[DATAFLOW.md]], [[REPORT_pl_algo_stage5_assessment_20260806.md]],
[[_NEW_REPORT_20_hpwl_gradient_opt_20260828.md]]

---

## ▶ NEXT SESSION STARTS HERE

**Step 3 is DONE** (`b4130e6`, `6cdcc8d`) — the `formats.hpp` wall is broken (option (a)) and all
four modules + `node_footprint` are tier-1 covered. `make test` runs 11 harnesses; coverage 10 of
~14 real modules. So the immediate work is now **step 4: close the datapath divergences** the
harnesses documented, cheapest/most-isolated first:

- **`node_footprint` in-die shift** — the PL module shifts a footprint to stay on-grid; sw_only
  `computeNodeFootprint` (Grid.cpp:9, 35-37) does NOT (its `enforceDieBoundaries` pre-projects nodes
  so the box is legal by construction). `node_footprint_test.cpp` tests the PL behaviour and flags
  this in its header. Decide: adopt the sw_only invariant (project in `iteration_update`, drop the
  shift) or keep the shift as a PL-side guarantee. Whichever, make the two agree and retarget the
  golden to `computeNodeFootprint`.
- **movable-macro weight override** — sw_only sets `weight = target_density` for a movable macro when
  td < 1 (Grid.cpp:31-32, TODO #11b); the PL module has no macro/filler flag crossing the boundary in
  v1. Needs the boundary to carry that flag before the module can match. Add a `[5]` to
  `node_footprint_test` when it lands.
- **die-clamp box, fillers** — the remaining two from the original step-4 list; not yet analysed.

**Do NOT compose the resident loop (step 6 / the v1 milestone) yet.** DATAFLOW.md is emphatic:
*"compose this loop LAST, not next."* Step 4 must reconcile the divergences first, or the composed
loop bakes in a wrong footprint that is invisible until a full sw_emu cycle.

Also still open from step 2 (need non-adaptec1 fixtures): the divergence-conjunct, phase-relative
counters, and jolt-from-config items — see "Remaining step-2 items" below.

### Done 2026-08-28 (step-3 completion, this session)
- `b4130e6` — **wall fix (option a).** `formats.hpp` guards its HLS includes + `axis_t`/`beat_t`
  behind `#ifndef PL_TIER1_STUB` (byte-identical preprocessed output for the real build).
  `test/tier1_stub.hpp` sets the macro + a `std::deque` `hls::stream<T>`. `density_bin_model.cpp`
  upgraded to call the **real** `density_bin()`/`node_footprint` (stale copies deleted; found the
  module's `acc*inv_area` multiply, mirrored it to stay bit-exact). New `node_footprint_test.cpp`.
- `6cdcc8d` — `force_gather_test`, `metrics_test`, `iteration_update_test` (+ `memory_writer`), each
  vs an independent double golden. Margins 6e-9 … 7e-5, all with ≥10× headroom.

---

## What v1 is (decided 2026-08-28, Mark)

**Phase-1 GP, device-resident, bit-comparable. NO phase 2, NO backtracking** (both deferred until a
measured need). **pl_algo pins to the frozen sw_only HEAD.** Still open (§10 q3): pin sw_only to
grid 1024 for the A/B, or build pl_algo per-design with `-DPL_GRID`.

The v1 milestone = step 6: replace `top.cpp`'s host-driven mode switch with one resident per-
iteration datapath that runs `bb_reduce` + `param_scheduler` on-device (schedule/convergence with
no host round-trip). Per-iteration order and resident-state contract are in DATAFLOW.md
"Per-iteration order" / "Resident state".

## Plan order (tasks.md #20)

1. Restore `dumpScheduleTrace()` + regenerate fixture — **DONE** (`f10dc2c`).
2. Re-verify `param_scheduler`, feed κ — **CORE DONE** (`f10dc2c`); 3 items left, see below.
3. Tier-1 harnesses for the uncovered modules — **DONE** (`b4130e6` wall + node_footprint/density_bin,
   `6cdcc8d` force_gather/metrics/iteration_update). 10 of ~14 real modules; `make test` = 11 harnesses.
4. Close the datapath divergences (node_footprint in-die shift, movable-macro weight, die-clamp box,
   fillers) — **NEXT** (see "NEXT SESSION STARTS HERE").
5. (report §-level) structural gaps — not started.
6. **Compose the resident loop = v1** — LAST.

## Done this session (4 commits, all `make test` green, `make test-regress` bit-identical)

- `3a26da1` — folded `memory_writer`→`iteration_update` and `refresh_*`→`hpwl_gradient` (never-
  independent modules); `HPWL_PARTIALS`→`HPWL_LANES`; wired `MODE_REFRESH_PINS` into
  `Driver.cpp::eval_gradients` (finished P2 host side).
- `f10dc2c` — **step 1** (dumpScheduleTrace restored, config-gated, 21 cols; proven no-op via
  test-regress; adaptec1 fixture regenerated, 652 iters, config now TOML) + **step 2 core**
  (sched_verify feeds `precond_kappa`, schedule/convergence bit-exact, escalation handled).
- `f6213a0` — **step 3**: `bb_reduce` tier-1 coverage.
- `3d69822` — summary.md progress.

## The bb_reduce template (replicate for the other step-3 modules)

`test/bb_reduce_test.cpp` is the pattern for a module with no cleanly-importable CPU golden:

1. **Golden = the same math re-derived in `double`, inline in the harness** (not the sw_only
   function — those are `Placer` methods wired into `Node`/`db`; pulling them in drags the host
   object graph into a pure-g++ TU). For an op computed per-element in float, replicate that exact
   float op and assert **bit-exact**; for a reduction, use a `double` reference and a **rel tol
   sized from the observed value with real margin** (bb_reduce: observed 2e-8/9.5e-7, bound 1e-5).
2. **Drive every branch**: bb_reduce mixes `precond` 1.0 and >1 to exercise `inv_p==1` and `inv_p<1`.
3. **A test asserts** — compute the verdict, exit 0/non-zero; every printed number is also `if`-checked.
4. Wire into `test/Makefile` in **two** places: `HARNESSES` and the `for h in ...` loop in `test:`.
5. If the module's only HLS-type dependency is an **unused** `formats.hpp` include (bb_reduce's
   was), just drop it. If it's a **real** dependency, that's the wall above — do (a) first.

Run: `cd vck5000 && make test` (seconds). Coverage now **10 of ~14** real modules
(`fft_pl`, `field_solve_pl`, `param_scheduler`, `hpwl_gradient`, `bb_reduce`, `node_footprint`,
`density_bin`, `force_gather`, `metrics`, `iteration_update`+`memory_writer`). Remaining: the
DCT/transpose transform modules (`dct_1d`, `dct_transpose`, `spectral`, `transpose`), covered at the
recipe level by `density_model`/`fft_pl`/`field_solve`.

## Remaining step-3 modules + their specific gotchas

- **`iteration_update`** — uses `hls::stream<coord_t>` (the `memory_writer` half streams v_{k+1}).
  Real formats.hpp dependency → needs the wall fix. Golden: sw_only `combineGradients` + `Node::step`
  + `enforceDieBoundaries`. Note the known divergence (#20 step 4): it clamps to `[0, die−w]` where
  sw_only clamps to the √2-expanded box.
- **`metrics`** — includes `../formats.hpp`; also its `hpwl_sweep` is slated for deletion once the
  host stops calling `hostHPWL` (P1b). Golden: `computeTotalWirelength("HPWL")` + `computeOverflow`.
- **`density_bin`** — `density_bin_model.cpp` already tests a **stale copy** of `node_footprint`;
  the wall fix lets it `#include` the real `node_footprint.hpp` and delete the copy (tasks.md #20
  step 3 calls this out). Golden: `Grid::computeBinOverlaps` / `computeOverlaps`, and the
  `capFixedDensity` cap (`common/include/Grid.h`, #36).
- **`force_gather`** — golden `computeElectrostaticForce` (with the overlap-area term, fixed
  2026-07-03). Shares `node_footprint` with `density_bin`.
- **`node_footprint`** — shared helper; cover it once and both `density_bin`/`force_gather` lean on it.

## Remaining step-2 items (need fixtures adaptec1 can't produce)

adaptec1 converges cleanly in phase 1, so it cannot exercise these — they need a **different**
golden trace, regenerated the same way (below):

- **overflow-rising conjunct on the coarse divergence test** — needs a *diverging* run
  (`mgc_des_perf_b` reaches `divergence_guard`; candidate).
- **phase-relative counters** — needs a *mixed-size / phase-2* run (any MMS design).
- **jolt params read from config vs hardcoded** — a `param_scheduler.hpp` cleanup, no fixture needed.

## Key procedures / gotchas

- **Any touch of `host/src/sw_only/` must be `make test-regress` bit-identical before AND after.**
  The dump is config-gated instrumentation for exactly this reason; it stayed a proven no-op.
- **Regenerate a sched fixture:** derive a config off `default_config.toml` (never edit it) with
  `random_seed`, `bins_per_row`+`maximum_utilization` from `tools/benchmarks.py`,
  `convergence_max_iterations` matching `sched_verify` (1200), and `[output] dump_schedule_trace =
  true`; run the exe; copy `<run>/schedule_trace.csv` + `config_used.toml` into `test/fixtures/`;
  update `sched_verify.cpp`'s hardcoded convergence params if they differ. The trace must be a run
  that **stopped on its own** (sched_verify checks the stop fires on the last row).
- **sched_verify escalation insight (do not regress):** κ[i] is produced by `precond_coef[i-1]`
  (at an x2 boundary the row logs the new pc but its κ reflects the old one). The c-derivation
  groups by the *producing* pc and the closed-form check uses a per-regime c. Grouping by the
  logged pc puts each boundary row at half its plateau's c and blows the spread to ~51%.
- **`stop_reason` column is per-iteration in-progress reason** (0/RUNNING on a clean converged run;
  the terminal CONVERGED is set after the last dump). A mid-run divergence guard *would* show.
- **Bash tool runs on Windows** — wrap every command `wsl -e bash -c "cd /home/msears/phd/AIEplace && <cmd>"`.
- **Commit message heredocs break on apostrophes** in the WSL wrapper — write the message to a file
  and `git commit -F <file>` (a `/mnt/c/...` scratchpad path works).

## Pointers

- Harnesses: `vck5000/test/*.cpp`, wired in `vck5000/test/Makefile`. Fixtures in
  `vck5000/test/fixtures/` (committed; see its README).
- Modules: `vck5000/pl/src/pl_algo/src/modules/*.hpp`. Contract: `DATAFLOW.md`.
- sw_only golden: `vck5000/host/src/sw_only/src/placer/`.
- Comment signature in this repo is the `Meow.` suffix (CLAUDE.md updated; the two `// CLAUDE CODE:`
  lines left in `density_bin.hpp` convert when that file is next touched).
