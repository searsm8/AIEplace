---
title: "banked drain timing: control path, deferred URAM write-back"
author: Searsm8
date: 2026-10-28
branch: pl_algo
touches:
  - pl/synth/bin_density_banked/banked_top.cpp
---

## Decided
- drain_banks (pl/synth/bin_density_banked) closed csynth timing at 3.33 ns (-2.14 -> +0.10 ns) with three edits: exit on finished[] counts taken BEFORE the cycle's reads (one idle cycle), one-hot FIFO pick with the 8 reads OR-merged (non-taken read = 0), and the URAM store writes the PREVIOUS cycle's sum from forwarding slot 0 so its data is a register.
- Bit-exact by construction (integer adds); banked_check is the gate, with three mutants (old pitch, broken one-hot, dropped write-back).

## Tried and ruled out
- Nothing failed; the first attempt met timing. A per-bank 8->4->2->1 merge tree was not needed.

## Watch out
- The critical path was in TWO places, not one: besides the control chain (state 2), the URAM dout -> forward select -> add -> store chain was 3.13 ns in state 5. Read every state's path in *.verbose.sched.rpt ('critical path consists of'), not just the worst one.
- Writing back from fwd slot 0 makes the read->write distance one ITERATION longer even though the load/store states do not move. C sim cannot see it; the 64-bank co-sim must sweep BANKED_FORWARD_DEPTH (6 may now have zero margin). Raise it if needed, never lower it.
- `if (pick == p) u = read()` over p becomes a serial phi/mux chain in HLS; write one-hot + OR instead.
- A mutant that breaks one-hot makes the drain hang (two lasts in one cycle count once). Run mutants under `timeout`, and never `pkill -f` a pattern that also matches your own `bash -c` line.
- Full banked_top csynth takes ~12 min on this box.
