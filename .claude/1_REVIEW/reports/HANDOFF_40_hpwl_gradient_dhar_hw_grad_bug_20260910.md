# HANDOFF — hpwl_gradient_dhar: real-hardware failure is a TIMING CLOSURE bug

## Status (2026-09-10, root-caused): functionality is correct; the routed design misses timing

**The bug is not in the kernel math, the DDR readback, or the host/device transfer path.** The `hw`
build's own routed `v++` timing report (`_x/reports/link/imp/impl_1_..._timing_summary_routed.rpt`,
already on disk from the build that produced the failing xclbin — no rebuild needed to find this) shows:

```
WNS = -0.710 ns   TNS = -11,741.808 ns
43,859 / 196,028 endpoints (22%) FAIL setup on clkwiz_aclk_kernel_00_clk_out1 (the 300 MHz kernel clock)
```

`v++` shipped the xclbin anyway (the default flow warns on unmet timing, it doesn't block bitstream
generation), so the real-hardware run was always going to fail regardless of anything in the
host<->PL transfer path. All 10 of the worst violated paths in the report land inside
`grp_hpwl_gradient_dhar_Pipeline_net_loop_fu_336` — the `net_loop:` pipeline region in
`hpwl_gradient_dhar.hpp` (now `vck5000/bring_up/hpwl_gradient_dhar/src/modules/`), the fully-unrolled
16-lane term-gen/adder-tree/combiner block. The worst path is **85% route delay, 15% logic** (only 5
logic levels) — a congestion signature, not a too-deep datapath: `term_gen`'s 16-way `UNROLL` calls
`hpwl_lut_exp` 4×/lane = 64 concurrent on-chip LUT reads per cycle, and a BRAM/LUTRAM only has 1-2
read ports, so HLS replicated the `lut_BRAM` cache (and its control fan-out) many times to feed all
64 readers at once — the failing destination registers are literally named `lut_BRAM_load_127_reg`,
`ce_reg_replica_15`, etc. Kernel resource utilization is only 6.36% LUT / 3.99% REG of the
reconfigurable region, so it's *local* fan-out congestion, not a capacity problem.

⚠️ **hw_emu would not have caught this** — it's cycle-accurate to the HLS-*scheduled* RTL, not to
post-place-and-route timing, so it would almost certainly pass. This is a classic FPGA
physical-design problem (too much parallel logic in one pipeline stage), not a functional bug.

## Update 2026-09-11: the LUT-specific fix regressed P&R; the real problem is net_loop's density

Three pragma-level experiments, each verified via a real `v++ -c` C-synthesis re-run (not guessed)
and a fourth via a full P&R rebuild — full detail and evidence in tasks.md `#40`:

- **`lut_BRAM` cyclic-partitioned (factor=16)** — worked exactly as designed at the C-synthesis
  level (`BRAM_18K` 14→0). **A full P&R rebuild then FAILED to route at all** (1126 unrouted
  signals, 771 illegal node overlaps) — worse than the original "routes but misses timing" state.
  The +42% LUT cost landed as LUTRAM in the same fabric the surrounding floating-point logic needed,
  worsening local congestion. **Reverted.**
- **`load_net` UNROLL→PIPELINE** — zero effect (Vitis HLS auto-flattens loops nested inside an
  already-`PIPELINE`d outer loop, overriding the inner pragma). Left in place, harmless.
- **`hpwl_total` → 8 named lane accumulators + `switch(n&7)`** — didn't eliminate the underlying
  carried-dependence warning, just moved it to `hpwl_part6` (HLS can't prove a runtime switch never
  revisits a lane on consecutive iterations; the sibling module's actual trick needs a *statically*
  unrolled outer-stride loop, not named variables). Kept, not validated.

**Conclusion: relocating where `net_loop` gets its resources doesn't fix this — the fully-unrolled
16-lane term-gen/adder-tree/combine block is just too dense (dozens of parallel float multiply/FMA/
add DSP units) for the local fabric to legally route, and every fix so far has moved the
congestion, not reduced it.** The P&R log's own top-10 congested-node list confirms this: dominated
by `fmul`/`fmadd`/`faddfsub` logic, not `lut_BRAM`. Next real lever is reducing `net_loop`'s
parallelism *width* (partial unroll, e.g. 4-8 lanes per wave instead of 16) — not yet attempted.
Validate through tier-1 → C-synthesis (cheap) before spending another ~4hr P&R run.

## Original investigation (2026-09-10, before the timing report was read)

Kept verbatim below — it correctly ruled out the kernel math and correctly noted the failure
pattern, but the "DDR-bank/host readback" hypothesis it converged on was wrong. Worth keeping for
the reasoning trail (in particular, #2's "HPWL being right means the segmented-reduction passes are
executing" is still true — it's *this specific 22%-failing region* that HPWL's simpler accumulation
path happened not to route through).

## Status: kernel logic verified correct offline; real-hardware gradient readback is not

**Tier-1 offline test PASSES** (`vck5000/test/hpwl_dhar_test.cpp`, pure g++, no Vitis,
`cd vck5000 && make test`):

```
[1] structure  rel_rms=1.481e-06  max_rel=1.730e-05   (tol 1e-05 / 1e-04)
[2] lut budget rel_rms=1.447e-05  max_rel=9.687e-05   (tol 1e-04 / 1e-03)
[6] hpwl       emitted=8.82178960e+07  golden=8.82178978e+07  rel=2.096e-08 (tol 1e-06)
PASS
```

**Real hardware run FAILS** (`hacc-gpu-u55c-01`, `./run_hw.sh`, 500 movable / 600 nodes / 600
nets synthetic design — same `golden()` in `src/host.cpp` as the tier-1 test, so apples-to-apples):

```
gradient  rel_rms=1.824e+01  max_rel=2.960e+02   (tol 1e-05 / 1e-04)
hpwl      emitted=8.18303650e+06  golden=8.18303633e+06  rel=2.068e-08 (tol 1e-06)
TEST FAILED
```

## The localization, and why it points at the grad readback path specifically

Three facts pin this down more than it might look at first:

1. **The tier-1 test exercises the identical kernel and golden** (`src/host.cpp`'s header
   comment: "Same golden as test/hpwl_dhar_test.cpp -- this is that test carried across the
   real host<->PL path") and passes at ~1e-6, well inside tolerance. So the HLS algorithm
   itself — the fast-adder-tree gradient math in
   `vck5000/pl/src/pl_algo/src/modules/hpwl_gradient_dhar.hpp` — is not in question.
2. **HPWL matches almost exactly on real hardware too** (rel=2.068e-08, same order as the
   tier-1 run's 2.096e-08). HPWL is `bo_hpwl`, a single `float` scalar, `krnl.group_id(7)`
   (`src/host.cpp:233`, `:256`). Getting this right on real silicon means the net/pin data is
   loaded correctly, the LUT is loaded correctly, and the segmented-reduction passes that feed
   both HPWL and the gradient are executing.
3. **Only the per-node gradient array is wrong, and it's wrong by orders of magnitude**
   (rel_rms ~18, max_rel ~296 — not a few-percent drift). That is `bo_grad`,
   `krnl.group_id(6)` (`src/host.cpp:232`, read back at `:253,255`), sized `d.M *
   sizeof(coord_t)`. A magnitude this large does not look like a timing/precision issue; it
   looks like the array being read from the wrong place, the wrong size, or stale/uninitialized
   DDR content.

**Working hypothesis**: something specific to how `bo_pin_grad` (group_id(5), scratch,
`num_node_pins`-sized) and/or `bo_grad` (group_id(6), the actual output, `d.M`-sized) get
mapped to DDR banks or synchronized on *real hardware* differs from both the untimed tier-1
C-simulation and (presumably) `sw_emu`. Not yet confirmed — see next steps.

## Why this is plausible and not (yet) ruled in

- `bo_hpwl` at group_id(7) is the *last* kernel argument and a single scalar — cheapest thing
  to get right, and it works. `bo_pin_grad`/`bo_grad` at group_id(5)/(6) are the two array
  writes in the middle of the argument list — more surface area for a bank/connectivity
  mismatch (e.g. if the kernel's `m_axi` bundle assignment for these two ports doesn't match
  what the host assumes, or if `d.M`/`num_node_pins` disagree between host and device build).
- The device-loading path itself is now known-fragile on this multi-card node — the
  `xrt::device(0)` bug fixed in this same commit picked the wrong *card*. It's worth directly
  ruling out a related but different bug: wrong *buffer bank* on the right card, which
  `xrt::device` selection does nothing to prevent.

## Next steps, cheapest first — SUPERSEDED, see tasks.md `#40` for the current plan

All four items below were written *before* the routed timing report was read. None of them are
wrong to eventually check, but none is the next action anymore — the failure is already localized
to `net_loop`'s timing, not to any of these. Kept for the record.

1. ~~hw_emu, not yet tried.~~ Skip — see the root-cause section above for why it wouldn't have
   caught this.
2. ~~Dump raw `bo_grad` bytes on a tiny design.~~ Would still show garbage, for a now-understood
   reason (timing-corrupted registers in `net_loop`), not an offset/stride bug — not informative.
3. ~~Check `bo_pin_grad`/`bo_grad` bank assignment.~~ Moot — `bo_hpwl` reads correctly through the
   same host/device transfer mechanism, and the routed report shows no host-side involvement in the
   43,859 failing paths (all internal to the kernel's `net_loop` pipeline).
4. ~~Confirm `num_node_pins`/`d.M` size agreement.~~ Same — a size mismatch wouldn't produce a
   uniform ~-0.71ns setup violation concentrated in one pipeline region.

## Context

- Verified 2026-09-10 on `hacc-gpu-u55c-01` (4-card node: 2x U55C, 2x VCK5000; this design
  now probes all enumerated devices and picks the first that accepts the xclbin, see the
  commit that added this file).
- xclbin built on `hacc-build-01` with Vitis 2022.2 targeting
  `xilinx_vck5000_gen4x8_qdma_2_202220_1`; run with Vitis/XRT 2024.2 on the hw-run node
  (`VITIS_SETTINGS=/tools/Xilinx/Vitis/2024.2/settings64.sh ./run_hw.sh` — the script's
  default points at 2022.2, which isn't installed on the hw-run node).
- Design moved here (`vck5000/bring_up/hpwl_gradient_dhar/`) from a former top-level
  `vck5000/hpwl_gradient_dhar/` in the same commit, to sit alongside the other single-kernel
  hardware harnesses (`hpwl_pl`, `fft_pl`, `field_solve_pl`, `add1_pl`, `iteration_pl`,
  `dct_fft_aie`).
