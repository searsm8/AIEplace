# REPORT #45 — `hpwl_gradient_CU` routed timing above 200 MHz (2026-10-22 .. 10-27)

*Context: [[REPORT_45_hpwl_gradient_hw_flow_20261019.md]].*

## Where it stands
- **230 MHz meets timing: routed WNS +0.001 ns, WHS +0.010 ns, 0 failing endpoints** (build F, default strategy).
  220 MHz also meets it (+0.005 ns). Both xclbins are packaged on `build`:
  `~/AIEplace_timing/expF/pl/build/run230/hpwl_gradient_CU_top.xclbin` (md5 `f725da2a85f501419e3bb62e99444caa`)
  and `run220/` (md5 `9ca22ba401b9cedcf43d846a959954f8`). Built from server `b3d0e87` + F's hunks, before the
  #47 rename (`bfe68c7`); HEAD differs from that source only in identifier names.
- **Card run (2026-10-27, `vck`, card `0000:c1:00.1`): PASS bit-exact at 230 MHz** on all four designs.
  `KERNEL_FREQ_HZ` is now 230 MHz in `pl/Makefile`. The +0.001 ns margin is thin but signed off.

  | design | [1] image vs g++ CU | [3] vs sw_flat | kernel run per axis, 230 MHz | at 200 MHz (10-20) |
  |---|---|---|---|---|
  | synthetic (16 chunks) | 0 / 17,344 | n/a | 9.5 / 0.6 ms | 9.3 / 0.8 ms |
  | mgc_fft_a | 0 / 34,592 | 0 / 30,625 | 0.2 / 0.1 ms | 0.2 / 0.1 ms |
  | mms/adaptec1 | 0 / 227,712 | 0 / 210,967 | 0.8 / 0.7 ms | 0.9 / 0.8 ms |
  | bigblue4 (3 chunks) | 0 / 2,616,832 | 0 / 2,169,183 | **7.9 / 7.8 ms** | 8.4 / 8.3 ms |

  Wall-clock from `kernel(...)` to `run.wait()`, launch overhead included, so bigblue4 gains 6% for a 15%
  clock (axis 0 of synthetic is the first launch's warm-up). The host on `vck` is the `b3d0e87` tree, the
  xclbin's own base, plus one change:
- **`vck` now has two U55Cs that enumerate before the VCK5000s,** so `xrt::device(0)` loaded the xclbin onto
  a U55C (`failed to load xclbin: Invalid argument`, err −22). `hpwl_gradient_xrt` takes `XRT_DEVICE_BDF`
  (e.g. `0000:c1:00.1`); run as `XRT_DEVICE_BDF=0000:c1:00.1 TARGET=hw bash test/hpwl_gradient_xrt.sh`.
- **250 MHz does not close. The best routed WNS is −0.178 ns (build F); the baseline was −0.378 ns.**
- The 250 MHz strategy sweep (`runS`, 4 Vivado strategies) died with the first `run230` attempt: the server home
  hit its disk quota while implementation wrote checkpoints. It was not rerun; the 230/220 builds came first.
- **F is verified short of the card** (bit-exact in each check below):
  - tier 1 4/4;
  - C-synth 24/24 loops II=1 at a 2.5 ns target;
  - sw_emu 4/4 (synthetic, fft_a, mms/adaptec1, bigblue4);
  - hw_emu 2/2 (synthetic 16 chunks, fft_a);
  - RTL co-sim PASS (capacity 1536, hazard 4).

## Routed timing, kernel clock `clkout1_primitive`
| build | source | HLS clock | link clock | WNS (ns) | TNS (ns) | failing endpoints |
|---|---|---|---|---|---|---|
| 10-19 (report) | `3b8a1b9` | 250 | 250 | −0.729 | −5,902 | 22,902 |
| base | unchanged (`b3d0e87` hpwl files) | 250 | 250 | −0.378 | −1,804 | 11,114 |
| A | URAM `latency=2` (pos, grad) | 250 | 250 | −0.947 | −3,612 | 14,702 |
| B | unchanged | 300 | 250 | −0.303 | −672 | 5,084 |
| C | B + `style=frp` on `hpwl_CU_loop` | 300 | 250 | −0.311 | −1,109 | 9,184 |
| E | unchanged | 400 | 250 | −0.234 | −58 | 826 |
| **F** | **unshared fold/merge adders** | **400** | **250** | **−0.178** | −87 | 1,596 |
| run225 | B's RTL + no-BUFG hook | 300 | 225 | −0.294 | −581 | 3,937 |
| **run230** | **F's RTL + no-BUFG hook** | **400** | **230** | **+0.001** | **0** | **0** |
| run220 | F's RTL + no-BUFG hook | 400 | 220 | +0.005 | 0 | 0 |
| runS | F's RTL, 4 impl strategies | 400 | 250 | *died (disk quota), not rerun* | | |

- **Server disk quota:** a `--save-temps` tree is several GB; four of them filled the home quota and killed two runs mid-checkpoint (the v++ parents hung instead of exiting). Delete `_x` once the reports are archived.

- **Build-to-build noise is large.** The same source at 250 MHz gave −0.729 ns (10-19) and −0.378 ns (10-22). One build per variant ranks only big effects.
- **The first 225 MHz run never placed:** `opt_design` put a 25 K-fanout pipeline enable on a BUFG_FABRIC, and the platform's dynamic-region Pblock has no BUFG range (DRC HDPR-18). [[pl/synth/hpwl_gradient_CU/no_bufg.tcl]] sets `CLOCK_BUFFER_TYPE NONE` on the kernel's nets above 1,000 pins (7,051 nets) and fixes it.

## Failing-path groups (every failing endpoint, by destination and source stage)
Classifier: `classify_paths.tcl` + `group_paths.py` / `coarse_groups.py` (in `.claude/2_ARTIFACTS/scratch_45/timing_250/`; on the server in `~/timing_tools/`).

**Baseline (11,114 endpoints, almost every group at the same ≈ −0.37 ns, 60–95% route delay):** many structures, not one.
| group | endpoints | what |
|---|---|---|
| `wa_gradient` float datapath | ~5,000 | shared FP outputs → next stage, fdiv internals, DSP operand muxes, pipeline-enable fanout |
| fan-out INTO the URAMs | 2,855 | gather row addresses, a shared adder's output → write data of every bank, FSM → enables |
| FSM / AXI control fan-out | ~1,500 | `ap_CS_fsm` → FIFO/BRAM enables, `int_offset_bits` → 16 lanes' decode |
| `wa_sums` | ~1,000 | half of it is the `sum_beats` FIFO's `full_n` stall fanout |
| tail exchange `send_ops` | ~500 | the 10-19 worst path; a minor group |
| URAM read data → gather | ~430 | the only group `latency=2` addressed |

**B (HLS 300):** TNS −63%. What was left: the shared `grp_fu_4675` adder → `pos_URAM` write data (867), and DSP operand muxes in `wa_gradient` (~960) and `wa_sums` (~350).

**F (HLS 400 + unshared adders), 1,596 endpoints:**
| group | endpoints | WNS |
|---|---|---|
| tail exchange `send_ops` (row-buffer address / op decode → 16 output lanes) | ~460 | −0.172 |
| `wa_sums` DSP operand inputs | 448 | −0.062 |
| `fold_macros` / `load_slots` → `pos_URAM` address fan-out | ~280 | −0.176 |
| `grad_URAM` → `fold_macros` reads, merge → `grad_URAM` | ~130 | −0.178 |

## What changed, and why
1. **`pl/Makefile`: `HLS_FREQ_HZ ?= 400000000`, separate from `KERNEL_FREQ_HZ`.** HLS then gives the FP cores and wide muxes more stages: WNS −0.378 → −0.234 ns, TNS −1,804 → −58. Every loop stays II=1, and the beat-FIFO deadlock margin holds (hw_emu and co-sim PASS at the deeper stages).
2. **Fold, merge and refresh each get their own adder** (`slot_arrays.hpp` `acc_plus_pin`: `primitivedsp latency=1`; `borrowed_copies.hpp` `merged`: `primitivedsp latency=3`; refresh unchanged at `latency=2`).
   - HLS had shared one 1-cycle fadd between `fold_macros` and `merge_tail_block`. Its output fed the write data of every bank of both arrays: the largest group left in B.
   - Distinct bindings stop the sharing. Fold's add sits inside the distance-4 read-add-write, so it can only take latency 1; latency 2, or the fabric adder, gives II=2 or 3 at a 400 MHz target.
3. **`no_bufg.tcl` on the `hw` link** (`LINK_OPTS_hw` in `pl/Makefile`), as above.
4. **Default `KERNEL_FREQ_HZ` 250 → 200 → 230 MHz:** 200 while it was the fastest clock verified on the card; 230 after the card run of 10-27.

## Tried and dropped
- **`BIND_STORAGE ... latency=2` on both URAM arrays: worse** (−0.947 ns). It registers the URAM read output, but the failing paths are the INPUT side.
- **`latency=3`**, which adds per-bank address and write-data registers, the input-side fix: `fold_macros` becomes II=2, because `pos_URAM` doubles as fold's accumulator and read+add+write no longer fits `HAZARD_DISTANCE` = 4.
- **`style=frp` (free-running pipeline):**
  - It applies only to `hpwl_CU_loop`; HLS estimates that loop's enable fanout at 59,506.
  - `wa_sums` and `wa_gradient` refuse it: their per-net stream read is predicated on the beat read in the same iteration, which HLS cannot evaluate in stage 1.
  - Routed: −0.311 vs −0.303 ns, TNS worse. Reverted.

## URAM shape (correction)
**All 256 URAMs are `URAM288E5` at 32K × 9.** Each 32K-float bank is 4 URAMs side by side in width (one byte of each float), not 4 or 8 deep, so there is no in-bank read mux. The packer's `URAM_WORDS × FLOATS_PER_WORD` became `URAM_FLOATS = 8192`. Learning: `.trail/learnings/2026-10-26-versal-uram-shapes-32k-x-9-searsm8.md`.

## Resources (routed, kernel only, `kernel_util_routed.rpt`)
| build | REG | LUT | LUT as memory | BRAM | URAM |
|---|---|---|---|---|---|
| 200 MHz on the card (HLS 200) | 158,854 (9.0%) | 122,478 (13.9%) | 11,206 | 228 | 256 (55.3%) |
| B (HLS 300) | 191,121 (10.8%) | 130,438 (14.8%) | 13,408 | 228 | 256 |
| E (HLS 400) | 220,126 (12.4%) | 146,028 (16.6%) | 19,141 | 228 | 256 |
| F (HLS 400, unshared adders) | 220,211 (12.4%) | 145,460 (16.5%) | 19,150 | 228 | 256 |

The deeper pipelines cost +61 K REG (+39%) and +23 K LUT (+19%) against the card build. Memories are unchanged.
Reports are archived on the server in `~/timing_tools/reports/<build>/`, with each build's `.xo`.

## Next for 250 → 300 MHz
- **`send_ops`:** B4 (#47) removes the exchange passes from this CU. Until then, register its row-buffer address and op decode (C-synth II check).
- **URAM input fan-out:** `latency=3` on `pos_URAM` is blocked only by fold. Fold at II=2 costs (macro pins / chunk) cycles, which is small next to the beat loop, but the brief says II=1 everywhere: Mark's call. The alternative is a separate fold hazard distance in the packer (`schedule_macro_pins`).
- **300 MHz** needs all of the above plus `wa_sums`' DSP inputs, at roughly 3.0 ns of routing on a die where the kernel holds 55% of the URAMs.

## Waveform (side deliverable, Mark 10-26)
- **Figures:** [[.claude/2_ARTIFACTS/hpwl_gradient_timing_20261026/page.html]]: a call-level schematic, a beat-pipeline schematic, and two figures rendered from an RTL co-sim VCD (synthetic design at a 400 MHz HLS target).
- **Scripts:** `.claude/2_ARTIFACTS/scratch_45/waveform/` (`cosim_setup.tcl`, `wave_vcd.tcl`, `vcd_phases.py`, `render_*.py`, `build_page.py`).
- **Finding:** on the synthetic design the two exchange passes are 87% of a call (61 K + 62 K of 142 K cycles). That matches the card's 0.8 ms per axis at 200 MHz.
