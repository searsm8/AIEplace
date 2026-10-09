# REPORT #45 step 5: `sw_flat_place`, the CPU phase-1 placer, bit-identical to sw_only

*2026-10-09. Branch `pl_algo`. Was `HANDOFF_45_sw_flat_placer_20261009.md` (design notes only); the
code and the verification below closed it the same day.*

## Result
`host/sw_flat/placer/` (~900 lines, seven files) runs sw_only's phase-1 global placement on the
FlatDesign + slot layout from steps 3–4. **Under the D6 config it reproduces sw_only bit for bit:**
`schedule_trace.csv` is identical as text, so every iteration's HPWL, overflow, BB norms, γ, λ, step,
momentum, precond coefficient and κ match at `%.9e` (which round-trips a float). The stop, the
restored best solution and the final report match too.

| design | grid | iterations | stop | slots / chunks / fillers | sw_only | sw_flat |
|---|---|---|---|---|---|---|
| ispd2015/mgc_fft_a | 256 | 628 | converged | 65,536 / 1 / 30,943 | 7 s | 3 s |
| ispd2015/mgc_pci_bridge32_b | 128 | 715 | converged | 32,736 / 1 / 2,468 | 4 s | 2 s |
| ispd2005/adaptec1 | 512 | 654 | converged | 388,896 / 1 / 160,067 | 62 s | 21 s |
| mms/adaptec1 (62 movable macros, phase 1 only) | 512 | 657 | converged | 387,808 / 1 / 160,066 | 62 s | 21 s |
| ispd2005/bigblue4 (chunked) | 2048 | 759 | converged | 5,502,432 / 3 / 2,885,579 | 1102 s | 369 s |

Runs from `test/placer_check.sh`, artifacts in `host/sw_flat/build/placer_check*/` (gitignored).
Times are wall clock on the shared 8-vCPU box.

The check fails when it should. With `init_gamma` changed from 4 to 4.000001 (about 2 ULP), the
trace differs from row 1. `make test-regress` (sw_only) PASS at write time; sw_only was not touched.

```bash
cd host/sw_flat && make placer && make check-placer               # the two fast designs, ~20 s
bash test/placer_check.sh ispd2005/adaptec1 ispd2005/bigblue4      # any designs from benchmarks.py
```

## Decision: bit-identical first (Mark, 2026-10-09)
D3 expected a tolerance A/B. Mark chose the stricter bar: the state arrays live in slot order, but
every order-sensitive float sum runs in sw_only's order through stored orders. A bug therefore shows
as a diff, not as noise within a tolerance. **Summing in slot order (the PL's order) is the next,
separate step, and it gets its own measured tolerance.**

The orders (`placer.hpp`), each one sw_only's:
- **`movable_slots`**: non-FIXED components in NAME order (DataBase's `std::map`), then the fillers.
  Used by the initial draws, the deposit, and every `OrderedReduce` sum (BB norms, combine L1s,
  precond a1/a2, initial λ).
- **`fixed_nodes`**: FIXED components (not IO pads) in name order, for the fixed deposit.
- **Nets**: the gradient replay in FILE order (`DataBase::mv_nets`, net then pin; the handoff said
  name order, which was wrong); the HPWL metric in NAME order (`mv_nets_by_name`), keeping the first
  of a repeated name as `std::map::emplace` does.

## How it maps onto sw_only (D3: sw_only's structure)
| sw_flat | sw_only |
|---|---|
| `setup.cpp` | `Setup.cpp`: config, read, macro tags, fillers, area analysis and grid formula, initial placement, initial λ |
| `wirelength.cpp` | `Partials.cpp` (`computeHpwlPartials_CPU`, parallel per net then ordered replay) + `DataBase::computeTotalWirelength` |
| `density.cpp` | `Density.cpp` + Grid.cpp: footprint, deposit, fixed cap, DCT field (sw_only's `DCT.cpp` is linked, not copied), force gather, overflow metric |
| `step.cpp` | `Step.cpp` without backtracking: Nesterov step, iteration-1 BB estimate, combine, die bound |
| `schedule.cpp` | `Schedule.cpp` + `BestSolution.cpp`: throttled γ/λ, precond coef, the five stop tests, the three trackers and the ship rule |
| `main.cpp` | `AIEplace.cpp`'s loop + `dumpScheduleTrace` + the final restore and metrics |

Scope guards: the config is rejected if it asks for backtracking, a non-`cpu` method, a non-HPWL
wirelength, `deterministic = false`, or phase 2 on a design with movable macros. Movable macros with
`enable_phase2 = false` run, as they do in sw_only (mms/adaptec1 above).

## Arithmetic details that decide bits (kept from the handoff, all now confirmed by the diff)
- Same build flags as sw_only (`-std=c++2a -O2 -g -fopenmp`, no `-march`, so no FMA contraction).
- `exp` / `fabs` on floats are the float overloads; `std::pow(float, float)`; `sqrtf`.
- The two `gauss()` (and `rand()`) calls inside one `Position(...)` run in the compiler's order, so
  the expression is copied verbatim with `AIEplace::Position`. `dieW` is `Grid::getDieWidth()`, an
  **int**; the centre is `Position(dieW/2, dieH/2)` (integer division).
- Footprint: the sqrt(2) clamp applies to fixed nodes in the solver deposit but not in
  `computeOverflow`. The deposit's overlap is `min(xh, col*bw + bw) - max(xl, col*bw)` and the
  metric's is `min(xh, (c+1)*bw) - max(xl, c*bw)`. They round differently, so both are kept.
- Bin areas: each bin's own `Box` area for the fixed cap; `getBinDensities` scales by bin [0][0]'s
  area; `computeOverflow` uses `bw*bh`.
- `BinOverlap.overlap` is a double holding a float, and `Bin::total_overlap` is a float. Adding or
  multiplying in double and rounding once to float gives the same float result (53 ≥ 2·24+2), so
  sw_flat keeps these quantities in float. It also recomputes the footprint at gather time: nothing
  moves between the deposit and the gather, so no per-node overlap lists are needed.
- `num_pins` (preconditioner) counts the node's pins over ALL nets, of any degree.
- No component on the 44 designs is PLACED (checked in step 3), so every one is drawn. FlatDesign
  carries no PLACED flag, which leaves a gap if such a design ever arrives.

## Not done, and why
- **No DEF / `.pl` output**, so `dse.py` cannot score sw_flat yet. The plan's legal-vs-legal check is
  redundant while the placer is bit-identical: the final placement is sw_only's. It becomes needed once
  the sums move to slot order.
- **The final-position hash is not compared.** The restored solution is one of the traced iterations,
  picked by the same rule. The check compares the stop and the four final metrics at the report's
  3 digits. sw_only's DEF hash would need a DEF writer here. Given a 9-digit identical
  trajectory over 600+ iterations this is a weak gap, but it is a gap.
- sw_flat's deposit runs serially, which is still 2–3× faster than sw_only end to end; it has not
  been optimized.

## Next
1. **Slot-order sums**: flip each order-sensitive sum to slot order, one at a time, and measure the
   trajectory drift and final HPWL against this bit-identical baseline. This is what makes sw_flat the
   per-module golden for step 6's slot-order PL modules.
2. Step 6, the density path in slot order, with each module checked against the matching `sw_flat`
   function.
