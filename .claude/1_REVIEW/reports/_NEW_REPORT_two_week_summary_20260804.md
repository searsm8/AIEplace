# Two-week summary: 2026-07-21 → 2026-08-04

Synthesis of the last fortnight across `sw_only`, tooling, and `pl_algo`. Sources: git log,
the reports and handoffs in `1_REVIEW/`, `0_TODO/TODO.md`, and the 2026-08-04 session.

**One-line version:** sw_only went from "GP-only, phase-1, several accidental divergences from
XPlace" to "two-phase mixed-size flow, evaluated end-to-end through legalization and detailed
placement, landing **+1.17% post-DP HPWL vs XPlace** at equal-or-better density on 7 of 8
density-constrained designs" — plus a ~2× threading speedup and a substantial refactor.

---

## 1. The arc

Three themes ran in parallel, and they reinforced each other.

**(a) Faithfulness to XPlace as the working method.** Nearly every quality win this fortnight came
from the same move: read XPlace's source, find a place where sw_only silently differs, decide
deliberately whether to match it, and document the choice. This produced the filler-sizing fix,
the `#11a`/`#11b` footprint decisions, the phase-2 flow itself, and the divergence-test conjunct.
It also repeatedly showed that our *heuristics* were the problem and XPlace's formulation was the
answer — not the reverse.

**(b) Infrastructure catching up to the science.** Threading, the module split, the logger, the
TOML migration, the benchmark manifest, and the sensitivity tooling all landed in this window.
None of it changes a number, but the 2× speedup and the resumable sweeps are why a 16-design A/B
is now a routine overnight job instead of a project.

**(c) Evaluation rigor.** The most consequential change is what we now consider a result. Two weeks
ago the headline was phase-1 GP HPWL versus XPlace's Mixed-GP. It is now post-DP HPWL versus
XPlace's post-DP HPWL, with a density axis alongside it — because GP-vs-GP systematically flatters
whichever placer spread *less*.

---

## 2. Algorithm and quality

### Mixed-size phase 2 — the largest win (TODO #13)

sw_only implemented only phase 1 of XPlace's three-stage mixed-size flow. Phase 2 (macro
legalization → freeze macros → re-run std-cell GP) landed 2026-08-01 (`a5c0dde`).

- **Built:** phase-relative iteration counter, macro-definition unification, `placer/Phase2.cpp`
  (freeze + rebuild fillers + re-seed + reset), `placer/MacroLegalize.cpp` — a constraint-graph +
  LP formulation solved by the bundled CBC, with longest-path refinement ported 08-02.
- **newblue5**, the suite's hardest design: `divergence_guard` + fallback → **`converged` with a
  primary best**, −9.4% HPWL, −35% exact overflow.
- **Mechanism confirmed:** it is *freezing* the macros, not legalizing them, that does the work —
  legalization moved HPWL 0.08%. Phase 1 already leaves macros nearly legal (1 overlapping pair of
  91 on newblue5, vs XPlace's own legalizer moving 2484).
- **Breadth (08-02):** 14 of 15 designs enter phase 2. A premise correction fell out — MMS's
  td=1.0 designs are *not* macro-free (62–959 movable macros each).
- `convergence_max_iterations` 1200 → 10000, matching XPlace's `inner_iter`, which spans both phases.

### Filler sizing (TODO #13 prerequisite P1, 07-31)

Six divergences from XPlace's `compute_filler_without_fence` fixed in one landing. The headline: on
macro-heavy low-target-density designs sw_only was producing **zero fillers**, because macro area
appeared in both terms of the budget. **adaptec5 went 0 → 310,073 fillers, matching XPlace's own
log exactly** on count and size; newblue4 0 → 205,682.

### Footprint faithfulness (TODO #11)

- **#11a `xplace_die_projection` — adopted unconditionally.** Measured neutral (±0.7%); taken for
  faithfulness, and it deletes a whole deposit-time branch.
- **#11b macro deposit — rejected, then reversed, then locked.** The original A/B said +5.2% HPWL
  worse. That A/B ran on **zero-filler arms**. Re-run 08-02 with correct fillers: **mean +0.61%
  over 8 macro-heavy designs, −0.38% excluding adaptec5**, and it converts a phase-1 divergence
  into a clean converged run. Toggle retired, faithful branch now unconditional.

### Earlier in the window

- **Faithful-field + preconditioner set validated (07-26):** `dct_normalize_inverse=false`,
  `precond_raw_area=true`, `dff_force_ratio=true` beat legacy **16/16 on MMS, mean −9.6% HPWL**.
- **XPlace-faithful initial step estimate (07-25):** `init_step_length` → `init_step_seed`, pinned
  at 0.01 as a self-calibrating default rather than a swept knob.
- **newblue5 TODO #8 answered (07-31):** the zero-area `terminal` nodes are **inert** — total fixed
  area is exactly 0.0, so they cannot form a density hotspot. The theory was killed structurally,
  not merely unsupported. Real causes were zero fillers plus unbounded λ.

---

## 3. Performance and code health

### Threading (TODO #12, 07-30/31)

The premise was wrong and the profile said so: no single function exceeds 25% of an iteration on a
real design, so "parallelize the transforms" would have capped at ~1.25×. The whole iteration was
threaded with OpenMP instead.

| design | pre-#12 | deterministic | atomics |
|---|---|---|---|
| adaptec1 @512 | 11.03 s | **2.03×** | **2.65×** |
| adaptec1 @1024 | 19.58 s | **2.36×** | **2.69×** |
| superblue11_a @1024 | 53.48 s | **1.89×** | **2.37×** |
| newblue3 @2048 | 111.44 s | **2.05×** | **2.38×** |

`params.deterministic` (default true) keeps the golden **bit-identical** — verified on all 211447
adaptec1 cell positions at 1, 3, and 8 threads. Roughly half the win is single-threaded (cached DCT
twiddle tables, flat node index, no per-call allocation). Remaining headroom is an SoA layout, not
more threads: the flat kernels are memory-bound over pointer-chased `Node`/`Bin` objects.

### Refactor and hygiene (07-28 → 08-02)

`AIEplace.cpp` split into `placer/` modules; `DataBase` constructor and `Visualizer` draw passes
split; Logger rewritten (ordered severity scale, plain renderer, per-run `run.log`); run config
migrated JSON → TOML; dead `DebugFramework` scaffolding removed; config defaults given a single
source of truth after they had silently drifted; five settled legacy toggles retired. `pl_algo`
got its own cleanup pass (TODO #10): stale docs rewritten, dead `density_manager.hpp` deleted,
16 `run-*` make targets folded into one define (~90 lines), build artifacts untracked.

### Tooling

Morris + Sobol sensitivity tooling; a master benchmark manifest (`tools/benchmarks.py`) with
launch-time validation of design names; measured concurrent-vs-sequential sweep throughput (a tie,
so sequential everywhere); `dse.py --resume` (08-04).

---

## 4. Evaluation — the 2026-08-04 session

### Full pipeline GP → LG → DP (TODO #3)

sw_only GP results now run through XPlace's own legalizer and detailed placer via
`--global_placement False --given_solution` — **stock XPlace, no source changes**. Harness:
`gen_lgdp_inputs.py` → `run_lgdp_suite.sh` → `analyze_lgdp_suite.py`. 15 designs in 9.5 minutes
of GPU time.

**Post-DP HPWL mean +1.17% vs XPlace over 14 clean designs** (median +1.21%, worst +3.60%, three
designs better). Our phase-2 macro placement passes XPlace's own macro-legalization check on all
16 — **15 of them at zero displacement**, an independent validation of `MacroLegalize.cpp`.

### Post-DP density

`tools/post_dp_density.py` runs both tools' written placements through one implementation, so the
unresolved overflow-definition gap cannot contaminate the comparison. **7 of 8 target-density<1
designs match or beat XPlace.** The exception is adaptec5, which buys its −7.4% HPWL with **+14.0%
overflow** — the suspicion that motivated the metric, now measured.

Two structural limits, recorded so they are not re-derived: post-DP overflow is **identically zero
at target_density = 1.0** (legalization caps occupancy at 1.0 and the capacity *is* 1.0), and a
"top 5% bin utilisation" proxy reads exactly 1.000 for both tools everywhere because the busiest
bins are movable-macro interiors.

### TODO #4 closed out

The **density-weight runaway** was real — pre-#11b adaptec5 held overflow pinned at 0.423 while λ
grew **×564,000** and HPWL doubled, which tripped the coarse divergence test and cost the run its
phase 2. But it is a **symptom**: #11b removes the overflow floor that starved λ's feedback loop,
and adaptec5's phase 1 now converges at iteration 649. No clamp was added; XPlace has none either.
Two candidate fixes were built, measured, and deliberately discarded (a λ freeze deadlocks — the
plateau is what λ exists to end; a `stuck_plateau_window` exit never fires once #11b is in). The
`clamp` → `smooth` rename in `computeOverflow` is done and verified a no-op.

---

## 5. The uncomfortable theme: how often the record went stale

Worth stating plainly, because it recurred and it cost real time.

- The **#11b "RULED OUT"** verdict stood for four days and was wrong — measured on zero-filler arms.
- The phase-2 suite report claimed **no XPlace phase-2 reference existed** and that 15 GPU runs were
  needed. The reference had been on disk since 07-17; the whole flow is in those logs.
- Three TODO #3 items were marked open but had **already been fixed in code**; only the TODO
  disagreed.
- `GP Stop!` prints **masked** HPWL *and* overflow, not exact — quoting it overstated our overflow
  gap 2–3×. I then made the mirror-image error in the other direction on HPWL and corrected it.
- The 08-02 baseline is **stale for the 8 target-density<1 designs**, because #11b landed after it.
  My control for that was adaptec1 — a td=1.0 design, i.e. the one case that cannot exercise the
  branch under test.

The pattern is consistent: results get superseded by a later landing, and nothing re-runs. The
mitigations now in place are `dse.py --resume`, binary md5s recorded per sweep, and provenance
lines in the reports. The remaining gap is that no process forces a re-baseline when a
behaviour-changing flag lands.

---

## 6. State and what is open

**Running now:** full 16-design re-baseline on the current tree (`/tmp/rebase`, binary md5
`894bbaf9…`). It closes the last TODO #4 item (adaptec5 confirmation) and refreshes the stale GP
inputs for the LG/DP suite. adaptec1 in it is **byte-identical** to the 08-02 row, confirming the
session's code changes are no-ops on the unaffected half of the suite.

**Open, roughly by value:**

- **pl_algo Stage 5** — the unified per-iteration datapath and device-resident loop. The 08-02
  measurement is the justification: ~12 kernel launches/iteration cost ~0.9 ms, but ~76 MB/iteration
  of host round-trip costs ~7.6 ms. The DMA is the target, not the launch count.
- **adaptec3 segfaults XPlace's `greedyLegalization`** (3/3, the only failure in 16). The ragged-core
  hypothesis was tested and ruled out; two untested leads remain.
- **Overflow reconciliation** — XPlace reads 0.0484 on adaptec1 where we report macro-excluded 0.109
  on the same placement at the same grid. HPWL round-trips exactly, so it is a metric-definition gap.
- **TODO #9 host merge** — `sw_only` and `pl_algo` are a silent 15-file fork with large diffs.
- **TODO #6/#7** — XPlace's operator-level optimizations and κ(η) stage-aware scheduling.
- **TODO #14** zoomable visualizer, **TODO #15** net-local coordinate frames (analysis complete,
  parked at Mark's request 08-03).
- **Thesis outline v1** exists (08-03), structured after Klaisoongnoen; breadth axis is the four
  kernels of one iteration, energy is first-class, and VCK5000 power measurement is flagged as the
  early risk.
