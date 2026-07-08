# PL port plan — the ePlace *iteration algorithm* onto the PL

**Scope (per your ask):** build the *algorithm control* on the PL — `iteration_update`,
`metrics`, and a new `param_scheduler` module (the XPlace `param_scheduler.py` analogue).
This is the "reflect the tuned golden into the PL" step (checkpoint NEXT-STEP #2). The heavy
per-node / per-bin datapaths (HPWL gradient, density solve, DCT/FFT, force gather) are already
drafted and C-synth-clean; this plan does **not** touch them except to consume their outputs.

**The golden we are matching:** markv1 CPU (`host/src/markv1/src/AIEplace.cpp`), now at
XPlace parity across the suite after the γ fixes (adaptec1 0.98 … adaptec2@1024 1.05). Every
formula below is cross-referenced to a markv1 function so each PL block can be diffed against it.

---

## 0. The key realization that shapes this plan

The scheduler is **all scalar / small-reduction work** — O(1) plus a couple of O(M) reductions
per iteration. It is *not* a throughput bottleneck and gains **nothing** from hardware
parallelism. Its entire reason to live on the PL is **loop residency**: today the host runs the
schedule between device passes (`Placement.hpp`), so every iteration round-trips PL→host→PL.
Moving the schedule on-chip lets the whole iteration loop stay on the device (open device/graph
once, iterate N times, read back once) — which is the Stage 6 goal and the only way the AIE FFT
path is viable (sw_emu can't reopen the graph mid-process; see `pl_algo_stage5c`).

**Consequence:** correctness-faithful first, then residency. Do **not** optimize the scheduler;
implement it as a plain sequential HLS function (`#pragma HLS INLINE off`, no unroll) that is
*bit-comparable* to the golden. The win is architectural (loop stays on device), not cycle count.

**Second realization — the current PL schedule is a stub.** `host/src/pl_algo/src/Placement.hpp`
is the *reduced* "5c.5" policy: it has γ, the BB step, momentum, λ-init, and precond, but it is
**missing** the tuned parts of markv1's `updateDensityWeight` — the HPWL-trend-driven μ, the
overflow-plateau 2× jolt, and the divergence/`life` guards in `checkConvergence`. Stage 5c was
only ever run for ~6 iterations ("stable descent"), never to convergence. So the PL has **never
been shown to converge to golden quality.** The tuned schedule *is* the algorithm you spent the
last sessions building; porting it faithfully is the substance of this task, not a formality.

---

## 1. What moves, what stays (v-by-v)

| Concern | markv1 golden fn | Now (PL v1) | Target |
|---|---|---|---|
| HPWL + overflow_sum reduce | `computeTotalWirelength`, `Grid::computeTotalOverflow` | `metrics.hpp` (done) | keep; finish overflow normalization on-chip |
| Combine+precond+BB-step+momentum+clamp | `combineGradients`,`Node::step`,`enforceDieBoundaries` | `iteration_update.hpp` (done) | keep; **add BB reduction outputs** |
| γ schedule | `updateGamma` | host `updateGammaValue` | → `param_scheduler` |
| λ update (trend μ + plateau jolt) | `updateDensityWeight` | **absent** in PL | → `param_scheduler` (the real work) |
| Nesterov coeff recurrence | `performNextStep` | host `momentumCoeff` | → `param_scheduler` |
| BB α finalize + clamp | `computeLipshitzEstimate` | host `bbStepLength` (O(M) on host) | split: **norms on PL**, finalize in `param_scheduler` |
| λ init (iter 1) | `initializeDensityWeight` | host `initDensityWeight` | → `param_scheduler` (iter-1 branch) |
| precond weights | `updatePrecondWeights` | host `updatePrecondWeights` | keep host in v1 (precond is OFF anyway); port later |
| convergence + guards | `checkConvergence`,`checkDivergence`,`checkOverflowPlateau` | host | → `param_scheduler` (returns a stop flag) |
| best-solution snapshot/restore | `snapshotBestPlacement` | host | **stays host** (it owns DDR readback) |

**Precondition note:** precond is OFF in every tuned config (`enable_preconditioning:false`), and
the checkpoint retired it as a PL gap. So `iteration_update` already divides by `precond[n]`
which the host fills with 1.0. Keep it that way in v1 — do **not** port `updatePrecondWeights` yet.
`density_force_fraction` (needed by the λ trend) reduces to a **closed-form scalar** when precond
is off (see §3.2), so we don't need the per-node precond pass to get the schedule right.

---

## 2. Module: `metrics` (exists — verify & close the normalization)

`metrics.hpp` already reduces `HPWL` (segmented net bbox) and `overflow_sum = Σ_bins max(0,ρ−t)`
in `double`. Two items to finish so the scheduler can consume it without host help:

- **Overflow ratio on-chip.** Golden overflow = `bin_area · Σ max(0,ρ−t) / total_movable_area`.
  Today the host does the `· bin_area / movable_area` scaling. Pass `bin_area` and
  `total_movable_area` (both compile-time-known after parse, host-set scalars) into `metrics` (or
  into `param_scheduler`) and emit the **ratio**, since every schedule branch keys off the ratio,
  not the raw sum. Cheapest: leave `metrics` emitting the raw sum and do the one multiply in
  `param_scheduler` (it already takes scalars) — fewer kernel-arg changes.
- **Reproducibility.** Keep the `double` accumulators (already there). A float sum over ~1e6
  nets/bins reorders to ~0.3%, and overflow drives the stop test — verify the PL `double` reduce
  matches the golden to < 1e-4 relative on adaptec1 (`MetricsVerify.cpp` already exists; extend it
  to assert the ratio, not just the sum).

**Verify:** `MetricsVerify` on adaptec1 @512, a mid-run placement snapshot → PL HPWL and overflow
ratio vs `Placer::computeTotalWirelength` / `computeOverflow` within 1e-4 rel.

---

## 3. Module: `param_scheduler` (NEW) — the heart of this task

A single sequential HLS function called **once per iteration** after `metrics`. It owns the
**persistent schedule state** (kept in DDR scratch or, at Stage 6, in a resident on-chip struct)
and consumes the two reductions from `metrics` + the two BB norms from `iteration_update`.

### 3.1 State (persists across iterations) — one small POD struct
```
struct SchedState {
  float lambda;              // density weight (λ)
  float base_gamma;          // set once (host, after grid built — already grid-indep)
  float nesterov_ak;         // momentum recurrence accumulator (a_k)
  float prev_hpwl;           // last iteration's HPWL (trend for μ)
  float precond_coef;        // escalation accumulator (stays 1.0 while precond off)
  int   iteration;
  int   conv_countdown;      // -1 until overflow first < stop; then counts down conv_iters
  int   life;                // divergence-guard budget (MAX_LIFE)
  int   last_jolt_iter;
  float ovfw_ring[64];       // small ring for plateau/divergence windows (<=50 deep)
  float hpwl_ring[64];
  float best_hpwl, best_ovfw; int best_iter; int best_valid;  // for divergence reference
};
```
Rings are ≤ 64 deep (golden windows are 3/25/50) → a couple of BRAM/URAM, trivial. On PL this is
a resident struct; for the incremental host-driven bring-up it round-trips as a tiny DDR buffer.

### 3.2 The per-iteration math (each line ↔ a golden line)

**(a) γ — `updateGamma` (Partials.cpp:182).** Pure function of the overflow ratio:
```
coef  = 10^((overflow - 0.1)*(20/9) - 1)
gamma = coef * base_gamma;   inv_gamma = 1/gamma   // inv_gamma is what hpwl_CU consumes
```
Stateless. `base_gamma` is already the grid-independent value from the γ-fix (host computes it
once and seeds `SchedState`). `param_scheduler` emits `inv_gamma` for next iteration's HPWL pass.

**(b) BB α — `computeLipshitzEstimate` (AIEplace.cpp:281).**
`α = ‖Δv‖ / ‖Δg_precond‖`, clamped `[1e-4, 4000]`. The two **norms are O(M) reductions** and must
run in the PL where v and g live — do them in `iteration_update` (§4), which already streams over
all movable nodes and has both `v_k` (node_box) and the gradients. `param_scheduler` just receives
`pos_norm_sq`, `grad_norm_sq` and finishes: `α = clamp(sqrt(pos2)/sqrt(grad2+1e-8), 1e-4, 4000)`.
**Timing subtlety (kept identical to golden):** the norms use `Δg = g(v_{k+1}) − g(v_k)`, and
`g(v_{k+1})` isn't known until *next* iteration's gradient pass. So α computed at iteration k is
applied at iteration k+1 — exactly markv1's carry-over (`step_length` persists across the loop).
Seed iteration 1 with `init_step_length` (0.1).

**(c) λ update — `updateDensityWeight` (AIEplace.cpp:324).** The tuned part. Faithful port:
```
if (iteration < 2) skip;                                  // need a prev HPWL
slow_phase = (iteration < 50) || (0.5 < dff < 0.95);      // dff = density_force_fraction
if (slow_phase && iteration%3 != 0) skip;                 // cadence gate
dHPWL = hpwl - prev_hpwl;
if (dHPWL < 0)  mu = max_step * max(0.9999^iter, 0.98);   // improving → grow near max
else            mu = max_step * clamp(max_step^(-rel*100), min_step, max_step);  // worsening → damp
lambda *= mu;
// plateau 2x jolt (once per run): iter>window && ovfw>high && plateau(25,1e-3) → lambda*=2
```
`density_force_fraction` when precond is OFF has a **closed form** (no per-node pass needed): with
`a1 = Σ pins = total_pins` (constant) and `a2 = precond_coef·λ·Σ(area/avg) = precond_coef·λ·M`
(since `Σ area/avg = M`), `dff = a2/(a1+a2)`. Host supplies `total_pins` and `M` once;
`param_scheduler` computes `dff` from the current λ. (When precond is later turned on and
escalates, this stays exact because `a2` is still `precond_coef·λ·M`.)

**(d) Nesterov coeff — `performNextStep` (AIEplace.cpp:855).**
```
a_next = (1 + sqrt(4*a_k^2 + 1))/2;   coeff = enable_momentum ? (a_k-1)/a_next : 0;   a_k = a_next;
```
Pure scalar recurrence on `nesterov_ak`.

**(e) convergence + guards — `checkConvergence` (AIEplace.cpp:652).** Returns a **stop flag** the
host reads each iteration. Port in tiers (verify each before adding the next):
  1. **min/max iters + NaN** (trivial).
  2. **overflow countdown**: first time `ovfw < stop`, set `conv_countdown = conv_iters(30)`;
     decrement; stop at 0; reset to −1 if overflow rises back above stop.
  3. **divergence guards** (`checkDivergence` + `checkOverflowPlateau`, the `life` budget). These
     need the ovfw/hpwl rings and the `best_primary` reference. Port last; they only fire in the
     near-converged band and are the fiddliest. **Recommended v1: ship tiers 1–2**, keep tier 3 on
     the host initially (host still sees every metric), port it once 1–2 match the golden stop
     iteration on the suite.

**(f) λ init — `initializeDensityWeight` (AIEplace.cpp:611).** Iteration-1-only branch:
`λ = (Σ|g_wl| / Σ|g_density|) · init_multiplier`. The two L1 sums are O(M) reductions over the
iter-1 gradients → compute them in the same PL pass that produces the iter-1 gradients (or a tiny
dedicated reduce), hand the two scalars to `param_scheduler`'s iter-1 branch. `init_multiplier`
= 8e-5 (config).

### 3.3 I/O contract (host-driven bring-up form)
```
param_scheduler(
   in : SchedState* state (DDR, R/W),      // persistent
        float hpwl, float overflow_sum,    // from metrics
        float pos_norm_sq, float grad_norm_sq,   // from iteration_update BB reduce
        float iter1_gwl_L1, float iter1_gden_L1, // iter-1 only
        scalars: bin_area, total_movable_area, total_pins, M,
                 min_step, max_step, init_multiplier, conv_iters, stop_overflow,
                 enable_momentum, max_iters
   out: float* lambda_out, float* inv_gamma_out, float* alpha_out, float* coeff_out,
        int* stop_flag)
```
At Stage 6 this collapses into the resident loop: no DDR round-trip, `state` lives on-chip, and
the outputs feed straight into the next `hpwl_CU` (inv_gamma) and `iteration_update` (λ,α,coeff).

---

## 4. Module: `iteration_update` (exists — add the BB reduction)

`iteration_update.hpp` already does combine + precond + BB-step + momentum + clamp and streams
`v_{k+1}`. **One addition:** accumulate the two BB norms in the same pass and emit them, so α can
be finalized on-chip (§3.2b) instead of on the host:
```
// inside node_loop, alongside the step:
d_v  = (v_{k+1} - v_k);                  pos2  += d_v·d_v            // ‖Δv‖²
d_g  = inv_p * (g_total_k+1 - g_total_k);grad2 += d_g·d_g           // ‖Δg_precond‖²
```
This needs `g_total` at **both** `v_k` and `v_{k+1}`. Today `iteration_update` sees only the
current gradients. Two clean options:
- **(A) two-buffer:** host keeps `g_prev` (already does for the host BB path); pass it in, diff.
  Smallest change, matches the current host-driven structure. **Recommended for the incremental
  step.**
- **(B) resident:** at Stage 6 the loop keeps `g_prev` on-chip; no host buffer.

Emit `pos_norm_sq`, `grad_norm_sq` as two float outputs (reuse a 2-float DDR buffer like metrics).
Keep the accumulation in `double`, narrow at the end (same reproducibility rule as metrics).

**Verify:** `IterVerify.cpp` already checks the step; extend it to assert `pos_norm_sq`/
`grad_norm_sq` and the finalized α match `computeLipshitzEstimate` on a two-iteration golden trace
(feed it v_k, v_{k+1}, g_k, g_{k+1} captured from markv1).

---

## 5. Staging — each stage verified against the markv1 golden before the next

Following CLAUDE.md's math→golden→hardware-draft→verify workflow. Golden = markv1 CPU; the host
verify harness (`*Verify.cpp`) is the diff mechanism; all sw_emu.

- **S1 — metrics close-out.** Add overflow-ratio scalars; `MetricsVerify` asserts HPWL + ratio on
  adaptec1 @512 within 1e-4 rel. *(small)*
- **S2 — iteration_update BB reduce (option A).** Add pos/grad norms + α finalize; `IterVerify`
  asserts α == `computeLipshitzEstimate` on a captured 2-iter golden trace. *(small)*
- **S3 — `param_scheduler` scalar core (γ, coeff, α-finalize, λ-init).** New `SchedVerify.cpp`
  drives it with a **recorded golden trace** (dump `{iter, hpwl, ovfw, pos2, grad2, gwl_L1,
  gden_L1}` from a markv1 run via a debug hook) and asserts `{inv_gamma, coeff, alpha, lambda}`
  match markv1 **iteration-by-iteration** to ~1e-5. This isolates the scalar math with zero device
  noise. *(medium — the core deliverable)*
- **S4 — λ trend + plateau jolt.** Add `updateDensityWeight`'s μ logic + `dff` closed form + jolt;
  extend `SchedVerify` to assert λ tracks the golden λ trajectory over a full recorded run
  (adaptec1 and adaptec2@1024 — different λ dynamics). *(medium — the tuned part)*
- **S5 — convergence tiers 1–2.** `stop_flag` matches the golden's stop *iteration* on
  adaptec1/bigblue1/matmult_b (the designs that reach 0.04). Tier-3 guards stay host-side. *(small)*
- **S6 — wire the resident loop.** Replace the host-side `Placement.hpp` calls with on-device
  `param_scheduler` in `runPlacement`; run adaptec1 @512 end-to-end in sw_emu to **convergence**;
  assert final HPWL/overflow within tolerance of the markv1 golden (the first true PL-vs-golden
  convergence check — see the §0 stub caveat). *(the integration milestone)*
- **S7 — divergence guards (tier 3)** ported and verified, then the schedule is fully on-device.

**Order rationale:** S1–S2 are cheap and unblock the scheduler's inputs. S3–S4 are the substance
and are verified *offline against a recorded trace* (no device, no flakiness) so the tuned math is
nailed before any sw_emu integration. S5–S6 turn it into a resident loop. S7 is the last, fussiest
guard and is deferred without blocking convergence.

---

## 6. Risks / watch-items
- **Float reproducibility across the loop.** The schedule branches on `dHPWL` sign and `ovfw`
  thresholds; tiny float drift can flip a branch and diverge the trajectory from the golden after
  many iterations. Mitigate: `double` reductions (metrics, BB, L1), and verify against the
  *recorded golden trace* (S3–S4) so drift is caught as soon as it appears, not 200 iters later.
- **History windows on-chip.** Plateau(25)/divergence(3,50) need small rings — bounded and cheap,
  but they make `param_scheduler` stateful; keep the ring writes in a single owner to stay II=1.
- **`best_*` snapshot lives with DDR readback.** The scheduler *references* best-HPWL for the
  divergence guard but does **not** own the best-placement snapshot (that's a full coords copy the
  host/DDR owns). Pass `best_hpwl/ovfw/iter` scalars into the scheduler; keep the copy on the host.
- **Precond stays off.** Everything above assumes precond OFF (the tuned config). The `dff` closed
  form and `precond[n]=1` both rely on it. When precond is eventually revisited, `updatePrecond`
  becomes a real O(M) PL pass — out of scope here.
- **AIE FFT still required for the density solve.** This plan is the *control*; the field solve
  keeps using the AIE FFT. Nothing here changes that path.

---

## 7. Concrete first PR (smallest useful slice)
S1 + S2 + S3-core in one branch: finish `metrics` normalization, add the BB reduce to
`iteration_update`, add `param_scheduler.hpp` with γ/coeff/α-finalize/λ-init, and a
`SchedVerify.cpp` that replays a recorded adaptec1 golden trace and asserts the four scheduler
outputs iteration-by-iteration. That lands the scaffold + the offline verification rig; S4 (the λ
trend) then has a home and a test to grow into.
```
new:      pl/src/pl_algo/src/modules/param_scheduler.hpp
          host/src/pl_algo/src/SchedVerify.{hpp,cpp}
edit:     pl/src/pl_algo/src/modules/{iteration_update,metrics}.hpp   (BB norms; ratio scalars)
          pl/src/pl_algo/src/{top.cpp,host_interface.hpp}             (MODE_PARAM_SCHEDULER)
golden hook: host/src/markv1/src/AIEplace.cpp  (debug dump of the per-iter trace for SchedVerify)
```
