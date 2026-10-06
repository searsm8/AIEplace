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

## Decisions, second round (Mark, 2026-10-06)

**D1. Mailbox: option A now, C later, B as the fallback** (from
[[_NEW_REPORT_42_mailbox_widening_20261002.md]]). E = external slots; each crosses the mailbox 4×.

| option | what | mailbox cycles | extra LUT | bigblue4 end-to-end |
|---|---|---|---|---|
| **A (now)** | fix the two non-burst 1-float loops (skip padding segments in the outer parcel loop) | 4E | ~0 | 86.7 → 48.9 s |
| B (fallback) | widen all four loops to 16 floats/beat, as prototyped | 4E/16 | ~128 K (four crossbars) | 28.7 s |
| **C (later)** | widen, routing mailbox beats through the beat loop's existing gather (`pin_bbox`) and scatter-add (`wa_gradient`) | 4E/16 | ~0 new (est.) | ≈ B |
| D | widen with one shared gather + one shared scatter | 4E/16 | ~64 K (est.) | ≈ B |

A goes into `hpwl_gradient_CU` in step 2. C is a post-integration optimization: it rewires the beat
loop, which the integration must not change at the same time. With A, FM 1 pass is −32% end to
end; once widened it is +8% at 1000 iterations, which is why both partitioners stay switchable.

**D2. `_CU` is the module suffix.** `CLAUDE.md`'s "Naming a PL module" rule is amended: the top-level
function is `<output>_CU` — the name says what it computes, and `_CU` says it is the compute-heavy
unit `top.cpp` calls (`hpwl_gradient_CU`: "a compute unit whose output is the HPWL gradient").

**D3. Build `sw_flat` new; sw_only stays frozen as the oracle.** A flattening refactor of sw_only
cannot stay bit-identical (slot order changes the float summation order of the density scatter and
the gradient merge), so it would cost a tolerance A/B and a re-score anyway, and it would destroy the
oracle it must be checked against. `sw_flat` runs on the same slot-ordered arrays as the PL: the
software fallback *and* the per-module golden. **It will likely become the final host** (Mark), so
it **keeps sw_only's high-level structure** — the same stages under recognisable names (setup,
initial placement, wirelength gradient, density, Nesterov step, schedule, output) — so a reader can
map one onto the other. sw_only stays the full-featured reference (phase 2, backtracking, LP macro
legalization) until `sw_flat` has scored.

**D4. Layout: hardware in `/AIEplace/pl`, all software in `/AIEplace/host/sw_flat`.**
```
pl/                        # hardware only
  README.md, DATAFLOW.md   # DATAFLOW.md becomes authoritative once the first module lands
  Makefile                 # test / synth / cosim / hw
  src/format/              # the host<->device contract (pin_record.hpp, ...): HLS-compilable,
                           #   included by host/sw_flat, never the reverse
  src/modules/  src/top.cpp
  test/                    # tier-1 harnesses: each HLS module vs its sw_flat function
host/sw_flat/              # all software: parse sink, packer + partition, CPU placer, driver
```
`host/sw_flat` includes `pl/src/format/`; nothing under `pl/src/` includes `host/`. AIE sources
(FFT) move to a top-level `aie/` when the density path arrives (step 6).

**D5. #20 and #40 closed** into history.md as superseded by #45.

**D6. `sw_flat` scope = PL v1 scope** (phase-1 GP, no backtracking, nets ≤ 96); its golden run is
sw_only with `enable_backtracking = false`, phase 1 only, `ignore_net_degree = 96`. **Control stays
on the CPU, XPlace's model (#44 arm B):** one kernel launch per iteration, a few scalars back
(HPWL, overflow, BB norms), λ / γ / step written in. No `param_scheduler` on the PL in v1.

**D7. `origin/geert` stays unmerged**; the two lines of work continue independently.

**Netless movable nodes (Mark's step-4 question), measured 2026-10-06** on all 44 designs (no net of
degree 2..100; `.claude/2_ARTIFACTS/scratch_45/netless_nodes.txt`): zero on 29 designs, at most
0.58% (mgc_superblue14, 3,560 of 612 K); the ISPD2015 superblues carry most of them. They are real
cells whose only nets are 1-pin or >100-pin (masked, e.g. clock/reset) — or no net at all. They
**occupy area**, so the density force must still spread them; they get no wirelength gradient.
**Fillers** are the same case at scale (no nets by construction; adaptec1 has ~160 K).

## Move policy (so nothing forks silently)
Copy a piece into `pl/`, then treat the `vck5000/` original as **read-only legacy**: fixes go to
`pl/` only. The vck5000 harnesses keep passing on the legacy copies until `vck5000/` is deleted in
step 9. A piece's first commit in `pl/` names its source path and commit.

## Steps

Each step: what, then how it is verified. Don't start a step until the previous one verifies.

1. **Skeleton.** `pl/` and `host/sw_flat/` per D4; `pl/src/format/pin_record.hpp` moved in as the
   first contract header, with one harness that asserts it; `make -C pl test` iterates a single
   `HARNESSES` list (the lesson of #42: the old runner kept its own copy and skipped two).
   → verify: `make -C pl test` passes, and exits non-zero when a harness is mutated to fail.
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
4. **Slot layout for the whole design.** Global order = the gradient's slots (chunk, local slot),
   then a **density-only tail**: netless movable nodes, then fillers (sw_only's count, size and
   seed). The tail has no pins, so it never enters a record beat, a bank colouring or a chunk's URAM
   capacity — the gradient module never sees it, and its chunking is unchanged; the density path
   streams the whole range. Per-node attributes (size, kind, preconditioner weight) are arrays in
   this order. **No caching** (Mark): every run treats the design as unseen, so start-up (parse +
   encode + partition) is part of every end-to-end number.
   → verify: an invariant checker (node↔slot bijection over all movable nodes and fillers; external
   slots are copies only; filler count and area equal sw_only's); chunk counts and external slots
   unchanged from today's `encode_chunked` on all 44.
5. **`sw_flat`**: CPU phase-1 GP on the flat slot-ordered arrays (D6 scope), using the same
   `partition_fm_passes` switch.
   → verify: trajectory (HPWL, overflow, λ, γ per iteration) vs sw_only under the matching config,
   within a stated tolerance, on adaptec1, mgc_fft_a and one chunked design (bigblue4); final
   quality through `dse.py` on a subset, legal-vs-legal, against sw_only at the same config.
6. **Density path in slot order**, one module at a time (`node_footprint`, `density_bin`, DCT/FFT
   pre/post + AIE FFT, `force_gather`, `iteration_update`, `bb_reduce`, `metrics`), each copied from
   `vck5000/pl`, converted, and verified.
   → verify: each module's tier-1 harness vs the matching `sw_flat` function, before the next starts.
7. **Compose.** `top.cpp` + the `sw_flat` driver with a `--backend cpu|device` switch: one kernel
   launch per iteration, γ/λ schedule and stop test on the CPU, a few scalars each way (D6, #44 arm B).
   Positions, gradients and density maps stay device-resident between launches.
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
- If the density path cannot stream the tail-after-gradient order efficiently on chunked designs
  (it must visit every chunk's own slots plus the tail, skipping external copies), the device-wide
  slot order needs a second, density-side index — which reintroduces the permutation this plan
  avoids. Check it on bigblue4 / newblue7 when `density_bin` is ported (step 6).
