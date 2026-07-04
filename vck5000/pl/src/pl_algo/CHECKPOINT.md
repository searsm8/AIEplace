# Checkpoint — pl_algo Stage 5c COMPLETE; pivoting to markv1 CPU validation + full algorithm review

## Where we are
The **entire ePlace iteration** runs on the PL/AIE, sw_emu-verified end to end (see
`build_reports/stage5c.md`). A 6-iteration `--place` run on `mgc_pci_bridge32_b` gives a clean,
stable, monotonic HPWL descent with the BB/Nesterov step behaving correctly. That closes the
functional hardware *draft*.

**Decision (2026-07-04): pause PL work and turn back to the pure-software (markv1) reference.**
Getting a full PL iteration running is a milestone, but before investing in hardware optimization
we want to confirm the *algorithm itself* is as good as it can be on CPU. markv1 is the golden
reference the whole PL design is checked against — if the software has quality bugs (like the
`overlap_area` force-gather bug we already caught), they propagate into the PL. So the active work
moves to running full CPU placements in markv1, finding and fixing quality issues, and then
reviewing the optimizer (Nesterov + backtracking) in depth.

## Active plan (this is what to work on next)

### A. markv1 CPU-only full placements (item 3)
Run markv1 end-to-end on CPU (no AIE, `partials_compute_method`/`density_compute_method` = CPU)
across benchmarks and confirm the algorithm converges well.
- Build: `cd vck5000 && make host HOST=markv1` (no XRT needed for CPU-only). Run via its config
  (see markv1 `run_config.json` / the DSE harness `dse.py`).
- Sanity vs references: HPWL and overflow trajectories should match ePlace behavior — HPWL rises
  early as cells spread, overflow falls below the stop threshold, converges in ~50–200 iters.
  Compare final HPWL against the DSE baseline table in auto-memory `architecture` / MEMORY.md
  (adaptec1 ~1.09e8 vs XPlace 7.3e7 — there is a real quality gap to investigate).
- **Hunt for more correctness bugs like the force-gather `overlap_area` miss.** Candidate areas to
  scrutinize (cross-check against Xplace/DREAMPlace, the source of truth):
  1. Density force scale/sign end-to-end (`computeElectrostaticForce` now has `overlap_area`, but
     re-verify the field solve normalization + `local_density_weight` usage vs the fixed force).
  2. Preconditioner (`updatePrecondWeights`) — area normalization by `avg_node_size` vs XPlace's
     coordinate normalization by `site_width`; confirm the O(1) scaling is right.
  3. Overflow metric exactness (`computeTotalOverflow`) and the γ/λ schedules
     (`updateGamma`, `updateDensityWeight`) vs `param_scheduler.py`.
  4. Fillers are DISABLED (standing pending item) — re-enable and verify; they materially change
     density/quality and the HPWL-vs-XPlace gap.
  5. Pin offsets: auto-memory `pin_architecture` notes all pins sit at the component origin (LEF
     pin data discarded) — this understates HPWL/HPWL-gradient for macros; likely a real quality bug.

### B. Full algorithm review — Nesterov + backtracking (item 4)
After CPU placements look healthy, review the optimizer in depth (this extends the 5c audit, which
only checked the *step math*, not the full solver dynamics):
- **Nesterov** (`performNextStep`, `stepAllNodes`, `Node::step`): momentum recurrence, warmup
  (momentum/backtracking disabled for first N iters), the first-iteration HPWL-only quirk the audit
  flagged (markv1 skips `combineGradients` before the first step — decide keep vs fix).
- **Backtracking** (`performNextStep` do-while, Algorithm 2): the BB/Lipschitz line search. Note
  markv1's `computeLipshitzEstimate` is the ePlace *nobb* Lipschitz step `‖Δv‖/‖Δg‖` (matches
  DREAMPlace `step_nobb`), NOT the BB-short step `s·y/y·y` (`step_bb`). Decide whether to adopt the
  true BB step, and whether v1's "no backtracking" is leaving quality/stability on the table (the PL
  6-iter run saw α saturate to the 4000 clamp — a place backtracking would normally intervene).
- References (source of truth): `~/phd/Xplace/src/{nesterov_optimizer,param_scheduler,calculator,
  initializer}.py`, `~/phd/DREAMPlace/dreamplace/NesterovAcceleratedGradientOptimizer.py`.
- Any fix lands in markv1 (the golden) first, then is reflected back into the PL design.

### C. Hardware deployment (item 2 — DEFERRED until Mark confirms remote access)
Question raised: *can we build targeting hardware and transfer only the executable + xclbin?*
**Short answer: mostly yes, with caveats — it is not a bare two-file copy.** The target needs a
compatible runtime, not just the artifacts. Checklist for when we deploy:
- **Build:** `make all TARGET=hw AIE=pl_algo PL=pl_algo HOST=pl_algo BUILD_XRT=1
  AIE_DENSITY_INSTANCES=8` → produces the `hw` xclbin (PL bitstream + AIE) and the host `.exe`.
  This is a long place-and-route build (hours), unlike sw_emu.
- **What must exist on the target card/host:**
  - The **VCK5000 platform/shell** flashed on the card, matching the build platform
    (`xilinx_vck5000_gen4x8_qdma_2_202220_1`).
  - **XRT installed** (matching major version) — provides `libxrt_coreutil.so` the host links.
  - A **compatible `libstdc++`/glibc** — the host mixes GLIBCXX ABIs (old for Limbo, new for XRT);
    if the target OS/GCC differs from the build box this can break. Building on a box matching the
    target (or Geert's environment) is the safe path.
  - The **benchmark input files** (DEF/LEF/bookshelf) and any run config — these are read at runtime,
    not baked into the exe.
- **What travels inside the exe:** the Limbo parser libs are static (`.a` in `markv1/lib`), so they
  do NOT need to be on the target. Only the dynamic libs above do.
- **No emulation env on hw:** drop `XCL_EMULATION_MODE` / `emconfig.json` (those are sw_emu-only).
- **Risk ranking:** platform-shell mismatch > XRT version mismatch > libstdc++/glibc ABI mismatch.
  Mitigation if the target env is unknown: statically link libstdc++ (`-static-libstdc++
  -static-libgcc`) to shrink the runtime dependency to just XRT + the platform shell.

## Stage 6 (PL hardware optimization) — deferred, not abandoned
When PL work resumes (after A/B and hardware bring-up): re-tune λ/γ schedule for the 1024² grid,
re-enable fillers on the PL, fuse the 8-pass density solve + widen ports to 128-bit beats,
single-kernel iteration, BB α on the PL, backtracking if B concludes it's needed. Full detail was
in the prior checkpoint / `build_reports/stage5c.md` "Stage 6 plan".

## Stage 5c artifacts (for reference)
- **PL modules:** `pl/src/pl_algo/src/modules/{iteration_update,memory_writer,metrics}.hpp` (new),
  `top.cpp` (`iteration_step_df`, MODE_ITERATION_UPDATE/MODE_METRICS), `host_interface.hpp`
  (port aliasing), `formats.hpp`, `DATAFLOW.md`.
- **Host:** `host/src/pl_algo/src/{Placement.hpp, Driver.cpp (runPlacement), IterVerify, MetricsVerify,
  main.cpp}`. Flags: `--iter-update`, `--metrics`, `--place <bench> <xclbin> [iters]`
  (make `run-iter-update` / `run-metrics` / `run-place`).
- **Verified sw_emu:** all prior flags + `--iter-update` (3.28e-08), `--metrics` (~4e-09),
  `--place` (6-iter stable descent). Commits f81212b, b1bfd92, b5c2e75, edcc81a.

## Key references
- **markv1 CPU golden:** `host/src/markv1/src/AIEplace.cpp`, `Density.cpp`, `Partials.cpp`,
  `include/Node.h`; DSE harness `vck5000/dse.py`.
- **Source of truth:** Xplace `~/phd/Xplace/src/*.py`, DREAMPlace
  `~/phd/DREAMPlace/dreamplace/*`.
- **Auto-memory:** `pl_algo_stage5c`, `pl_algo_5c_algo_audit`, `pl_algo_force_gather`,
  `pl_algo_density_manager`, `architecture`, `pin_architecture`.

Stage 5c done, clean tree. Next active work: markv1 CPU placements (A) + optimizer review (B).
