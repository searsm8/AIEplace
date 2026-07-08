# MARK_TO_REVIEW — session artifacts (2026-07-07)

Artifacts from the session that (1) closed the last golden HPWL gap and (2) planned the PL
algorithm port. Newest-to-review at top.

## 1. `PL_PORT_PLAN.md` — detailed plan for porting the iteration algorithm to the PL
The main deliverable for task 2. Focuses on the three control modules you named:
`iteration_update`, `metrics`, and a **new `param_scheduler`** (the XPlace `param_scheduler.py`
analogue). Key points to look at:
- **§0** — the framing: the scheduler is scalar/cheap; its value on PL is *loop residency*, not
  speed. And a caveat worth your attention — the current PL schedule (`Placement.hpp`) is a
  **reduced stub** (missing markv1's tuned λ-trend, plateau jolt, and divergence guards), so the
  PL has *never* been run to convergence at golden quality. Porting the tuned schedule is the
  substance of the task.
- **§1** — a what-moves-what-stays table mapping every markv1 golden function to its PL home.
- **§3** — the `param_scheduler` design: persistent state struct, and each schedule formula
  (γ, λ-with-trend, Nesterov coeff, BB α, convergence) tied line-by-line to its markv1 source.
  Note the nice result that `density_force_fraction` has a **closed form** when precond is OFF
  (which it always is in the tuned config) — so we don't need the per-node precond pass.
- **§5** — a staged plan, each stage verified against the markv1 golden via a recorded-trace
  offline test before any sw_emu integration.
- **§7** — the concrete first PR.

## 2. `PERFORMANCE_SNAPSHOT.md` — full-suite scorecard (COMPLETE)
A snapshot of markv1 global placement vs XPlace's published HPWL across the **whole 28-benchmark
suite** (ISPD2005 + ISPD2015), each design at its XPlace grid, best defaults, seed 42, stop
masked-overflow 0.04.

**Result — markv1 GP ≈ XPlace across the board: mean ratio 1.014, median 1.020, 23/28 within ±5%,
18/28 within ±2%.** Range 0.93 (fft_b, a win) to 1.09 (bigblue4). The large ISPD2015 superblues
(500K–930K cells) land at 1.02–1.06 and adaptec2@1024 — the outlier we just fixed — is 1.05. The
weakest spots are the **bigblue family** at 1.07–1.09 (bigblue2@1024, bigblue3@2048, bigblue4@2048):
the largest designs at the finest grids. Note **bigblue3 did NOT converge** (floored at overflow
0.183, not 0.04), so its 1.08 is an unmatched-spread comparison, not a fair loss; pci_bridge32_a/b
also float high (0.28/0.32) — the known ISPD2015 low-density-target designs — and matrix_mult_a
early-stopped at overflow 0.198 in only 178 iters on its re-run (its 1.02 is likewise under-spread;
its siblings matrix_mult_b/c converged to 0.04 in ~900 iters, so this one merits a second look). The
remaining
0.04-converged bigblues (bigblue4 @0.0398 = 1.09) are the one real ~9% gap left, consistent with the
earlier "large grid" theme even after the γ fix — a candidate for future attention.

Context: markv1 is **global placement only** (no detailed placement / legalization), and the XPlace
reference is **post-DP**. So ~1.0 is effectively at-parity, and the residual is largely the DP gap
(checkpoint NEXT-STEP #3 — adopt an open-source detailed placer to close it and make the comparison
fully apples-to-apples).

*(All 28 designs present. mgc_matrix_mult_a crashed once during the oversubscribed peak and was
re-run standalone; on the re-run it early-stopped at 178 iters / overflow 0.198 — a divergence-guard
stop worth a follow-up, since matrix_mult_b/c converge cleanly.)*

## 3. `visualizations/` — per-benchmark placement + convergence artifacts
For a few representative designs (rendered from dedicated `visualize:true` runs; the sweep itself
runs headless). Each design folder has:
- `convergence_hpwl_overflow.png` — normalized HPWL (blue) and overflow (red) vs iteration. The
  **textbook ePlace signature**: HPWL collapses early as cells cluster, overflow rises as λ ramps
  and cells spread, then overflow decays to ~0 while HPWL ticks up slightly (the WL cost of legal
  spreading). Look at `mgc_fft_a` for the clean version.
- `final_placement.png` — final global placement. Red = fixed macros, blue = movable cells,
  yellow = a few highlighted nets. `mgc_fft_a` shows clean macro-aware spreading; `mgc_pci_bridge32_a`
  is a deliberately *hard* low-target-density design that spreads uniformly but floors above the
  0.04 stop (a realistic non-converging case, not a defect).
- `placement_animation.gif` — the full run animated (first 10 iters, then every 15th).
- `step_length_history.png` — the Barzilai-Borwein step length over the run.

Designs: `mgc_fft_a` (macro-dominated, clean), `mgc_pci_bridge32_a` (hard low-density),
`mgc_matrix_mult_b` (mid-size, converges — may still be rendering).

---

### What landed in git this session (branch `pl_algo`)
- `8ce73d2` — grid-independent WA γ (`gamma_ref_grid`), fixing the adaptec2@1024 outlier
  (1.20 → 1.05) with the tuned @512 suite left bit-identical. This was NEXT-STEP #1; the
  wirelength chase is now essentially done.
- `da06199` — checkpoint update.
(The full-suite sweep, the scorecard tool, and the PL plan are session outputs; see the commits
that follow for the tooling.)
