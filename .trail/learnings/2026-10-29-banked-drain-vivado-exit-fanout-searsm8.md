---
title: "banked drain in Vivado: the loop exit drives a 14 K-fanout enable"
author: Searsm8
date: 2026-10-29
branch: pl_algo
touches:
  - pl/synth/bin_density_banked/banked_top.cpp
  - pl/synth/bin_density_banked/impl_check.tcl
---

## Decided
- banked_top meets 3.33 ns in Vivado OOC synthesis (2.928 ns, WNS +0.402) once the drain's while-loop exit reads a REGISTERED all_finished (`done = all_finished; all_finished = AND(...)`), on top of the 2026-10-28 csynth edits. This supersedes that learning's "first attempt met timing": csynth was met (+0.10) but Vivado was not (4.94 ns).
- style=frp on the drain pipeline was measured too (2.880 ns): only 0.05 ns better, so it was not taken.

## Tried and ruled out
- Exit on the AND of the counts taken before the cycle's reads (csynth fix only): Vivado -1.608 ns, all 20,995 failing endpoints start at finished[]. The exit feeds the pipeline enable, which fans out to every URAM EN and pipeline register: 11 levels, 78 % routing. csynth models neither.

## Watch out
- In an HLS pipelined while loop, the exit condition sits on the global enable. Make it come straight from a flop. A csynth estimate under the target says nothing about that net.
- To bucket failing paths, open the IP checkpoint `impl/verilog/project.runs/bd_0_hls_inst_0_synth_1/bd_0_hls_inst_0.dcp`, not `synth_1/bd_0_wrapper.dcp`, which holds the kernel as a black box. Neither carries the clock: re-issue `create_clock -period 3.330 [get_ports ap_clk]` (the export's banked_top.xdc).
- impl_check.tcl takes 77 min for banked_top on the build server (csynth 18 + Vivado 58). Run variants in parallel dirs, detached with setsid nohup.
