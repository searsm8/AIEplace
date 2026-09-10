# add1_pl — pure-PL "hello world" for the VCK5000

One PL kernel, two DDR ports, a scalar arg: `out[i] = in[i] + 1`. **No AIE anywhere** — no
graph, no AXIS stream ports, no `link.cfg` connectivity.

## Why this exists

`../dct_fft_aie` (build with Vitis 2022.2, run on the hw node's XRT 2024.2) fails to load with:

```
[XRT] ERROR: See dmesg log for details. err = -22
terminate called after throwing an instance of 'xrt_core::system_error'
  what():  failed to load xclbin: Invalid argument
```

`dct_fft_aie/HANDOFF.md` has the full investigation. Short version: the toolchain-version route
is exhausted (2022.2 is the only Vitis generation that successfully *builds* this platform at
all — 2023.2 fails at NoC placement, 2024.x refuses the platform outright before compiling
anything), so the mismatch has to be solved on the load/runtime side. The working theory there is
that it's specifically the **AIE array config (CDO/PDI) download** that the hw node's 2024.2
driver stack rejects — not the base bitstream, since XRT's own AIE-less `verify.xclbin` (built
the same way, same platform, same day) loads fine on the same card.

This design tests that theory directly, stripped to the minimum that still produces a loadable
xclbin with a real DDR round-trip:

- **If `add1_pl` loads and runs on hardware** → the AIE-CDO theory holds. The 2022.2-build /
  2024.2-run combination is fine in general; the fix belongs on the AIE side specifically (older
  XRT alongside the 2024.2 one, a CDO/PDI format shim, etc.).
- **If `add1_pl` *also* fails with `err = -22`** → the AIE theory is wrong. The 2022.2-build /
  2024.2-run combination doesn't work on this platform at all, full stop — the "build on 2022.2,
  run on 2024.2" plan needs to be reconsidered for everything, not just AIE designs.

## Layout

| path | what |
|------|------|
| `src/pl/top.cpp` | the entire kernel — one loop, `out[i] = in[i] + 1` |
| `src/host/host.cpp` | XRT driver: fill `in[]`, run, verify `out[i] == in[i] + 1` |
| `run_hw.sh` | hw run wrapper (mirrors `dct_fft_aie/run_hw.sh`) |
| `Makefile` | 3-step Versal build (no AIE step) |

## Build & run

Source the tools first:
```bash
source /tools/Xilinx/Vitis/2022.2/settings64.sh
source /opt/xilinx/xrt/setup.sh
```

Then, cheapest-first:

```bash
make csynth                # 1. v++ -c of the PL kernel — confirms it synthesizes (fast)
make TARGET=sw_emu run      # 2. full build + sw_emu functional check
make TARGET=hw  all         # 3. build the real-silicon xclbin  →  hand to the hw node
./run_hw.sh                 #    run on the real VCK5000
```

If `PLATFORM_REPO`'s default (`$HOME/xilinx_local/opt/xilinx/platforms`) doesn't exist on the
build node, override it, same as `dct_fft_aie`:
```bash
make TARGET=hw all PLATFORM_REPO=/opt/xilinx/platforms
```

Expected on success: `TEST PASSED  n=1024  add1 on VCK5000 OK`.

On the hw node, override `XILINX_VITIS` for the runtime `LD_LIBRARY_PATH` if `/tools/Xilinx/Vitis/2022.2`
isn't present there:
```bash
XILINX_VITIS=/tools/Xilinx/Vitis/2024.2 ./run_hw.sh
```

Note: since there's no AIE graph, `hw_emu` should also work here (unlike `dct_fft_aie`, which is
`sw_emu`-only because `hw_emu` lacks `xclGraphOpen` on this platform) — `make TARGET=hw_emu run`
if a second emulation cross-check is ever useful.
