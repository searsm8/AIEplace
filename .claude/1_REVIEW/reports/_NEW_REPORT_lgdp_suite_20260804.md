# Full-pipeline evaluation (GP → LG → DP) — TODO #3, first complete pass

**Date:** 2026-08-04. Pushes sw_only's phase-2 global placements through XPlace's own legalizer and
detailed placer, so the headline number is **post-DP HPWL against XPlace's post-DP HPWL** rather
than GP-vs-GP.

**Inputs:** the 15 MMS GP results from the 2026-08-02 phase-2 suite
(`2_ARTIFACTS/phase2_suite_results.tsv`), reused as-is — no placement was re-run for this.
**Harness:** `2_ARTIFACTS/gen_lgdp_inputs.py` → `run_lgdp_suite.sh` → `analyze_lgdp_suite.py`.
**Raw data:** `2_ARTIFACTS/lgdp_suite_results.tsv`, logs in `/tmp/lgdp/logs/`.

---

## 1. The XPlace reference already existed — no new XPlace runs were needed

`NEW_REPORT_phase2_mms_suite_20260802.md` §5 says a real "our phase 2 vs XPlace phase 2" number
"needs 15 more local XPlace runs". **That is wrong.** The `2026-07-17-23:0x_*` batch under
`~/phd/Xplace/result/` is `--dataset mms --mixed_size True --seed 42`, and each log carries the
entire flow, not just Mixed-GP:

```
After Mixed-GP, best solution eval, exact HPWL: 1.534520E+08 exact Overflow: 0.1247   <- phase 1
Start running Macro Legalization... #Macros: 58, #MovMacros: 58.
GP Stop! #Iters 1554 masked_hpwl: 1.543190E+08 overflow: 0.0466                       <- phase 2
After GP, best solution eval, exact HPWL: 1.544092E+08 exact Overflow: 0.1232
***** Finish Legalization, HPWL: 1.613617E+08 *****                                   <- LG
After DP, HPWL: 1.590915E+08                                                          <- DP
```

Now recorded as `_XPLACE_MMS_FINAL` in `tools/benchmarks.py` and surfaced in `BENCHMARKS.md`.

**A second correction falls out of the same lines.** `GP Stop!` prints the **masked (smoothed)**
overflow; the **exact** value is on the following line. XPlace's exact post-GP overflow is
**0.106–0.184**, not the 0.042–0.094 that `GP Stop!` shows. Comparisons in the notes that put our
exact overflow next to XPlace's `GP Stop!` figure overstate the gap by 2–3×.

## 2. Mechanics — how a sw_only GP result goes through XPlace's legalizer

Stock XPlace, no source change. `--global_placement False` takes the placement as given
(`run_placement_nesterov.py:15-25`) and hands it to the same `detail_placement_main` a full run
uses, so LG and DP are bit-for-bit the same code path as the reference numbers above.
(`tools/legalize_swonly_mms.sh`'s header claim that this needs "the XPlace skip-GP + mixed_size
macro-LG branch" is stale — there is no such branch in the tree and none is required.)

```bash
python3 2_ARTIFACTS/gen_lgdp_inputs.py
nohup bash 2_ARTIFACTS/run_lgdp_suite.sh > /tmp/lgdp/runner.log 2>&1 &
python3 2_ARTIFACTS/analyze_lgdp_suite.py
```

Step 1 patches each design's original bookshelf `.pl` with the coordinates from sw_only's
`RowBasedPlacement.def` (`tools/def_to_bookshelf_pl.py`). This transfers with **no scaling**, for
two reasons worth stating because both are easy to get wrong:

- sw_only writes the DEF back in the *original benchmark frame* — `DataBase::writeDieArea` and
  `writeComponents` both add `m_die_shift` back on.
- `Node::node_pos` is the cell's **lower-left** corner, which is bookshelf `.pl` semantics.
  (`Grid.cpp:38` forms the footprint as `pos + size/2 - clamped/2`, i.e. it constructs the centre
  from the position — so the position is not itself the centre.)

Step 2 runs, per design:

```bash
python main.py --dataset mms --design_name <d> --load_from_raw True --mixed_size True \
  --global_placement False --given_solution /tmp/lgdp/pl/<d>.pl --num_threads 8 --seed 42
```

**The round-trip is verified, not assumed:** XPlace's `Input solution, exact HPWL` reproduces
sw_only's own reported final HPWL on every design (adaptec1 6.385415E+07 vs our 6.385e+07,
adaptec3 1.576476E+08 vs 1.576e+08, …).

**Both sides are UNMASKED (all nets)** — and this is a second `GP Stop!`-style trap, so it is worth
stating precisely. XPlace has two HPWL functions and only one masks: `fast_evaluator` uses
`masked_scale_hpwl(..., data.net_mask, ...)` and produces the per-iteration `masked_hpwl:` lines
*including the one printed inside `GP Stop!`*, while `get_obj_hpwl` → `get_hpwl` calls
`hpwl_cuda.hpwl` with **no mask** and produces every number used here (`exact HPWL`,
`After DP, HPWL`). The matching sw_only column is therefore **"Final HPWL (exact, all nets)"**
(`final_hpwl_exact`, `Output.cpp:511`), which is what `phase2_suite_results.tsv` and this suite
both use — not "Final HPWL", which is masked at `ignore_net_degree = 100`. On MMS adaptec1 the two
differ by 0.06% (2 nets out of 221142).

Whole suite: **9.5 minutes of GPU time for 15 designs.**

## 3. Results

| design | our GP | our LG | our DP | XPlace DP | ΔDP vs XPlace | our LG cost | XPlace LG cost |
|---|---|---|---|---|---|---|---|
| adaptec1 | 6.385e+07 | 7.047e+07 | 6.782e+07 | 6.814e+07 | **−0.47%** | +10.4% | +8.6% |
| adaptec2 | 7.320e+07 | 7.931e+07 | 7.692e+07 | 7.618e+07 | +0.97% | +8.4% | +6.9% |
| adaptec3 | 1.576e+08 | — | — | — | **crashed, see §4** | — | — |
| adaptec4 | 1.445e+08 | 1.489e+08 | 1.465e+08 | 1.414e+08 | +3.60% | +3.0% | +4.9% |
| adaptec5 | 3.021e+08 | 3.042e+08 | 2.900e+08 | 3.131e+08 | −7.36% ⚠ | +0.7% | +1.6% |
| newblue5 | 3.929e+08 | 3.985e+08 | 3.955e+08 | 3.899e+08 | +1.44% | +1.4% | +2.0% |
| bigblue1 | 8.445e+07 | 8.719e+07 | 8.610e+07 | 8.567e+07 | +0.49% | +3.2% | +3.8% |
| bigblue2 | 1.246e+08 | 1.294e+08 | 1.272e+08 | 1.257e+08 | +1.21% | +3.8% | +4.4% |
| bigblue3 | 2.724e+08 | 2.903e+08 | 2.819e+08 | 2.767e+08 | +1.89% | +6.6% | +7.7% |
| bigblue4 | 6.595e+08 | 6.765e+08 | 6.671e+08 | 6.464e+08 | +3.20% | +2.6% | +4.2% |
| newblue1 | 5.895e+07 | 6.137e+07 | 6.067e+07 | 6.005e+07 | +1.03% | +4.1% | +4.3% |
| newblue2 | 1.490e+08 | 1.529e+08 | 1.516e+08 | 1.524e+08 | **−0.54%** | +2.6% | +3.5% |
| newblue3 | 2.638e+08 | 2.668e+08 | 2.651e+08 | 2.727e+08 | **−2.78%** | +1.1% | +1.7% |
| newblue4 | 2.338e+08 | 2.356e+08 | 2.340e+08 | 2.298e+08 | +1.80% | +0.8% | +1.0% |
| newblue6 | 4.217e+08 | 4.253e+08 | 4.200e+08 | 4.083e+08 | +2.85% | +0.8% | +1.9% |
| newblue7 | 8.950e+08 | 9.057e+08 | 8.953e+08 | 8.803e+08 | +1.70% | +1.2% | +2.3% |

**Mean +1.17% post-DP HPWL vs XPlace over the 14 clean designs** (excluding adaptec3, which
crashed, and adaptec5, whose win is not like-for-like — see §4). Median +1.21%, worst +3.60%.

Two secondary findings:

- **Our phase-2 macro placement survives XPlace's own macro legalizer.** All 15 report `Check Pass
  in Macro Legalization`, and 14 of 15 at **zero displacement** — XPlace's checker accepts the
  macro positions `placer/MacroLegalize.cpp` produced without moving them. Only adaptec5 needed a
  nudge (displacement 84.0, on macros our phase 1 never legalized because it diverged). This is
  independent third-party validation of the LP legalizer.
- **We pay slightly more HPWL in legalization on the two designs where we start lowest**
  (adaptec1 +10.4% vs XPlace's +8.6%, adaptec2 +8.4% vs +6.9%) and slightly *less* on the other
  eleven. Consistent with our GP ending marginally less spread on the adaptecs and the LG absorbing
  the difference — which is exactly the effect that makes GP-vs-GP the wrong comparison.

## 3b. Post-DP density — and adaptec5's "win" is confirmed as bought

`tools/post_dp_density.py` computes the density of the **legalized** result. Both sides are run
through the same implementation, over each tool's own written `placement_<design>_dp.pl`, so the
unresolved overflow-definition gap in §4 cannot contaminate the comparison — any quirk applies
equally and cancels.

| design | td | our overflow | XPlace overflow | Δ |
|---|---|---|---|---|
| adaptec5 | 0.5 | 0.4364 | 0.3828 | **+14.0%** ⚠ |
| newblue1 | 0.8 | 0.1433 | 0.1883 | **−23.9%** |
| newblue2 | 0.9 | 0.0874 | 0.0909 | −3.9% |
| newblue3 | 0.8 | 0.1908 | 0.1965 | −2.9% |
| newblue4 | 0.5 | 0.3332 | 0.3332 | +0.0% |
| newblue5 | 0.5 | 0.3255 | 0.3246 | +0.3% |
| newblue6 | 0.8 | 0.1401 | 0.1424 | −1.6% |
| newblue7 | 0.8 | 0.1550 | 0.1565 | −0.9% |

**7 of 8 match or beat XPlace on post-DP density; the single exception is adaptec5.** So the
suspicion in the previous version of this report is now measured, not hypothesised: **adaptec5
buys its −7.4% HPWL with +14.0% overflow**, and its row should be excluded from the headline.
Every other design's HPWL delta stands on a density that is as good as XPlace's or better —
newblue1 notably lands −23.9% overflow for +1.03% HPWL.

`max_util` is exactly 1.000 for both tools on all 8, which is the legality check passing:
non-overlapping cells cannot exceed the area of their bin, so anything above 1.0 would mean the
"legalized" placement still overlaps.

**Two structural limits, both documented in the module and neither fixable by a better statistic:**

- **At `target_density = 1.0` post-DP overflow is identically zero**, because legalization caps
  bin occupancy at 1.0 and the capacity *is* 1.0. It carries no information on the other 8 MMS
  designs — there, legalization answers the density question by itself and HPWL is the whole story.
- **A "top 5% bin utilisation" congestion proxy was tried and dropped.** It reads exactly 1.000
  for both tools on every design, at the GP grid *and* at a coarse 64×64 grid, because the busiest
  bins are movable-macro interiors, which are 100% occupied by definition. It measures macro
  presence, not placement quality. (XPlace's own `top5overflow` comes from ntuplace3 via
  `eval_by_external` and is not produced by these runs.)

## 4. What is not trustworthy yet

**adaptec3 segfaults inside XPlace's `gpudp.greedyLegalization`** — reproducible 3/3, the only
failure in 15, and inside XPlace's compiled CUDA op rather than our code. Investigated:

- Crash is after `Check Pass in Macro Legalization`, at `Start running Greedy Legalization`.
- **Ruled out — the ragged-core hypothesis.** adaptec3's core is a staircase (rows carry their own
  `SubrowOrigin`/`NumSites`) and our placement leaves 315 cells outside their row's span, far more
  than any other design (next worst: newblue4 25). Clamping all 315 into their spans and re-running
  **still segfaults, in the same place** — so this is not the cause.
- **Untested leads:** adaptec3 is the only MMS design with **zero fixed nodes** (`#Fix = 0`), and in
  skip-GP mode XPlace creates fillers (1.81M here) but never places them. Filler count alone does
  not explain it — adaptec4 (2.58M) and bigblue4 (3.39M) both pass. The clean discriminator is to
  feed XPlace its *own* 07-17 phase-2 output back through the same skip-GP path: if that crashes
  too, the bug is in the skip-GP path, not in our placement.

**The overflow XPlace reports on our given solution does not reconcile with ours.** XPlace reads
adaptec1 0.0484 / adaptec3 0.0190 where we report macro-excluded 0.109 / 0.071 on the same
placement at the same 1024 grid — a consistent 2–4×, direction unexplained. HPWL round-trips
exactly, so the transfer itself is sound; the discrepancy is in what each side counts. Treat
`gp_ovfl_in` in the TSV as a diagnostic until this is closed.

**newblue5 was re-run, not reused** — its 08-01 GP predates the 2026-08-02 phase-2 convergence
change. The fresh run converges at 1486 iterations (phase 1 ending at 685), and its row is
included above: +1.44% HPWL and +0.3% density vs XPlace.

## 5. Provenance — and a correction to it

Re-running adaptec1 today reproduces the 2026-08-02 suite row exactly (1373 iterations, phase 1
ending at 613 / 6.357e+07 / 0.12, final exact HPWL 6.385e+07). An earlier version of this section
concluded from that "the current tree is the same code state that produced all 15 DEFs".
**That conclusion was wrong, and adaptec1 was the one design that could not detect it.**

The working tree makes `macro_deposits_target_density` unconditional (TODO #8/#11b, landed
2026-08-02 *after* this suite ran; the suite report records it ran with the flag `false`). That
branch is gated on `target_density < 1.0`, so it is a no-op on adaptec1 and on every other td = 1.0
design — and a large change on the other eight. Re-running adaptec5 on the current tree gives an
identical HPWL at iteration 1 (1.463e+08, so the placement path is untouched) but a different
overflow (0.5656 vs 0.8980), and the trajectories separate from there.

What this does and does not affect:

- **The 8 td = 1.0 rows** (adaptec1-4, bigblue1-4) are on the current code state. Unaffected.
- **The 7 td < 1.0 rows** (adaptec5, newblue1-4, 6, 7) were produced by a **superseded** code state.
  Their LG/DP numbers are still a valid legal-vs-legal comparison *of those placements* against
  XPlace — the pipeline and the XPlace reference are unchanged — but they are not the placements
  the current code produces.
- **newblue5 is the odd one out**: it was re-run today, so it is on the *current* tree while the
  other six td < 1.0 rows are not. It is comparable to XPlace but **not to its neighbours in the
  table**.

A full 16-design re-baseline on the current tree is running (`/tmp/rebase`, binary md5
`894bbaf9…`); the td < 1.0 half of the table should be regenerated from it.
