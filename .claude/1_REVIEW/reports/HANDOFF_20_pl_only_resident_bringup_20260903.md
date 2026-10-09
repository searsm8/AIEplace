# HANDOFF #20 — PL-only resident loop stood up in sw_emu (Path B), degenerate trajectory localized

**Date:** 2026-09-03, updated 2026-09-04 · **Branch:** pl_algo · **Status:** root cause of the
degenerate trajectory (§4 below) is now CONFIRMED to be Finding A alone (the α seed); a fix is
**implemented in `Driver.cpp` but NOT yet sw_emu-verified** — a separate, apparently pre-existing
performance wall in the bring-up-mode dispatch path stalls indefinitely at this design's real scale
before the fix could be exercised end-to-end. See §8 for the 2026-09-04 session.

## 8. 2026-09-04 session — root cause narrowed to Finding A alone; fix blocked on a dispatch stall

**Finding B (density force "OFF") is REFUTED as an independent bug.** Added a diagnostic printf in
`resident_iteration` (top.cpp, guarded `#ifndef __SYNTHESIS__`) that prints `gwl_L1`/`gden_L1` right
before `param_scheduler`'s iteration-1 λ formula consumes them. A real sw_emu run (gdb-attached,
since the plain run's stdout gets lost when the sw_emu scheduler thread SIGSEGVs harmlessly during
teardown — see below) on `mgc_pci_bridge32_b` at `PL_GRID=512` printed:
```
[resident] iter1 bootstrap: gwl_L1=2.196807e+03  gden_L1=1.701058e+16  init_mult=8.000e-05
```
`λ = (gwl_L1/gden_L1)*8e-5 = 1.033e-17` — matches the beacon's printed `lambda=1.0331e-17` exactly,
confirming the arithmetic. Cross-checked against a **fresh sw_only golden run**, same design, same
grid (`bins_per_row=512` — must be under `[params]`, not `[output]`, or it silently falls back to
the ePlace formula's 128), `/tmp/swgold/pci_b.toml` (not durable, recreate if missing):
```
DETAIL  Initial HPWL gradient L1 norm: 2768.963135
DETAIL  Initial density gradient L1 norm: 1455328591872.000000   (= 1.455e12)
DETAIL  Estimated initial step_length (BB): 177351  (seed 0.01 x site_width 200)
```
`gwl_L1` matches within ~25% (2197 vs 2769 — plausibly a masked-net or std-cell-only bring-up-mode
difference, not alarming). `gden_L1` is **~11,700x larger** on the PL kernel (1.7e16 vs 1.46e12) —
but **the λ formula is a ratio that self-normalizes against this**: λ's whole job is to make
`λ·gden_L1 ≈ 8e-5·gwl_L1` at iteration 1, by construction, REGARDLESS of gden_L1's absolute scale.
Confirmed: `1.033e-17 * 1.701058e16 = 0.1757 ≈ 8e-5 * 2196.807 = 0.1757`. So the iteration-1 force
balance is correct even with the scale anomaly — it does **not** explain the observed overflow
rising / trajectory freezing. (The absolute-scale mismatch in `gden_L1` is still real and worth a
separate investigation — a units/normalization difference between `field_solve_pl`'s unnormalized
DCT convention and whatever sw_only's Density.cpp effectively integrates to — but it is NOT
load-bearing for the frozen trajectory, so it is demoted to a followup, not blocking.)

**Finding A (the α seed) is the whole story.** sw_only's real BB-calibrated initial step for this
design/grid is **177,351**; the resident kernel seeds α = `init_step_seed(0.01) * site_width(200)`
= **2.0** — a ~88,700x shortfall, unrelated to λ. At α=2.0 essentially nothing moves at iteration 1,
so iteration 2's BB estimate divides two near-zero deltas and collapses toward 0, freezing the run.
This is exactly what `estimateInitialStep`/`estimate_initial_learning_rate` (sw_only Step.cpp,
XPlace `initializer.py:171`) exists to prevent, and exactly what `resident_place`'s own comment
already flagged as missing (an in-kernel version "segfaulted the HLS elaborator").

### Fix implemented (untested end-to-end): host-side BB trial-step bootstrap in `runResidentPlacement`
`Driver.cpp::runResidentPlacement` now runs the SAME trial-step estimate `runPlacement`'s
`estimate_initial_step`/`initDensityWeight` already do (`Placement.hpp` — `bbStepLength`,
`initDensityWeight`, both reused directly, no duplication), via the shared bring-up-mode ports
(0-13), **before** the resident `MODE_PLACE` call:
1. `eval_probe(v0)` — REFRESH_PINS → HPWL_GRAD → DENSITY_BIN → field solve → FORCE_GATHER — to get
   `g_hpwl0`/`g_density0` at the initial placement. PL_ONLY field solve = `MODE_FIELD_SOLVE_PL`
   (needs `-DPL_FIELD_SOLVE` added to the build alongside `-DPL_ONLY`, a bring-up-only kernel mode
   that was already wired but not previously combined with `-DPL_ONLY` in the resident recipe);
   `!PL_ONLY` mirrors `runPlacement`'s AIE `field_pass` chain (**UNVERIFIED this session** — full-
   grid AIE resident sw_emu is intractable per §1, so this branch has no sw_emu path to test on).
2. `lambda0 = initDensityWeight(g_hpwl0, g_density0, ...)` (host-side, same formula as the kernel's
   own iteration-1 seed — `INIT_MULTIPLIER=8e-5f` must be kept in sync with `resident_place`'s
   hardcoded `sp.init_multiplier`, flagged with a comment at both sites).
3. Trial step `v' = v0 - (seed*site_width)*g0` via `MODE_ITERATION_UPDATE` (precond=1, coeff=0).
4. `eval_probe(v')` for `g_hpwl_t`/`g_density_t`; `alpha0 = bbStepLength(v', v0, gtot_t, gtot0, M)`.
5. Restore `node_pos`/`node_box` to v0 (u/v_prev/g_total_prev were never touched by the bootstrap).
6. The final `MODE_PLACE` call now passes **`alpha0`** where it used to pass the crude `init_step`.

Buffer changes needed to carry this: `b_lut` (gmem4) widened `lut_size↔M` floats (exp_lut vs
precond, same aliasing `runPlacement` already relies on); `b_din`/`b_dout` (gmem10/11) widened from
`sizeof(float)` to the full `matB` (they're genuinely unused by `MODE_PLACE` itself, only by the new
bootstrap's bring-up-mode calls beforehand). `g++ -fsyntax-only` clean for both kernel
(`-DPL_ONLY -DPL_FIELD_SOLVE -DPL_GRID=512`) and host. **Known pre-existing limitation inherited
from `runPlacement`, not introduced here:** the bring-up `MODE_DENSITY_BIN`/`MODE_FORCE_GATHER`
hardcode `first_macro=first_filler=num_movable` (std-cell-only verify pack), so the bootstrap's
`g_density0` skips the #11b movable-macro deposit-weight override on a design WITH movable macros.
Harmless for `mgc_pci_bridge32_b` (0 movable macros — see §5); would need a real `MODE_DENSITY_BIN`
arg to be faithful on an MMS-style design.

### Blocked: bring-up-mode dispatch appears to stall indefinitely at this design's real scale
Three sw_emu attempts (`PL_GRID=512`, `128`, `64`, `PLACE_ITERS=3`, `mgc_pci_bridge32_b` — 31,382
movable / 29,417 nets) each ran for **40-60+ minutes at ~115-200% CPU with ZERO output**, not even
the bootstrap's own first diagnostic printf, which fires after just the FIRST of 11 extra kernel
launches (`MODE_REFRESH_PINS` on `v0` — a trivial O(num_pins) loop, confirmed by reading
`refresh_net_pins`/`refresh_node_pins`). Timing instrumentation was added around `run_mode1`
(`std::chrono`, printed per-launch) specifically to localize this — even that never printed once in
3+ minutes on a fresh grid=64 rebuild, meaning the very FIRST `xrt::run.../.wait()` in the bootstrap
does not return in any practical time. Two threads stay in kernel-reported state `R` (`/proc/*/task/
*/stat`) throughout — genuinely spinning, not blocked on a futex/IO — but `gdb -p <pid>` cannot
attach (`ptrace_scope` blocks it, no passwordless sudo available in this session) so no backtrace
could be captured to see WHERE the spin is.

**This does not look like it is caused by the bootstrap's logic** (grid size made no measurable
difference — 512/128/64 all stalled identically, ruling out compute cost; the operation itself is a
linear loop). It looks like a property of invoking `top()` via the single-shot bring-up modes many
times in one process, AT THIS DESIGN'S REAL SCALE, that nothing has previously exercised: `test/
synth_check.tcl` and the tier-1 harnesses test modules in isolation (no XRT), and the ORIGINAL
single-launch `MODE_PLACE`-only resident run (no bootstrap, confirmed working via gdb earlier this
session — see §3/§4 for its actual iter-1 output) never calls a bring-up mode as a *separate*
top-level launch at all. `runPlacement`'s own bring-up-mode calls (the pattern this bootstrap
mirrors) may never have been sw_emu-verified at this design's real scale either — the "sw_emu-
verified, stable descent" claim in auto-memory `pl_algo_stage5c` should be treated as unconfirmed
for a design this large until re-checked; it may have been demonstrated on a smaller pack.

**Next session:** do not re-attempt a longer wait — three attempts already spent 2+ hours of wall
clock with no signal. Instead: (a) get a real backtrace (root/sudo ptrace, or add per-line printfs
inside `refresh_net_pins`'s HLS loop itself, tier-1-style, to see if it's even entering the kernel
body at all vs stuck in XRT's own launch/sync machinery); (b) try `runPlacement`'s existing
bring-up-mode path standalone (e.g. `make run-hpwl-grad` if it exists) on this same real design to
see if a SINGLE such call, with no bootstrap code involved, also stalls — that would prove the issue
predates this session's change entirely; (c) if confirmed pre-existing and unfixable quickly,
consider whether `alpha0` can be estimated some other way that avoids the bring-up-mode round trip
(e.g. a tiny standalone verification host program with its own device session, run ONCE, outside the
main resident-placement binary, to sanity-check the dispatch path in isolation).

**Status of the code change:** implemented, self-consistent by inspection, reuses already-verified
`Placement.hpp` formulas, `g++ -fsyntax-only` clean — but **NOT confirmed to run end-to-end**, and
per this project's testing discipline that means it is not "done." Left in place (not reverted) at
Mark's live direction during the session; flag this the next time `runResidentPlacement` is touched.

Related: [[DATAFLOW.md]] (authoritative), TODO #20 step 6 (resident loop), auto-memory
`pl_algo_stage5c`, `init_step_length_xplace_gap`.

---

## 1. What prompted this

A `--resident-place` sw_emu run (AIE build, GRID=1024, `mgc_pci_bridge32_b`) had been pegging a
core for **4 days** with zero output. Diagnosis: the resident loop is device-resident, so the host
prints nothing until `top()` returns — and one iteration = six 1024² AIE-FFT passes through the
*software* AIE model, which is intractable at full grid. It had not completed a single iteration.
Killed (PID 2653029 tree). sw_emu of the full-grid AIE resident loop is not a viable vehicle;
hardware (Geert's card) is its home.

## 2. What landed

### (a) Per-iteration progress beacon — `top.cpp` `resident_place`
`#ifndef __SYNTHESIS__` printf, one line per iteration (HPWL, overflow, λ, α, γ, stop). Compiles
out for real HW. This is the only reason the rest of this handoff exists — the loop was previously
a black box. **Caveat:** it fires *after* each `resident_iteration` returns, so it proves an
iteration finished, not sub-iteration progress.

### (b) Path B — a PL-only backend for the resident loop (no AIE)
`density_gradient` now has two compile-time backends sharing one scatter/gather envelope:
- `!PL_ONLY`: the six `dct_transpose_pass` calls through the AIE FFT pool (unchanged, byte-identical).
- `PL_ONLY`: the whole solve on-chip via `field_solve_pl` (rho→BRAM→solve→Ex/Ey→DDR), **no AIE**.

`resident_iteration` / `resident_place` / the `MODE_PLACE` dispatch were lifted out of the
`#ifndef PL_ONLY` guard so they exist in both builds; only the trailing AIE stream params/args are
guarded. Host (`Driver.cpp::runResidentPlacement`): the `xrt::graph fft` open / `fft.run` /
`fft.wait` are guarded `#ifndef PL_ONLY` (the PL-only xclbin has no AIE graph). The `top(...)`
binding list is unchanged — AIE streams were never host args.

**Build recipe (no Makefile change needed):**
```bash
make run-resident-place TARGET=sw_emu AIE=none PL=pl_algo HOST=pl_algo BUILD_XRT=1 \
     EXTRA_DEFS="-DPL_ONLY -DPL_GRID=512" PLACE_ITERS=5
```
`AIE=none` auto-adds `-DPL_ONLY` to the kernel (common.mk:101); `EXTRA_DEFS` reaches both kernel
and host. Host needs `-DPL_ONLY` too (only via `EXTRA_DEFS`), hence it is passed explicitly.

**Gotcha for the next builder:** the host had never been compiled with `-DPL_ONLY` before. The
other AIE bring-up runners (`runDCTRowPass`, etc.) construct `xrt::graph fft` but are never called
in a resident run, so they compile fine under `-DPL_ONLY` — do **not** guard them. Only
`runResidentPlacement` needs the guard.

### (c) `_BRAM` memory-residence suffix (naming convention)
The on-chip statics in the PL-only `density_gradient` are now `rho_BRAM`, `Ex_BRAM`, `Ey_BRAM`,
`tA_BRAM`, `tB_BRAM`. Convention: suffix on-chip-resident data with its memory. NOTE: at
`PL_GRID>=256` these are ≥1 MB each and would really map to URAM if this path were ever
synthesized — the suffix is aspirational for the small-grid sim. (`density_bin_model.cpp` and the
`MODE_FIELD_SOLVE_PL` block still hold un-suffixed `rho_`/`Ex_` — left untouched, surgical.)

## 3. Verification — it runs, exit 0

| build | iters | result |
|---|---|---|
| `PL_ONLY, PL_GRID=64`  | 3 | exit 0, ~1 min, beacon prints each iter |
| `PL_ONLY, PL_GRID=512` | 5 | exit 0, few min, beacon prints each iter |

Both syntax-check clean (`g++ -fsyntax-only`, `!PL_ONLY` and `PL_ONLY`). The **wiring is proven**:
refresh → HPWL grad → PL density solve → `bb_reduce` → `param_scheduler` → step → carry all execute
on-device with no host round-trip, finite, cells move.

## 4. The trajectory is DEGENERATE — golden diff localizes why

sw_emu resident (`mgc_pci_bridge32_b`, td=0.143), identical at GRID 64 and 512:
```
iter 1  HPWL=8.794e8  overflow=0.3048  lambda=4.195e-16  alpha=2.00e+00  stop=0
iter 2  HPWL=8.606e8  overflow=0.9557  lambda=4.195e-16  alpha=1.02e-06  stop=0
iter 3  HPWL=8.606e8  overflow=0.9557  lambda=4.405e-16  alpha=0.00e+00  stop=0
(HPWL frozen thereafter; stops at iter 5 << min_iters=50)
```
sw_only golden (same design, grid 512, td 0.143, `random_seed=42`, `/tmp/swgold/pci_b.toml`,
run `results/single_runs/mgc_pci_bridge32_b/20260903_172842_385_cpu_cpu`):
```
Iter, HPWL,     OVFW,   step_len, density_weight
001,  6.660e+08, 0.978, 2.226e+05, 1.231e-12
002,  7.053e+08, 0.959, 2.196e+05, 1.231e-12
003,  7.224e+08, 0.954, 1.219e+05, 1.231e-12
```

### Finding A — initial step is ~1e5× too small (α seed)
Resident α(iter1) = **2.0** = `init_step_seed(0.01) * site_width(200)`. sw_only's actual initial
`step_len` = **2.23e5**. The resident's "estimateInitialStep seed" is NOT sw_only's
estimateInitialStep — sw_only does a Barzilai-Borwein trial step (`||Δx||/||Δg||`, seed 0.01,
≈2e5), the resident just uses `0.01*site_width`. So v0 barely moves; the iter-2 BB reduction over a
near-zero displacement collapses α to 1e-6 then 0, and HPWL freezes. See auto-memory
`init_step_length_xplace_gap` — this is exactly that gap, now biting the resident loop.

### Finding B — density force is effectively OFF (λ seed)
Resident λ = **4.2e-16** vs sw_only **1.23e-12** (~3000× smaller). Consequence is unambiguous
regardless of cross-tool λ-scale comparability: resident overflow **rises** 0.30→0.96 (cells
collapsing to cut wirelength, nothing spreading them) while sw_only overflow **falls** 0.98→0.95
(spreading works). The iter-1 λ seed flows through the `gwl_L1`/`gden_L1` reduction feeding
`param_scheduler`; a units/scale error there (or the missing host bootstrap, §5) would zero it.

### Control that proves density is inert: 64 vs 512 are BIT-IDENTICAL
The GRID=64 and GRID=512 resident trajectories match to every printed digit (HPWL 8.793969e8,
overflow 0.3048, λ 4.1952e-16…). Confirmed the kernel really rebuilt at `-DPL_GRID=512` and
`DENSITY_GRID==PL_GRID`, so the grid genuinely changed. A live density force would spread a 512
grid differently from 64. It doesn't → density contributes nothing → Finding B, independently
confirmed. (HPWL is grid-independent so its match is expected; overflow's exact match is the tell.)

## 5. Input data given to sw_emu (answers "what is fed to the device")

`main.cpp:379` `--resident-place`: parses the design (`DataBase`), `tagMovableMacros` +
`addFillers`, `packDesign`, then `runResidentPlacement` uploads **once**: initial node positions
`pk.node_pos` (+`node_box`), nets/pins/offsets, `exp_lut`, per-node `area[]`, and config scalars
(grid=`PL_GRID`, `base_gamma=4*(die_x+die_y)/512`, td=0.143, `init_step_seed=0.01`,
`density_weight_init_multiplier=8e-5`, `overflow_threshold=0.07`, `min_iters=50`).

**KEY:** `main.cpp` passes `pk.node_pos` **raw** — there is **NO host-side iteration-0 bootstrap**.
The DATAFLOW/`top.cpp` comments claim the host runs `initializeDensityWeight` + `estimateInitialStep`
and seeds v_prev=v0 / g_total_prev=0. It does not — v0 is just the packed initial placement, α is
seeded from the crude `0.01*site_width`, and λ from the kernel's iter-1 grad-ratio. **Findings A and
B are the direct consequence of that missing bootstrap.** This is the thing to fix.

## 6. Next actions (for the next session)
1. **Implement the real iteration-0 bootstrap on the host** (`main.cpp`/`Driver.cpp`): sw_only
   `estimateInitialStep` (BB trial step → ~2e5, not 2.0) and `initializeDensityWeight` (→ ~1e-12),
   seed α and λ across the boundary. This is the documented-but-absent step; it should fix both
   findings at once.
2. Re-run `PL_GRID=512 PLACE_ITERS=50` and diff the beacon vs the sw_only golden trajectory above
   — expect α~2e5, λ~1e-12, overflow falling.
3. Only after the trajectory tracks the golden is the PL-only resident loop "verified". Until then
   it is "runs, wiring proven", not "correct".
4. When closing: fold this into the report, update DATAFLOW.md §"Status" (MODE_PLACE now has a
   PL_ONLY backend that runs in sw_emu; flag the seed caveat), and the stale `run-hpwl-grad` Makefile
   comment (AIE=none DOES compile out the ports now, common.mk:101 — comment says otherwise).

## 7. Files touched
- `pl/src/pl_algo/src/top.cpp` — beacon, Path B `density_gradient` backend, guards lifted, `_BRAM` rename.
- `host/src/pl_algo/src/Driver.cpp` — `runResidentPlacement` AIE-graph guards (`#ifndef PL_ONLY`).
- Logs: `vck5000/run_resident_plonly.log` (GRID=64), `vck5000/run_resident_g512.log` (GRID=512).
- Golden: `/tmp/swgold/pci_b.toml` (NOT durable — /tmp), run dir under `results/single_runs/`.
