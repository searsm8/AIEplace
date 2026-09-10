# fft_pl — PL-only hardware harness for the pl_algo FFT/DCT module

One PL kernel, two DDR ports, a scalar `mode`: runs `plalgo::dct_1d_pl` / `idct_1d_pl` /
`idxst_1d_pl` from `vck5000/pl/src/pl_algo/src/modules/fft_pl.hpp` on real VCK5000 hardware.
**No AIE anywhere** — no graph, no AXIS stream ports, no `link.cfg` connectivity, same shape as
`../add1_pl`.

## Why this exists

`../add1_pl` proved the PL load/run path works on the hw node's VCK5000 (2022.2-build,
2024.2-run, with the device-enumeration fix documented in its host.cpp). This is the next step
up in complexity: a real algorithmic module (an in-house radix-2 FFT feeding the Makhoul
DCT/IDCT/IDXST transforms — the same math `pl_algo`'s field solve depends on), still with zero
AIE, so the hardware bring-up and the AIE-CDO question stay decoupled.

`src/pl/top.cpp` includes `fft_pl.hpp` directly (`-I` in the Makefile) — it is not a copy — so
this harness always tests the actual module, not a fork of it.

## Layout

| path | what |
|------|------|
| `src/pl/top.cpp` | kernel wrapper: `mode` (0/1/2) selects DCT/IDCT/IDXST, calls straight into `fft_pl.hpp` |
| `src/host/host.cpp` | XRT driver: random input, run all 3 transforms, verify vs. the naive double golden |
| `run_hw.sh` | hw run wrapper (mirrors `../add1_pl/run_hw.sh`) |
| `Makefile` | 3-step Versal build (no AIE step), `PL_GRID` controls transform length N (default 64) |

The golden and tolerance (`rel_rms < 1e-6`) are the same ones `vck5000/test/fft_pl_test.cpp`
already verifies offline (tier 1, seconds, no Vitis) — this harness is that check taken onto
real silicon, not a new one. Run the offline one first if `fft_pl.hpp` has changed:
```bash
cd ../vck5000 && make test
```

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

If `PLATFORM_REPO`'s default (`$HOME/xilinx_local/opt/xilinx/platforms`) doesn't exist on the
build node, override it, same as `../add1_pl`:
```bash
make TARGET=hw all PLATFORM_REPO=/opt/xilinx/platforms XILINX_XRT=/opt/xilinx/xrt_2024.2
```

Expected on success:
```
DCT   rel_rms=...e-07  PASS
IDCT  rel_rms=...e-07  PASS
IDXST rel_rms=...e-07  PASS
TEST PASSED  N=64  (worst rel_rms=...e-07, tol 1e-06)  fft_pl on VCK5000
```

On the hw node, override `XILINX_VITIS` for the runtime `LD_LIBRARY_PATH` if
`/tools/Xilinx/Vitis/2022.2` isn't present there:
```bash
XILINX_VITIS=/tools/Xilinx/Vitis/2024.2 ./run_hw.sh
```

Note: since there's no AIE graph, `hw_emu` should also work here — `make TARGET=hw_emu run` if a
second emulation cross-check is ever useful.
