# milestones.md — the landmarks

One line per milestone, most recent first. A milestone is a capability we did not have before, verified
against a golden, not a task closing. Each entry gives the date, what became true, the evidence
(commit + report), and the bar it cleared. The detail lives in the report. This file is the list
you show someone. Add an entry the day it lands. Never rewrite one; annotate it.

| date | milestone | verified by | evidence |
|---|---|---|---|
| 2026-10-29 | **The 64-bank, 8-producer density deposit is bit-exact in RTL** (co-sim, grid 64 and 60): FIFO interleaving across producers, termination and the forwarding hold; the read→write distance is 4 (depths 1–3 fail, 4–6 pass; code keeps 6) | RTL co-sim vs a plain uint64 deposit, every word | [[REPORT_48_banked_cosim_20261029.md]] |
| 2026-10-28 | **Macro-pin refresh and fold at 16 pins per cycle, on the VCK5000 at 230 MHz**, still **bit for bit** on synthetic, mgc_fft_a, mms/adaptec1 and bigblue4. Macro-pin cycles ~11–14× fewer on MMS (adaptec1 16,265 → 1,203 per pass; kernel 0.8 → 0.7 ms per axis); routed WNS 0.000 ns | device grad image + int64 HPWL vs the g++ CU; owned slots vs sw_flat | `6d3961c` (build G), [[REPORT_45_macro_beats_20261027.md]] |
| 2026-10-27 | **`hpwl_gradient_CU` on the VCK5000 at 230 MHz** (from 200), still **bit for bit** on synthetic, mgc_fft_a, mms/adaptec1 and bigblue4 (~7.9 ms wall-clock per axis, from 8.4). Routed WNS +0.001 ns; HLS scheduled at 400 MHz + unshared fold/merge adders | device grad image + int64 HPWL vs the g++ CU; owned slots vs sw_flat | `b0e2290`, [[REPORT_45_hpwl_gradient_timing_20261027.md]] |
| 2026-10-20 | **First run on the real VCK5000.** `hpwl_gradient_CU` at 200 MHz, driven through XRT, matches the software golden **bit for bit** on synthetic (16 chunks), mgc_fft_a, mms/adaptec1 and bigblue4 (3 chunks, 2.17 M slots, ~8.4 ms wall-clock per axis) | device grad image + int64 HPWL vs the g++ CU; owned slots vs sw_flat | `4972a61`, [[REPORT_45_hpwl_gradient_hw_flow_20261019.md]] |
| 2026-10-19 | `hpwl_gradient_CU` passes RTL hardware emulation bit-exact (synthetic 16 chunks, pci_bridge32_b, fft_a); hw build closes timing at 200 MHz (WNS +0.031 ns) | hw_emu vs the g++ CU | `d2608bb`, `8c0405c`, same report |
| 2026-10-16 | The CPU placer `sw_flat` computes the PL's gradient arithmetic **bit-exact to `hpwl_gradient_CU`**, so the hardware has a software golden that matches it exactly, end to end | `make -C host/sw_flat check-record-stream`, 6 designs | `8323939`, [[REPORT_45_pl_arithmetic_20261016.md]] |
| 2026-10-09 | `sw_flat`, the new slot-order CPU placer, is **bit-identical to sw_only** on phase-1 GP | `make -C host/sw_flat check-placer` | `ec4d553`, [[REPORT_45_sw_flat_placer_20261009.md]] |
| 2026-10-06 | Native LEF/DEF/Bookshelf parser; Limbo out of the host; all 44 designs parse **5.3× faster**, bit-identical | `make test-parser` | `dbf1390` (#43, history.md) |
| 2026-08-25 | **sw_only at parity with XPlace**, legal-vs-legal over all 28 ISPD designs: median 1.0096, mean 1.0115 (MMS mean 1.0110). Frozen as the golden | dse.py suite vs XPlace references | `271d024` (#35), `GOLDEN_sw_only_frozen_20260825/` (superseded by `_20261019`, journal.md) |

Meow.
