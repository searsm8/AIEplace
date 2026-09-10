# iteration_pl — PL-only hardware harness for one full pl_algo Nesterov placement step

One PL kernel, eleven DDR ports: runs a complete gradient-eval-and-step — both gradient
sources computed and combined into a position update — on real VCK5000 hardware. **No AIE
anywhere** — no graph, no AXIS stream ports, no `link.cfg` connectivity, same framework as
`../add1_pl`, `../fft_pl`, `../field_solve_pl` and `../hpwl_pl`.

## Why this exists

The bring-up ladder so far:
1. `../add1_pl` — proved the PL load/run path (`out = in + 1`).
2. `../fft_pl` — the 1D DCT/IDCT/IDXST transforms.
3. `../field_solve_pl` — the full 2D electrostatic field solve built on those transforms.
4. `../hpwl_pl` — the weighted-average HPWL wirelength gradient (the other gradient source).
5. **this** — combine both proven building blocks into one full step:
   ```
   g_hpwl    = hpwl_CU(v_k, net connectivity)                    (../hpwl_pl, proven)
   rho       = density_bin(v_k, node sizes)                      (scatter, area-conserving)
   Ex, Ey    = field_solve_pl(rho)                                (../field_solve_pl, proven)
   g_density = force_gather(v_k, Ex, Ey)                          (adjoint of the scatter)
   u_{k+1}, v_{k+1} = iteration_update(g_hpwl, g_density, v_k, u_k, precond, lambda, alpha, coeff)
   ```

This is the sw_emu `--one-iter` bring-up mode in `vck5000/host/src/pl_algo/src/main.cpp`
(density_bin → field_solve_pl → force_gather → iteration_update) with the real HPWL
gradient wired in — that mode currently runs `g_hpwl=0` (density-only), an open item never
picked up. Here it's real, standalone, and on real silicon.

**This is deliberately *not* the device-resident Stage 5 loop** described in
`vck5000/pl/src/pl_algo/DATAFLOW.md`: there is no on-chip schedule, no convergence test, no
looping. `lambda`/`alpha`/`coeff` are host-supplied scalars for a single step, matching
pl_algo's v1 host-owned policy. Composing the resident loop is explicitly deferred (see task
`#20` in `.claude/0_WORKFLOW/tasks.md`) until the algorithm modules it depends on
(`param_scheduler`, `bb_reduce`) are re-verified against a fresh sw_only trace — this harness
does not touch that gap, it only proves the gradient-and-step datapath on real hardware.

`src/pl/top.cpp` includes every module directly (`-I` in the Makefile) — not copies — so this
harness always tests the actual modules.

## Layout

| path | what |
|------|------|
| `src/pl/top.cpp` | kernel wrapper: chains `hpwl_CU` → `density_bin` → `field_solve_pl` → `force_gather` → `iteration_update`/`memory_writer` |
| `src/host/host.cpp` | XRT driver: synthetic random design (movable + fixed nodes, netlist), run, verify `u_{k+1}`/`v_{k+1}` vs. a from-scratch double golden |
| `run_hw.sh` | hw run wrapper (mirrors `../hpwl_pl/run_hw.sh`) |
| `Makefile` | 3-step Versal build (no AIE step); `PL_GRID` (default 64) sizes the on-chip density matrices, `ITER_MAX_NODES` (default 128) bounds the on-chip node/gradient scratch |

There is no `vck5000/test/*iteration*` tier-1 g++ harness, so this design's golden is written
into `host.cpp` directly, reusing the exact math each already-established sw_emu check uses:
- HPWL: the same exact-exp full-WA gradient as `../hpwl_pl` (`HpwlGradVerify.cpp`).
- density scatter / force gather: `node_footprint.hpp`'s √2 sub-bin clamp geometry,
  replicated exactly in double (this harness's synthetic cells are small enough that the
  clamp matters, unlike `ForceVerify.cpp`'s own golden which uses raw rectangles).
- field solve: the same naive double 2D DCT/spectral/inverse as `ForceVerify.cpp`'s
  `golden_field()`.
- combine + step + clamp: the same math as `IterVerify.cpp`'s `runIterUpdateVerify()`.

The dominant error source is `hpwl_CU`'s LUT approximation of `exp()` — same as `../hpwl_pl`
— so the tolerance (`rel_rms < 3e-2` on the combined `[u_out; v_out]` state) is the same order
as that harness's, not a new bound.

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

On this build node, override the platform repo and XRT paths, same as the other harnesses:
```bash
make TARGET=hw all PLATFORM_REPO=/opt/xilinx/platforms XILINX_XRT=/opt/xilinx/xrt_2024.2
```

Expected on success:
```
[iteration_pl] M=48 N=56 nets=30 npins=...  GRID=64  span=... gamma=... lut=...
max_abs=...e-0x  rel_rms=...e-0x
TEST PASSED  M=48  (rel_rms=...e-0x, tol 3e-02)  iteration_pl on VCK5000
```

On the hw node you'll also see one or more `[XRT] ERROR: … err = -22` lines *before* the PASS
— that is the host's device-probe loop rejecting the non-VCK5000 cards (U55C) on the way to
the VCK5000. **Benign** when followed by a PASS.

Override `XILINX_VITIS` for the runtime `LD_LIBRARY_PATH` if `/tools/Xilinx/Vitis/2022.2`
isn't present on the hw node:
```bash
XILINX_VITIS=/tools/Xilinx/Vitis/2024.2 ./run_hw.sh
```

Note: no AIE graph, so `hw_emu` should also work — `make TARGET=hw_emu run` for a second
emulation cross-check if ever useful.
