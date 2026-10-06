# pl — the AIEplace hardware (Programmable Logic)

The placement datapath for the AMD Versal VCK5000. This tree holds **hardware only**: HLS modules,
the kernel top, and the tier-1 harnesses that check each module against its software golden. All
software — parser sink, packer, partitioner, the CPU placer `sw_flat`, the driver — lives in
[`host/sw_flat/`](../host/sw_flat/README.md).

Built up under #45 (plan: `.claude/1_REVIEW/reports/_NEW_PLAN_45_integration_20261006.md`) from the
verified pieces in `vck5000/`, which is legacy and goes away once everything has moved.

## Layout
```
src/format/    the host<->device contract: HLS-compilable, included by host/sw_flat
src/modules/   one compute unit per file: <output>_CU.hpp defines <output>_CU
src/top.cpp    the kernel top (later)
test/          tier-1 harnesses: plain g++, no Vitis, seconds
```

## Rules
- **Slot order everywhere.** Every per-node array the PL touches is indexed by `node_slot`
  (`src/format/pin_record.hpp`): the gradient's slots, chunk by chunk, then a density-only tail
  (netless movable nodes, then fillers).
- **Contract direction:** `host/sw_flat` includes `pl/src/format/`; nothing under `pl/src/` includes
  `host/`. Tier-1 harnesses under `pl/test/` may include both.
- **Moving a piece from `vck5000/`:** copy it here, name its source path and commit in the first
  commit, and from then on fix it here only. The `vck5000/` copy is read-only legacy.
