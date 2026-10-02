# pl_algo data flow

> **Block diagrams** now live in `.claude/2_ARTIFACTS/diagrams/`:
> [[DIAGRAM_pl_overview.md]] (the whole PL region, simplified), [[DIAGRAM_iteration.md]]
> (the per-iteration loop) and [[DIAGRAM_density.md]] (the density branch expanded).
> This file stays authoritative for the contract; the diagrams follow it.

One placement iteration, as data moves through the PL modules. Byte/word layouts are
defined precisely in `src/formats.hpp`; this file is the narrative. All transport uses a
128-bit logical word (4 packed floats) as the application-level packing unit -- this is
*not* the physical DDR/AXI4 `m_axi` interface width (512-bit on this platform); it's how
`formats.hpp` groups floats into `beat_t`/`axis_t` records. AXI-stream transfers (PL<->AIE)
are genuinely 128-bit beats at the hardware level; DDR-resident buffers are just arrays of
these 128-bit words, and burst efficiency across the wider physical `m_axi` bus depends on
row-contiguous access (true here) plus HLS port widening, to be checked at the dataflow-
optimization stage.

Hardware grid is **1024 x 1024**. Each real matrix (bin density, Ex, Ey) is 4 MB and each
complex FFT scratch matrix is 8 MB, so all matrices are **DDR-resident** and streamed
through the PL in row tiles; on-chip BRAM/URAM holds only the working tiles.

## ⚠️ Pin records carry ABSOLUTE positions, and must be refreshed every iteration (P2, `ed25f1a`)

`NodePin` is `{node_idx, x, y, net}` — `x,y` are the pin's **absolute position**, not the static
offset it used to hold. The static offsets live in a parallel upload-once `PinOffset[]`.

**The pin refresh must run at every new probe, before the gradient and before metrics.**
`refresh_net_pins` / `refresh_node_pins` (in `hpwl_gradient.hpp` — they are the prep pass for
that CU, not a standalone module) implement `MODE_REFRESH_PINS`, which folds `node_pos` (v_k) +
`PinOffset[]` into both pin arrays. Skipping it does
not crash and produces no obvious symptom — it silently evaluates the gradient at the *previous*
iterate's positions. Per-iteration order is therefore: **refresh → HPWL gradient / metrics**.

Why: this removed the three random `node_pos` gathers from `hpwl_gradient` phases 1/2/3 and the
fourth from `metrics::hpwl_sweep`. Those gathers were the measured bottleneck (they were the only
ports HLS could infer no burst for). All four loops now stream and burst; `sweep_bbox`'s pipeline
depth halved 146 → 73. The one remaining random gather is inside `refresh_net_pins`, deliberately
concentrated there because it has no float datapath behind it and can be tuned on its own. The
node-major refresh is monotone and much cheaper. Full evidence:
[[_NEW_REPORT_20_hpwl_gradient_opt_20260828.md]].

**Wired (`3a26da1`):** `eval_gradients` issues `MODE_REFRESH_PINS` before `MODE_HPWL_GRAD` at
every probe, so the `--place` path folds v_k into the pin arrays on the device each iteration.
(`runMetrics()` is a standalone verifier that uploads pre-folded pins with inert offset buffers,
so it neither can nor should refresh.)

**In the resident loop this becomes Memory Writer's job** — it already owns writing v_{k+1}, and
it is the only place that knows a node moved. That is why the offsets must be device-resident.

`hpwl_gradient` also now emits total HPWL as a by-product of its bbox pass (P1b, `21adad6`), so
`metrics::hpwl_sweep` is redundant and can be deleted once its consumer switches over. That path
requires `bb_DDR` to be **zeroed before first use** (masked nets are never written; zeroed, they
contribute a zero-extent box). Both `Driver.cpp` allocation sites memset it.

## Node index space & kinds (fillers, #20 step 5, 2026-08-29)

The flat node index is nested by kind, so a per-node consumer classifies by index alone -- no
per-node kind field is stored (Mark's call: nested ranges over a struct field):

```
0 ----- first_macro ----- first_filler ----- num_movable(M) ----- num_nodes(N)
|  std   |    macro        |    filler        |      fixed          |
| [0,fm) |   [fm,ff)       |   [ff,M)         |     [M,N)           |
```

`DesignHeader.first_macro`/`first_filler` carry the two boundaries; `classifyNode(n, ...)`
(`host_interface.hpp`) returns `{is_std_cell, is_movable_macro, is_filler, is_fixed}`, exactly one
true. Two consumers use it: the **movable-macro deposit-weight override** (#11b -- `node_footprint`
sets `weight = target_density` for a movable macro when `td<1`; `density_bin`/`force_gather` derive
`is_macro` per node and stay adjoints because they share `node_footprint`), and **fillers** -- on no
nets (density force only), placed uniform-random across the die by the packer (`tagMovableMacros` +
`db.addFillers` run host-side before `packDesign`).

Two density maps follow from this split. The **force / field-solve map** scatters ALL movable
`[0,M)` (fillers included) -- that is what `density_bin` emits today. The **convergence-overflow
map** must scatter movable-EXCLUDING-fillers `[0,first_filler)` -- **not yet built**; the resident
loop (step 6) adds it (`metrics` currently reduces the force map, so `--place` overflow is
filler-inclusive until then). A bring-up verify pack has no macros/fillers, so
`first_macro==first_filler==M` and both reduce to the old all-std behaviour.

## Stage-by-stage

| # | Stage | Producer -> Consumer | Format |
|---|-------|----------------------|--------|
| 0 | **Pin refresh** | node_pos + PinOffset -> `refresh_pin_pos` -> net_pins, node_pins | DDR in place, `NodePin{node_idx,x,y,net}` |
| 1 | Node coords | (Memory Writer / host) -> HPWL Mgr, Density Mgr | DDR, 1 beat/node `{x,y,_,_}` |
| 2 | HPWL gradient | HPWL Mgr -> Iteration Update | DDR, 1 beat/node `{gx,gy,_,_}` (scatter-accumulated) |
| 2a| HPWL packet | HPWL Mgr <-> AIE HPWL graph | stream: pin coords out `{x,y,...}`, partials back `{dW/dx,dW/dy,...}` |
| 3 | Bin density | Density Mgr (internal) | DDR, 1024x1024 real, 256 words/row (128-bit words) |
| 3a| FFT I/O | Density Mgr PL pre/post <-> AIE FFT pool | stream cfloat `{re,im,re,im}`, 512 beats/row, 8 lanes |
| 3b| E-field Ex,Ey | Density Mgr -> Iteration Update | DDR, 1024x1024 real each, 256 words/row (128-bit words) |
| 4 | Updated coords | Iteration Update -> Memory Writer -> coords buffer | stream `coord_t` v_{k+1}, 1/node; Memory Writer is the single coords writer |
| 5 | Nesterov state | Iteration Update (owns) | DDR `coord_t u`[M] committed positions; v lives in the coords buffer (stage 1). v is the gradient anchor, so `node_box.{x,y}` carries v_k and `{w,h}` the cell size. BB `alpha` is a host scalar in v1, so no on-PL prev-gradient slot is needed. |
| 6 | Status | Metrics -> host | out `{hpwl, overflow_sum}`; host scales overflow_sum by `bin_area/movable_area` |

## Control / policy (v1)
The host owns the gamma schedule, the lambda update (and "jolt"), and the convergence
test. It passes `gamma`, `lambda`, `alpha` into each `top` invocation over AXI-Lite and
reads the status beat back between iterations. The on-PL iteration loop is retained so
this policy can later migrate onto the PL (gamma becomes a ROM indexed by overflow) with
no change to the datapath.

## Device-resident iteration loop (target; supersedes the v1 host-owned control)
The whole schedule + convergence now lives on the PL in `modules/param_scheduler.hpp` (verified
bit-for-bit vs the sw_only golden), and the Barzilai-Borwein step norms reduce on-device in
`modules/bb_reduce.hpp`. So the loop can run **fully on the device**: host uploads the static design
once, the kernel runs N iterations with **no per-iteration round-trip**, host downloads final coords
once. This removes ~8 XRT kernel-launches/iter (~50-100us each) + the schedule sync per iteration --
the reason the schedule was moved on-chip (residency, not speed).

**Per-iteration order (one gradient eval per iteration, at the current probe v_k):**
1. HPWL gradient: `hpwl_gradient(v_k, inv_gamma_k)` -> g_hpwl        (inv_gamma_k from scheduler, iter k-1)
2. Density solve: bin_scatter(v_k) -> DCT/IDCT/IDXST via AIE FFT -> `force_gather` -> g_density
3. `bb_reduce(v_k, v_{k-1}, g_hpwl, g_density, g_total_{k-1}, precond, lambda_k)`
   -> pos_norm_sq, grad_norm_sq, and materializes g_total_k (= g_total_prev for iter k+1)
4. `metrics(v_k, bin_density)` -> HPWL, overflow_sum; loop forms overflow = sum*bin_area/movable_area
5. `param_scheduler(state, hpwl, overflow, pos_norm_sq, grad_norm_sq, kappa=sched_kappa(lambda,c))`
   -> inv_gamma_{k+1}, alpha_{k+1}, coeff_{k+1}, lambda_{k+1}, **stop**
6. `iteration_update(v_k, g_hpwl, g_density, alpha_k, coeff_k, lambda_k)` -> v_{k+1} (Memory Writer)
7. carry state: v_{k-1} <- v_k, g_total_{k-1} <- g_total_k, SchedState persists; if `stop`, exit.

**Resident state** (no host between iterations): `SchedState` (lambda, nesterov_ak, prev_hpwl,
best_primary/fallback, life, conv_remaining, 64-deep hpwl/ovfw rings) truly on-chip; the M-sized
`v_prev[M]` and `g_total_prev[M]` stay DDR-resident (too big on-chip at M~1e6).

**Host boundary (once each):** upload design + config scalars incl. `base_gamma`,
`kappa_coef = precond_coef*K/total_pins` (K = sum movable+filler normalized areas),
`overflow_threshold`, `bin_area`, `movable_area`; download final coords + status (final HPWL/overflow,
stop reason, iters). Precond stays OFF (precond[n]=1), so no per-node preconditioner pass and `kappa`
uses the closed form `sched_kappa`. `kappa` is XPlace's `weighted_weight`
(param_scheduler.py:386) = sw_only's `precond_kappa`; it was called `dff` here until 2026-08-09,
which is what hid TODO #19b.

**Status (2026-08-29): the resident loop IS COMPOSED in `top.cpp` (#20 step 6, STRUCTURAL DRAFT).**
`top.cpp` now holds three diagram-level functions -- `density_gradient` (bin_scatter -> AIE-FFT
field solve -> force_gather, mirroring Driver.cpp:547-556), `resident_iteration` (the DATAFLOW
per-iteration order below), and `resident_place` (the loop + on-chip `SchedState`) -- driven by
`MODE_PLACE` with a dedicated resident ABI (gmem14-27 + 8 scalars). It **syntax-checks clean against
the Vitis HLS headers** (`g++ -fsyntax-only -I$XILINX_HLS/include`, both `!PL_ONLY` and `PL_ONLY`)
and tier-1 stays green. **NOT yet C-synthesized (tier-2) or sw_emu-verified (tier-3)** -- that is the
next gate. Two pieces deferred (Mark, "structure first"): the **movable-only overflow map**
`[0,first_filler)` (`metrics` still reduces the filler-inclusive force map) and the **best-position
snapshot** (hook marked). **Host co-design pending:** the resident args (gmem14-27) mean `Driver.cpp`
needs a `runResidentPlacement()` for `MODE_PLACE` AND dummy bindings for the bring-up modes (XRT
requires every kernel arg set) -- the bring-up sw_emu modes will error until that lands.

> ### ⚠️ 2026-08-06 — compose this loop LAST, not next. See TODO #20.
> The algorithm in these modules is pinned to the **2026-07-14** sw_only. `param_scheduler.hpp` has
> not been touched since; sw_only has taken 20 commits plus the uncommitted #19 since, including the
> #11a in-die-shift deletion, #11b's movable-macro deposit weight, XPlace-faithful filler sizing, the
> coarse-divergence overflow conjunct, phase-relative counters, and mixed-size phase 2.
>
> The drift went unnoticed because **`Placer::dumpScheduleTrace()` was deleted from sw_only as dead
> code** (`44612cc`, 2026-07-28). It was the only producer of the golden `vck5000/test/sched_verify.cpp`
> replays, and that consumer is in another variant and names it by filename — nothing in the build
> could see the coupling. So the fixture cannot be regenerated, and `sched_verify` passes against a
> 2026-07-18 golden and always will. It is not evidence about the current algorithm.
>
> Also: only 5 of 18 modules are covered at tier 1 (`fft_pl`, `field_solve_pl`, `param_scheduler`,
> `hpwl_gradient`, and `bb_reduce` as of 2026-08-28 — the only ones a harness `#include`s;
> `density_bin_model.cpp` holds its own stale copy of `node_footprint`), so most modules Stage 5
> must change are still unverifiable without a full sw_emu cycle.
>
> Restore the trace + the tier-1 coverage first. Full assessment, including the known datapath
> divergences and the structural gaps (second movable-only density map for the convergence overflow,
> backtracking, best-position buffer, fillers, phase-2 re-entrancy):
> `vck5000/1_REVIEW/reports/_NEW_REPORT_pl_algo_stage5_assessment_20260806.md`.

> **`bb_reduce.hpp` and `param_scheduler.hpp` are NOW WIRED into `top.cpp`** (2026-08-29, #20 step 6):
> `resident_iteration` calls `bb_reduce` then `param_scheduler` on-chip, no host round-trip. They are
> still ALSO covered standalone by `vck5000/test/synth_check.tcl` (C-synth: 0 errors, Fmax 411 MHz,
> bb_loop II=1) and `vck5000/test/sched_verify.cpp` (bit-for-bit golden replay in `make test`). The
> composed `top()` is syntax-clean vs the HLS headers but not yet C-synthesized/sw_emu-verified.
> ⚠️ The HOST side has NOT caught up: `Driver.cpp::runPlacement` + `Placement.hpp` still run the
> equivalent policy math on the host per-iteration (the pre-step-6 loop), and nothing drives
> `MODE_PLACE` yet. Composing the resident kernel and wiring the host driver are two steps; the second
> is pending. Do not re-derive these modules -- they exist and they match.

### #41 record-stream gradient inside the resident loop: keep positions and gradients in URAM (2026-10-01)
The standalone `bring_up/hpwl_gradient_computer` loads every position from DDR and drains every
gradient back each call. On adaptec1 that is >=26 K of the ~65 K cycles per axis (51.5 K beat loop;
the gradient zeroing is already fused into the load). **In the resident loop those two phases must
disappear:** `iteration_update` writes v_{k+1} straight into the position URAM, and whoever consumes
the gradient (`bb_reduce` / `iteration_update`) reads it on chip, zeroing as it reads. Open
constraints, none decided:
- **URAM budget.** One axis is 256 of 463 URAMs at the 1 M-slot capacity (pos + grad, 128 each), so
  both axes resident at once does not fit (512). Options: size the capacity to the design, process
  axes in turn, or keep one axis in DDR. Density's bin scatter also needs positions.
- **Chunking.** The 8 of 44 designs over 1 M slots (`hpwl_computer_v3` / `hpwl_gradient_computer_v2`)
  cannot hold all positions on chip: their per-chunk load, ghost exchange and drain through DDR
  stay, so the Big Fix applies fully only to unchunked designs.
- **Overlap belongs between modules, through streams.** DATAFLOW between phases that share a URAM
  array does not work: it would ping-pong the array (2x URAM), and `pos_URAM` has several writers.
-> [[_NEW_REPORT_41_ddr_bundles_20261001.md]]

## Open format decisions (to finalize as modules are implemented)
- AoS vs SoA and 1-vs-2 nodes per beat for the coord/gradient buffers.
- Exact net packet grouping for the AIE HPWL graph (mirror sw_only `prepareNetGroup`).
- Final AIE PLIO port names for the FFT pool and HPWL graph (with `aie/src/pl_algo`).
- ~~IDXST path (Ey)~~ -- DONE (Stage 4): same FFT + twiddle ROM as IDCT, plus an input reversal
  and an odd-output sign flip. `modules/dct_transpose.hpp`, `TF_IDXST`.
