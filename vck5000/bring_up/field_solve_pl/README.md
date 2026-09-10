# field_solve_pl — PL-only hardware harness for the full pl_algo field solve

One PL kernel, three DDR ports (`rho` in, `Ex`/`Ey` out): runs `plalgo::field_solve_pl` from
`vck5000/pl/src/pl_algo/src/modules/field_solve_pl.hpp` on real VCK5000 hardware — the **entire**
electrostatic density field solve. **No AIE anywhere** — no graph, no AXIS stream ports, no
`link.cfg` connectivity, same framework as `../fft_pl` and `../add1_pl`.

## Why this exists

The bring-up ladder so far:
1. `../add1_pl` — proved the PL load/run path on the hw node's VCK5000 (`out = in + 1`).
2. `../fft_pl` — first real algorithm on silicon: the 1D DCT/IDCT/IDXST transforms.
3. **this** — the full 2D field solve built *on top of* those transforms, still zero AIE:
   ```
   a_uv   = DCT_x( DCT_y(rho) )                              (forward 2D DCT)
   Ex_hat = a_uv·w_u / (w_u²+w_v²),  Ey_hat = a_uv·w_v / (…)  ([0][0]=0)
   Ex     = IDXST_x( IDCT_y(Ex_hat) ),  Ey = IDCT_x( IDXST_y(Ey_hat) )   (inverse)
   ```

`src/pl/top.cpp` includes `field_solve_pl.hpp` directly (`-I` in the Makefile) — not a copy — so
this harness always tests the actual module. It mirrors `MODE_FIELD_SOLVE_PL` in
`pl_algo/top.cpp`: copy `rho` DDR→BRAM, solve entirely on-chip, copy `Ex`/`Ey` BRAM→DDR.

## Layout

| path | what |
|------|------|
| `src/pl/top.cpp` | kernel wrapper: load `rho`, call `field_solve_pl`, store `Ex`/`Ey` (on-chip scratch) |
| `src/host/host.cpp` | XRT driver: random `rho`, run, verify `Ex`/`Ey` vs. the naive double golden |
| `run_hw.sh` | hw run wrapper (mirrors `../fft_pl/run_hw.sh`) |
| `Makefile` | 3-step Versal build (no AIE step), `PL_GRID` controls grid side N (default 64) |

The golden and tolerance (`rel_rms < 2e-6`) are the same ones
`vck5000/test/field_solve_test.cpp` already verifies offline (tier 1, seconds, no Vitis) — this
harness is that check taken onto real silicon. Run the offline one first if the module changed:
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

On this build node, `PLATFORM_REPO`'s default (`$HOME/xilinx_local/opt/xilinx/platforms`) and
`XILINX_XRT`'s (`/opt/xilinx/xrt`) don't exist — override both, same as `../fft_pl`:
```bash
make TARGET=hw all PLATFORM_REPO=/opt/xilinx/platforms XILINX_XRT=/opt/xilinx/xrt_2024.2
```

Expected on success:
```
Ex rel_rms=...e-07  Ey rel_rms=...e-07
TEST PASSED  N=64  (worst rel_rms=...e-07, tol 2e-06)  field_solve_pl on VCK5000
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
