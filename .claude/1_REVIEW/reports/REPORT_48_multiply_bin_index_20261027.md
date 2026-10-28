# REPORT #48: bin index by multiply, not divide (golden first, then the device)

*2026-10-27. Branch `pl_algo`. Mark's decision (2026-10-27): do it. Started as the handoff of the same name
(cb6a58e); this is that file, finished. Evidence for the decision:
[[PLAN_48_bin_density_lanes_20261020.md]] ("What one producer costs"), [[EXPLAINER_48_bin_density_CU_overview_20261022.md]].*

## Result

- The golden's bin index is `(int)(edge * inv_bin_w)`, `inv_bin_w = 1.0f / bin_w` computed once on the
  host, behind `[sw_flat] bin_index = "divide" | "multiply"` (default divide: check-placer still
  bit-identical to sw_only). **Step 6's golden is now `pl+Bprime+fixed+mul`.**
- **The multiply adds no drift on 4/5 designs and noise-sized drift on bigblue4.** On 4 designs (grid <= 1024) no index differs (0 of 4.4 G evaluations) and
  the schedule trace is byte-identical to `pl+Bprime+fixed`. On bigblue4 (grid 2048) 1.6e-5 of indices
  differ (multiply one bin higher); the run parts from `pl+Bprime+fixed` at iteration 82 and ends at
  HPWL -0.002% vs base (fixed: -0.013%), inside its +/-0.01% noise band.
- `bin_density_CU` and the producer match it bit for bit: `check-bin-density` **44/44**, producer_check and
  banked_check 0 bins differ, `make -C pl test` PASS.
- RTL co-sim: the deposit pipeline is 13 stages shorter (117 → 104) but the URAM read→write distance is
  **still 5** (depth 0 and 4 FAIL, 5 and 6 PASS), so `BIN_FORWARD_DEPTH = 6` stands.
- Registered in `CLAUDE.md` → "Deliberate divergences from XPlace". XPlace divides
  (`~/phd/Xplace/cpp_to_py/density_map_cuda/density_map_cuda_kernel.cu:27-30`); sw_only is frozen and divides.

## Why

Every footprint needs 4 bin indices. On the device that was 4 float dividers per producer. **Vivado
synthesis** (build server, 2022.2, one producer, 3.33 ns met):

| bin index | LUT | FF | DSP |
|---|---|---|---|
| divide (old golden) | 21,682 | 17,231 | 90 |
| multiply by `1/bin` | 13,840 | 14,525 | 94 |

The dividers cost +7.8 K LUT per producer, ~63 K LUT over the planned 8 producers (7% of the chip).
(HLS's C-synthesis estimate reports the divider cores as 0 LUT/FF/DSP: it cannot be used for this.)
A multiply rounds differently from a divide, so the device can only be bit-exact if the golden changes first.

## What changed

| where | change |
|---|---|
| `host/sw_flat/placer/{placer.hpp,setup.cpp,density.cpp}` | `bin_index_multiply` switch, `inv_bin_w/h` in setup, one `bin_index` helper at every site: `for_each_overlap` (deposit, fixed base, force gather) and `compute_overflow` |
| `host/sw_flat/test/sum_order_drift.py` | variant `pl+Bprime+fixed+mul` |
| `host/sw_flat/density_inputs.hpp`, `test/bin_density_check.cpp` | `inv_bin_w/h` handed to the CU; the check forces `bin_index = "multiply"` like `deposit = "fixed"` |
| `pl/src/modules/bin_density_CU.hpp` | new arguments `inv_bin_w`, `inv_bin_h`; `footprint_window` multiplies; `BIN_FORWARD_DEPTH` comment re-proved |
| `pl/synth/bin_density_producer/` | `PRODUCER_DIVIDE` default 0; producer_check now fails if the multiply differs |
| `pl/synth/bin_density_CU/` (top, cosim top and tb), `pl/synth/bin_density_banked/banked_check.cpp` | the new arguments |
| `pl/test/bin_density_CU_test.cpp` | passes `1/bin`; its double golden **stays a divide**: it is the math, independent of the device, and a bin-edge index difference would be a sliver far below its bounds |

## Drift (step 2)

`sum_order_drift.py ... --variants pl+Bprime+fixed pl+Bprime+fixed+mul`, D6 configs, rows vs `base` (the
sw_only-identical default), as the 10-19/10-20 tables. Full output:
`.claude/2_ARTIFACTS/48_mul/drift_20261027.md`.

| design | variant | first differing row | iterations | stop | HPWL | in noise | HPWL exact | overflow exact | run s |
|---|---|---|---|---|---|---|---|---|---|
| ispd2015/mgc_fft_a | base | identical | 628 | converged | 5.8622e+08 | noise -0.451..+0.038% | 5.8781e+08 | 0.1836 | 6.178 |
| ispd2015/mgc_fft_a | pl+Bprime+fixed | 1 | 628 | converged | +0.011% | yes | +0.011% | -0.95% | 5.896 |
| ispd2015/mgc_fft_a | pl+Bprime+fixed+mul | 1 | 628 | converged | +0.011% | yes | +0.011% | -0.95% | 5.754 |
| ispd2015/mgc_pci_bridge32_b | base | identical | 715 | converged | 6.6134e+08 | noise -0.312..+1.760% | 6.6292e+08 | 0.2428 | 3.164 |
| ispd2015/mgc_pci_bridge32_b | pl+Bprime+fixed | 1 | 713 | converged | -0.017% | yes | -0.017% | -1.97% | 2.906 |
| ispd2015/mgc_pci_bridge32_b | pl+Bprime+fixed+mul | 1 | 713 | converged | -0.017% | yes | -0.017% | -1.97% | 2.853 |
| ispd2005/adaptec1 | base | identical | 655 | converged | 7.0668e+07 | noise -0.026..+0.023% | 7.0710e+07 | 0.1158 | 40.453 |
| ispd2005/adaptec1 | pl+Bprime+fixed | 1 | 654 | converged | +0.004% | yes | +0.004% | +0.08% | 43.773 |
| ispd2005/adaptec1 | pl+Bprime+fixed+mul | 1 | 654 | converged | +0.004% | yes | +0.004% | +0.08% | 43.679 |
| mms/adaptec1 | base | identical | 658 | converged | 6.2500e+07 | noise -0.024..+0.011% | 6.2544e+07 | 0.0757 | 41.530 |
| mms/adaptec1 | pl+Bprime+fixed | 1 | 657 | converged | +0.052% | **no** | +0.052% | +0.62% | 168.390 |
| mms/adaptec1 | pl+Bprime+fixed+mul | 1 | 657 | converged | +0.052% | **no** | +0.052% | +0.62% | 167.744 |
| ispd2005/bigblue4 | base | identical | 758 | converged | 7.3886e+08 | noise -0.010..+0.007% | 7.4162e+08 | 0.1061 | 1332.124 |
| ispd2005/bigblue4 | pl+Bprime+fixed | 1 | 759 | converged | -0.013% | **no** | -0.014% | -0.78% | 997.724 |
| ispd2005/bigblue4 | pl+Bprime+fixed+mul | 1 | 758 | converged | -0.002% | yes | -0.002% | -0.13% | 662.234 |

**mul vs fixed directly** (the comparison that matters): the `pl+Bprime+fixed+mul` schedule trace is
byte-identical to `pl+Bprime+fixed`'s on fft_a, pci_bridge32_b, adaptec1 and mms/adaptec1, so those rows
are the same run (mms/adaptec1's **no** is `pl+Bprime+fixed`'s known two-outcome flip, not this change).
On **bigblue4** the two part at iteration 82 (an ulp in `density_force_fraction`) and end 1 iteration and
0.011% HPWL apart, about the width of its noise band; mul lands inside the band, fixed just outside it.

**How many indices differ.** A throwaway build (not committed; `/tmp/cnt48`) computed both indices at every
`bin_index` call of a `pl+Bprime+fixed+mul` run and counted disagreements:

| design | index evaluations | differ |
|---|---|---|
| mgc_fft_a | 310,794,064 | 0 |
| mgc_pci_bridge32_b | 179,861,952 | 0 |
| adaptec1 | 1,950,033,440 | 0 |
| mms/adaptec1 | 1,959,263,604 | 0 |
| bigblue4 (grid 2048) | 30,770,623,512 | 497,831 (1.6e-5), all with the multiply one bin higher |

Each instrumented trace was byte-identical to the uninstrumented one. `edge * (1/b)` and `edge / b` can
differ by one ulp; it matters only when an edge is within an ulp of a bin boundary, which happened on none of
the four smaller designs and on 1.6e-5 of bigblue4's edges at 2048. There the multiply rounds up to the
boundary, so the window starts or ends one bin later and the sliver of overlap below the boundary (at most
an ulp wide) is dropped or the extra bin's overlap is <= 0 and skipped. The device copies the multiply, so it
stays bit-exact either way; only XPlace-faithfulness moves, by those slivers. Not measured: bigblue4 at
1024, the device's grid (the 10-20 drift tables ran it at 2048 too).

## Device (step 3)

- `make -C host/sw_flat check-bin-density`: **44 of 44 designs PASS** (rho, excess, overflow bit-exact,
  shuffled order too). Log: `.claude/2_ARTIFACTS/48_mul/check_bin_density_20261027.log`.
- `producer_check` (PRODUCER_DIVIDE=0): 0 of 262,144 bins differ; at PRODUCER_DIVIDE=1 also 0 (printed
  only). `banked_check`: 0 of 1,048,576 bins differ.
- `make -C pl test`: all 4 tier-1 harnesses PASS.

## Forwarding (step 4)

`pl/synth/bin_density_CU/cosim/run_depths.sh 0 4 5 6` (grid 64, stream re-hitting bins at distances 1..5):

| depth | C sim | RTL |
|---|---|---|
| 0 | PASS | **FAIL** (79 bins, excess 2.95e9 vs 7.82e9) |
| 4 | PASS | **FAIL** (32 bins) |
| 5 | PASS | PASS |
| 6 (default) | PASS | PASS |

Schedule (`deposit_fast_Pipeline_deposit_fast_loop.verbose.sched.rpt`): iteration latency 104 (was 117), no
`fdiv` left; URAM load issued at state 99, store at 104. The dividers sat before the read, so removing them
shortened the front of the pipeline and left the read→write distance at 5. The handoff expected the distance
to change; it did not, and the RTL run is what shows it.

## Not done / follow-ups

- Real LUT/FF of the full CU with the multiply needs Vivado on the build server (this box has csynth only);
  the per-producer numbers above are the measured basis.
- Next in #48: the producers' 64 bank-address multiplies, the drain's timing, macro tiles
  ([[HANDOFF_48_macro_tiles_20261022.md]]).
