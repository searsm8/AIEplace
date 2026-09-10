# hpwl_pl — PL-only hardware harness for the pl_algo HPWL gradient compute unit

One PL kernel, eight DDR ports: runs `plalgo::hpwl_CU` from
`vck5000/pl/src/pl_algo/src/modules/hpwl_gradient.hpp` on real VCK5000 hardware — the
weighted-average HPWL wirelength gradient (dW/dx, dW/dy) for every movable node, computed via
three segmented reductions (CSR/SpMV pattern; see the module's header comment). **No AIE
anywhere** — no graph, no AXIS stream ports, no `link.cfg` connectivity, same framework as
`../fft_pl`, `../field_solve_pl` and `../add1_pl`.

## Why this exists

The bring-up ladder so far:
1. `../add1_pl` — proved the PL load/run path on the hw node's VCK5000 (`out = in + 1`).
2. `../fft_pl` — the 1D DCT/IDCT/IDXST transforms.
3. `../field_solve_pl` — the full 2D electrostatic field solve built on those transforms.
4. **this** — the *other* half of the per-iteration datapath: the HPWL wirelength gradient,
   `hpwl_CU` (already wired into `pl_algo/top.cpp` as `MODE_HPWL_GRAD`, and already
   sw_emu-verified there via `HpwlGradVerify.cpp`). Here it gets its own dedicated kernel with
   only the HPWL buffers as args, on real silicon, standalone.

Unlike `field_solve_pl`'s fixed-size on-chip grid, `hpwl_CU`'s whole design point is
**arbitrary size, no on-chip per-key accumulator** — every array (`node_pos`, `pins`, `npins`,
`bb`, `sums`, `node_grad`) is DDR-resident and streamed. This harness's synthetic netlist
(built in `host.cpp`, no parser) is sized to exercise that: 800 movable + 200 fixed nodes, 550
nets of random degree 2–8, with roughly 1/11 forced to degree-1 (`net = -1`) to exercise the
kernel's no-gradient-net skip path, and ~15% of pins carrying a nonzero macro-pin offset.

`src/pl/top.cpp` includes `hpwl_gradient.hpp` directly (`-I` in the Makefile) — not a copy —
so this harness always tests the actual module.

## Layout

| path | what |
|------|------|
| `src/pl/top.cpp` | kernel wrapper: 8 DDR ports (`node_pos`, `net_ptr`, `pins`, `npins`, `exp_lut`, `bb`, `sums` scratch, `node_grad` out) straight into `hpwl_CU` |
| `src/host/host.cpp` | XRT driver: synthetic random netlist, run, verify `node_grad` vs. the exact-exp full-WA golden |
| `run_hw.sh` | hw run wrapper (mirrors `../field_solve_pl/run_hw.sh`) |
| `Makefile` | 3-step Versal build (no AIE step) |

There is no `vck5000/test/*hpwl*` tier-1 g++ harness yet (unlike `fft_pl_test.cpp` /
`field_solve_test.cpp`), so this design's golden is written into `host.cpp` directly: the same
exact-exp full-WA gradient math as
`vck5000/host/src/pl_algo/src/HpwlGradVerify.cpp`'s `gradientGolden()`, the check that already
verifies `MODE_HPWL_GRAD` in sw_emu. `hpwl_CU` itself approximates `exp()` with a
host-supplied LUT, so the golden's `exp()` and the kernel's LUT necessarily disagree a little
— the tolerance (`rel_rms < 2e-2`) isolates that LUT-vs-exp error, same as `HpwlGradVerify`
uses on real designs. If `hpwl_gradient.hpp` changes, there's no offline tier-1 check to run
first (yet); sanity-check the module's math by eye or add one under `vck5000/test/` before
trusting a change here.

## Build & run

Source the tools first:
```bash
source /tools/Xilinx/Vitis/2022.2/settings64.sh
```

Then, cheapest-first:

```bash
make csynth                # 1. v++ -c of the PL kernel — confirms it synthesizes (fast)
make TARGET=sw_emu run      # 2. full build + sw_emu functional check
make TARGET=hw  all         # 3. build the real-silicon xclbin  →  hand to the hw node
./run_hw.sh                 #    run on the real VCK5000
```

On this build node, `PLATFORM_REPO`'s default (`$HOME/xilinx_local/opt/xilinx/platforms`) and
`XILINX_XRT`'s (`/opt/xilinx/xrt`) don't exist — override both, same as `../field_solve_pl`:
```bash
make TARGET=hw all PLATFORM_REPO=/opt/xilinx/platforms XILINX_XRT=/opt/xilinx/xrt_2024.2
```

Expected on success:
```
[hpwl_pl] M=800 N=1000 nets=550 pins=... npins=...  span=... gamma=... lut=...
max_abs=...e-0x  max_rel=...e-0x (800 nodes)  rel_rms=...e-0x
TEST PASSED  M=800  (rel_rms=...e-0x, tol 2e-02)  hpwl_pl on VCK5000
```

On the hw node you'll also see one or more `[XRT] ERROR: … err = -22` lines *before* the PASS —
that is the host's device-probe loop rejecting the non-VCK5000 cards (U55C) on the way to the
VCK5000. **Benign** when followed by a PASS; it is not the `dct_fft_aie` AIE-CDO load failure.

Override `XILINX_VITIS` for the runtime `LD_LIBRARY_PATH` if `/tools/Xilinx/Vitis/2022.2` isn't
present on the hw node:
```bash
XILINX_VITIS=/tools/Xilinx/Vitis/2024.2 ./run_hw.sh
```

Note: no AIE graph, so `hw_emu` should also work — `make TARGET=hw_emu run` for a second
emulation cross-check if ever useful.

## Profiling (per-port throughput)

`build/hw`'s bitstream has no AXI Performance Monitors, so `device_trace_*.csv` /
`summary.csv` never populate (see `build/hw/HANDOFF.md`). `make hw_profile` builds a second,
separately-instrumented bitstream in its own `build/hw_profile/` tree — it never touches
`build/hw`, so it's safe to run even while another build is live there:

```bash
make hw_profile PLATFORM_REPO=/opt/xilinx/platforms XILINX_XRT=/opt/xilinx/xrt_2024.2
```

Copy **both** `build/hw_profile/hpwl_pl.hw.xclbin` and `build/hw_profile/hpwl_pl.hw.ltx` to
the hw node (the `.ltx` carries the new monitor addressing; the xclbin alone can't use it),
then:

```bash
./run_hw_profile.sh
```

`build/hw_profile/xrt.ini` (`device_trace=fine`, written by the `hw_profile` target) is
already in place, so `device_trace_*.csv` and `summary.csv`'s Kernel Execution / Data
Transfer tables should populate with real per-port beats and MB/s this time.
