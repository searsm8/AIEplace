---
title: "hpwl_gradient_CU 250 MHz: DSP operand latency in the beat loop (build H)"
author: Searsm8
date: 2026-10-28
branch: pl_algo
touches:
  - pl/src/modules/hpwl_gradient/wa_gradient.hpp
  - pl/src/modules/hpwl_gradient/wa_sums.hpp
  - pl/synth/bin_density_producer/impl_prj_div0/hls.app
  - pl/synth/bin_density_producer/impl_prj_div0/kernel.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.automg_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.autopilot_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_impl.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_syn.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/__ctype_info__.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/a.g
---

## Decided
- Build H runs wa_sums/wa_gradient float ops at the floating-point IP's max internal latency via bound helpers in wa_sums.hpp: beat_add/beat_sub latency 2, beat_mul latency 3 (G was 1/1/2). Target: G's 250 MHz failures ending at DSP_FPM_PIPEREG (the multiplier's register, so the DSP input register was off).
- wa_gradient's adds use gradient_add (latency 1, bound explicitly). Muls/subs there still get the extra stage.

## Tried and ruled out
- beat_add (latency 2) next to a default grad_URAM += in wa_gradient: scheduled at 2 but RTL built all 113 fadds as the latency-1 module. Binding the += explicitly at latency 1 did not split them.
- grad_URAM += at latency 2: wa_gradient_loop II=2 (does not fit HAZARD_DISTANCE 4).
- Local Vivado OOC synth to read DSPFP32 register bits: no synthesis license on the WSL box (build has one).

## Watch out
- HLS 2022.2 primitivedsp BIND latency vs IP c_latency: fadd/fsub L<=2 -> IP L, L=3 -> IP 2 + fabric din_buf, 4 rejected; fmul L<=3 -> IP L, L=4 -> IP 3 + din_buf.
- HLS builds every fadd in one loop as ONE module: mixed latencies silently collapse to one. Check the RTL module names (fadd_32ns_32ns_32_<N>), not just the bind report.
- An unbound a*b+c fuses into fmadd (fma core, latency 2): binding the mul keeps it separate, at +1 DSP each.
- Untested at route: build H's 250 MHz link was not yet run when this was written.
