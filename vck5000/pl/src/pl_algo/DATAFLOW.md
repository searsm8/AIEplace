# pl_algo data flow

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

## Stage-by-stage

| # | Stage | Producer -> Consumer | Format |
|---|-------|----------------------|--------|
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
1. HPWL gradient: `hpwl_CU(v_k, inv_gamma_k)` -> g_hpwl        (inv_gamma_k from scheduler, iter k-1)
2. Density solve: bin_scatter(v_k) -> DCT/IDCT/IDXST via AIE FFT -> `force_gather` -> g_density
3. `bb_reduce(v_k, v_{k-1}, g_hpwl, g_density, g_total_{k-1}, precond, lambda_k)`
   -> pos_norm_sq, grad_norm_sq, and materializes g_total_k (= g_total_prev for iter k+1)
4. `metrics(v_k, bin_density)` -> HPWL, overflow_sum; loop forms overflow = sum*bin_area/movable_area
5. `param_scheduler(state, hpwl, overflow, pos_norm_sq, grad_norm_sq, dff=sched_dff(lambda,c))`
   -> inv_gamma_{k+1}, alpha_{k+1}, coeff_{k+1}, lambda_{k+1}, **stop**
6. `iteration_update(v_k, g_hpwl, g_density, alpha_k, coeff_k, lambda_k)` -> v_{k+1} (Memory Writer)
7. carry state: v_{k-1} <- v_k, g_total_{k-1} <- g_total_k, SchedState persists; if `stop`, exit.

**Resident state** (no host between iterations): `SchedState` (lambda, nesterov_ak, prev_hpwl,
best_primary/fallback, life, conv_remaining, 64-deep hpwl/ovfw rings) truly on-chip; the M-sized
`v_prev[M]` and `g_total_prev[M]` stay DDR-resident (too big on-chip at M~1e6).

**Host boundary (once each):** upload design + config scalars incl. `base_gamma`,
`dff_coef = precond_coef*K/total_pins` (K = sum movable+filler normalized areas),
`overflow_threshold`, `bin_area`, `movable_area`; download final coords + status (final HPWL/overflow,
stop reason, iters). Precond stays OFF (precond[n]=1), so no per-node preconditioner pass and `dff`
uses the closed form `sched_dff`.

**Status:** control modules (param_scheduler, bb_reduce, metrics, iteration_update) built; the
bb_reduce + param_scheduler core C-synthesizes (`vck5000/test/synth_check.{cpp,tcl}`). Remaining = compose
the datapath (stages 1-3) + control into one resident `top` loop with the AIE FFT streaming per
iteration, then sw_emu-verify the trajectory vs the golden (needs the Vitis/AIE env).

> **`bb_reduce.hpp` and `param_scheduler.hpp` are BUILT AND VERIFIED BUT NOT WIRED INTO `top.cpp`.**
> This is the correct in-progress state, not an oversight -- they are the *device-resident* control
> path, and nothing consumes them until the resident loop above is composed. They are exercised
> today only through `vck5000/test/synth_check.tcl` (HLS C-synthesis: 0 errors, Fmax 411 MHz, bb_loop II=1)
> and `vck5000/test/sched_verify.cpp` (offline bit-for-bit replay vs the sw_only golden trace -- runs in
> `make test` against the committed `vck5000/test/fixtures/schedule_trace_adaptec1.csv`). Meanwhile
> `top.cpp` still runs the host-owned loop above, with the equivalent policy math on the host in
> `host/src/pl_algo/src/Placement.hpp`. Do not re-derive these modules -- they exist and they match.

## Open format decisions (to finalize as modules are implemented)
- AoS vs SoA and 1-vs-2 nodes per beat for the coord/gradient buffers.
- Exact net packet grouping for the AIE HPWL graph (mirror sw_only `prepareNetGroup`).
- Final AIE PLIO port names for the FFT pool and HPWL graph (with `aie/src/pl_algo`).
- ~~IDXST path (Ey)~~ -- DONE (Stage 4): same FFT + twiddle ROM as IDCT, plus an input reversal
  and an odd-output sign flip. `modules/dct_transpose.hpp`, `TF_IDXST`.
