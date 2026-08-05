# vck5000/test — verification harnesses

```bash
cd vck5000 && make test
```

Seconds, pure `g++`, no Vitis/XRT/device/xclbin and no toolchain env to source. Exits non-zero
if any module has drifted from its golden. **Run it after every edit under
`pl/src/pl_algo/src/modules/`.**

The full policy — what counts as a golden, what tolerance to use, why a test must assert rather
than print — is in `AIEplace/CLAUDE.md` § *Verification Loop*. Read that before adding a harness.

## What's here

| harness | verifies | golden | tolerance |
|---|---|---|---|
| `density_model` | Makhoul FFT/DCT recipe | sw_only CPU transforms | 1e-12 |
| `density_bin_model` | strip-tiled binning | naive full-grid scatter | bit-exact |
| `fft_pl_test` | `fft_pl.hpp` | naive double transforms | 1e-6 |
| `field_solve_test` | `field_solve_pl.hpp` | naive double field solve | 2e-6 |
| `sched_verify` | `param_scheduler.hpp` | recorded sw_only trace | bit-exact |
| `synth_check.{cpp,tcl}` | control core synthesizes | — | 0 errors, II=1 |

`synth_check` is **tier 2** — it needs Vitis and is not part of `make test`:

```bash
cd vck5000/test && vitis_hls -f synth_check.tcl
```

Numerical correctness and synthesizability are independent; passing one says nothing about the
other, so both are checked.

## Layout notes

- The harnesses `#include` the **real** PL modules via `-I../pl/src/pl_algo/src`, not copies —
  a module edit is what the test actually exercises.
- `fixtures/` holds committed test *inputs*. They deliberately do not live under
  `vck5000/results/`, which is gitignored: an automated test cannot depend on a file that isn't
  in the repo. See `fixtures/README.md` before swapping one.
- Binaries build into `build/` (gitignored).
- Everything here currently covers **pl_algo**. sw_only has no automated tests yet — that gap,
  and the plan for closing it, is **TODO #17**.

Moved here from `pl/src/pl_algo/model/` on 2026-08-05 so the tests are visible at the top level.
