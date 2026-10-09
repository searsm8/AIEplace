# sw_flat — the flat, slot-ordered placer (software)

All AIEplace software for the integrated design (#45): one parse of LEF/DEF/Bookshelf straight into
flat arrays, the pin-record packer and partitioner, the CPU placer, and the driver that runs the same
iteration either on the CPU or on the VCK5000 (`--backend cpu|device`). The γ/λ schedule and the
stop test always run here, on the CPU; the device returns a few scalars per iteration.

It keeps **sw_only's high-level structure** (setup, initial placement, wirelength gradient,
density, Nesterov step, schedule, output) so the two can be read side by side. sw_only stays frozen
as its golden until `sw_flat` has been scored.

Device-side layouts come from `pl/src/format/`; this tree includes them, never the reverse.

Plan: `.claude/1_REVIEW/reports/_NEW_PLAN_45_integration_20261006.md`. So far:

| file | what |
|---|---|
| `flat_design.hpp` | one parsed design as flat arrays: file facts, DataBase's units and conventions |
| `flat_database.{hpp,cpp}` | `FlatDatabase` (a `DesignSink`) + `read_flat_design`: the one parse (step 3) |
| `flat_netlist.hpp` | sw_only's movable-macro rule; `to_netlist` -> the packer's `Netlist` |
| `export_netlist.cpp` | one design -> the packer's `Netlist` file; `make netlists` writes all 44 to `build/netlists/` |
| `packer/` | pin-record packer + partitioner (from `vck5000/bring_up/beat_packer`); no parser of its own, it reads `Netlist` files (`read_netlist`) |
| `model/` | #47 cycle / layout models |
| `test/flat_database_check.*` | the step-3 gate: FlatDatabase vs DataBase (bit-exact) and vs the legacy packer reader |

`make check-database` runs the gate on the 44 benchmarks and 3 fixtures (~60 s); it exits non-zero on
any difference.
