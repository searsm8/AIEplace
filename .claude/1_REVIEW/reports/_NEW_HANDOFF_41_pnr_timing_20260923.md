# HANDOFF #41 — place-and-route of the record-stream modules (2026-09-23, mid-task)

Continues [[_NEW_REPORT_41_record_datapath_20260922.md]]. Interrupted by a machine shutdown
while diagnosing gradient-module timing.

## Where the runs are
- **Build server** scratch copy (not the server's git checkout, which has its own local edits):
  `~/aieplace_pnr/vck5000/bring_up/<module>/`. The sources are as of commit `43fff96`.
- **Flow:** Vitis 2024.2, run as `vitis-run --mode hls --tcl pnr.tcl` (= `synth_check.tcl` +
  `export_design -flow impl`). The server has no `vitis_hls` 2022.2. Each run writes
  `pnr_run.log` and `pnr.done`.
- **Where results land:** `synth_check_prj/sol1/impl/report/verilog/*_export.rpt`, with routed
  timing in `…/impl/verilog/report/*_timing_routed.rpt`.
- **Still running when I stopped:** `hpwl_gradient_computer_v2` (launched detached with
  `setsid nohup`, so it should finish on its own; check its `pnr.done`).

## Results (post-route, xcvc1902, 3.33 ns target)
| module | LUT | FF | DSP | BRAM | URAM | post-synth | post-route | timing |
|---|---|---|---|---|---|---|---|---|
| hpwl_computer_v2 | 31 K | 29 K | 24 | 76 | 128 | 2.695 | **3.321** | met |
| hpwl_computer_v3 | 43 K | 47 K | 24 | 109 | 128 | 2.672 | **3.103** | met |
| hpwl_gradient_computer | 110 K | 125 K | 596 | 220 | 256 | 3.426 | **4.335** | **not met** (about 231 MHz) |
| hpwl_gradient_computer_v2 | – | – | – | – | – | 3.809 | running | – |

- **Every loop is II=1 under HLS 2024.2 as well.** Beat-loop depths are 24 for v2 / v3 and
  72 / 70 for the gradient modules.
- **URAM is real and efficient:** 128 per 1 M-slot array (about 4.6 MB physical for 4 MB of data),
  so Vivado packs it and no manual 2-floats-per-word work is needed. Two arrays take 256 of 463.
- **The HLS estimates were well off:** v2 LUT 64 K estimated vs 31 K routed.

## Gradient timing: what's known
- **Numbers:** worst negative slack −1.005 ns, total −7,109 ns, **20,309 failing endpoints**.
- **The top 10 paths all start at one shared float adder** `grp_fu_1939` (function level, outside
  the beat loop). Its output fans out as write data into all 64 URAM banks (`pos_URAM_*`,
  `grad_URAM_*` DIN_B). That is the adder HLS shared between `refresh_macro_pins`
  (`pos[pin] = pos[macro] + offset`) and `fold_macro_pins` (acc and grad writes). Both write to a
  runtime-selected bank, so the data broadcasts to every bank. 64% of the delay is routing.
- **Unknown:** the breakdown of the other ~20 K endpoints (beat loop or not). I was about to run
  `path_groups.tcl`, which opens the routed checkpoint
  `…/impl/verilog/project.runs/impl_1/bd_0_wrapper_routed.dcp` and groups the 2,000 worst
  endpoints by module. The script text is in this session's transcript; rewrite it if needed.

## Next steps
1. **Diagnose:** group the failing endpoints (above). Also read gradient v2's result.
2. **Candidate fixes**, cheapest first:
   - Stop the adder sharing and register the adder output before the bank broadcast in
     refresh / fold (`BIND_OP ... latency`, or an explicit register stage). These loops are
     short, so extra depth is free.
   - Synthesize with more margin (a larger `set_clock_uncertainty`) so HLS pipelines deeper
     everywhere.
   - ⚠️ Deeper pipelines lengthen the read-add-write. If HLS can no longer meet the declared
     distance 4 it raises II. Then raise `HAZARD_DISTANCE` (to 8 costs <0.5% bubbles) and
     **re-run the co-simulation spacing sweep**: co-simulation, not tier 1, is the gate for that
     contract.
3. **Build-server etiquette** (rules.md): the tunnel dies unattended, and only Mark reopens it.
   Launch long jobs detached and keep an `ssh` stream open while waiting.
