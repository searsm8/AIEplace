# PLAN #45 — Bring the native parser, partitioning and the chunked gradient together in `/AIEplace/pl`

*2026-10-06. Branch `pl_algo`. Written from the survey of #41 / #42 / #43 and Mark's decisions the
same day. Status lives in [[tasks.md]] #45; this file is the reasoning and the step list.*

## Where the three threads stood (survey, 2026-10-06)

Each thread is verified **on its own**, and none is connected to a placer:

| thread | state | lives in |
|---|---|---|
| #43 native parser | closed. `DesignReader` → abstract `DesignSink`; two sinks: `DataBase` (host) and beat_packer's `native_netlist.hpp`. 44/44 identical both ways | `host/src/common/` |
| #42 partitioning | closed. FM 1 pass is `encode_chunked`'s default; `Config::partition_fm_passes = 0` gives the bare BFS cut | `vck5000/bring_up/beat_packer/` |
| #41 chunked gradient | closed as a module. `hpwl_gradient_computer_v2`: tier-1 exact, II=1, co-sim PASS; 3.955 ns post-route | `vck5000/bring_up/hpwl_gradient_computer_v2/` |

What kept them apart:
1. **Two node orders.** pl_algo's `Packer.cpp` orders nodes std | macro | filler | fixed by walking
   `db.getComponents()`; every density-path module indexes that order. The gradient indexes
   `node_slot` (bank-coloured, padded, fixed rows inside each bank, chunk-local).
2. **Two data models from two reads.** `DataBase` (density, fillers, die) and beat_packer's `Netlist`
   (nets, offsets) are read separately, with separately written classification rules (height-mode
   macro rule, COVER = fixed, IO pins on first use).
3. **Netless nodes have no slot.** `assign_node_slots` skips any node with no in-scope pin
   (`NO_BANK`), and fillers do not exist in beat_packer at all. Density needs both.
4. `top.cpp` still runs the old `hpwl_gradient.hpp` on the `PackedDesign` pin list.

## Decisions (Mark, 2026-10-06)

- **Slot order everywhere.** The beat_packer format is the device-wide data format: every per-node
  array the PL touches is in slot order.
- **One parse feeds both routes:** the software route (when the VCK5000 is unavailable) and the
  hardware route. Rebuilding portions of sw_only from the ground up is on the table.
- **One chunk-capable gradient module, `hpwl_gradient_CU`**, grown from `hpwl_gradient_computer_v2`.
  An unchunked variant may come later, as an optimization.
- **`hpwl_computer*` were stepping stones** to the gradient computer. They are not carried forward
  (the gradient module already emits per-net HPWL as a by-product, at the same `v_k`).
- **Both partitioners stay**, switched by a config option (`partition_fm_passes`: 1 = FM, 0 = bare
  cut — already in `beat_packer::Config`; the host must expose it).
- **Start fresh in `/AIEplace/pl`.** Pieces from `vck5000/bring_up` and `vck5000/pl` are brought in
  one at a time. `/vck5000` stays until everything has moved, then goes.
- **sw_only uses nets up to 96 pins** (was 100) → **#46**, so the golden and the PL mask agree.
- **Gradient `max_rel` tolerance 1e-3; rel_rms stays 1e-5.** DONE 2026-10-06
  (`vck5000/test/wa_gradient_golden.hpp`): newblue3 4.18e-4, mms/bigblue4 1.57e-4, newblue7
  1.50e-4 all pass; rel_rms ≤ 7.5e-7. `make test` 21/21 PASS.
- **Post-route timing waits** until everything is brought together.

## Open decisions for Mark

**D1. Mailbox widening (from [[_NEW_REPORT_42_mailbox_widening_20261002.md]]).** E = external
slots; each crosses the mailbox 4× (send / receive / return / collect).

| option | what | mailbox cycles | extra LUT | bigblue4 end-to-end |
|---|---|---|---|---|
| A | fix the two non-burst 1-float loops (skip padding segments in the outer parcel loop) | 4E | ~0 | 86.7 → 48.9 s |
| B | widen all four loops to 16 floats/beat, as prototyped | 4E/16 | ~128 K (four crossbars) | 28.7 s |
| C | widen, routing mailbox beats through the beat loop's existing gather (`pin_bbox`) and scatter-add (`wa_gradient`) | 4E/16 | ~0 new (est.) | ≈ B |
| D | widen with one shared gather + one shared scatter (send+return, receive+collect) | 4E/16 | ~64 K (est.) | ≈ B |

**Recommendation: A now, inside `hpwl_gradient_CU`** (free, needed under every option, and it keeps
FM worth having). **C later**, as a post-integration optimization: it is the best end state but
rewires the beat loop, which is exactly what the integration must not be changing at the same time.
With A, FM 1 pass is −32% end to end; with B/C/D it is +8% at 1000 iterations unless the encoded
arrays are cached (step 4) — so the config switch earns its keep either way.

**D2. The name `hpwl_gradient_CU` conflicts with a `CLAUDE.md` rule** ("Naming a PL module": name it
for what it computes, "not an implementation detail like 'CU'"; it cites `hpwl_CU` → `hpwl_gradient`
as the example). Either amend the rule (e.g. "`_CU` marks the one top-level compute unit per
module file") or pick another name. Plan text uses `hpwl_gradient_CU` pending this.

**D3. Flatten sw_only, or build a new flat software route?** Recommendation: **build new, keep
sw_only frozen as the oracle.**
- A flattening refactor cannot stay bit-identical: slot order changes the float summation order of
  the density scatter and the gradient merge. It would need a tolerance A/B and a re-score anyway —
  the cost of building new — and it would destroy the oracle it must be checked against.
- The Parked "SoA layout" item in tasks.md already flagged this as freeze-breaking.
- So: a new CPU route (working name **`sw_flat`**) on the same slot-ordered arrays the PL uses. It
  is the software fallback *and* the per-module golden for the PL. sw_only stays the full-featured
  reference (phase 2, backtracking, LP macro legalization) until sw_flat has scored; then decide
  what to retire.

**D4. Layout of `/AIEplace/pl`** (proposal):
```
pl/
  README.md, DATAFLOW.md   # DATAFLOW.md is authoritative for the new design, as vck5000's was
  Makefile                 # test / synth / cosim / hw
  format/                  # the host<->device contract: pin_record.hpp, slot layout, ChunkDesc
  packer/                  # host-side encoder (beat_packer + partition), header-only C++14
  sw/                      # sw_flat: CPU kernels on the format/ arrays
  src/modules/  src/top.cpp
  test/                    # tier-1 harnesses + fixtures
```
The host driver becomes a new variant under the top-level `host/` (`--backend cpu|device`).
AIE sources (FFT) move to a top-level `aie/` when the density path arrives (step 6).

**D5. #20 and #40.** #45 re-homes #20's remaining steps (5: fillers + macro weight, 6: compose the
loop) and supersedes `hpwl_gradient_dhar` (#40). Close both into history.md, or keep them open?

**D7. Merge `origin/geert` first?** It is still unmerged (12 commits ahead, 2026-10-06), and his
`host/src/v2/` is an independent hand-written LEF/DEF host — a third parser next to `DesignReader`
and beat_packer's legacy readers. #21 said merge before restructuring; `pl/` is a restructure. At
minimum, tell Geert that `/AIEplace/pl` is coming.

**D6. sw_flat scope = PL v1 scope** (phase-1 GP, no backtracking, nets ≤ 96). Its golden run is
sw_only with the matching config (`enable_backtracking = false`, phase 1 only, `ignore_net_degree
= 96`). Confirm.

## Move policy (so nothing forks silently)
Copy a piece into `pl/`, then treat the `vck5000/` original as **read-only legacy**: fixes go to
`pl/` only. The vck5000 harnesses keep passing on the legacy copies until `vck5000/` is deleted in
step 9. A piece's first commit in `pl/` names its source path and commit.

## Steps

Each step: what, then how it is verified. Don't start a step until the previous one verifies.

1. **Skeleton.** `pl/` per D4, its own `make test`, hooked into the top-level flow.
   → verify: `make -C pl test` runs and fails when a harness fails (mutant check on the runner).
2. **`hpwl_gradient_CU`** = `hpwl_gradient_computer_v2` + its harness, copied and renamed, plus
   mailbox option A (or D1's pick). K=1 covers the unchunked designs.
   → verify: tier-1 [1]–[5] on synthetic + all 44 designs (relaxed max_rel, rel_rms 1e-5); the
   K=1 path on an unchunked design equals `hpwl_gradient_computer`'s output; C-synth II=1;
   RTL co-sim on the medium chunked case (spacing 4 PASS / 1 FAIL as the negative control).
3. **One parse, one flat model.** A new `DesignSink` (`FlatSink`) collects nets, pins, offsets *and*
   geometry (sizes, fixed status, die, rows, site width) straight into flat arrays — no `DataBase`
   objects. Classification rules (movable macro, COVER, IO pins) follow **sw_only's** definitions,
   since sw_only is the golden; reconcile `native_netlist.hpp`'s legacy rules against them.
   → verify: a canonical dump equal to `DataBase`'s on all 44 (extend `test/parser/parse_bench`);
   the encoder fields equal to `check-reader`'s `Netlist` on all 44.
4. **Slot layout for the whole design.** Every movable node gets a slot, netless nodes and fillers
   included (fillers: sw_only's count, size and seed). Global order = (chunk, local slot). Per-node
   attributes (size, kind, preconditioner weight) are arrays in slot order. The encoded arrays are
   cached per design (keyed on the input files + `Config`), so start-up partition cost is paid once.
   → verify: an invariant checker (node↔slot bijection over movable nodes; external slots are copies
   only; filler count and area equal sw_only's); a cache hit is byte-identical to a fresh encode.
5. **`sw_flat`**: CPU phase-1 GP on the flat slot-ordered arrays (D6 scope), using the same
   `partition_fm_passes` switch.
   → verify: trajectory (HPWL, overflow, λ, γ per iteration) vs sw_only under the matching config,
   within a stated tolerance, on adaptec1, mgc_fft_a and one chunked design (bigblue4); final
   quality through `dse.py` on a subset, legal-vs-legal, against sw_only at the same config.
6. **Density path in slot order**, one module at a time (`node_footprint`, `density_bin`, DCT/FFT
   pre/post + AIE FFT, `force_gather`, `iteration_update`, `bb_reduce`, `metrics`), each copied from
   `vck5000/pl`, converted, and verified.
   → verify: each module's tier-1 harness vs the matching `sw_flat` function, before the next starts.
7. **Compose.** `top.cpp` + host driver, per-iteration launch with host-side γ/λ schedule first (#44
   arm B: simplest, no `param_scheduler` area), resident loop after.
   → verify: sw_emu trajectory vs `sw_flat` on adaptec1, then a chunked design.
8. **Hardware.** Timing closure (Mark: after integration; continue
   [[_NEW_REPORT_41_next_steps_20261002.md]] §2), the NMU bandwidth check (§1 there: can one m_axi
   port feed 64 B/cycle at 300 MHz?), the per-axis URAM strategy (one axis is 256 of 463 URAM, so
   the axes run in turn), then #44's A/B on real HW.
9. **Retire `/vck5000`.** Move what is still needed (`tools/`, `test/regress/`, AIE, docs), delete
   the legacy copies, fix paths in `CLAUDE.md` / READMEs / skills.

## Carried in from closed tasks
- From #41: post-route timing (3.453 / 3.955 ns vs 3.33), the NMU check, the "Big Fix" (positions and
  gradients stay in URAM in the resident loop; does not apply to chunks), per-phase cycle numbers.
- From #42: D1; a contracted-hypergraph multilevel or Mt-KaHyPar (parked: neither pays at 1000
  iterations today).

## What would falsify this plan
- If step 5 cannot get `sw_flat` within tolerance of sw_only, slot order (or the flat model) hides a
  semantic difference: find it before any PL module is ported to slot order.
- If step 4's slot layout cannot hold netless nodes and fillers without breaking the gradient
  contracts (bank colouring, hazard spacing), the device-wide slot order needs a second, density-side
  index — which reintroduces the permutation this plan avoids. Measure on the largest designs first.
