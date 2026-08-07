# pl_algo Stage 5 — situation assessment before wiring the full design

*Written 2026-08-06. Unreviewed. Goal it serves: get `pl_algo` running the **same algorithm as
sw_only** (and by extension XPlace), end to end.*

**One-line answer to "which TODO?":** there isn't one. TODO #10 is pl_algo *cleanup* and is
mostly closed; the only forward-looking statement about pl_algo anywhere is a status paragraph in
`DATAFLOW.md` ("compose the datapath + control into one resident `top` loop"). This report proposes
**TODO #20** and argues for its ordering.

**The headline is not "compose the loop."** It is that pl_algo's algorithm is pinned to the
**2026-07-14** sw_only, the mechanism that would have caught the drift was deleted from sw_only on
**2026-07-28**, and the one test still guarding the control path passes against a **2026-07-18**
golden and will keep passing forever. Composing the resident loop on top of that would harden a
three-week-old algorithm into hardware.

---

## 0. What I actually ran

| check | result |
|---|---|
| `make test` (tier 1, 5 harnesses) | **PASS**, all 5 |
| `make host HOST=pl_algo` at HEAD | **FAILED** — stale `.d`; see §1 |
| ...after `make clean HOST=pl_algo` | **PASS**, links `aieplace_pl_algo.exe` |
| κ-vs-dff analysis of the committed fixture | see §4 — decisive |

Not run: HLS C-synthesis (tier 2) and sw_emu (tier 3). Both need Vitis, and the box is busy —
another session's 16-design MMS suite is live (`newblue7` running, ~470% CPU).

---

## 1. The build is broken on arrival, for a boring reason

A fresh `make host HOST=pl_algo` dies with:

```
No rule to make target '.../host/src/pl_algo/src/DataBase.cpp', needed by '.../obj/DataBase.o'
```

This reads like the `host/src/common/` extraction (TODO #9, commit `323133b`) broke pl_algo. It did
not. `build/hw/host/pl_algo/obj/DataBase.d` is dated **2026-07-10** and still names the pre-move
path; `host/Makefile` does `-include $(HOST_DEPS)`, so that stale file injects a prerequisite make
cannot satisfy. The two pattern rules in `host/Makefile` are correct and the sources are all where
they should be.

**Fix:** `make clean HOST=pl_algo` once. Verified — clean build, links, exit 0.

Worth a line in the README, because it is the first thing a returning session hits and it points at
the wrong culprit.

---

## 2. pl_algo's algorithm is frozen at 2026-07-14

`pl/src/pl_algo/src/modules/param_scheduler.hpp` — the on-device schedule + convergence — was last
touched on **2026-07-14** (`3a42098`). Since then sw_only has taken **20 commits** plus the
currently-uncommitted TODO #19. The ones that changed the algorithm:

| sw_only change | date | pl_algo status |
|---|---|---|
| #11a `xplace_die_projection` unconditional; deposit-time in-die shift **deleted** | 07-31 | ✗ `node_footprint.hpp` still does the shift |
| #11b movable macros deposit at `weight = target_density` | 08-02 | ✗ absent |
| XPlace-faithful filler sizing (adaptec5 0 → 310 073 fillers) | 07-31 | ✗ pl_algo has **no fillers at all** |
| Coarse divergence gains the `overflow rising` conjunct | 07-31 | ✗ still HPWL-only |
| Mixed-size **phase 2**: macro LP legalization + fixed-macro restart | 08-01 | ✗ absent |
| Phase-relative counters (`phaseIteration()`) throughout | 08-01 | ✗ absolute `iteration` |
| #19a every overflow metric excludes fillers | 08-06 (uncommitted) | ~ vacuously true (no fillers) |
| #19b schedule throttle gates on `precond_kappa`, not `density_force_fraction` | 08-06 (uncommitted) | **✓ already faithful — by accident.** §4 |

Note the asymmetry: the *host* loop in `Driver.cpp` (last touched 08-05) is **ahead** of the
*device* module. It has the preconditioner auto-enable and the `precond_coef` escalation;
`param_scheduler.hpp` has neither. Whoever composes the resident loop must port those *up* into the
device module, not assume the device module is the newer one.

---

## 3. The golden-trace generator was deleted from sw_only as dead code

`Placer::dumpScheduleTrace()` — config-gated `dump_schedule_trace`, wrote
`schedule_trace.csv` with `iter, hpwl, overflow, pos_norm_sq, grad_norm_sq,
density_force_fraction, base_gamma, gamma, inv_gamma, step_length, nesterov_ak, momentum_coeff,
density_weight, precond_coef, precond_a1_norm, precond_a2_norm` — was **removed in `44612cc`**
(2026-07-28, *"sw_only refactor phase 1: readability pass … plus a couple of small pre-existing
dead code items"*).

It was not dead. It was the **only** producer of the golden that `test/sched_verify.cpp` replays,
and that consumer is in a different variant and references it by *filename*, not by symbol — so
nothing in the build could see the coupling.

Consequences, all live today:

- `test/fixtures/schedule_trace_adaptec1.csv` **cannot be regenerated**. Its companion config is
  still `.json` — it predates the TOML migration.
- `sched_verify` therefore validates pl_algo's scheduler against **sw_only as of 2026-07-18**. It
  passes. It will pass no matter how far sw_only moves. This is exactly the failure mode
  `CLAUDE.md` warns about, one level up: not a test that prints instead of asserting, but a test
  that asserts correctly against a golden that quietly stopped being the golden.
- `make test`'s green light on `sched_verify` is currently **evidence of nothing** about
  faithfulness to the current algorithm.

**This is the first thing to fix, before any pl_algo code changes** — it is the instrument every
later step is measured with.

---

## 4. `sched_dff` is already XPlace's κ. It is mislabeled, and nobody noticed.

TODO #19b's finding is that the schedule throttle must gate on XPlace's `weighted_weight`
(preconditioner mass ratio κ), not on a gradient-norm ratio. pl_algo's `param_scheduler.hpp` gates
on a quantity it *calls* `density_force_fraction`, computed by the closed form

```c
sched_dff(λ, c) = cλ / (1 + cλ),   c = dff_coef = precond_coef · K / total_pins
```

Substitute XPlace's own definitions — α₁ = per-node pin count (‖α₁‖₁ = total pins), α₂ =
precond_coef·λ·area (‖α₂‖₁ = precond_coef·λ·K) — and

κ = ‖α₂‖₁ / (‖α₁‖₁ + ‖α₂‖₁) = cλ / (1 + cλ) = `sched_dff`, **identically**.

Confirmed numerically against the committed fixture, which happens to carry `precond_a1_norm` /
`precond_a2_norm` so κ is directly computable from it:

```
fit c from  q/(1-q) = c·λ_prev  over 692 iterations:
  q = κ    →  c median 70.41, min 70.05, max 70.83   spread   1.12 %   ← a constant
  q = dff  →  c median 69382,  min 2.34,  max 1.48e6  spread 2136 %    ← not that function
```

and the throttle behaves exactly as #19b predicts:

| iter | κ | dff | throttled by κ | throttled by dff |
|---|---|---|---|---|
| 301 | 0.0002 | 0.507 | off | **ON** |
| 501 | 0.267 | 0.404 | off | off |
| 601 | 0.939 | 0.589 | **ON** | **ON** |
| 692 | 0.980 | 0.621 | off | **ON** |

κ crosses the (0.5, 0.95) window once and leaves; dff wanders in and out and is still inside at the
last iteration.

Two things follow.

**(a) The harness was already printing this and it was written off.** `sched_verify` reports
`closed-form dff max rel err vs golden: 1.608` (161 %) and
`fixtures/README.md` explains it away: *"This run had preconditioning on, and the `sched_dff` closed
form assumes it off, so a large value there is expected and does not fail the harness."* The real
reason for the 161 % is that the closed form is not computing dff at all — it is computing κ, and
κ ≠ dff. The explanation was plausible, which is why it held for three weeks.

**(b) It is only *half* right, and the missing half bites on MMS.** `dff_coef` is a single
host-precomputed scalar, but κ's `c` carries `precond_coef`, which sw_only **escalates ×2 every 20
iterations once overflow < 0.3, capped at 1024**. The fixture never exercises this
(`precond_coef ≡ 1.0` for all 692 iterations of that adaptec1 run), so the constant-c model looks
perfect. On any macro-heavy design it is wrong by up to 1024× on the λ axis — precisely in the
endgame the throttle governs. `dff_coef` must become `precond_coef_k · K / total_pins`, recomputed,
not a `SchedParams` constant. (`K` is a genuine constant: total movable+filler area. Also note the
comment calls it "normalized areas" — stale wording; TODO #2 removed the `avg_area` normalization
on 2026-08-02 and the areas are raw.)

Also: `sched_verify` feeds the trace's **dff column** into `param_scheduler`, so even a regenerated
trace verifies the module against the wrong input unless the harness is changed to feed κ.

---

## 5. Tier-1 coverage is 3 modules out of 17

Only three harnesses `#include` a real module: `fft_pl_test` → `fft_pl.hpp`, `field_solve_test` →
`field_solve_pl.hpp`, `sched_verify` → `param_scheduler.hpp`. `density_model` and
`density_bin_model` are *models* — `density_bin_model.cpp` contains its **own copy** of
`node_footprint`, so the two can drift silently, and today they are stale *together* (both still do
the deleted in-die shift).

Uncovered at tier 1: `node_footprint`, `density_bin`, `hpwl_gradient`, `force_gather`,
`iteration_update`, `memory_writer`, `metrics`, `bb_reduce`, `spectral`, `transpose`,
`dct_transpose`, `dct_1d`. `bb_reduce` and `param_scheduler` get tier-2 (synthesis) only.

Every module §6 says must change is in the uncovered set. That is the practical blocker: today,
changing `node_footprint.hpp` cannot be checked without a full Vitis + sw_emu cycle.

---

## 6. Datapath divergences from the golden (specific, small, verifiable)

1. **`node_footprint.hpp` still shifts the footprint in-die.**
   ```c
   if (xl + cw > grid_w) xl = grid_w - cw;   //  ← deleted from sw_only by #11a
   if (xl < 0.0f) xl = 0.0f;
   ```
   sw_only's `computeNodeFootprint` (`common/src/Grid.cpp`) has no in-die correction: the
   projection now lives in exactly one place, `enforceDieBoundaries`. Keeping both is the double
   correction #11a removed.

2. **`node_footprint.hpp` lacks the movable-macro deposit weight.** sw_only:
   `if (cfg.target_density < 1.0f && node_p->isMovableMacro()) weight = cfg.target_density;`
   — it *replaces* the area-conserving ratio (XPlace `database.py:921-923`). pl_algo has no macro
   tag in `NodeBox` at all, so this needs a contract change (a flag bit or a fifth field).

3. **`iteration_update.hpp` clamps to the wrong box.** It uses `[0, die − w]` on the raw size;
   sw_only clamps to the **√2-expanded** bound `[(cw−w)/2, die − (cw+w)/2]`, with a
   centre-the-node fallback when a footprint is wider than the die. These agree only for macros
   and only with the clamp off.

4. **No fillers, anywhere.** `Packer.cpp` walks `db.getComponents()`, which excludes
   `db.getFillers()`. On MMS designs fillers are a large fraction of the movable set (adaptec5:
   310 073) and they carry density force with no wirelength force. Without them the field pl_algo
   solves is not the field sw_only solves — this is a first-order difference, not a detail.
   XPlace's `initializeDensityWeight` balance counts them too, so λ starts wrong as well.

Everything else I checked is faithful: `force_gather` is the correct area-weighted adjoint of
`bin_scatter`; `Packer.cpp` masks both degree ≤ 1 and degree > 100 (`IGNORE_NET_DEGREE`);
`bb_reduce` applies 1/precond to the gradient delta consistently with the step (the `b20a2cc` fix);
the momentum recurrence, γ formula and λ trend match; `metrics` accumulates in double.

---

## 7. Structural gaps between "one PL iteration" and "the sw_only algorithm"

These are design questions for Stage 5, not bugs — but they must be answered *before* the loop is
composed, because each one changes what state the resident loop has to hold.

- **The convergence overflow needs a second density map.** sw_only's convergence signal is
  `computeOverflow(smooth=true, include_fillers=false)`, which **rebuilds an independent map**:
  fixed at sharp size then capped, movable smoothed, fillers omitted. The solver's own map (the one
  the field is solved from) includes fillers and √2-inflates the fixed nodes. pl_algo's `metrics`
  reduces the *solver's* `rho`. Overflow is `Σ max(0, ρ−t)` — nonlinear, so you cannot subtract the
  filler contribution afterwards.
  *Cheap way out:* adopt XPlace's decomposition — scatter movable-only → `rho_mov`, take overflow
  from that, then scatter fillers into a second map and add for the force. One extra scatter over
  the fillers, not two full passes.
- **Backtracking line search.** `enable_backtracking = true` is the default. Each rejected trial is
  a *complete extra gradient evaluation* (HPWL + density solve + field solve). In a device-resident
  loop that is a re-entrant inner loop around the whole datapath. DATAFLOW.md's "No backtracking in
  v1" is a real divergence from the golden, and it is the one XPlace relies on in place of a step
  clamp (`CLAUDE.md`: "XPlace bounds the BB step with its backtracking line search alone").
- **Best-solution snapshot/restore.** sw_only reports the *restored best* placement, not the last
  iterate, and phase 2 freezes the macros from it. Needs an M-sized DDR buffer plus a conditional
  copy inside the loop; `param_scheduler` tracks the best *metrics* but nothing saves the positions.
- **Divergence guards.** `param_scheduler`'s coarse test is `hpwl > 2·best` alone; sw_only requires
  `AND overflow rising`. The fine guard's life-drain arithmetic matches, but arming is on absolute
  `k`, not `phaseIteration()`.
- **Jolt / plateau parameters are hardcoded** (window 25, threshold 0.001, high-overflow 0.7,
  interval 1000). sw_only reads `adaptation_window`, `slow_improvement_threshold`,
  `high_overflow_threshold`, `density_jolt_interval`. They happen to agree with today's
  `default_config.toml`; that is luck, not a contract.
- **Phase 2 cannot go on the device.** It is an LP (cbc) over macro positions, a filler rebuild, and
  a full re-seed — `MacroLegalize.cpp` is 603 lines and `Phase2.cpp` 228. It is host work that runs
  *once*, between two GP phases. The device-resident loop has to be re-enterable with new static
  data rather than run once.
- **Grid.** `GRID` is a compile-time **1024**; sw_only runs the ePlace-formula grid per design (512
  on adaptec1). `base_gamma` is grid-independent now (`gamma_ref_grid`), but the density resolution
  is not. Any A/B must pin `bins_per_row = 1024` on the sw_only side, or build pl_algo with
  `-DPL_GRID=<n>`.

---

## 8. The moving-target problem

TODO #19 is **uncommitted** in the working tree (`Schedule.cpp`, `Output.cpp`, `AIEplace.h`,
`default_config.toml`, …), its 16-design validation suite is **running right now**, and
`test/regress/baselines/mms_adaptec1.baseline` is **deliberately stale** pending that result.
"Match sw_only exactly" is not currently a fixed target.

Recommendation: **pin pl_algo to a named sw_only commit** — the one that lands TODO #19 — and say so
in `DATAFLOW.md`. Chasing HEAD guarantees the same silent drift that produced this report.

---

## 9. Proposed TODO #20 — ordering

Cheap-and-load-bearing first. Steps 1–4 need no Vitis and no free CPU.

| # | step | verify | cost |
|---|---|---|---|
| 0 | `make clean HOST=pl_algo`; note it in the README | build links | done |
| 1 | **Restore `dumpScheduleTrace()` in sw_only**, with today's columns (`precond_kappa`, `precond_coef`, phase, `phaseIteration`, stop reason, `backtrack_steps`). Regenerate the adaptec1 fixture + its `config_used.toml`. | `make test-regress` bit-identical before/after (dump is config-gated ⇒ must be a no-op) | small, touches sw_only |
| 2 | **Re-verify `param_scheduler` against the new trace.** Feed κ, not dff. Fix what falls out: escalating `dff_coef`, coarse-divergence overflow conjunct, phase-relative counters, jolt params from config. | `make test` | tier 1, seconds |
| 3 | **Tier-1 harnesses for the uncovered modules** — `node_footprint`, `density_bin` (include the real header; delete the copy in `density_bin_model`), `iteration_update`, `bb_reduce`, `metrics`, `force_gather` — each against its named sw_only golden function. | `make test` | the enabling step for 4–6 |
| 4 | **Close the §6 datapath divergences** under that coverage (in-die shift, macro weight, die clamp box). | `make test` | small edits, now checkable |
| 5 | **Fillers** — packer, initial placement (uniform-random, not centre-clustered), λ init balance, and the movable/filler split the two density maps need. | tier 1 + a tier-3 A/B | largest single change |
| 6 | **Compose the resident loop (Stage 5 proper)** with the second density map, the best-position buffer, and re-entrancy for phase 2 designed in from the start. | tier 2 then tier 3 | the original item |

Steps 1 and 2 alone convert `make test` from a green light that means nothing into one that means
"pl_algo's control path matches the current sw_only". That is worth doing even if Stage 5 slips.

---

## 10. Open questions for Mark

1. **Scope of "the exact same algorithm".** Does it include (a) mixed-size **phase 2** — LP macro
   legalization on the host, and a re-enterable device loop — and (b) the **backtracking line
   search**? Or is the v1 target "phase-1 GP, device-resident, bit-comparable to sw_only phase 1"?
   This changes step 6 substantially.
2. **Step 1 touches sw_only** while TODO #19 is uncommitted and a suite is running. Green light, or
   wait for the suite to land?
3. **Pin to a commit?** (§8) — my recommendation is yes, the TODO #19 commit.
4. **Grid** — pin sw_only to 1024 for the comparison, or build pl_algo at each design's formula
   grid via `-DPL_GRID`?
