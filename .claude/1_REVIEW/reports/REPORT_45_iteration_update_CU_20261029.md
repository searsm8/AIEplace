# REPORT #45 step 6 / #47 M3–M5 — `iteration_update_CU`, slot order, fused borrowed exchange (2026-10-29)

**Result: `iteration_update_CU` is bit-exact against sw_flat's `Placer::iteration_update` on 5 real designs
(two chunked: bigblue3 K=2, bigblue4 K=3), 3 consecutive iterations each, and against #47 M2 on synthetic
designs with mutants. C-synthesis: UPDATE_STEP's loop II=1; UPDATE_COMBINE's loop does not yet finish
scheduling (open).** Contract and design choices:
[[PLAN_45_iteration_update_CU_20261029.md]].

## What was built
| piece | file |
|---|---|
| the CU: `UPDATE_COMBINE` (merge + combine + 4 norms), `UPDATE_STEP` (precond + Nesterov step + die clamp + send + 2 norms) | `pl/src/modules/iteration_update_CU.hpp` |
| the fused exchange's op steps (#47 B4 = A): GROUP of ≤ 4 entries in distinct banks, BEAT half-rows, per-borrower ring | `pl/src/modules/iteration_update/borrowed_exchange.hpp` |
| the device's sum order: 16-lane tree, 16 rotating partials, final tree | `pl/src/modules/iteration_update/slot_reduce.hpp` |
| the op format (`FUSED_*`, `FusedOwnerDesc`, 64 bytes) | `pl/src/format/pin_record.hpp` |
| host op builder + beat-level golden (`fused_owner_ops`, `fused_device_ops`, `replay_fused_merge/_send`) | `host/sw_flat/borrowed_fusion.hpp` |
| `[sw_flat] reductions = "lanes"`: BB, L1, precond and λ₀ sums in the device's order (default off) | `placer/{step,schedule,setup}.cpp`, `Placer::reduce_sum` |
| tier 1 (synthetic, M2 + sw_flat expressions, mutants) | `pl/test/iteration_update_CU_test.cpp`, in `make -C pl test` |
| real designs vs the Placer | `host/sw_flat/test/iteration_update_check.{cpp,sh}`, `make -C host/sw_flat check-iteration-update` |
| C-synth check | `pl/synth/iteration_update_CU/` |
| contract in the skeleton | `host/sw_flat/device_loop.hpp` |

## Verification
**sw_flat default unchanged:** `make -C host/sw_flat check-placer` PASS, fft_a 628 and pci_bridge32_b 715
iterations bit-identical to sw_only after the `reduce_sum` refactor.

**Tier 1** (`make -C pl test`, 5/5 harnesses pass). Synthetic netlist, chunked by the packer:
| | capacity 4096 (K=3, ≤ 2 borrowers/owner) | capacity 3600 (K=4, ≤ 3) |
|---|---|---|
| [1] beat-level golden vs M2, merge / send | 0 / 0 differ | 0 / 0 differ |
| [2] COMBINE gradients (both axes) | 0 of 10,988 | 0 of 10,760 |
| [2] 4 norms = slot_reduce order; rel vs double | equal; ≤ 2.1e-8 | equal; ≤ 8.2e-8 |
| [3] STEP node / probe / 1/precond; borrowed (send) | 0 / 0 / 0; 0 of 4,090 | 0 / 0 / 0; 0 of 7,152 |
| [4] mutant: borrower ranks reversed | 25 differ | 114 differ |
| [4] mutant: one merge entry dropped | 2 | 2 |
| [4] mutant: borrower 0's merge skipped | 242 | 378 |
| [4] mutant: borrower 0's send BEATs skipped | 242 | 378 |
| [4] mutant: one send entry dropped | 12 | 12 |

A single dropped send BEAT is NOT caught and need not be: the ring keeps the half-row, so the next BEAT
writes it and only a block's last half-row is lost, which is often padding. The stream mutant drops all of
one borrower's BEATs instead. Bare cuts at small capacity give owners up to 19 borrowers, past
`FUSED_MAX_BORROWERS = 4`; the host exits on that, so the second synthetic config is K=4.

**Real designs** (`check-iteration-update`; D6 config + `wa_sums = "tree"`, `grad_merge = "stream"`,
`hpwl = "fixed"`, `wa_exp = "lut"`, `reductions = "lanes"`; iterations 5, 6, 7). Per iteration: COMBINE's
gradients and 4 norms vs `merge_borrowed_gradients` + `combine_gradients` + `lipschitz_estimate`; then the
placer's schedule and `take_step`, and STEP (with the host's scalars, into the OLD current buffers — the
swap) vs node, probe, borrowed copies, 1/precond_weight, a1, a2. **All 33 checks pass on every design,
bit for bit:**
| design | chunks | movable slots | borrowed slots | beats streamed | cycles per call (COMBINE / STEP) |
|---|---|---|---|---|---|
| mgc_fft_a | 1 | 61,568 | 0 | 3,850 | 3,850 / 3,850 |
| mgc_pci_bridge32_b | 1 | 31,382 | 0 | 1,964 | 1,964 / 1,964 |
| mms/adaptec1 | 1 | 371,033 | 0 | 24,208 | 24,208 / 24,208 |
| bigblue3 | 2 | 2,040,914 | 50,398 | 127,584 | 141,973 / 141,974 |
| bigblue4 | 3 | 5,054,762 | 266,623 | 315,960 | 399,804 / 399,806 |
Logs: `.claude/2_ARTIFACTS/scratch_45/iteration_update_check_20261029/`.

## Cost of the exchange, as built
bigblue4: 16,714 shared beats carry 100,558 merge ops (≈ 6.0 per shared beat, 2.65 entries per op), so a
call is 399.8 K cycles for 316.0 K beats (+26.5%). Three things set it, in order: borrowers are served in
turn, not in parallel (PLAN deviation 3); a group stops at a bank already used or at 4 entries; each block's
half-rows are their own ops. For comparison, the B3 passes this replaces in `hpwl_gradient_CU` cost (M0
model) ≈ 0.6 M cycles per axis on bigblue4. Parallel per-borrower units are the
obvious next lever; they need the same DDR argument in several processes, so a different port plan.
(Section below gives the M0 comparison.)

## Synthesis (`pl/synth/iteration_update_CU/`, 2.5 ns = `HLS_FREQ_HZ`, local Vitis HLS 2022.2)
- **Run 1:** `step_beats` Final II = 2. Cause: the rotating partials `partial[beat % 16]` read to HLS as a
  distance-1 carried dependence on each partitioned partial. Fixed by making `SlotReducer` a 16-deep shift
  register (add into `acc[15]`, the partial that took beat b − 16; shift in at `acc[0]`): the same sums —
  tier-1 norms unchanged to the bit, `check-iteration-update` 5/5 still bit-exact.
- **Run 2: `step_beats` Final II = 1, depth 207** (UPDATE_STEP: precond, step, clamp, send groups and BEATs).
  **`combine_beats` (UPDATE_COMBINE) did NOT finish:** the scheduler was still on its first attempt at it
  after ~75 min when the 90-min timeout killed the run. No II is reported, so
  "C-synthesizes at II=1" is **met for STEP, not for COMBINE**. Log:
  `.claude/2_ARTIFACTS/scratch_45/iteration_update_check_20261029/csynth_run2_timeout.log`.
  What COMBINE has that STEP does not, i.e. the suspects, most likely first: (1) the merge GROUP's reads
  `ring_BRAM[s][axis][b][row]` with a run-time `s`, feeding a 32:1 bank mux per entry and a run-time-lane
  write into `addend_REG[s][axis][lane]` (STEP only writes the ring); (2) the 4-deep masked add chain per
  lane and axis right after it, in the same cycle; (3) `next_grad` read and rewritten through one port with
  `DEPENDENCE inter false`. Next try: register the merged addends one cycle (split the GROUP's steering from
  the BOUNDARY's chain), then synthesize COMBINE alone (a `mode`-specific top) to bound each run.

## Cost of the exchange, M0 comparison
The B3 passes this replaces cost ≈ 0.6 M cycles per axis on bigblue4 (M0: C8 1.57 M vs B4 0.98 M), each
gradient evaluation; the fused version as built adds ≈ 84 K cycles to each of the two update calls.

## Not done here
- **`hpwl_gradient_CU` without its borrowed passes (#47 item 4/D).** Proposed as its own task: it changes the
  interface of the kernel that passes on the card at 250 MHz (build H), the XRT host and cosim benches,
  and orphans `borrowed_copies.hpp` + `borrowed_copies_test`.
- `initialization()` on the device; the force gather (input array for now); the DDR port plan; a 2-adder
  merge in place of the 4-deep chain.
