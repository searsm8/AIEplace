# NEW_ Handoff: investigate the macro-heavy spreading difficulty (adaptec5 / newblue4 / newblue5)

*Written 2026-07-27 as a session wrapped (context full). Self-contained; act without the prior chat.*

## Where we are (condensed)
Chasing XPlace-faithful sw_only GP on the MMS (movable-macro) suite. Recent arc:
1. **Field-faithful A/B** (`1_MARK_TO_REVIEW/NEW_mms_dct_ab_20260726.md`): `dct_normalize_inverse=false`
   (already default) beats legacy 16/16, mean −9.6% HPWL. Solid.
2. **Overflow-metric investigation** (`NEW_mms_overflow_faithfulness_20260726.md`,
   memory `overflow-metric-grid-faithfulness`): the overflow *formula/grid/target* are already faithful.
   The real issue was that sw_only's **convergence excluded filler density** while XPlace includes it.
3. **Fix validated but PARTIAL** (`2_ARTIFACTS/mms_fillconv_results.tsv`): setting
   `convergence_include_fillers=true` cleanly fixed newblue3 (−12.7%→−1.7% vs XPlace) and helped several,
   but **did NOT fix the hardest designs**, which is the open work below.

## THE OPEN PROBLEM (item 3): some macro-heavy designs genuinely won't spread
Even with the correct (filler-inclusive) convergence metric and running longer, these stay under-spread —
their *physical* (sharp/+filler) overflow stays high and HPWL sits far below XPlace (artificially low
because cells are still clumped):

| design | HPWL vs XPlace GP | sharp/+filler overflow | iters | note |
|---|---|---|---|---|
| adaptec5 | −22% (2.408e8 vs 3.098e8) | 0.144 | 730 | ran longer, didn't spread |
| newblue4 | −25% (1.721e8 vs 2.299e8) | 0.176 | 952 | ran longer, didn't spread |
| newblue5 | −10.6% (3.440e8 vs 3.846e8) | **0.613** | 646 | **DIVERGED** — restored a bad un-spread best |

(For contrast, newblue3 with the same flag spread fine: 345→640 iters, HPWL +12.5% to −1.7% of XPlace.)

## FIRST TASK: make GIFs of these hard benchmarks to SEE why they don't spread
Visual diagnosis worked great for newblue2 (`2_ARTIFACTS/newblue2_placement.gif` showed cells clumped in a
central band). Do the same for **adaptec5, newblue4, newblue5**. Watch for: cells stuck against macros,
clumped in one region, oscillating, fillers not pushing cells out, or a divergence blow-up (newblue5).

**Recipe (deterministic, seed 42; no rebuild needed):**
1. Config: copy `host/src/sw_only/run_config.json`, set `input.benchmark=host/benchmarks/mms/<design>`,
   `random_seed=42`, `dct_normalize_inverse=false`, **`visualize=true`**, and
   **`convergence_include_fillers=true`** (to see the current best behavior). (The `/tmp/mms_ab/configs/<d>_false.json`
   from this session are a starting point — just flip `visualize` to true and add the fillers flag.)
2. Run: `./build/hw/host/sw_only/aieplace_sw_only.exe <config>` — with viz on, `Output.cpp:504` auto-builds
   `<run_dir>/full_placement.gif` from per-iteration PNGs (frame every 10 iters, in `<run_dir>/placement/`).
   Output dir is printed as "Created output directory: results/single_runs/<design>/<ts>_cpu_cpu".
   **Viz is SLOW** (~50 s/frame cairo on big designs; adaptec5=843K, newblue4=646K, newblue5=1.23M nodes) —
   run in the background; each is ~1 hr.
3. Compact for viewing: `python3 tools/gif_builder.py <run_dir>/placement --resize 900 747 -d 120 -o 2_ARTIFACTS/<design>_placement.gif`
4. To send to Mark: copy to the Windows scratchpad first (`cp … /mnt/c/…/scratchpad/`) — `SendUserFile`
   rejects `\\wsl.localhost\…` UNC paths.

## Diagnosis leads (after seeing the GIFs)
Why would cells refuse to spread even at ~950 iters? Candidate causes to check:
- **Density force too weak** late in the run (λ / density_weight schedule) — cells don't get pushed apart.
- **Preconditioner over-damping** the movable macros or cells on these designs (precond `λ·area` term).
- **The √2 smoothing hides the hotspots** so GP thinks it's done — see the **Smoothing-schedule idea** in
  `0_TODO/TODO.md` → `# Improvements` (ramp the footprint inflation from √2 down to 1 over the run so late
  convergence tracks the true physical density). Strong candidate; don't implement before diagnosing.
- **newblue5 divergence** is its own bug — the divergence guard restored an early, un-spread "best". Trace
  `checkConvergence`/best-solution logic on that run.

## Also queued (all in `0_TODO/TODO.md #4`, don't lose these)
- Adopt `convergence_include_fillers=true` as the run_config default (XPlace-faithful).
- Make `include_fillers` **always true** (convergence + final report) — XPlace always counts fillers.
- Report the **exact (sharp) + filler** overflow as the headline (Output.cpp:396 → `computeOverflow(false,nullptr,true)`)
  so our number is directly XPlace-comparable.
- Rename `clamp` → `smooth` in `computeOverflow` (verbiage; "smoothed" is the intuitive term).

## Key references
- Code: `computeOverflow` (`Density.cpp:426`); convergence signal (`Output.cpp:697-702`); `[OVFW-DIAG]`
  line (`Output.cpp:404-407`) prints all four {smooth,sharp}×{±filler} numbers per run.
- XPlace: `evaluator.py:26` (`get_obj_overflow`, exact+filler), `run_placement_nesterov.py:41` (`overflow_fn`).
- XPlace GP baseline: `~/aieplace_tmp/xplace_mms_reference.md`. Re-measure overflow via
  `2_ARTIFACTS/run_xplace_overflow.sh`.
- Memories: `overflow-metric-grid-faithfulness`, `mms-faithful-field-ab-result`, `document-decisions-not-just-code`.
- The MMS runners are resumable: `2_ARTIFACTS/run_mms_{ab,fillconv}.sh`.
