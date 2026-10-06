# sw_flat — the flat, slot-ordered placer (software)

All AIEplace software for the integrated design (#45): one parse of LEF/DEF/Bookshelf straight into
flat arrays, the pin-record packer and partitioner, the CPU placer, and the driver that runs the same
iteration either on the CPU or on the VCK5000 (`--backend cpu|device`). The γ/λ schedule and the
stop test always run here, on the CPU; the device returns a few scalars per iteration.

It keeps **sw_only's high-level structure** (setup, initial placement, wirelength gradient,
density, Nesterov step, schedule, output) so the two can be read side by side. sw_only stays frozen
as its golden until `sw_flat` has been scored.

Device-side layouts come from `pl/src/format/`; this tree includes them, never the reverse.

Empty for now: step 1 of the plan (`.claude/1_REVIEW/reports/_NEW_PLAN_45_integration_20261006.md`).
