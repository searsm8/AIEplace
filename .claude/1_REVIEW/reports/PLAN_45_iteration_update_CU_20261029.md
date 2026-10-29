# PLAN #45 step 6 / #47 M3–M5 — `iteration_update_CU` in slot order, with the fused borrowed exchange (2026-10-29)

Golden: sw_flat `Placer::iteration_update` (`host/sw_flat/placer/placer.cpp`), run with the record-stream
arithmetic on (`wa_sums = "tree"`, `grad_merge = "stream"`, `hpwl = "fixed"`, `wa_exp = "lut"`) plus the new
`reductions = "lanes"` (below). Exchange golden: #47 M2 (`host/sw_flat/borrowed_fusion.hpp`). Skeleton:
`host/sw_flat/device_loop.hpp`. Design of the exchange: [[PLAN_47_borrowed_copies_status_20261026.md]].

## Two kernel calls per iteration, one kernel
`Placer::iteration_update` has one global dependency in the middle: the BB step length
`sqrt(pos_norm_sq) / sqrt(grad_norm_sq + 1e-8)` needs a sum over EVERY movable slot before the first slot
can step, and the host's schedule (λ, precond_coef, the stop test) sits between them. So one kernel,
`iteration_update_CU(mode, ...)`, called twice:

| call | `mode` | sw_flat functions it replaces | reads | writes | returns |
|---|---|---|---|---|---|
| 1 | `UPDATE_COMBINE` | `merge_borrowed_gradients`, `combine_gradients`, `lipschitz_estimate`'s two sums | next.probe_grad (owned: WL gradient; borrowed blocks), density_force, current/next probe_pos, current.probe_grad, inv_precond | next.probe_grad (owned + fillers: combined) | pos_norm_sq, grad_norm_sq, gwl_L1, gden_L1 |
| host | | BB step, `record_iteration_results`, `update_schedule`, stop test, `++iteration` | the 4 norms + the gradient CU's HPWL + the field's overflow | step_length, momentum_coeff, λ, precond_coef | |
| 2 | `UPDATE_STEP` | `update_precond_weights`, `take_step` (`cache_state`, `step_all_nodes`, `enforce_die_boundaries`, `send_borrowed_positions`) | current node_pos / probe_pos / probe_grad (= the old next: the host swaps the [current, next] buffer pointers, which IS `cache_state`), num_pins, area, width, height | next node_pos / probe_pos (owned + fillers), borrowed blocks of next.probe_pos, inv_precond | a1_norm, a2_norm |

Both calls stream every owner chunk's OWNED beats `[slot_beat_offset, + first_borrowed_slot/16)`, then the
filler tail. Borrowed, macro-pin and fixed-pin rows are never streamed: borrowed rows move only through the
exchange; macro pins are refreshed on chip by `hpwl_gradient_CU`; fixed pins are constant in both buffers.

Momentum's `nesterov_ak` recurrence is two scalars: host. The `[current, next]` swap replaces `cache_state`
because `cache_state` copies every movable slot and nothing reads a non-movable slot of `current`.

## Host -> device scalars
`step_length`, `momentum_coeff`, `lambda_area_coef = precond_coef * density_weight` (one float product, as
sw_flat), `precond_enabled`, `die_w`, `die_h` (floats of sw_flat's ints), `clamp_w = bin_w * (float)M_SQRT2`,
`clamp_h` (0 when `enable_density_clamp` is off: `max(w, 0) = w`).

## Device -> host
The 6 norms above. NaN: the host tests the norms (any NaN slot makes a norm NaN); sw_flat's
`nan_detected` comes from the gradient, which is `hpwl_gradient_CU`'s report, not this CU's.

## The force gather is an INPUT (`density_force`, per slot, per axis)
`electrostatic_force(slot)`, λ included, is the gather over the slot's footprint bins of `field`. The PL
field solve is not in `pl/` (legacy `vck5000/` only, #48 phase 3) and `bin_density_CU` (#48) ends at ρ, so
the CU takes the gathered force as an array; a `force_gather_CU` replaces that array later with the same
values. Bit-exactness is unaffected: combine is `g - force`, the gather's arithmetic is upstream.

## Reductions: a device order, modelled in sw_flat
A serial float sum over slots cannot run at 16 slots per cycle. Device order (`slot_reduce.hpp`, plain C++,
included by both sides like `dhar_tree.hpp`): each beat's 16 lane terms (0 for a non-movable lane) by a
balanced tree, beat sums into 16 rotating partials (`partial[beat % 16]`, so the carried add has 16 cycles),
a tree over the partials at the end. Beats count over the streamed sequence (chunks' owned beats, then the
filler tail). sw_flat gets `[sw_flat] reductions = "lanes"` (default off: check-placer unchanged) that sums
`lipschitz_estimate`, `combine_gradients`, `update_precond_weights` and `initialize_density_weight` in
exactly that order, so the whole run — not just one call — is bit-comparable.

## The fused exchange (#47 B4 = A), and where it differs from PLAN_47
The owner streams its shared beats (C8: every shared slot is below `first_private_slot`) and, beside them,
replays ONE host-built op list per owner (`fused ops`, 64-bit), interleaving its borrowers:
- `GROUP` — borrower s, up to 4 entries (owner lane, borrower bank), all in one owner beat and in 4 distinct
  banks, so each bank's row buffer does at most one access per cycle (the "4-wide assembly buffer").
  Merge: read `row_buf[s][bank][next_row]` into the borrower's addend lane. Send: write the new position there.
- `BEAT` — borrower s's next half-row: merge loads it from the borrower's block in grad DDR, send writes it out
  to the block in pos DDR. Scheduled as B3's (`borrowed_block_ops`): send when the half-row is final, merge
  just before the first group needing it; per-borrower ring of `BORROWED_BUFFER_ROWS`.
- `BOUNDARY` flag on a GROUP (or a NOP): this owner beat is done (merge) / starts (send).

Merge's add: at the boundary each lane sums `wl + a_0 + a_1 + ...` in ascending borrower order, a masked
chain (a lane no borrower holds is selected, not `+ 0.0`, which would flip -0.0). Bit-identical to M2 and B3.

Deviations from PLAN_47 / the brief, and why:
1. **No separate `borrowed_send` / `borrowed_merge` stream processes.** In a DATAFLOW region the merge would
   read `next_grad` (borrowed blocks) while the update writes it (owned rows): one argument, a reader and a
   writer in different processes, which HLS's dataflow check rejects. The exchange is instead op-step
   functions (`iteration_update/borrowed_exchange.hpp`) inlined into the CU's one pipelined loop, one
   access site per argument per cycle. Tier 1 and synthesis test them inside the CU.
2. **The add is a MAX_BORROWERS-deep masked chain per lane, not 2 adders with stalls.** The 2-adder version
   needs a per-lane compaction and a stall controller; the chain needs neither and never stalls on adds.
   Cost: 16 lanes x 2 axes x MAX_BORROWERS adders, reported by synthesis. 2-adder is a later optimization.
3. **Borrowers are served in turn, not in parallel.** A shared beat costs max(1, sum over borrowers of its
   groups) cycles plus block beats; parallel per-borrower units need the same pointer in several processes
   (deviation 1). Cycles are counted by the golden and reported.

## Verification
- pl tier 1 `iteration_update_CU_test`: synthetic chunked designs (FM and bare cut), random state, vs M2 +
  the sw_flat expressions, bit for bit; mutants: parcel order reversed, one entry dropped (merge count off
  by one), one borrower's ops skipped, a BEAT op dropped (send).
- `make -C host/sw_flat check-iteration-update`: the real Placer, `run_until(n)`, then both calls vs
  `iteration_update`'s state, bit for bit, on fft_a, pci_bridge32_b, mms/adaptec1 and chunked bigblue4.
- `pl/synth/iteration_update_CU/`: C-synth, every loop II=1 at `HLS_FREQ_HZ`.

## Not in this step
`initialization()` on the device (needs a combine without subtract for λ_0 and the BB trial step's
restore); the DDR port budget (PLAN_47 item 8); the `hpwl_gradient_CU` pass removal follows once this passes.
