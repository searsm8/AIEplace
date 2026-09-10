# hpwl_gradient_dhar -- standalone kernel for the Dhar HPWL-gradient module

A self-contained Vitis design that wraps the pl_algo module
[[hpwl_gradient_dhar.hpp]] (`vck5000/pl/src/pl_algo/src/modules/`) as a single PL kernel and
drives it from a native XRT host. This is the **tier-3** (emulation / on-device) counterpart of the
tier-1 offline harness `vck5000/test/hpwl_dhar_test.cpp`: same module, same capped WA-HPWL gradient
golden, exercised across the real host<->PL transfer path.

Pure PL, no AIE (modelled on `~/phd/toy_design`). Versal 3-step flow: `v++ -c` -> `-l` -> `-p`.

- `src/hpwl_gradient_dhar_top.cpp` -- kernel top: m_axi per buffer (gmem0..7), one s_axilite
  `control` bundle. Calls `hpwl_gradient_dhar` unchanged. The kernel takes pins that ALREADY carry
  absolute positions (the host runs the refresh, as `MODE_REFRESH_PINS` does on device).
- `src/host.cpp` -- builds a small netlist (masked nets + LIVE >16-pin nets so the 16-cap has
  something to drop), computes the double-precision capped golden, runs the kernel, checks the
  per-node gradient (`rel_rms`) and total HPWL. Reuses the module header for the POD types, the
  refresh passes and the LUT geometry.
- `Makefile` -- `TARGET=sw_emu` (default, WSL) or `TARGET=hw` (real card).
- `run_hw.sh` -- build-if-needed + run on the physical VCK5000 (Geert's card).

## sw_emu (in WSL)
```bash
source /tools/Xilinx/Vitis/2022.2/settings64.sh
source /opt/xilinx/xrt/setup.sh
cd ~/phd/AIEplace/vck5000/hpwl_gradient_dhar
make run          # -> "TEST PASSED"
```
`v++` finds the platform via `PLATFORM_REPO_PATHS` (normally exported by `~/.bashrc`). In a fresh,
non-login shell set it first: `export PLATFORM_REPO_PATHS=$HOME/xilinx_local/opt/xilinx/platforms`.
`run_hw.sh` does this fallback itself.

## Real hardware
```bash
./run_hw.sh        # builds the hw xclbin if missing (hours), then runs on the card
```

Expected output (both paths): `gradient rel_rms ~1e-6`, `hpwl rel ~1e-8`, then `TEST PASSED`.
