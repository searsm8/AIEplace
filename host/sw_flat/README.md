# sw_flat — the flat, slot-ordered placer (software)

All AIEplace software for the integrated design (#45): one parse of LEF/DEF/Bookshelf straight into
flat arrays, the pin-record packer and partitioner, the CPU placer, and the driver that runs the same
iteration either on the CPU or on the VCK5000 (`--backend cpu|device`). The γ/λ schedule and the
stop test always run here, on the CPU; the device returns a few scalars per iteration.

It keeps **sw_only's high-level structure** (setup, initial placement, wirelength gradient,
density, Nesterov step, schedule, output) so the two can be read side by side. sw_only stays frozen
as its golden until `sw_flat` has been scored.

Device-side layouts come from `pl/src/format/`; this tree includes them, never the reverse.

Plan: `.claude/1_REVIEW/reports/PLAN_45_integration_20261006.md`. So far:

| file | what |
|---|---|
| `flat_design.hpp` | one parsed design as flat arrays: file facts, DataBase's units and conventions |
| `flat_database.{hpp,cpp}` | `FlatDatabase` (a `DesignSink`) + `read_flat_design`: the one parse (step 3) |
| `flat_netlist.hpp` | sw_only's movable-macro rule; `to_netlist` -> the packer's `Netlist` |
| `fillers.hpp` | filler count and size: sw_only's `DataBase::addFillers` on a FlatDesign |
| `slot_layout.hpp` | the whole design in device slot order (step 4): the chunks back to back, then the filler tail |
| `export_netlist.cpp` | one design -> the packer's `Netlist` file; `make netlists` writes all 44 to `build/netlists/` |
| `packer/` | pin-record packer + partitioner (from `vck5000/bring_up/beat_packer`); no parser of its own, it reads `Netlist` files (`read_netlist`) |
| `model/` | #47 cycle / layout models |
| `test/flat_database_check.*` | the step-3 gate: FlatDatabase vs DataBase (bit-exact) and vs the legacy packer reader |
| `density_inputs.hpp` | #48: `bin_density_CU`'s host inputs from a placer (footprint table, per-slot index); `make check-bin-density` holds the module bit-equal to sw_flat's deposit on real designs |
| `borrowed_fusion.hpp` | #47 M2: the chunk exchange as `iteration_update` will do it (option A); `pl/test/borrowed_copies_test` holds it bit-equal to B3's device exchange |
| `placer/` | `sw_flat_place`, the CPU phase-1 placer (step 5): sw_only's algorithm on slot-ordered state, one file per sw_only file (see `placer.hpp`) |
| `test/placer_check.sh`, `test/d6_config.py` | the step-5 gate: `sw_flat_place` vs sw_only under the D6 config, schedule traces identical as text |

`make check-database` runs the gate on the 44 benchmarks and 3 fixtures (~60 s); it exits non-zero on
any difference. `make check-layout` is step 4's gate (`test/slot_layout_check.*`): the slot-layout
invariants and the fillers bit-equal to DataBase's, on all 44.

`make check-placer` is step 5's gate: it builds sw_only, writes the D6 config (phase 1, no
backtracking, nets ≤ 96 pins) for each design, runs both placers, and requires identical
`schedule_trace.csv` plus the same stop and final report. Pass other designs as arguments to
`test/placer_check.sh` (`ispd2005/adaptec1`, `ispd2005/bigblue4`, ...).

**sw_flat is bit-identical to sw_only, not merely close.** Its state is in slot order, but every
order-sensitive float sum runs in sw_only's order (`placer.hpp` names the orders). Summing in slot
order, the order the PL uses, is a separate change that will be measured on its own.
