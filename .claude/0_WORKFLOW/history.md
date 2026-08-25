# Completed Tasks — History

Cross-session task archive. A task moves here when every one of its items is done — see the
repo-root `CLAUDE.md`, "Keep tasks.md from bloating". Entries are **frozen at archive time**:
if a later task overturns one, add a retraction banner at its top rather than editing the body,
so the reasoning stays readable and the reversal is impossible to miss. See `tasks.md` for active
work.

> **Naming note, 2026-08-07.** The active file was renamed `TODO.md` → `tasks.md` and `TLDR.md` →
> `summary.md`. Only these header pointers were updated. **Every archived entry below still says
> `TODO.md`, and that is deliberate** — those are frozen records of what was written at the time,
> and rewriting them would be exactly the hard edit this file's own rule forbids.

**Archived 2026-08-07:** #2, #4, #8, #12, #13, #16, #18 (and #5's duplicate copy in TODO.md
removed — it had been sitting in both files since 2026-07-30).

**Compacted 2026-08-07:** the full pre-compaction text of the still-OPEN tasks (#1, #3, #6, #7, #9, #10, #11, #14, #15, #17, #19, #20, #21, #22, #23, Parked, Improvements) - first section below.

**Closed 2026-08-25:** **#35** — the MMS regression was root-caused to `#3`'s XPlace-faithful fixed-density scale (`min(ρ,1)·td`) and **fixed by landing experiment "D"**: the formula is reverted to the cap `min(ρ,td)` in all four sites (`Grid::clampFixedDensity` canonical, `Density.cpp::computeOverflow`, `density_bin.hpp`, `density_bin_model.cpp`). Experiment D (`DSE_20260824_161319`, formula-only, else HEAD, source reverted after) isolated `#3` cleanly: **D vs HEAD = −2.38 pp of MMS mean** (1.0347 → 1.0110), entirely on the 8 td<1 macro/mixed-size designs (adaptec5 −14.57 pp, newblue1 −8.47), every td=1 design flat because the two formulas are identical at td=1; D (1.0110) beats even pre-`#3` A (1.0161). **This is a DELIBERATE divergence from XPlace** — Mark-authorized 2026-08-25, overriding `CLAUDE.md`'s prefer-XPlace rule — registered in `CLAUDE.md`'s new "Deliberate divergences from XPlace" section and named as such in all four code comments so it is not reverted-to-faithful by accident. Regress baselines regenerated (`mgc_fft_a`, `mgc_pci_bridge32_b`; reason recorded in-file; reproduces D bit-for-bit, `pci_bridge32_b` sha `43fa7e73a889`); td=1 `mms_adaptec1` bit-identical at 1274 iters, verifying the no-op-at-td=1 claim. The handoff's leads 1–2 died in static reads (macro deposit weight is already 1.0 for macro-sized nodes; filler rebuild at the phase boundary is idempotent — `computeAreaBreakdown` holds `addFillers`'s inputs invariant). The **not-taken** alternative: keep the faithful scale and fix the fixed-node √2 field-inflation divergence instead — recorded in the entry, the lead if MMS returns to focus. Commits `24c500b` (record) + `271d024` (land). Physical reading: a frozen macro actually occupies its area, so scaling its contribution by td tells the optimizer there is room where there is none; the cap is conservative on macro-heavy designs, which is why the same faithful scale *helps* low-td ISPD (no frozen-macro/phase-2 path) and *hurts* MMS.

**Closed 2026-08-25:** **#34** — the MMS regression's metric-consistency hypothesis, opened when `Placer::computeOverflow` was found still computing the pre-`#3` cap under a comment claiming it mirrored `clampFixedDensity` (a THIRD stale copy, `density_bin.hpp`, was also found). **Hypothesis falsified**: the metric-consistency fix (`02464d0`, all four copies made to agree at `min(ρ,1)·td`) closed the ISPD gap (mean 1.0126 → 1.0112) but moved MMS by only 0.04 pp — so the stale metric was NOT driving the MMS regression. That handed the real cause to **#35** (the field formula itself), now closed by landing D. The "DECISION FOR MARK — do not freeze MMS at 1.0351" item resolved exactly as its option (b) predicted: revert `#3`, and 7a/7b alone leaves MMS better than 2026-08-14 (D 1.0110 < 1.0161). The one residual — **`make test` reproduces `density_bin.hpp` in `density_bin_model.cpp` rather than `#include`-ing it, so it verifies two reference copies, not the shipped header** (which is how the module silently kept the old formula for four days) — is NOT dropped: it is **#20 step 3** (*"density_bin — include the real header; delete density_bin_model's own stale copy"*), where it already lived. The tier-3 spot-check also closed here (16/16 `XPlace DP HPWL` match `benchmarks._XPLACE_MMS_FINAL`). Retraction note for the trail: this entry's early text argued the metric inconsistency was "live inside the optimizer"; true, but measured worth 0.04 pp — the fix was kept on its own terms (three copies disagreeing is a bug), not for MMS.

**Closed 2026-08-25:** **#17** — sw_only regression tripwire (`vck5000/test/regress/`, `make test-regress`): per design, exact no-tolerance assert on both `iterations.dat` (row-for-row) and the output `.def` sha256, with `random_seed = 42` pinned. Both items done. The originally-scoped reference-number-plus-tolerance harness was **deliberately not built**: the 2026-08-17 sw_only freeze inverts the requirement — a bit-identical trajectory + position hash is a *stronger* guard than any XPlace-ratio tolerance, admitting no drift at all, with the ratio pinned instead by the committed 28-design headline. Also landed a `readDEF()` diagnostic that names the accepted DEF filename and what it found (the hardcoding itself unchanged; control flow bit-identical). ⚠️ **Reopen if the freeze lifts** — the quality-vs-stability gap it named returns the moment sw_only behaviour is allowed to move again; the coverage blind spot that persists under the freeze is #23's "add one large design". (This entry has since seen the freeze *deliberately* moved once — #35's Mark-authorized D landing — which regenerated two baselines through the tripwire's own `--update-baselines` path; the tripwire did its job, catching the change and demanding a reason.)

**Closed 2026-08-25:** **#3** — Tooling & evaluation workflow, a long-running task: full GP→LG→DP evaluation built and measured; `run-benchmark` + `viz-gif` skills landed (a third, `xplace-compare`, was benchmarked at a dead tie and scrapped — lesson in this file); `dse.py --resume`; adaptec3's XPlace-legalizer segfault root-caused to our own harness (`gen_lgdp_inputs.py` hardcoded the `.pl` template instead of reading the one the `.aux` names) and fixed for all 16; the 3-column overflow tooling (`Best OVFW` / `Our Exact OVFW` / `XPlace In OVFW`) that closed the `gp_ovfl_in` reconciliation via #31. The XPlace-reference traps it accumulated stay in this file (a result dir isn't a reference until you check its argv AND that it reached `After DP`; `gp_ovfl_in` is macro-included; newblue4 is build-sensitive at ~1%). One item **tabled, not dropped**: *"sw_only has no per-row site model"* — 11 of 16 MMS designs have ragged (staircase) cores, `enforceDieBoundaries` clamps to the die rectangle, and `check_row_spans.py` finds cells parked in the notch (adaptec3 315, worst overhang 4122 DBU); moved to **Improvements** as "respect the row site model, minimize overhang, measure the difference", gated on first measuring XPlace's own overhang (its GP makes the same rectangular assumption, so it may be a win to take rather than a faithfulness gap). Not a #35 lead: adaptec3, the worst offender, is td=1 and flat.

**Closed 2026-08-17:** **#24** — best-solution tracking rebuilt to match XPlace's `get_best_solution`: three trackers (`best_primary`/`best_aux`/`best_rollback`), each with its own geometry buffer in `Node` (a single shared buffer meant last-writer-won, so the log's provenance line named a placement that wasn't the one shipped — confirmed on 17 of 29 `full44_v2` runs, all converged), plus a torn-restore fix (`syncProbeToCommitted()`) so reported overflow describes the shipped placement instead of the last iteration. All three test suites green, 3 baselines regenerated, MMS suite re-run (16/16). Closed with two remaining faithfulness gaps and an A/B that needs widening spun off to **#32**, rather than reopening this ticket for follow-on work its own defects didn't block.

**Closed 2026-08-17:** **#32** — best-solution tracking made faithful to XPlace, and the aux-ratio question settled. **7a**: we now snapshot and measure HPWL on the lookahead `v_k` (`probe_pos`) rather than the committed `u`, via a new `at_probe` argument threaded through `computeTotalWirelength`/`computeWirelength_HPWL`, so HPWL, overflow and the stored solution describe ONE position — XPlace has a single position variable (`p` IS `v_k`). **7b**: `BEST_SOL_MIN_ITER` is phase-relative (`phaseIteration()`), matching `param_scheduler.py:393`; `Schedule.cpp` already did this everywhere and `Output.cpp` was the lone holdout. `syncProbeToCommitted()` deleted, folded into `restoreBestPlacement()` (restores BOTH halves) after an A/B showed the perturbation its comment claimed was **bit-exact identical** — that claim is retracted. **A/B**: 28 designs x 2 arms (`DSE_20260818_113716`, 56/56, 159.6 min) — **keep 1.005**, but the real finding is that the knob binds on **1 design of 28**, so the effective n is 1 and widening the design set cannot help. Where it binds (`mgc_des_perf_a`) 1.005 wins by 0.71 pp post-DP, and **DP amplified the penalty rather than absorbing it**, reversing report §5's "spread legalizes better". The unswept ACCEPT budget (a second, hardcoded 1.005) spun off to **#33**. Cost of 7a/7b+#3 together: +0.13 pp of suite mean, accepted per the prefer-XPlace rule.

**Closed 2026-08-17:** **#14** — zoomable visualizer finished, all three remaining items. The MMS zoom path ran end-to-end on newblue1 and `MIN_SIZE` was cleared: at zoom it floors **0%** of std cells, fillers and macros, and the only nodes it does floor are 337 **zero-area** bookshelf terminals (`w == h == 0`), where flooring is the desired behaviour. **Node-lock** landed (`--lock <name>|index:N|most-moved`): the window re-centres on one tracked cell every frame, verified by assertion at **0.0000 px** from the reticle across all 31 frames and all three generations. That needed a dump change — `PositionDump.cpp` now writes a sparse per-generation `names_gen<N>.txt`, because `freezeMovableMacros()` + `rebuildFillers()` reshuffle indices at the phase-2 boundary and the name is the only identifier that survives it (placement bit-identical, `make test-regress` unchanged). **Multi-view** landed (`--add-view`, repeatable): N windows in one pass, decoding each frame once, verified byte-identical to N separate invocations. Builds on #16, which moved rendering out of the placer.

**Closed 2026-08-16:** **#31** — the `Best OVFW > 0.1` stalls AND the ~7x overflow-vs-XPlace gap were one root cause: XPlace caps `num_bin` at `num_rows` (`database.py:161`) but sw_only applied its identical `row_cap` only on the auto grid path, so `dse --grid xplace`'s explicit 512 bypassed it — 13 of 20 ISPD2015 designs ran finer than XPlace. Fixed by capping any grid at `num_rows` (`Setup.cpp`, Mark's call over a manifest patch); headline 1.0149 -> **1.0113 mean / 1.0096 median**, GP-ratio mean 1.0223 -> 1.0047, and the overflow *signal* (which drives the gamma/lambda schedule) now matches XPlace on the std-cell designs. An independent naive reference proved `computeOverflow` correct at every grid — it was never a metric bug. Also landed the 3-column overflow tooling (`Best OVFW` / `Our Exact OVFW` / `XPlace In OVFW`), closing #3's `gp_ovfl_in` reconciliation. Residual macro fixed-density difference -> #3; `node_pos`-deposit question -> tasks.md Topics.

**Closed 2026-08-16:** **#25** — RETRACTED, not resolved: the premise was false. XPlace does NOT force `target_density = 1.0` on ISPD2015 — its `setup_dataset.py` sets the same per-design td we do (0 mismatches on all 20). The `target_density: 1.0` in XPlace's log is a params-dict echo; the effective value (`target density = 0.65`, `database.py:839`) matches ours. The overflow ratios the entry cited were real but were the td<1 grid divergence (#31), measured against a mislabeled column. Guard against re-deriving: grep `setup_dataset.py` for the `mgc_` branches.

**Closed 2026-08-07:** **#7** — both items resolved. Item 1 (κ scheduling) turned out to be already
implemented, on the wrong quantity, and was closed against #19b. Item 2 (Xplace-Route detailed
routability) is **out of scope** — Mark's call.

**Closed 2026-08-11:** **#27** — `mgc_matrix_mult_a`, the suite's 3.27x outlier, was a stray space in its `placement.constraints`, parsed as target density 60 instead of 0.6 (29.8M fillers, 20x the die area). One byte; ratio now 1.0171 and the suite mean is quotable for the first time. Not an algorithm defect — and its apparent +7.71% response to #26 was noise.

**Closed 2026-08-11:** **#22** — the 8 ISPD2015 designs with no XPlace reference. Resolved by **#26**, and neither of this entry's two options was the answer: `ispd2015_fix` is *generated* by XPlace's own `data/fix_ispd2015_route.py`, not downloaded and not hand-built. Its construction-validation demand was honoured anyway (the regenerated `mgc_pci_bridge32_b` DEF is byte-identical), and its standing caveat — that design's reference being `_fix` while sw_only places the region-bearing DEF — is now measured and void: sw_only produces a bit-identical placement from either variant. The ISPD suite is 28 of 28.

**Closed 2026-08-12:** **#26** — fence regions. The 9 ISPD2015 designs carrying DEF `REGIONS`/`GROUPS` are now scored (suite: 19 -> **28 of 28**, median 1.0106) against XPlace references generated from `ispd2015_fix`, which turned out to be **built locally by XPlace's own `data/fix_ispd2015_route.py`** rather than downloaded — closing #22 as well. The other half is a correctness finding: we place **59-94%** of those designs' fence-constrained cells outside their region (controlled against the contest's own legal solutions, 0 of 190,010 outside), and ~10 percentage points of our margin there is the missing constraint rather than the placer. **Mark's decision: do NOT implement fence regions — document that we ignore them, as XPlace does.** That decision lives in `CLAUDE.md`, because an archived entry like this one is not loaded next session.

**Closed 2026-08-14:** **#29** — the XPlace GP reference (N/A on 22 of 28) is fixed, and by Mark's
redirection it lives in **dse.py from `benchmarks.py`**, not the placer: the exe now writes only raw
measured columns and `Placer::lookupXplaceReferenceHPWL`'s hardcoded 6-entry map is gone.
`benchmarks.py` gained `_XPLACE_GP_MASKED` (all 28, recovered from `/tmp/xref/logs` + the TSV) and
`xplace_gp_masked_in_sw_frame()`; dse.py enriches results.csv with `XPlace GP HPWL` + `GP Ratio`,
**masked-paired** (not the exact number — they differ 7.9% on superblue12) and **site-width-correct**
(200 mgc_*, 100 superblue). Same enrichment carries the DP columns, so GP and DP comparisons share one
file (part of #30). The "ratio next to an unconverged overflow" sub-item is addressed as a footer
caveat ("meaningful only where Best OVFW converged"); a per-row gate stays deferred to #3. This also
closed the "two tables that can disagree" duplication the old map's own doc-block flagged.

**Closed 2026-08-12:** **#28** — `dse.py` is now the single launch point for multi-benchmark runs,
846 → 371 lines. `DSE_RUN_SET`/`MORRIS_RUNSET` env vars, the nine `_RUN_SETS` functions, the
entirely-commented-out `dse_sweep` dict and its Cartesian machinery, two dead functions
(`modify_config_parameter`, `run_AIEplace`) and the Popen worker pool for `MAX_PARALLEL=1` are all
gone, replaced by `--designs / --set / --grid / --runset / --resume / --dry-run`. Both surveyed
defects are fixed: result columns are now a positive list that warns on an `Output.cpp` schema
change (the denylist had been silently blanking `Best HPWL` on every row), and grid +
`target_density` come from `benchmarks.py` instead of a duplicate table. Every sweep now writes
`sweep.json` — the manifest of exactly what was launched — which is also what makes `--resume`
label-keyed and what the summary joins swept parameters from. The two live defects it *found* and
did not fix are now **#29** (the XPlace reference belongs in the placer, masked-vs-masked and
site-width-correct) and **#30** (LG+DP inside `dse.py`).

---

## #32 — Best-solution tracking: XPlace divergences + the aux-ratio A/B (opened 2026-08-17, CLOSED 2026-08-17)

**Spun off #24 at close.** #24 fixed the shared-buffer and torn-restore defects and made the
*selection rule* faithful to XPlace's `get_best_solution`. Auditing that closure ("is it faithful
now?") turned up two things the rule itself doesn't reach, plus unfinished confidence on a value
the rule depends on. None of these are regressions — #24's fixes are correct as far as they go.

- [x] **DONE 2026-08-17 — we now track on the lookahead v_k, as XPlace does (7a).** Mark's call:
      adopt XPlace's position. `snapshotBestPlacement()` stores `next.probe_pos`, and
      `recordIterationResults()` measures HPWL at the probe via a new `at_probe` argument threaded
      through `computeTotalWirelength` → `computeWirelength` → `computeWirelength_HPWL`
      (`host/src/common/`, defaults false so the pl_algo host is unaffected). HPWL, overflow and
      the stored solution now describe **one** position, which is XPlace's whole structure: `p` IS
      `v_k` (`nesterov_optimizer.py:71`) and `evaluator_fn(mov_node_pos)` measures both metrics
      there (`run_placement_nesterov.py:142-145`). Before, we stored u, measured HPWL at u and
      overflow at v — a pair no iteration ever held.
      ⚠️ **This is a behavior change, not a bug fix, and it moves every number.** All three regress
      baselines regenerated. On `mms_adaptec1` (frozen regress config): **1259 → 1274 iterations,
      HPWL 6.366e7 → 6.344e7 (−0.35%), smoothed overflow 0.0405 → 0.0453**.
      **The 28-design suite has NOT been re-scored** — summary.md's 1.0096/1.0113 headline predates
      this and is now stale. → next action: `make dse`.
- [x] **DONE 2026-08-17 — `BEST_SOL_MIN_ITER` is now phase-relative (7b).** `Output.cpp` gates on
      `phaseIteration()`, not absolute `iteration`, matching `param_scheduler.py:393` (XPlace resets
      `init_iter` at every optimizer restart). `phaseIteration()` was already the established
      analogue everywhere in `Schedule.cpp` — including `past_warmup = phaseIteration() >= 50` at
      `Schedule.cpp:39`, which mirrors `param_scheduler.py:286` — so this call site was the lone
      holdout. Phase 2 now gets the same 50-iteration settling window phase 1 does.
- [x] **DONE 2026-08-17 — `syncProbeToCommitted()` deleted; folded into `restoreBestPlacement()`,
      which now restores BOTH halves of the (node_pos, probe_pos) pair.** Mark asked what it was
      for; the answer is that a restore wrote only `node_pos` while every density/overflow metric
      reads `probe_pos`, so the reported overflow described the last iteration rather than the
      shipped placement (#24 defect 2). 7a does **not** retire that — it changes which value is
      torn, not whether it is torn.
      **The comment blocking the fold was wrong.** It claimed folding perturbs the phase-2 restart
      (`mms_adaptec1: 1325 → 1288 iterations, HPWL +0.24%`). Measured A/B on the frozen config,
      isolating the fold from 7a/7b: **bit-exact identical** — trajectory row-for-row and the same
      `.def` sha256 (`91cbbdee0d59`), on all three regress designs. Mechanism: the phase-2 restore
      is immediately followed by `freezeMovableMacros()` (collapses macro state onto `node_pos`,
      `DataBase.cpp:421`) and `reinitializeStdCells()` (re-seeds everything else), so the
      `probe_pos` write is overwritten before anything reads it. At the *final* restore the fold is
      equivalent by construction. Verified the path actually ran (no "no best placement recorded"
      warning, phase 1 = 654 iterations).
- [x] **DONE 2026-08-17 — A/B settled on the full suite: KEEP 1.005 (XPlace's literal).**
      `DSE_20260818_113716`, all 28 designs x 2 arms = **56 runs, 56/56, 159.6 min**. Full-suite
      rather than a subset because the flip set cannot be predicted from traces.

      | arm | ISPD2005 (8) | ISPD2015 (20) | **all 28** |
      |---|---|---|---|
      | **1.005** | 1.0057 / 1.0057 | 1.0101 / 1.0153 | **1.0097 / 1.0126** |
      | 1.010 | 1.0057 / 1.0057 | 1.0101 / 1.0157 | 1.0097 / 1.0128 |

      *(median / mean DP ratio.)*

      **The headline finding is that the knob almost never binds: 1 design of 28 selects a
      different solution.** ISPD2005 is byte-identical across arms (0 movers). So the suite-mean
      difference (+0.02 pp for 1.010) is *entirely* `mgc_des_perf_a`, and the effective n for the
      decision is **1, not 28** — widening the design set cannot help, because the set was already
      everything.

      Where it does bind, 1.005 wins clearly. `mgc_des_perf_a`: 1.005 ships iter 630
      (GP 1.946e9, ovfw 0.0694) → **DP ratio 1.0005**; 1.010 ships iter 659 (GP 1.957e9,
      ovfw 0.0468) → **DP ratio 1.0076**. The looser budget buys −32.6% overflow for +0.565% GP
      HPWL, and post-DP that is **+0.705%**.
      ⚠️ **DP AMPLIFIED the penalty (−25% "recovery"), it did not absorb it** — reversing report
      §5's finding that DP recovers most of the cost (74% on `bigblue2`, 41% on `mgc_superblue19`).
      The "more spread legalizes better" story does **not** hold here.
      ⚠️ **Why so few movers now?** The 2026-08-10 A/B found 3 flippers in 8 designs; those three
      (`mgc_superblue19`, `mgc_superblue16_a`, `bigblue2`) no longer flip. **Hypothesis, not a
      finding:** the selection rule's second conjunct is an overflow test
      (`aux.overflow * 1.1 < primary.overflow`), and #31's grid cap moved 13 ISPD2015 designs from
      512 to 128/256, which changes overflow substantially. Untested.
      ⚠️ **Only the PREFERENCE budget was swept.** The accept-rule 1.005 is still hardcoded — see
      **#33**.

→ [[_NEW_REPORT_24_best_solution_trackers_20260810.md]] §7 (the faithfulness audit that found the
  first two), §5 (the original A/B)

---

## #14 — Zoomable visualizer window (opened 2026-07-31)

Done: a configurable `ViewWindow` (centre/span as **fractions of the die**, so one setting means the
same magnification on every benchmark), four zoom-only detail layers (row pitch, density bins, cell
outlines, filler/cell separation), and the GIF path. Verified by rendering on `mgc_pci_bridge32_a`.

⚠️ **The y axis was mirrored until 2026-08-05.** Every PNG/GIF produced before that date is
vertically flipped relative to every one produced after — including `.claude/2_ARTIFACTS/newblue5_placement.gif`
and the whole `GIFS_*` pile. **Do not compare an old frame against a new one and conclude the
placement moved.**

- [x] **DONE 2026-08-17 — MMS zoom path run, and `MIN_SIZE` floors nothing at zoom that it
      shouldn't.** `make_viz_gifs.py --designs newblue1 --zoom --every 50` ran end-to-end (512,198
      nodes, 3 generations, 31 frames, both GIFs produced). Measured the floor directly off the
      dump rather than eyeballing frames:

      | view | floored | std cells | fillers | macros | fixed |
      |---|---|---|---|---|---|
      | full die | 100.0% | 100% | 100% | 0% | 100% |
      | zoom span 0.05 | **0.1%** | **0%** | **0%** | 0% | 100% |
      | zoom span 0.01 | **0.1%** | **0%** | **0%** | 0% | 100% |

      The only thing floored at zoom is the 337 fixed terminals, and those are **exactly zero-area**
      (`w == h == 0`, bookshelf terminals) — flooring them is the desired behaviour, since otherwise
      they draw as nothing. Real cells are 3–1140 units wide against a 0.7-unit floor threshold at
      span 0.05, i.e. 4× clear at the *narrowest* cell. Nothing to fix.
- [x] **DONE 2026-08-17 — node-lock landed: `generate_viz.py --lock TARGET`.** The window re-centres
      on one tracked cell every frame. TARGET is a node name, `index:N`, or `most-moved`.
      **Needed a dump-format change**: the dump carried no names, so `PositionDump.cpp` now writes
      `names_gen<N>.txt` — sparse `"<index> <name>"`, skipping fillers (they are generated
      whitespace, not design objects, and are ~35% of the node set). Written **per generation**, and
      that is the point: `freezeMovableMacros()` + `rebuildFillers()` reshuffle the node set at the
      phase-2 boundary, so index *i* is a different node either side of it and the name is the only
      identifier that survives. Placement behaviour unaffected — `make test-regress` bit-identical.
      **Verified by assertion, not by eye**: for all 31 newblue1 frames, spanning **all three
      generations**, the tracked cell's centre lands **0.0000 px** from the canvas reticle (tol 0.5 px).
      `most-moved` picks the furthest-travelling *named movable* cell across generation 0 — a stable
      target, deliberately not the per-frame winner, which would pan the window randomly rather than
      follow anything.
- [x] **DONE 2026-08-17 — multiple windows per run: `--add-view` (repeatable).** `--add-view full
      --add-view zoom:0.4,0.6,0.02` renders any number of windows in ONE pass, each to its own
      window-named directory (`viz_render/zoom_c0.4-0.6_s0.02`). Each frame is decoded **once** and
      drawn into every window — `read_frame` is the file I/O plus dequantization, which dominates a
      zoom render that then discards ~99.99% of the nodes. **Verified byte-identical** to the same
      windows rendered as separate invocations (3 frames × 2 views, `cmp` on every PNG).
      Single-view CLI is unchanged, so `make_viz_gifs.py` and the `viz-gif` skill still work.
      ⚠️ The original entry's premise ("today `output.zoom*` is one window fixed at setup and
      changing it means re-running the placement") was already void — #16 moved rendering offline.
      What was actually missing was doing several in one pass.

**Overlay note (2026-08-17):** header lines are scarce — with `DIE_START = 0.10` the **third** one
lands inside the die box and is drawn over the cells. Benchmark + frame tag + phase now share the
top line (Mark's call), and the locked-cell name rides the zoom line, which is what keeps a locked
two-phase frame at two header lines.

---

## #24 — best-solution trackers: shared buffer + torn restore (opened 2026-08-10, CLOSED 2026-08-17)

**FIXED — two defects, only one of which was known when this was opened.** Three trackers
(`best_primary`/`best_aux`/`best_rollback`) each with their own geometry buffer, plus XPlace's
`get_best_solution` selection; and `restoreBestPlacement()` now also sets `probe_pos = node_pos`.
`make test`, `make test-regress`, `make test-regress-slow` green; 3 baselines regenerated with
reasons; MMS re-run (16/16). **Closing note (2026-08-17):** every item below is checked off —
either done, or deliberately spun out to **#32** (two remaining faithfulness gaps + the A/B needs
widening) so this ticket doesn't stay open for follow-on work its own defects don't block.
→ [[_NEW_REPORT_24_best_solution_trackers_20260810.md]]

- **Defect 1 (as opened): one buffer, two writers.** Confirmed on 17 of 29 `full44_v2` runs — and
  those 17 are exactly the *converged* designs, the ones that get scored.
- **Defect 2 (NOT known when this was opened): a placement is two variables and the snapshot copied
  one.** HPWL reads `node_pos`; density/overflow deposits at `probe_pos` (`Grid.cpp:36`), which the
  restore never touched. After a restore the node held committed position from one iteration and
  lookahead from another — a state that existed at no point in the run.
  **This is what produced the evidence quoted below**, so that evidence does not prove what it says.
  The real proof of defect 1 is the *HPWL* mismatch: adaptec1 logged iter 728 (HPWL 7.035e+07) while
  `Final HPWL` read 7.051e+07 = iteration **751**. `freezeMovableMacros` (`DataBase.cpp:396`) had
  already diagnosed and locally patched defect 2 — it was never generalised.

⚠️ **The rule does NOT always ship the spread-out solution** — it prefers it only when
`aux_hpwl < best_hpwl*1.005` and `aux_ovfl*1.1 < best_ovfl`. Over 29 traces: aux 8, primary 11,
none 10. The *bug* shipped the spread one nearly always, so this makes 11 designs less spread,
deliberately. A/B on that 0.5% budget (`best_aux_max_hpwl_ratio`, `DSE_RUN_SET=best_sol_ab`):
1.010 buys ~35% less overflow for ~0.5-0.7% GP HPWL, DP recovers 41-74% of it but never all —
**keep XPlace's 1.005** (n=2 designs with usable DP data; see report Section 5).

<details><summary>Superseded framing as opened (2026-08-10) — kept for the retraction trail</summary>

> **The "Restored … from iteration N" log line names a placement that is not the one shipped.**
> Headline HPWL/overflow are still trustworthy; the *provenance* line is not.
>
> There is exactly one snapshot buffer, `Node::best_solution_pos` (`AIEplace.cpp:107`), and **both**
> `best_primary` and `best_fallback` write it through the same `snapshotBestPlacement()`
> (`Output.cpp:670-685`). Last writer wins. `restoreBestSolution()` (`Output.cpp:414`) then selects by
> *metadata* priority (primary > fallback), logs that metadata, and calls `restoreBestPlacement()`,
> which loads whatever geometry happens to be in the buffer. When the fallback updated after the
> primary last did — the common case, since overflow keeps falling after the threshold crossing — the
> log names the primary's iteration while the restored cells are the fallback's.

**Two corrections.** "Headline HPWL/overflow are still trustworthy" was wrong for **overflow**:
defect 2 meant every reported overflow described the last iteration, not the shipped placement.
And the adaptec1 overflow evidence below is defect 2's signature, not defect 1's.
</details>

**Evidence (adaptec1, `full44_v2` run 2026-08-10):**
```
log:            Restored primary (converged) best placement from iteration 728
                (HPWL: 70346752, overflow: 0.069424)
reported:       Final Overflow (smoothed, no fillers) = 3.746e-02
iterations.dat: iter 757 OVFW = 3.746e-02   <- exact match
                iter 728 OVFW = 0.0694
```
`m.final_smoothed_overflow` is recomputed on the restored positions (`Output.cpp:455`), so the
geometry is demonstrably iteration **757** while the log says **728**. Reproduced on
`mgc_matrix_mult_c` (log it863 / 0.0692; reported 4.317e-02 = iter 892).

Two defects, ranked:

- [x] **1. Selection has no control over the geometry.** DONE — three trackers, three buffers, one
      shared `selectBestSolution()`. Verified on **both** branches of the rule: adaptec1 ships aux
      iter 757, `mgc_pci_bridge32_b` ships primary iter 723, and in each `Final HPWL` equals the
      selected solution's HPWL.
      ⚠️ **the falsifier as written is unusable** — it assumes `Final Overflow` describes the
      restored placement, which was defect 2. It can only be applied on a design whose selected
      solution is *not* the last iteration.
- [x] **2. The log line is false, and it misleads.** DONE — the slot now travels in the same struct
      as the metadata, so they cannot disagree.

- [x] **The `best_sol_aux` faithfulness gap** (the warning note below, folded in once defect 1
      stopped confounding it). `best_fallback` was renamed `best_aux`, gated on convergence, and
      given XPlace's accept rule; the missing `best_sol_rollback` was added with its
      free-on-first-convergence lifetime. The inverted `OVFW_EPSILON = 0.005` rule is gone.

**Still open (at close):**
- [x] **Fix (B)'s scope — RESOLVED 2026-08-11 as (b).** `syncProbeToCommitted()` is a separate step
      called only from `restoreBestSolution()`; `restoreBestPlacement()` restores `node_pos` alone,
      so the phase-2 macro freeze is untouched.
      ⚠️ **the premise for choosing (b) was wrong, and the correction matters more than the
      choice.** (b) does **not** leave MMS bit-identical: it produces sha `e9cc52242ad0`,
      byte-identical to the (a) build. Fix (B) never affected MMS at all. The MMS change is **#24's
      selection fix** — `beginFixedMacroPhase` (`Phase2.cpp:72`) picks the placement to freeze
      macros at via `selectBestSolution()`. So **MMS results move under #24 either way**, and the
      `full44_v2` MMS exclusion (valid for #23, which provably could not touch bookshelf designs)
      does **not** carry over. Cause of the error: `test-regress-slow` was never run between the
      tracker port and (B), so the divergence was pinned on the most recent change. See report
      Section 6a.
- [x] **Re-run the MMS suite (16 designs) — DONE 2026-08-14.** `results/DSE_20260814_152306`:
      16/16 succeeded, all converged bar `newblue4` (overflow 0.116). DP ratio **median 1.0138 /
      mean 1.0161**, 13/16 within ±2%, better than XPlace on 3. `make test-regress-slow` re-verified
      bit-identical against this tree on 2026-08-16.
      ⚠️ not a controlled #24-isolated A/B — #26/#27/#30 also landed before this run, so it
      does not cleanly difference against a pre-#24 MMS baseline. No such baseline was ever recorded.
- [x] **Our overflow metric vs XPlace's disagree on direction for `mgc_superblue19`** — ROOT-CAUSED
      2026-08-11 as a `target_density` difference; **that diagnosis was WRONG and retracted
      2026-08-15 as #25**. The real cause (also 2026-08-15, via #31): XPlace caps `num_bin` at
      `num_rows` and sw_only's cap only applied on the auto-grid path, so 13/20 ISPD2015 designs ran
      finer than XPlace and their exact overflow read up to 7x high on an apples-to-oranges grid.
      Fixed in `Setup.cpp`; not a best-solution-tracking defect at all. See #3, #31,
      [[_NEW_REPORT_31_overflow_stall_grid_20260815.md]]
- [x] **Move the A/B data out of `/tmp`** — the two summary TSVs are in
      `.claude/2_ARTIFACTS/todo24_best_sol_ab/`. ⚠️ **partial** — `vck5000/results/DSE_20260810_173906`
      (the GP `.def`s and per-run logs behind them) no longer exists, so only the aggregate numbers
      survive — nothing to re-derive from. Widening the A/B (below) needs a fresh run regardless.
- [x] **Best-solution tracking still diverges from XPlace in two places, and widen the A/B (n=2)**
      (found while answering "is it faithful?", 2026-08-11 — the *rule* is faithful, these are not).
      **Spun off to #32** — carrying forward rather than reopening #24 for it:
      - XPlace snapshots the lookahead (v_k), we snapshot the committed position (u).
      - `BEST_SOL_MIN_ITER` is absolute; XPlace's is phase-relative.
      - `keep XPlace's 1.005` for `best_aux_max_hpwl_ratio` still rests on n=2 designs.

<details><summary>Superseded: the warning "Related but SEPARATE" note — now folded in and done</summary>

> ⚠️ **Related but SEPARATE — do not conflate.** sw_only has no equivalent of XPlace's
> `best_sol_aux`, and `best_fallback`'s accept rule is *inverted* against XPlace's: ours tolerates
> overflow degrading by `OVFW_EPSILON = 0.005` to gain HPWL, XPlace's requires overflow to strictly
> improve and tolerates 0.5% HPWL loss (`param_scheduler.py:432-441`), then prefers it over the
> HPWL-driven pick when `aux_hpwl < best_hpwl*1.005 and aux_ovfl*1.1 < best_ovfl`
> (`get_best_solution`, :563-577). That is a real faithfulness gap, **but defect 1 confounds any
> measurement of it** — fix 1 first, then re-measure.

Correct as written, and the sequencing advice was right — defect 1 *was* confounding it. Both were
fixed in one change once defect 1 landed, since the rename and the gate touch the same lines.
</details>

→ [[_NEW_REPORT_24_best_solution_trackers_20260810.md]],
  [[HANDOFF_24_best_solution_buffer_20260810.md]]

---

## #31 — Investigate designs that stall at overflow > 0.1 (opened 2026-08-14, CLOSED 2026-08-16)

Mark, 2026-08-14, on the 28-design `make dse` (`results/DSE_20260814_133037`): *"The few designs
with overflow above 0.1 should be flagged for investigation. … wait until the MMS designs are run."*

**⚠️ BLOCKED until the tier-3 (MMS) `make dse` lands** — deliberately. MMS is a different regime and
the 0.1 threshold does NOT transfer to it (see below); flag the MMS stragglers off their own XPlace
reference once that run exists, then start the investigation with the full ISPD+MMS picture.

**ISPD flagged (Best OVFW > 0.1, from the 28-design run):**

| design | Best OVFW | GP Ratio | DP Ratio |
|---|---|---|---|
| `mgc_pci_bridge32_b` | 0.321 | 1.0090 | 0.9843 |
| `mgc_pci_bridge32_a` | 0.273 | 1.0983 | 1.0540 |
| `mgc_fft_2`          | 0.146 | 1.0988 | 1.0278 |
| `mgc_des_perf_b`     | 0.105 | 1.0324 | 1.0154 |

These stop above the 0.07 XPlace-matched threshold at the 1200-iter cap (or a divergence guard),
i.e. GP never fully spread. Two observations to carry in:
- **The high-overflow designs have inflated GP ratios that LG+DP partly repairs** (Mark's note):
  `fft_2`/`pci_bridge32_a` are ~1.098 at GP but 1.028/1.054 after DP — legalization re-spreads the
  under-converged placement and closes the gap. So the *GP* ratio on these is not a QoR verdict; the
  DP ratio is. `pci_bridge32_b` even beats XPlace post-DP (0.9843) despite the worst overflow.
- `pci_bridge32_a/b` are the **low-row std-cell** designs where our forced 512 grid is finer than the
  row structure — the old #(grid_ab) diagnosis (XPlace caps num_bin ≤ num_rows; pci_bridge32_b has
  ~400 rows, XPlace uses 256 and converges at 725). That lead was never closed; start here.

* Note from Mark: * first thing to check is if the lg or dp tools can verify the overflow metric on the exact same data.

**MMS caveat — do NOT flag MMS on the flat 0.1 rule.** XPlace's own Mixed-GP reference stops at
**0.10–0.18 exact overflow on nearly every MMS design** — that is normal for macros-movable, not a
stall. Flag a tier-3 design only where OUR macro-excluded overflow materially exceeds its Mixed-GP
reference, not where it merely exceeds 0.1. Reference (`benchmarks._XPLACE_MMS_MIXED_GP`, exact,
filler+macro-excluded overflow):

| design | ref ovfw | design | ref ovfw | design | ref ovfw | design | ref ovfw |
|---|---|---|---|---|---|---|---|
| adaptec1 | 0.131 | adaptec5 | 0.149 | bigblue4 | 0.130 | newblue4 | 0.182 |
| adaptec2 | 0.096 | bigblue1 | 0.174 | newblue1 | 0.136 | newblue5 | 0.170 |
| adaptec3 | 0.125 | bigblue2 | 0.105 | newblue2 | 0.143 | newblue6 | 0.142 |
| adaptec4 | 0.135 | bigblue3 | 0.124 | newblue3 | **0.040** | newblue7 | 0.152 |

Compare against sw_only's **macro-excluded** overflow (`[OVFW-DIAG] macro-excluded=`, or the
"Macro-Excluded Overflow (exact, no fillers)" summary row on mixed-size runs) — NOT the
filler+macro-included "Final Overflow (exact, +fillers)".

**MMS DONE** — `results/DSE_20260814_152306` (16/16, DP Ratio median 1.0137 / mean 1.0161, 13/16
within 2%, better on 3). Our macro-excluded overflow (from each run's `run_summary.md`) vs the
Mixed-GP reference:

| design | ours | ref | over? | design | ours | ref | over? |
|---|---|---|---|---|---|---|---|
| **newblue3** | **0.107** | **0.040** | **2.7× — the standout** | adaptec2 | 0.125 | 0.096 | 1.30× |
| newblue4 | 0.209 | 0.182 | 1.15× (worst absolute) | newblue1 | 0.157 | 0.136 | 1.15× |

All 12 others sit at or below their reference. **Flag: newblue3** (materially over — exactly as
predicted, its 0.040 ref is the one a modest overflow trips), with adaptec2/newblue1/newblue4 as
mild secondaries. **Key finding: the overflow does NOT cost post-DP QoR** — every flagged design's
DP ratio is fine (newblue3 1.0144, newblue4 1.0138, adaptec2 1.0137, newblue1 1.0121); LG+DP
absorbs the under-spread. So this is a GP-convergence question, not a quality one.

**Mark's note (line above) — answered:** the LG tool already reports an independent overflow on our
**exact** placement. `main.py --global_placement False --given_solution <our.def>` logs
`Input solution … exact Overflow:` — XPlace's own overflow on the same cells we placed. That is the
cross-check to make. **It is TODO #3's open reconciliation**: XPlace's overflow-on-our-solution
(`gp_ovfl_in`) vs our `computeOverflow` runs a consistent ~2–4×, direction unexplained, fillers
explaining part (#19a) and a residual ~2.2× on adaptec1/adaptec4 with newblue1 the opposite sign.
`lgdp.py` currently scrapes only `lg_hpwl`/`dp_hpwl`; **first step is to also scrape
`Input solution … exact Overflow` and surface it as a results.csv column**, so every design carries
XPlace's overflow-verdict on our exact placement next to ours — turning #3 from a one-off into a
standing per-sweep check. Do this before chasing any single design's stall.

**Deliverable:** one report — per flagged design (ISPD + MMS), whether the stall is a
grid/schedule/step issue and whether it costs post-DP QoR (the metric that matters). Cheap to
reproduce per design via `make dse --designs <name>`; convergence history in the run's
`iterations.dat` (`plot_histories.py`).

### Investigation DONE 2026-08-15 → [[_NEW_REPORT_31_overflow_stall_grid_20260815.md]]

- [x] **Tooling (the mandated first step): 3 overflow columns now stand per-sweep.** `results.csv`
      carries `Best OVFW` (smoothed) + **`Our Exact OVFW`** + **`XPlace In OVFW`** on the same shipped
      `.def`. `lgdp.py` scrapes XPlace's `Input solution … exact Overflow` (`in_ovfl` = #3's
      `gp_ovfl_in`); `dse.py` scrapes our exact from `run_summary.md`. No exe change. Re-summarizing
      the 28-design run reproduces the headline exactly (DP 1.0106/1.0149).
- [x] **The STALL is a grid issue, NOT the optimizer** — but the *overflow-metric* half of my first
      read was wrong (corrected below). The stall fix: **Grid — we force 512; XPlace caps
      `num_bin ≤ num_rows`** (`database.py:161`, pow-2 floor). sw_only has the identical cap
      (`Setup.cpp:340`) but only on the AUTO path; an explicit `bins_per_row` override bypasses it,
      and `benchmarks.py` stored XPlace's *requested* 512 not its *effective* grid. **5 designs**:
      `pci_bridge32_a`→128, `pci_bridge32_b`/`des_perf_a`/`des_perf_b`/`edit_dist_a`→256. Fix landed
      (below), all 5 improved.
- [x] **RESOLVED 2026-08-15 — the overflow discrepancy was the GRID, and the grid fix was INCOMPLETE.**
      td matches XPlace on all 20 (corrected #25). The `fft_2` "7× at matched grid" was my error: I
      read the *requested* 512 from XPlace's eval-log header and missed the cap warning — XPlace
      evaluated fft_2 at **128** (171 rows). A naive reference confirms our metric is correct
      (512→0.161=ours, 128→0.020≈XPlace). **The #31 grid fix caught only 5 of 13 row-capped designs**
      (I derived them from XPlace's GP-reference logs, which anomalously print 512 for fft_1/fft_2).
      **Fixed properly:** sw_only now caps an explicit grid at `num_rows` too (Setup.cpp row_cap
      branch), so all 13 run at XPlace's grid AND our overflow signal matches XPlace's. See below.
- [x] **Grid A/B (all 5, full GP+LG+DP): matching XPlace's grid improves BOTH ratios on every one; 3
      flip to beating XPlace post-DP.** `pci_bridge32_a` GP 1.098→1.013 / DP 1.054→1.024;
      `pci_bridge32_b` DP 0.984→**0.976**; `des_perf_a` 1.018→**0.997**; `des_perf_b` 1.015→**0.993**;
      `edit_dist_a` 1.011→1.010. All previously-flagged low-row stallers converge < 0.07 smoothed at
      the correct grid. Exact overflow is NOT the QoR predictor (rises on some, DP still improves) —
      the wrong grid cost *wirelength* (stall/over-spread), consistent with #31's thesis.
- [x] **FIX LANDED 2026-08-15 (Mark's go-ahead) — `benchmarks.py` grid corrected for the 5 designs to
      XPlace's effective value** (`pci_bridge32_a`→128, the other four →256); the mislabeled
      `xplace_grid`/`xplace_target_density` header comments rewritten (grid now = XPlace's EFFECTIVE
      value, density col = OUR DEF value). Full 28-design re-run `DSE_20260815_105117`: **only the 5
      moved, all improved, 23 bit-identical** (deterministic GP). Headline **1.0106/1.0149 →
      1.0095/1.0120**, better-on 4→6. Per-design DP: `pci_bridge32_a` 1.054→1.024, `pci_bridge32_b`
      0.984→**0.976**, `des_perf_a` 1.018→**0.997**, `des_perf_b` 1.015→**0.993**, `edit_dist_a`
      1.011→1.010. (within-±2% 21→20 only because `pci_bridge32_b` now beats XPlace by >2%.)
- [x] **`fft_2` overflow gap was the GRID (RESOLVED).** XPlace evaluates it at 128 (171 rows capped),
      we ran 512; naive reference matches ours at 512 and XPlace at 128, so the metric is correct. The
      universal-cap code fix makes fft_2 run at 128 like XPlace. → #3.
- [x] **Universal grid-cap code fix LANDED 2026-08-15 (Mark's call — "code fix in sw_only, robust").**
      `Setup.cpp` now caps an explicit `bins_per_row` at `row_cap` (`min(requested, 2^floor(log2(num_rows)))`),
      not just the auto formula — matching XPlace's `database.py:161`. `benchmarks.py` reverted to
      XPlace's *requested* 512 for all 20 ISPD2015 (was pre-capped for 5; superblue12 1024→512 manifest
      fix); the code caps at run time and logs the effective grid. `make test-regress` **bit-identical**
      (regress configs use the auto path, untouched). **8 more designs now cap** (fft_1/fft_2/des_perf_1
      →128; fft_a/fft_b/matrix_mult_1/matrix_mult_2→256) + superblue12→512.
      **Re-run DONE `DSE_20260815_161306`: headline 1.0096/1.0113, 22/28 within 2%, GP mean
      1.0223→1.0047.** Mixed per-design (fft_a −2.7pp, fft_2 −1.4pp better; des_perf_1 +1.8pp, fft_1
      +0.6pp worse), net better. **Overflow signal now reconciles with XPlace on std-cell designs**
      (fft_2 ours 0.227/XPlace 0.228, des_perf_b 0.192/0.192, matrix_mult_1 0.185/0.185).
      ⚠️ **ISPD2005 unaffected** — their num_rows (890–2694) exceed the requested grid, so no cap fires;
      verified against the 105117 run's logged row_cap values.
- [ ] **Macro-design faithfulness (separate, minor):** XPlace *scales* fixed/blockage density by td
      (`initializer.py:82`), we *cap* it (`Grid.cpp:139`). Equal at td=1, ours slightly higher at td<1
      in macro-perimeter bins. Not the fft_2 cause (0 fixed there). Worth aligning for macro-heavy designs.

---

## #25 — RETRACTED 2026-08-15: XPlace does NOT force td=1; our td MATCHES XPlace (opened 2026-08-11)

**The entire premise was a misread and is false.** XPlace uses the **same per-design
`target_density` we do** on all 20 ISPD2015 designs — verified mechanically: our `benchmarks.py` td
vs XPlace's `setup_dataset.py` td = **0 mismatches** (fft_2 0.65/0.65, pci_bridge32_b 0.143/0.143,
des_perf_1 0.91/0.91, …). So there is **no target_density divergence** and nothing to reconcile or
A/B here.

**How the misread happened, so it doesn't recur.** XPlace logs *two* target_density lines: a
params-dict **echo** `target_density: 1.0` (`log_design_params`, always 1.0 — the arg default) and
the **effective** value `target density = 0.65` (`database.py:839`, after `setup_design_args`). #25
quoted the echo. `setup_design_args` **does** have per-design `mgc_*` branches
(`setup_dataset.py:95+`), reached via `find_design_params` (`run_placement_nesterov.py:428`) in both
GP and LG-eval paths. Confirmed straight from the lgdp eval logs that produced our `XPlace In OVFW`
numbers: `fft_2` computed at `target density = 0.65`, `pci_bridge32_b` at `0.14`, `des_perf_b` at
`0.50` — all matching ours.

**What survives from #25 → folded into #3.** The overflow numbers *do* differ 2–7× on `mgc_*` — but
that is NOT td (matches) and NOT grid (matches for fft_2); it is a genuine **overflow-metric
divergence at td<1**, now the open item under #3. The convergence-signal / filler / macro-weight
points below are still true consequences of td<1, they were just wrongly attributed to a td *gap*
that does not exist.

<details><summary>RETRACTED original claim (2026-08-11) — kept as the retraction trail</summary>

> **We and XPlace use DIFFERENT `target_density` on ISPD2015 … we are optimizing to a different
> density target on 20 of 28 ISPD designs.** … XPlace's `setup_design_args` branches "cover only
> ISPD2005 and the *classic* superblue names. No branch matches `mgc_*`, so ISPD2015 keeps the
> default 1.0." … measured `mgc_des_perf_1` ours 0.906 / XPlace 1.0, ratio 8.79.

False: the `mgc_*` branches exist and set our exact values; the "1.0" was the params echo, not the
effective td. The 8.79 ratio was real but is the td<1 *metric* divergence (#3), measured against a
mislabeled td column, not a td-value difference. **Do not re-derive "XPlace uses 1.0" — grep
`setup_dataset.py` for the `mgc_` branches and read `database.py:839`'s `target density =` line.**
</details>

→ [[_NEW_REPORT_31_overflow_stall_grid_20260815.md]] (the correction + source trail)

---

## #29 — The XPlace reference: apples-to-apples (opened 2026-08-12, CLOSED 2026-08-14)

> **Resolution, 2026-08-14.** Done, but NOT "in the placer" as this entry's title/opening assumed —
> Mark redirected during the results.csv cleanup: the exe writes only raw measured columns and
> **dse.py owns every XPlace-reference comparison**, from the one manifest. Both pairing rules below
> were implemented: `benchmarks._XPLACE_GP_MASKED` (all 28) + `xplace_gp_masked_in_sw_frame()` apply
> masked-with-masked and the per-design site_width; dse.py's `summarize()` emits `XPlace GP HPWL` +
> `GP Ratio`. The batch numbers were taken from the **2026-08-07** log batch consistently (not the
> exe's old 2026-07-10 six). Per-row unconverged-overflow gate deferred to #3 (footer caveat instead).
> Commit `edd268f`.

Mark, 2026-08-12: *"Yes, pair masked with masked. We should always try to pair apples to apples.
Same thing for tier 2 — the site_width normalization and the masking should be taken into account
when computing final HPWL numbers, but the correct place to do that is in the placer, not the
batch script."*

**Symptom:** `XPlace GP HPWL` and `Ratio` were `N/A` on **22 of 28** designs. **Root cause:**
`Placer::lookupXplaceReferenceHPWL` was a hardcoded 6-entry `std::map` (ispd2005 adaptec1-4,
bigblue1-2); everything else returned 0.0f. **Pairing rules that a naive fix gets wrong:** masked
pairs with masked (`Best GP HPWL` is masked; the exact post-GP number differs 7.9% on superblue12),
and tier 2 is in site units (×200 mgc_*, ×100 superblue — a blanket ×200 is 2× wrong on the five
superblues). The masked GP data was recovered from `/tmp/xref/logs` (ispd2005) + `xplace_ref_ispd.tsv`
(ispd2015). Resolved in dse.py rather than the placer, which also killed the "two tables that can
disagree" duplication the old map's doc-block flagged.

## #28 — `dse.py` refactor: generic launch point, and two live defects (opened 2026-08-12, CLOSED 2026-08-12)

Mark: *"Refine dse.py to be the generic launch point for any number of runs. It should be easy to
understand and configure by LLM agent or human hands. It's currently functional, but suffering from
bloat."* Deferred from the 2026-08-12 `tools/` cleanup (#1) — that session did `tools/` only.

**Two defects found while surveying, both fixed by the refactor.**

- [x] **The sweep summary is stale against the exe's CSV schema — it degrades SILENTLY.**
      `dse.py` selected result columns with a hardcoded `fixed_cols` **denylist** that no longer
      matched `Output.cpp`. The exe emits `Best GP HPWL`, `XPlace GP HPWL`, `Final HPWL Exact`,
      `Phase1 *`; dse.py still looked for `Best HPWL` / `XPlace HPWL`. Result: the `Best HPWL`
      column blank on every row of `results.md` and the console table, the HPWL-range footer gone,
      and result columns listed as *swept parameters*.
      **Was falsifiable at:** `head -12 results/DSE_20260810_173906/results.md`.
      **Fixed:** `RESULT_COLS` / `PHASE1_COLS` positive lists; a name missing from results.csv
      prints `[warn] … Output.cpp schema changed`. Swept parameters come from `sweep.json`, so a
      result column can no longer be mistaken for one. Verified by re-rendering
      `results/DSE_20260812_143722` — `Best GP HPWL` populated on all 28 rows, footer restored.

- [x] **`_full_suite()` duplicates `benchmarks.py`.** Its 28-design grid table was an **exact**
      duplicate of the manifest's `grid` column, and it never set `target_density` — the trap
      `gen_suite_configs.py` was written to close (#25), which would have bitten the moment an MMS
      design entered a sweep. **Fixed:** both come from `benchmarks.BENCHMARKS[path]`; the table
      is deleted.

**Bloat inventory (846 lines), all resolved.** Dead on arrival: `modify_config_parameter` (73
lines) and `run_AIEplace` (14 lines) had **no callers anywhere**. `dse_sweep` was 57 lines of
entirely commented-out entries. The 7 historical A/B run-sets in `_RUN_SETS` (~190 lines) were
experiment *records*, not tools — deleted per Mark's call, with the equivalent one-line CLI
annotated into the two reports that cite them (`_NEW_REPORT_24_best_solution_trackers_20260810.md`
for `best_sol_ab`, `report_density_weight_ramp.md` for `_gamma_ab`/`_dwramp_ab`).
`morris.py` and `sobol.py` now print `--runset`.

**Old → new, for reading any dated record that quotes `DSE_RUN_SET`:**

| was | is |
|---|---|
| `DSE_RUN_SET=full_suite` | `python3 tools/dse.py` (the default) |
| `DSE_RUN_SET=full_suite_autogrid` | `--designs tier1+tier2 --grid auto` |
| `DSE_RUN_SET=grid_ab` | `--designs mgc_pci_bridge32_b,mgc_fft_2,mgc_matrix_mult_1 --grid 512,256,128` |
| `DSE_RUN_SET=precond_ab` | `--set enable_preconditioning=true,false --set convergence_overflow_threshold=0.04` |
| `DSE_RUN_SET=gamma_ab` | `--set gamma_ref_grid=512,1024 --set convergence_overflow_threshold=0.04` |
| `DSE_RUN_SET=best_sol_ab` | `--set best_aux_max_hpwl_ratio=1.005,1.010` |
| `DSE_RUN_SET=morris MORRIS_RUNSET=X` | `--runset X` |

`_nonconverge_ab`'s four arms are `--set density_weight_worsening_hpwl_norm=<default>,-1.0
--set ignore_net_degree=100,1000000000` — the product, with each default passed explicitly.

---

## #26 — Fence regions (opened 2026-08-11, CLOSED 2026-08-12)

> **Annotation, 2026-08-12 (after archiving):** item 6's guards were written into
> `.claude/2_ARTIFACTS/`, which is gitignored — so they protected this box and nothing else. The
> **scoring pipeline was moved to the tracked `vck5000/tools/`** (10 scripts, `tools/README.md`),
> which is what makes them guards. Two further stale paths surfaced in the move: `run_suite.sh` and
> `run_lgdp_suite.sh` still defaulted to `vck5000/2_ARTIFACTS/`, gone since 2026-08-07, and
> `run_xplace_ref.sh` wrote its references to a different file than `run_xplace_ref_2015.sh`.
> Report §9.

**All 9 designs are scored; the ISPD suite is 28 of 28.** Their ratios are unremarkable — median
**1.0154**, 7 of 9 within 2%, `mgc_pci_bridge32_b` better than XPlace. ISPD median 1.0095 → **1.0106**.
→ [[_NEW_REPORT_26_fence_regions_20260811.md]]

**Step 2 was never an acquisition problem.** `ispd2015_fix` is *generated*, not downloaded:
`cd ~/phd/Xplace/data && python3 fix_ispd2015_route.py` builds all 20 designs from the same raw
files (which are a symlink to our own benchmarks). The regenerated `mgc_pci_bridge32_b` DEF is
**byte-identical** to the 2026-07-13 copy, so #22's "a hand-built _fix cannot be trusted" caveat is
retired — this is XPlace's own script.

**Ignoring the fence is a real gap and it is now measured: 59–94% of fence-constrained cells sit
outside their region** in our placements (`vck5000/tools/fence_check.py`). Controlled against the
contest's own legalized solutions, which report **0 of 190,010** outside. Against those legal
solutions we are 2.6% better on the 11 unfenced designs and 12.5% better on the fenced 9 —
**a 9.9 pp difference-in-differences**, i.e. ~10% of our margin there is the constraint, not the placer.

⚠️ **None of this touches the XPlace comparison** — XPlace strips fences too, and the 9 are scored
on the stripped variant on both sides. What it invalidates is any claim these are legal ISPD2015
solutions.

- [x] **1. Score `mgc_pci_bridge32_b`** — done, and the other 8 with it. Both harnesses now split on
      `^REGIONS` and record the data variant in a `variant` column; `analyze_full44.py` marks
      fence-stripped designs **†**, the paper's own convention.
- [x] **2. Obtain `ispd2015_fix` for the other 8** — regenerated (20 designs, 702 MB, ~90 s).
      8 new XPlace references added to `tools/benchmarks.py`, cross-checked against TCAD Table III:
      within 0.2% on 5 of 9, worst 5.4%, all on the better side (the paper's HPWL is NTUplace4dr's
      measurement of XPlace's placement, not XPlace's own).
- [x] **3. Measure what ignoring the fence costs** — above. Note the ticket's literal form of this
      step has a **null answer, proved**: placing the fence-carrying vs fence-stripped DEF with
      sw_only gives a **bit-identical** `iterations.dat` and output DEF, because the constraint is
      discarded either way.
- [x] **4. DECIDED 2026-08-12 (Mark): do NOT implement fence regions — document that we ignore
      them.** Reasoning in report §7: XPlace has no formulation to copy (it raises; the paper marks
      those 9 designs † *"with fence-region constraints removed"*), implementing it cannot improve
      any number we report, and the contest's own evaluation tool is inaccessible so legality could
      not be scored anyway. **Say "ISPD2015 with fence regions removed, as in XPlace" in any
      writeup.** The counter-argument stands recorded, not dismissed: the contest scored these
      designs *with* the constraint, and §4b prices it at ~10% of wirelength.
      **The decision lives in `CLAUDE.md`**, not only here — an archived tasks.md entry is not
      loaded next session, which is precisely how #22's analysis went stale and got re-derived.
- [x] **5. Stop reporting fence-carrying designs as if unconstrained** — `readDEF()` warns on every
      run ("…the result is NOT a legal ISPD2015 solution…"), the TSVs carry `variant`, the scorecard
      daggers the 9, and `benchmarks.py` documents which reference came from which variant.
- [x] **6. Guard against re-deriving all of this** (added 2026-08-12 when closing). The recurrence
      mode is a fresh box or re-downloaded benchmarks leaving `ispd2015_fix` **missing or stale**,
      after which the harnesses quietly skip 9 designs — the exact shape of the #22 → #26 loop.
      Both harnesses now fail **loudly on stdout** naming the regeneration command, and warn when
      the raw `floorplan.def` is newer than the derived `_fix` DEF. Exercised against a synthetic
      missing-`_fix` tree, not just read.

**KEEP THE FENCED ORIGINALS — do not "clean up" the duplicate benchmark data.** They are the input
`fix_ispd2015_route.py` reads, and the only copy of `after_legalized.ntup.fix.def`, the contest's own
legal placement, which is the control for `fence_check.py` and the only fence-legal reference we
hold. `_fix` carries neither that file nor `design.v`. Deleting the raw data would also break both
frozen regress configs, the run/DSE configs, four `tools/*.sh`, and XPlace's `data/raw/ispd2015`
symlink — to save 1.8 GB that is already gitignored and re-downloadable. Considered and rejected
2026-08-12.

<details><summary>Original entry, as opened 2026-08-11 (diagnosis, still accurate)</summary>

**Supersedes #22's "SKIP for now".** Same root cause, but the diagnosis is now exact and one design
is scoreable today with a one-line harness change. #22 stays for its retraction trail; work here.

### The problem, precisely

`mgc_pci_bridge32_b`'s `floorplan.def` carries `REGIONS 3` / `GROUPS 3` — a *fence region*, i.e. a
set of cells constrained to a sub-rectangle of the die rather than the whole core. Two independent
consequences, and they are usually conflated:

**(a) XPlace crashes on them, so our stage-2 legal-vs-legal scoring dies.** Not a DP failure — it
never reaches DP:

```
run_placement_nesterov.py:442  data.init_filler()
database.py:863                self.compute_filler(...)
database.py:655                return self.compute_filler_with_fence(...)      # enable_fence = len(regions) > 1
database.py:660                raise NotImplementedError("We haven't yet supported fence region.")
```

`run_lgdp44.sh` records this as `exit1_nodp` (exit code 1, no `After DP` line) — an accurate status
that reads like a DP bug and is not one.

**(b) sw_only ignores fence regions entirely.** `DataBase::add_def_region()` and
`add_def_group()` are **empty stubs** (`common/src/DataBase.cpp:662-665`) — Limbo hands us the
parsed regions and we discard them. So on these 9 designs we solve an *unconstrained* problem and
report an HPWL that is optimistically low against any tool that honours the constraint. Nobody has
measured how much. This is a correctness question, not a scoring inconvenience, and it is the part
that has never been written down.

### The affected set is exactly the paper's †

Our 9 `exit1_nodp` designs and Table III's 9 †-marked designs are the **same set**, no exceptions:
`des_perf_a`, `des_perf_b`, `edit_dist_a`, `matrix_mult_b`, `matrix_mult_c`, `pci_bridge32_a`,
`pci_bridge32_b`, `superblue11_a`, `superblue16_a`. † means "fence region constraints removed", so
**the paper never reports a fence-carrying number either** — every published figure for these 9 is
on the stripped variant. Any comparison we make must be against the stripped variant too.

### Why one of the 9 already works

`Xplace/main.py:95` rewrites `--dataset ispd2015` → `ispd2015_fix` (their released fence-stripped
data). `run_lgdp44.sh` uses `--custom_path ... benchmark:ispd2015` for the ISPD2015 tier, which is
dispatched *before* that rewrite and hands XPlace the fence-carrying DEF — so our harness walks
straight into the crash XPlace works around. `data/raw/ispd2015_fix/` exists locally but holds
**only `mgc_pci_bridge32_b`** (fetched 2026-07-13), which is why that one design has a reference.

Verified 2026-08-11: patching a sw_only placement into
`ispd2015_fix/mgc_pci_bridge32_b/mgc_pci_bridge32_b.def` and running
`--dataset ispd2015_fix --global_placement False --given_solution <patched>` **completes LG+DP
cleanly**. Die area, `COMPONENTS` count and `UNITS` are byte-identical between the two variants, so
the patch is a straight coordinate substitution. Post-DP 3.310820e+06 site units = 6.62e+08 DBU,
against XPlace's own 3.477053e+06 — we are ~5% *ahead* on this design.

### What to do, in order

- [ ] **1. Score `mgc_pci_bridge32_b` now (cheap, unblocks 1 of 9).** Teach `run_lgdp44.sh` to use
      `--dataset ispd2015_fix --design_name <d>` when `Xplace/data/raw/ispd2015_fix/<d>/` exists,
      falling back to the current `--custom_path`. Template for the patch becomes that dir's
      `<design>.def`. Verify against the 6.62e+08 DBU above before trusting it.
- [ ] **2. Obtain the official `ispd2015_fix` for the other 8** (`Xplace/tree/main/data`, per the
      paper's footnote 4). No code change needed beyond step 1. **Preferred over constructing it** —
      and if constructed instead, `mgc_pci_bridge32_b` is a free validation: a hand-built `_fix` must
      reproduce its known numbers or none of the 8 can be trusted (this check is inherited from #22
      and still stands).
- [ ] **3. Measure what ignoring the fence costs us.** With both variants of `mgc_pci_bridge32_b` in
      hand, place *both* and compare: if our fence-carrying GP HPWL is materially below our
      fence-stripped GP HPWL, we are buying wirelength with illegal placements and every ISPD2015
      number on those 9 is inflated in our favour. This is the measurement that decides whether #26
      is a scoring chore or a correctness bug.
- [ ] **4. Decide whether to implement fence regions at all.** XPlace doesn't (it raises), the paper
      sidesteps them, and the ISPD2015 contest scored them. Implementing = per-region filler areas
      and a per-region density objective. **Do not start this before step 3** — if step 3 says the
      cost is negligible, the honest fix is to document that we place the stripped variant and stop
      pretending otherwise. If it is large, it is a real gap in the placer.
- [ ] **5. Either way, stop reporting fence-carrying designs as if unconstrained.** Whatever step 4
      decides, the run log should say which variant was placed.
</details>

Related: [[#22]] (same root cause, superseded decision), [[#25]] (the other ISPD2015 comparability
defect — different `target_density`, and it applies to all 20 ISPD2015 designs including these 9).

---

---

---

## #22 — 8 ISPD2015 designs have no XPlace reference (opened 2026-08-07, CLOSED via #26 2026-08-11)

**RESOLVED via [[#26]] on 2026-08-11 — all 8 now have a reference.** Neither option below was the
answer: `ispd2015_fix` is **generated** by XPlace's own `data/fix_ispd2015_route.py`, not downloaded
and not hand-built. The construction-validation argument was right and was honoured anyway — the
regenerated `mgc_pci_bridge32_b` is byte-identical to the copy its reference was measured on. The ⚠️
caveat at the bottom is now **measured and void**: sw_only produces a bit-identical placement from
either variant, because it discards the regions. Kept below for the retraction trail.

<details><summary>Original entry (superseded)</summary>

**Work [[#26]], not this.** The root cause is now exact (XPlace raises `NotImplementedError` in
`init_filler`, before legalization), the affected set is proven identical to the paper's †-marked
designs, and `mgc_pci_bridge32_b` is scoreable today. #26 also raises the part this entry never
mentioned: our own parser discards `REGIONS`/`GROUPS`, so we place these designs unconstrained.
Kept below for its retraction trail and the construction-validation argument, which still holds.

**Decided 2026-08-07 (Mark): SKIP for now.** The 44-design snapshot ships with 36 of 44 carrying a
reference; these 8 appear with our own numbers and no ratio. This entry exists so the analysis is not
re-derived.

`Xplace/main.py:94-96` silently rewrites `--dataset ispd2015` → `ispd2015_fix`, and that directory
holds exactly **one** design. `--custom_path` is checked *before* the dispatch and bypasses the
rewrite — that got us the other 11. The remaining 8 carry `REGIONS`/`GROUPS` that XPlace says it
cannot handle, so they are recorded `blocked_fence_region` rather than given a **silently wrong**
reference.

If revisited: **Option 1 (preferred)** obtain the official `ispd2015_fix` — no code change needed.
**Option 2** construct it (merge tech+cells LEF, strip REGIONS/GROUPS) — but it is *not* a plain
concatenation, and the validation is free and must come first: we hold both variants of
`mgc_pci_bridge32_b`, so a constructed `_fix` must reproduce its known numbers or none of the 8 can
be trusted.

⚠️ **Caveat that applies even today:** `mgc_pci_bridge32_b`'s reference came from the `_fix` data
(fence regions stripped) while sw_only places the region-bearing DEF. For that one design the two
tools solved slightly different problems.
</details>

---

---

## #27 — `mgc_matrix_mult_a`: one trailing space parses 60% as 6000% (opened 2026-08-11, CLOSED 2026-08-11)

**The worst design in the suite is a two-line parser bug, not an algorithm failure.** It is the
single largest quality defect we have — 3.27× XPlace post-DP, and the only reason the suite mean
(1.1335) cannot be quoted while the median (1.0095) can.

### Root cause

`host/benchmarks/ispd2015/mgc_matrix_mult_a/placement.constraints` is **25 bytes**:

```
m a x i m u m _ u t i l i z a t i o n = 6 0 %  \n      <- trailing SPACE before the newline
```

Every other ISPD2015 design's file is 24 bytes with no space — `mgc_fft_b`, `mgc_matrix_mult_b` and
`mgc_matrix_mult_c` carry the *same* `60%` value and parse correctly. The design is not special; its
input file is.

`DataBase::readPlacementConstraints` (`common/src/DataBase.cpp:75-84`) tests
`value_str.back() == '%'` to decide whether to divide by 100. With the trailing space that test is
false, so it takes the else branch — and `std::stof("60% ")` stops at the `%` and returns **60.0**.
The design runs at `target_density = 60`, i.e. **100× its intended 0.6**. Its own log says so:

```
INFO  Read placement constraint: maximum_utilization=6000%
INFO  Using benchmark maximum_utilization: 60.000000
INFO  Fillers: 29779040 at (758, 2e+03), effective target density 60
```

### What it costs

`addFillers` budgets whitespace as `target_density * placeable_area - stdcell_area`, so a 100×
target buys a ~200× filler population:

| | actual | correct (td = 0.6) |
|---|---|---|
| fillers | **29,779,040** | ~144,900 |
| fillers per movable cell | 199 | ~1 |
| total filler area | 4.5e+13 | 2.2e+11 |

**The filler area alone is ~20× the entire die** (die = 2.25e+12). The density objective is
therefore meaningless from iteration 1, and GP dies on `divergence_guard` at iteration 271, handing
over at exact overflow 0.234. Post-DP 4.9557e+07 against XPlace's 1.5170e+07 = **3.2669×**.

This also explains why #26's sibling change made it *worse* (3.0331 → 3.2669): with 29.8M bogus
fillers the landscape is garbage, so any schedule change merely reshuffles the failure. **Do not
read `matrix_mult_a`'s response to any algorithm change as signal until this is fixed.**

### Blast radius: exactly one design

All 20 ISPD2015 `placement.constraints` were scanned byte-wise and every v3 run log checked — only
`mgc_matrix_mult_a` reports >100%. ISPD2005 and MMS are Bookshelf and have no constraints file at
all, so they cannot be affected.

- [ ] **Fix the parser, not the benchmark file.** Trim whitespace from `value_str` before the `%`
      test; the input is contest-supplied and must be taken as-is. Two lines in
      `common/src/DataBase.cpp:77-84`. While there, **reject implausible values** — anything above
      1.0 after parsing is a bug by construction and should warn loudly rather than run.
- [ ] **Add a fixture test.** `readPlacementConstraints` has no coverage. `"60% "`, `"60%"`, `"0.6"`,
      `"60 %"` and a missing file are five cases and one assert each — cheap, and this class of bug
      is exactly what silent parsing does. Note `mgc_superblue19`'s file has a **trailing blank
      line** and parses fine only because the loop `break`s on first match; pin that too.
- [ ] **Re-run and re-score `mgc_matrix_mult_a`.** Expect it to leave the outlier position entirely.
      If it does, the suite **mean** becomes quotable again for the first time (it is 1.0150 over the
      other 18 today), which matters because the mean is what a paper reports.
- [ ] **Then re-ask whether `divergence_guard` at 271 iterations was ever real.** The stop is
      currently a symptom of the filler explosion; it may vanish, or it may be a second defect
      underneath. Do not assume.

⚠️ Interacts with [[#25]]: that entry argues we should possibly *ignore* `placement.constraints` on
ISPD2015 to match XPlace, which reads `target_density = 1.0` for every `mgc_*` design. If #25 is
resolved that way this parser stops feeding the objective — but it still feeds our reported overflow
and filler count, so **fix the parser regardless of how #25 lands.**

→ [[_NEW_REPORT_26_precond_always_on_20260811.md]] §5

### Outcome (2026-08-11)

**Fixed by deleting the trailing space from the benchmark file** — Mark's call; a parser-side
refactor with its own header and tier-1 harness was written and withdrawn as too much machinery for
the problem. The file is now byte-identical to `mgc_matrix_mult_b/placement.constraints`. The
implausibility check was kept: `readPlacementConstraints` now hard-errors on a value outside (0, 1].

| | before | after |
|---|---|---|
| post-DP HPWL | 4.9557e+07 | **1.5430e+07** |
| ratio vs XPlace | **3.2669** | **1.0171** |
| stop reason | `divergence_guard`, iter 271 | **`converged`**, iter 715 |
| fillers | 29,779,040 | **144,904** (predicted ~144,900 before the re-run) |

Suite over the same 19 scored ISPD designs: median 1.0095 unchanged, **mean 1.1335 → 1.0151 —
quotable for the first time**, within ±2% 13/19 → 14/19, `divergence_guard` 8/28 → 7/28.
ISPD2015 mean 1.2023 → 1.0223.

⚠️ **The benchmark fix is NOT tracked.** `vck5000/host/benchmarks/.gitignore` ignores `ispd2015`, so
the corrected file exists only on this machine and is undone by any re-download or fresh clone.
Check with `wc -c vck5000/host/benchmarks/ispd2015/mgc_matrix_mult_a/placement.constraints` — must
be 24, not 25. **The kept implausibility check is what makes that recoverable**: a machine without
the repair now stops with an error naming the file instead of silently producing a 3.27x result.

→ [[REPORT_27_matrix_mult_a_stray_space_20260811.md]]

### Annotation (2026-08-12) — the last open risk is measured, and there is only ONE copy of the file

Two facts found after this entry was archived. Neither changes its conclusion; both were unstated
assumptions in it.

**1. XPlace's reference for this design was ALSO computed from the malformed file — and it did not
matter.** `~/phd/Xplace/data/raw/ispd2015` is a **symlink to `vck5000/host/benchmarks/ispd2015`**,
so the two tools never had separate copies: the reference run of 2026-08-07 read the same 25-byte
file. XPlace does not parse it in Python — `detail_placement.py:670,707` passes the path to its
external DP binary — so whether it was affected had to be measured, not reasoned. Re-running
XPlace's full flow on the corrected file:

```
stored reference (2026-08-07, malformed input)  1.516973E+07
re-run 2026-08-12 (corrected input)             1.516485E+07   -0.03%
```

Negligible; its DP engine is insensitive to the bad value. **The 3.2669 → 1.0171 result stands**
(1.0175 against the fresh reference). This was the one way the headline could still have been
apples-to-oranges, and it is now closed by measurement.

**2. The symlink means a re-download poisons everything at once**, including `ispd2015_fix` — which
#26 established is *generated* from the raw data, not downloaded. `ispd2015_fix/mgc_matrix_mult_a/
placement.constraints` is 24 bytes today only because it was regenerated (2026-08-11 23:06) after
the repair. So there is exactly one file to protect and exactly one thing protecting it: the
hard-error in `readPlacementConstraints`. That check is load-bearing, not belt-and-braces.

**Still true and still accepted:** no automated test exercises that guard (a harness was written and
withdrawn as too much machinery), and the corrected benchmark file remains untracked.

---

## #7 — Investigate two more XPlace contributions (opened 2026-07-29, CLOSED 2026-08-07)

Both items from the 2026-07-29 XPlace summary are resolved. Full pre-compaction text is in the
TODO.md compaction section below.

**Item 1 — placement-stage-aware parameter scheduling, κ(η).** Filed as *not implemented*. It
**was** implemented: the skip_update gate, the ×3 throttle, the (0.5, 0.95) window and the `<50`
warmup are all present in `Schedule.cpp::updateSchedule` and faithful to XPlace. Only the quantity
being gated on was wrong — `density_force_fraction` (a gradient-norm ratio) instead of XPlace's
`weighted_weight` (the preconditioner mass ratio, now `precond_kappa`). Because the gradient ratio
is not monotone in λ, the throttle stayed engaged through the endgame and λ ramped ~6× slower than
XPlace's. **Fixed under #19b**; nothing of item 1 remained.

**Item 2 — Xplace-Route (detailed-routability-driven placement). OUT OF SCOPE (Mark, 2026-08-07).**
XPlace extends beyond global-router validation to actual detailed routability: PG-rail and I/O-pin
density penalties, a GPU pattern router for a live congestion map, congestion-driven cell inflation
with a historical inflation ratio, and a pin-accessibility post-refinement pass. This was filed as
a research question — *is detailed routability in scope for this project at all, or is
HPWL/overflow-vs-XPlace the right target?* — and the answer is the latter. AIEplace has no router
and no DP-stage routability handling, and adding one is a far larger scope addition than the
operator-level work in #6. **The success metric stays HPWL and overflow against XPlace.**

Recorded rather than deleted so the question is not re-opened from the paper summary a third time.

---

## 2026-08-07 - TODO.md compaction: full pre-compaction text of every open task

TODO.md was compacted from **1621 lines** to a gist-plus-open-items form under `CLAUDE.md`
"Keep TODO.md from bloating" rule 2. **No task was closed by this edit** - every `#n` below is
still open in TODO.md. What moved here is the accumulated evidence: measurements, provenance
notes, retraction trails, and the completed sub-items of still-open parents.

**Read this when TODO.md's gist is not enough.** In particular, this is the only home for:

- **#3's XPlace-reference traps** - a result dir is not a reference run until you check its argv
  AND that it reached `After DP`; the log header is the argument dump, not what ran; `gp_ovfl_in`
  is macro-INCLUDED; newblue4 is build-sensitive at ~1%.
- **#19's retraction trail** and its measured before/after tables.
- **#11's layered corrections** on `xplace_die_projection` / `macro_td_expand_ratio`.
- **#17's "decisions made while building, worth not re-deriving"**.
- **#15's measured precision table** and the half-applied-shift trap.

Verbatim and unedited. Where a section contradicts itself that contradiction is preserved - see
TODO.md #11, which is flagged for Mark rather than resolved here.

---

## #1 — Clean house: repo / notes / code cleanup  (planned Mon 2026-07-27)

Fast iteration left breadcrumbs sprawling and we started tripping over them. Goal: one tidy,
committed, self-consistent state. Checklist:

**Already done 2026-07-25:** established the numbered workflow dirs (`0_TODO`, `0_OVERNIGHT_WORK`,
`1_REVIEW`, `2_ARTIFACTS`), all git-ignored local-only via one root pattern `[0-9]_*/` (see
memory `workflow-dirs-convention`). Merged the root `AIEplace/REVIEW` (incl. 160M `archive/`)
into `1_REVIEW`. Net effect: the review/handoff dirs are now OUTSIDE the tracked repo.

### git working tree
- [x] DONE 2026-07-27 (commit 2a1a561): committed the 8 `vck5000/REVIEW/*` deletions +
      the new `[0-9]_*/` .gitignore. Verified all 8 files preserved locally in `1_REVIEW/`
      (2 renamed to `NEW_*` are byte-identical to the tracked versions) — nothing lost from history.
- [~] `tools/eval_overflow_xplace.sh`: LEFT UNTRACKED per Mark (2026-07-27). No stray source
      deletions found. Revisit if it should be committed into `tools/`.

### 1_REVIEW/ reports (now local-only)
- [ ] Consolidate handoffs/reports; apply the `NEW_` convention consistently (un-prefix the ones
      Mark has read, keep `NEW_` on the rest).
- [ ] Fold the still-relevant findings into TODO.md / memory so old reports can be archived.

### results/ + scratch
- [x] **DONE 2026-08-05: freed 67 G.** `/` went 96% → 89% (40 G → 107 G free); `vck5000/results`
      87 G → 21 G. Machine was verified idle (no `AIEplace_exe`/`dse.py`/`morris.py`) first.
      Helper installed: **`tools/prune_run_artifacts.sh`** (dry-run by default, `--go` to delete,
      `--viz-only` mode; refuses to run while a sweep is live).

      **Key insight — this was not keep-or-delete.** Each sweep's analysis-relevant metadata
      (`results.csv`, `results.md`, `scorecard.md`, `configs/`) is only 23 K–5.6 M; the 72 G was
      per-run payload (output `.def` placements + `placement/iter_N.png` frames). Reports cite only
      `results.csv`/`results.md`, and `analyze_morris.py <DSE_dir> <morris_dir>` reads **only
      `results.csv`**. So 16 sweeps were *slimmed* (per-run benchmark subdirs dropped, all
      top-level files + `configs/` + `analysis/` kept) rather than deleted — every published number
      and a full morris re-analysis still work. Verified by re-running `analyze_morris.py` on both
      morris pairs post-prune: 360 result rows loaded, identical factor rankings.

      - **Slimmed (11 sweeps):** `DSE_20260724_005636` (6.9 G), `_20260724_145322` (6.8 G),
        `_20260724_205144` (5.1 G), `_20260724_100610` (1.7 G), `_20260723_200407` (1.5 G),
        `_20260725_104908` (1.5 G), `_20260714_181404` / `_20260716_161321` / `_20260717_005948` /
        `_20260715_233808` (1.4 G each), `_20260723_173423` (713 M).
      - **Slimmed (5 of 6 VIZ archives):** `DSE_min_iters_VIZ` (11 G), `DSE_init_spread_VIZ`
        (8.4 G), `DSE_init_spread_small_VIZ` (6.8 G), `DSE_lamba_max_step_VIZ` (5.5 G),
        `DSE_backtracking_TF_VIZ` (4.1 G). Mark's call 2026-08-05.
      - **Deleted whole (stale/aborted, 1.5 G):** `DSE_20260722_223824` (aborted, 169/170 rows,
        superseded by `_20260723_173423`), `DSE_20260716_214056` (partial, 10 rows),
        `DSE_20260724_142441` (8-row adaptec1 smoke).
      - **KEPT IN FULL, deliberately:** `DSE_Good_Visualizations_March21` (5.9 G) — explicitly
        curated by Mark, per-iteration GIF frames preserved. **All 7 `morris_*` dirs** (3.6 M
        total — deleting them frees nothing and they hold the sensitivity designs + analyses).
      - Out of scope, untouched: `2_ARTIFACTS` (1.8 G) and the non-`DSE_`/`morris_` results dirs
        (`single_runs` 6.1 G, `gridsweep` 1.9 G, `mms_suite_precondON` 1.7 G, `VIZ` 752 M, …
        ≈ 15 G total). **These are the obvious next target if more space is needed.**

- [x] ⚠️ **CORRECTION to this checklist's own keep-list.** `morris_20260725_104908` **does not
      exist** — that name conflated a morris dir with its sweep dir. Resolved by swept-column
      signature (run labels are generic `run_NNN`, so a label-join matches every sweep of equal
      size — it cannot disambiguate). True pairings:
      - baseline `morris_20260723_173411` (mgc_fft_a, `init_step_length`) ↔ **`DSE_20260723_200407`**
      - new-code `morris_20260725_104513` (mgc_fft_a, **`init_step_seed`** — unique) ↔
        **`DSE_20260725_104908`**

      **Trap:** pairing the baseline by *timestamp proximity* picks `DSE_20260723_173423`, which is
      wrong — that one has 170 rows and belongs to the 16-var pilot `morris_20260722_223818`.
      Anyone pruning by timestamp would have destroyed the baseline's actual data.

- [x] **DONE 2026-08-05: `~/aieplace_tmp/` (1.1 G) removed.** Re-verified before deleting:
      `xplace_mms_reference.md` is **byte-identical** to the committed `docs/` copy (fc89bcb,
      working tree clean), and `mms_mod.zip` (331 M) was already extracted into
      `vck5000/host/benchmarks/mms/` (2.6 G, 19 entries).

- [ ] **TODO #16 `<run_dir>/viz/` is covered by the same policy.** The new per-run node-position
      dumps (~96 MB per adaptec1 run, ~480 MB per bigblue4 run) are **reproducible output** — treat
      them exactly like the `.def`s and `iter_N.png` frames: never the thing worth keeping, first
      thing to drop. `results/*/*/viz` is already swept up by the default slim (it lives inside the
      per-run dirs), and `tools/prune_run_artifacts.sh --viz-only` drops *just* the viz dumps when
      the placements are still wanted. Given the per-run size, consider making viz output opt-in
      rather than default for sweeps.

### code / comments audit
- [x] DONE 2026-07-27 (commit bb7904b): fixed the misleading `AIEplace.h` defaults AND the root cause.
      The defaults were duplicated (header initializer + `loadConfig` `.value(key,<literal>)` fallback)
      and had drifted (dct_normalize_inverse/precond_raw_area/dff_force_ratio header inits were the stale
      LEGACY values). Fix = single source of truth: `loadConfig` now falls back to the member's own value
      (`.value(key, member)`), and every configurable member carries its default as a header initializer
      (added inits to 8 previously-bare members). Behavior-preserving (run_config supplies every key);
      builds clean + smoke-verified.
- [~] Reconciled the stale run_config.json "NEEDS a full-suite re-baseline" comment → "validated by the
      MMS A/B (2026-07-26)". UNCOMMITTED — bundled with the #2 run_config comment pruning + the
      `enable_pin_offsets` breadcrumb decision (see #2).

### external (XPlace) repo
- [~] CHECKED 2026-07-27 — actual state differs from the note above (no `run_placement_nesterov.py`
      change present). `~/phd/Xplace` on `main` has 3 uncommitted local edits, all tied to the precond
      basis study and all look intentional (Claude did NOT touch them — Mark's call to keep/commit):
        1. `src/calculator.py` — BUG FIX: `apply_precond` returned None under `--use_precond False`
           (crashed upstream); now returns `mov_node_pos.grad`.
        2. `src/param_scheduler.py` — `PRECOND_TRACE` env→CSV dump of the a1/a2 addend norms per iter;
           `update_precond_weight` now always computes `weighted_weight` even with precond off (isolates
           just the preconditioning); `precond_a1_norm`/`precond_a2_norm` instrumentation.
      Decide: commit these into XPlace (they're real infra + a genuine bug fix) or keep local-only.

### vck5000/ top level — DONE 2026-08-05
Three directories assessed for removal; only one was actually disposable.
- [x] **`vck5000/bin/` deleted.** 15 MB, zero tracked files, already gitignored, holding an
      `AIEplace.exe` from April 3 — output from a build layout that no longer exists (current
      builds land in `build/$(TARGET)/host/$(HOST)/`). Regenerable by `make host`.
- [x] **`vck5000/build_reports/` removed, content preserved.** Despite the name it was not build
      output: it held one *tracked* file, `stage5c.md`, the record of Stage 5c completing. Moved to
      `docs/archive/pl_algo_stage5c.md`. **Note for future cleanups:** it could NOT go into
      `1_REVIEW/` — `.gitignore` has `[0-9]_*/`, so filing a tracked doc there silently drops it
      from version control.
- [x] **`vck5000/configs/` removed; `INPUT_FILE=mkcfg` no longer exists.** The whole mechanism
      (a dir, an `ifdef`, an `include`, one config file) existed to avoid typing two variable
      assignments, and nothing else in the repo referenced it. Replaced by a named target:
      **`make pl-algo`** = `make pl PL=pl_algo HOST=pl_algo`, `TARGET` passes through. Shows up in
      `make help`. If muscle memory reaches for `INPUT_FILE=mkcfg`, that is why it stopped working.
- [x] **Verification harnesses moved to `vck5000/test/`** (from `pl/src/pl_algo/model/`) so tests
      are visible at the top level, with `make test` wired up. See `vck5000/test/README.md` and
      `AIEplace/CLAUDE.md` § *Verification Loop*; the sw_only gap is **#17**.

---

## #3 — Tooling & evaluation workflow

- [x] **DONE 2026-08-06: two Claude skills in `vck5000/.claude/skills/`** — `run-benchmark`
      (one sw_only run: derive a config instead of editing the tracked default, per-design td/grid
      from `benchmarks.py::_ROWS`, check the shared box first, read the run dir) and `viz-gif`
      (existing; refreshed). **`.claude/` is gitignored, so these do not travel with a clone.**
      They point at `benchmarks.py` / `Output.cpp` rather than restating values, precisely so a
      rename doesn't strand them (`viz-gif` had gone stale telling sessions to avoid a cairo path
      TODO #16 step 5 had already deleted).

- [x] **A third skill, `xplace-compare`, was written, benchmarked, and SCRAPPED the same day.**
      Worth recording because the negative result is the useful part. It encoded the four ways a
      "vs XPlace" number goes wrong (phase-1-vs-phase-2, masked `GP Stop!` figures, wrong sw_only
      row, tier-2 site-width frame). A 3-eval A/B against a no-skill baseline scored **16/17 vs
      16/17** — a dead tie. The baseline independently found the `GP Stop!` masked-overflow trap
      and quantified it at the same 2.6x.

      **Why it was redundant: `tools/benchmarks.py` already documents this well.** Its header
      comments on `_XPLACE_MMS_MIXED_GP` / `_XPLACE_MMS_FINAL` / `_ROWS` are what let the baseline
      get it right. **The lesson generalises — a skill earns its place where the repo does NOT
      already document something, or where the knowledge is procedural (do X before Y) rather than
      referential.** `run-benchmark` survives on exactly that test; `xplace-compare` did not.
      (One real split, kept below: the macro-excluded overflow row. And one instructive loss — the
      skill made its run *apply* the post-DP rule without *reporting* the reasoning, while the
      baseline derived and explained it. A rule that says "do X" buys compliance; the "and say why"
      is what buys a defensible number.)

### XPlace-reference traps found by that benchmark (keep — they are not written down elsewhere)

- **A result directory is not a reference run until you check its argv AND that it reached
  `After DP`.** The 2026-07-17 timestamp range mixes in: aborted runs (`22:54:09_adaptec1` vs the
  valid `22:57:06`), a `--dataset ispd2005` run (`12:55:49_adaptec1`), and
  `--global_placement False --given_solution swonly_<design>.pl` runs — those are XPlace
  legalizing *our* placement, not XPlace placing the design. All produce plausible numbers. Same
  class as the morris/DSE timestamp-pairing trap in #1.
- **The log header is the argument dump, not what ran.** `num_bin_x: 512` and
  `target_density: 1.0` are pre-override CLI defaults; `setup_dataset.py` overrides per design and
  the real values print later as `#Bins = (1024, 1024)` / `target density = 0.50`. Quote the
  post-setup lines.
- **`gp_ovfl_in` in `2_ARTIFACTS/lgdp_rebase_results.tsv` is macro-INCLUDED** (the
  `--given_solution` path never sets `ps.zero_macro_grad`), so it is not comparable to XPlace's own
  exact overflow. On newblue4 that is 0.3213 vs the comparable 0.1828 against XPlace's 0.1840.
  XPlace's post-GP eval runs under `zero_macro_grad=True`, so the sw_only counterpart is
  `final_overflow_macro_excluded`, not `final_overflow`.
- ⚠️ **newblue4 is build-sensitive at ~1%, and it is not recorded anywhere else.** The 2026-08-04
  build converges at 1734 iters with macro-excluded overflow 0.1828; the 2026-08-06 build gives
  2.326e8 at 0.2026 and stops on `divergence_guard`. Configs byte-identical apart from
  `results_dir`; binary md5s differ. No post-DP number exists for the newer build yet — re-run
  `2_ARTIFACTS/gen_lgdp_inputs.py` + `run_lgdp_suite.sh` after the TODO #19 faithful suite
  finishes before quoting a newblue4 line.
- **`REPORT_phase2_mms_suite_20260802.md` §5 is still uncorrected on disk** and keeps regenerating
  the "we need ~15 more XPlace GPU runs" myth. Its retraction is in this file and in
  `_NEW_REPORT_lgdp_suite_20260804.md` §1. Note the read-state inversion: the **wrong** report has
  no `NEW_` prefix (read), the **correction** still has one (unread).

- [x] **DONE 2026-08-04: resumability in `dse.py`.** `python3 tools/dse.py --resume results/DSE_<ts>`
      re-enters an existing sweep dir and runs only what is missing from its `results.csv`. A run's
      identity is design + the value it gave every swept column — the same tuple `write_run_config`
      already emits into `DSE_info`, which is where those CSV columns come from, so no new bookkeeping
      file. `_norm()` compares numbers numerically (the exe round-trips `0.5` out as `0.500000`).
      Config filenames continue past the highest existing `run_NNN.toml` instead of restarting at 1,
      which would have overwritten the original sweep's configs with different content.
      **Caveat, documented in the docstring:** runs are matched on parameter *values*, so editing
      `dse_sweep` between the original launch and the resume silently re-runs whatever no longer matches.
- [x] **DONE 2026-08-04 (first full pass): full-pipeline evaluation (GP → LG → DP).**
      Report: `1_REVIEW/reports/_NEW_REPORT_lgdp_suite_20260804.md`. Harness:
      `2_ARTIFACTS/gen_lgdp_inputs.py` → `run_lgdp_suite.sh` → `analyze_lgdp_suite.py`, raw data
      `2_ARTIFACTS/lgdp_suite_results.tsv`. **Stock XPlace needs no source change** — the
      `--global_placement False --given_solution` path is `run_placement_nesterov.py:15-25` and it
      feeds the same `detail_placement_main` a full run uses. (`tools/legalize_swonly_mms.sh`'s
      header claim that this "requires the XPlace skip-GP + mixed_size macro-LG branch" is stale.)
      **Result: post-DP HPWL mean +1.17% vs XPlace over 14 clean designs**, and our phase-2 macro
      placement passes XPlace's own macro-legalization check on all 16 (15 at zero displacement).
      > **REFRESHED 2026-08-06 — that first pass mixed two GP generations; this one does not.**
      > It reused the 08-02 phase-2 placements, but 7 designs had moved by the 08-04 re-baseline
      > (adaptec5 materially: 3.020e8 `diverged_hpwl` -> 3.248e8 `converged`), and newblue5's input
      > came from neither. Re-run over the **re-baseline** DEFs, one generation throughout:
      > **post-DP HPWL mean +1.23% over 15 designs** (median +1.21%, best newblue3 −4.35%, worst
      > adaptec5 +4.38%; adaptec3 still segfaults in XPlace's legalizer, see below).
      > Data `2_ARTIFACTS/lgdp_rebase_results.tsv`; regenerate with
      > `LGDP_PL=/tmp/lgdp2/pl LGDP_RES=…/lgdp_rebase_results.tsv bash 2_ARTIFACTS/run_lgdp_suite.sh`
      > then `LGDP_OURS_GLOB='2026-08-06*' python3 2_ARTIFACTS/analyze_lgdp_suite.py <tsv> --density`
      > (the harness gained `LGDP_{PL,LOG,RES,PROG}` and `LGDP_OURS_GLOB` overrides for this).
      > **Density is now at parity on all 8 designs where it is measurable** (td < 1.0): −23.2%
      > (newblue1) … +0.4% (newblue4), with **adaptec5 at +0.2%, down from +14.0%**. The headline
      > no longer needs a density-based exclusion.
      > ⚠️ Both numbers predate TODO #19; they must be re-measured on the faithful-pair placements.
      ⚠ **HPWL is UNMASKED on both sides, and this is a second `GP Stop!`-style trap.** XPlace has
      two HPWL functions: `fast_evaluator` → `masked_scale_hpwl` produces the per-iteration
      `masked_hpwl:` lines *including the one inside `GP Stop!`*, while `get_obj_hpwl` → `get_hpwl`
      calls `hpwl_cuda.hpwl` with **no `net_mask`** and produces `exact HPWL` / `After DP, HPWL`.
      Compare those against sw_only's **"Final HPWL (exact, all nets)"** (`final_hpwl_exact`,
      `Output.cpp:511`), NOT "Final HPWL" (masked at `ignore_net_degree = 100`). 0.06% apart on
      adaptec1. An earlier version of this entry asserted the opposite.
      Still open, in priority order:
      - [x] **DONE 2026-08-04: post-DP density measured** — `tools/post_dp_density.py`, run over both
            tools' own written `placement_<design>_dp.pl` by one implementation so the §4 overflow
            gap cannot contaminate it (`analyze_lgdp_suite.py --density`). **7 of 8 target_density<1
            designs match or beat XPlace; the one exception is adaptec5, which buys its −7.4% HPWL
            with +14.0% overflow** — the suspicion is now measured, and adaptec5 is excluded from
            the headline. newblue1 is the best case: −23.9% overflow for +1.03% HPWL. `max_util` is
            1.000 on both sides everywhere = the legality check passing.
            **Two structural limits, recorded so nobody re-derives them:** post-DP overflow is
            **identically zero at target_density = 1.0** (legalization caps occupancy at 1.0 and the
            capacity *is* 1.0), so it says nothing on 8 of 16 designs — there legalization answers
            the density question and HPWL is the whole story. And a **"top 5% bin utilisation"
            proxy was tried and dropped**: it reads exactly 1.000 for both tools on every design at
            both the GP grid and a coarse 64×64 grid, because the busiest bins are movable-macro
            interiors. It measures macro presence, not quality.
      - [~] **Reconcile the overflow XPlace reports on our given solution** (`gp_ovfl_in` in the TSV)
            with our own `computeOverflow`. XPlace reads adaptec1 0.0484 / adaptec3 0.0190 where we
            report macro-excluded 0.109 / 0.071 on the same placement and the same 1024 grid — a
            consistent ~2-4x, direction unexplained. Until this is closed, treat that column as a
            diagnostic, not a metric. (HPWL round-trips exactly, so the input transfer itself is fine.)
            **PARTIALLY EXPLAINED 2026-08-06 (TODO #19a): fillers.** Our headline exact overflow was
            filler-INCLUDED and XPlace's is not, which is most of a ~1.7x factor on its own (adaptec1
            0.186 +filler vs 0.109 no-filler on the identical placement). It is **not the whole
            story** — a residual ~2.2x remains on adaptec1/adaptec4, and **newblue1 has the opposite
            sign** (XPlace 0.1633 vs our 0.1242). That sign flip tracks target_density, which points
            at the second factor: `--global_placement False` never sets `zero_macro_grad`
            (TODO #8's 2026-08-01 correction), so XPlace's number here **includes movable macros**
            while our macro-excluded row drops them. Still open; the discriminator is a macro-included
            variant of our own metric — i.e. the `computeOverflow` variant TODO #8 already asks for.
- [x] **`verify_swonly.sh` diff bug — ALREADY FIXED, TODO was stale.** `function_statistics.md` goes
      to `timings/`, not `artifacts/`, and the script header documents exactly why. Verified 2026-08-04.
- [x] **`gen_footprint_ab_configs.py` staleness — ALREADY FIXED, TODO was stale.** Its header records
      the 2026-08-01 update that removed the `xplace_die_projection` arm. Verified 2026-08-04.

### New, opened 2026-08-04 by the LG/DP suite

- [x] **adaptec3 segfault inside XPlace's `gpudp.greedyLegalization` — ROOT-CAUSED AND FIXED
      2026-08-07. It was OUR harness, not our placement and not XPlace's skip-GP path.**
      `2_ARTIFACTS/gen_lgdp_inputs.py` hardcoded the patch template as `mms/<design>/<design>.pl`
      instead of reading the `.pl` the design's **`.aux`** actually names. adaptec3's `.aux` names
      **`adaptec3.2.pl`**, which carries `/FIXED` on 665 nodes; the default `adaptec3.pl` carries
      **zero**. So we handed XPlace 665 movable **0×0** terminals (`#Mov = 451650, #Fix = 0`,
      against its own reference `#Mov = 450985, #Fix = 665`).
      **The crash chain**, confirmed under gdb: a 0×0 movable non-macro reaches
      `cpp_to_py/gpudp/lg/greedy_legalize.cpp:265`, where `num_node_rows = ceilDiv(0, row_height)`
      is **0**, so the VLAs at 266/274/289 are zero-length; line 299 writes `blank_index_offset[0]`
      out of bounds and line 300's `std::fill(p+1, p+0, -1)` has `first > last`, which GCC lowers to
      a `memset` of `(size_t)(-4)` → SIGSEGV in `__memset_avx2_erms` called from `dp::legalizeBin`.
      **Verified by fixing only the flags:** same coordinates, 665 `/FIXED` markers restored →
      exit 0, full LG+DP. Input HPWL bit-identical to the crashing run (1.510788E+08).
      **Fixed** in `gen_lgdp_inputs.py` (`template_pl()` reads the `.aux`); verified for all 16.
      **Only adaptec3 was affected.** Three designs' `.aux` name a non-default `.pl`
      (adaptec1 → `adaptec1_graphplanner.pl`, adaptec2 → `adaptec2.nonlinear.pl`,
      adaptec3 → `adaptec3.2.pl`), but adaptec1/adaptec2's fixed sets are **identical** in name and
      coordinates between the two files, so the wrong template was harmless there. ⚠️ They differ in
      *line endings and whitespace* (CRLF + spaces vs LF + tabs), so a naive `diff` says they
      differ — normalize before concluding anything from that comparison.
      **sw_only itself was never wrong:** `DataBase.cpp:236-249` discovers and parses the `.aux`, so
      sw_only used `adaptec3.2.pl` and never moved those 665 terminals (verified byte-identical in
      our export). No sw_only re-run was needed. The old "HPWL round-trips exactly" check was valid
      but blind here — it round-tripped through the same wrong template, so it could not see a
      missing fixedness flag.
      **Latent XPlace bug, upstream's call, NOT patched by us:** `legalizeBin` is UB for *any*
      zero-height movable non-macro cell. XPlace never hits it only because zero-size nodes are
      always fixed terminals in its own flow. `num_node_rows = std::max(1, ceilDiv(...))` would
      harden it.
- [ ] **sw_only has no per-row site model; `enforceDieBoundaries` clamps to the die *rectangle* only.**
      11 of 16 MMS designs have a ragged (staircase) core — each `CoreRow` carries its own
      `SubrowOrigin`/`NumSites` — so "inside the die bbox" is weaker than "inside a row". Measured with
      the new `tools/check_row_spans.py`: adaptec3 315 cells outside their row's span (worst overhang
      4122), newblue4 25, adaptec5 23, newblue3 10, bigblue2 2, bigblue3 1, the rest 0. Harmless so far
      (every one of those designs legalized), and **NOT** the adaptec3 crash — but it is an unmodelled
      constraint that a downstream legalizer has to absorb, and XPlace's GP has the same rectangular
      assumption, so check what XPlace's own output does here before treating it as a divergence.

---

## #6 — Port Xplace's operator-level optimizations (opened 2026-07-29)

Xplace (Liu et al., TCAD 2023) gets ~2x over DREAMPlace almost entirely from operator-level
restructuring of the same ePlace math we already implement — not a new algorithm. Four distinct
techniques, worth evaluating separately since our C++/HLS setting differs from Xplace's
PyTorch/autograd setting (see conversation on 2026-07-29 for the paper summary):

- [ ] **Operator combination** — Xplace merges WA wirelength, WA gradient, and HPWL into one pass
      since all three need the same per-net min/max (`x+`, `x-`). Check `Partials.cpp`
      (`computeHpwlPartials_CPU`) for redundant min/max recomputation across these three quantities;
      merge into a single pass if found.
- [ ] **Operator extraction** — Xplace factors out the shared cell-density-map computation used by
      both the electrostatic-system objective and the overflow-ratio metric, computing it once.
      Check `Density.cpp` (`compute_eField_DCT`) and the overflow computation (`Output.cpp`,
      `computeOverflow`) for duplicate density-map builds; share one if found.
- [~] **Operator reduction** — Xplace's version of this is "skip PyTorch autograd, hand-derive
      gradients" — not directly portable since sw_only has no autograd layer. The analogous risk in
      our setting is redundant kernel launches / synchronization in `pl_algo`'s dataflow modules.
      **MEASURED BY INSPECTION 2026-08-02** (`Driver.cpp::runPlacement`, steady-state iteration):
      | per iteration | count | note |
      |---|---|---|
      | `top()` kernel launches | **12** | 1 HPWL_GRAD + 1 DENSITY_BIN + 8 field passes + 1 FORCE_GATHER + 1 ITERATION_UPDATE |
      | AIE graph `run`/`wait` pairs | 6 | the 6 DCT_TRANSPOSE field passes (the 2 SPECTRAL passes are pure PL) |
      | host↔device matrix DMA | **~76 MB** | 8 field passes × (4 MB up + 4 MB down) + 4 MB rho down + 8 MB E-field up, at GRID=1024 |
      Iteration 1 additionally pays a full second gradient evaluation + 1 ITERATION_UPDATE for the
      XPlace initial-step estimate (one time, by design).
      **Finding: launch count is NOT the bottleneck — the host round-trip is.** ~12 launches × ~75 µs
      ≈ 0.9 ms/iter of launch overhead, against ~76 MB/iter of DMA ≈ 7.6 ms at ~10 GB/s — roughly
      **8× more time in DMA than in launches**, and every byte of it is intermediate field data that
      never needed to leave device DDR (`field_pass` stages each 4 MB matrix through host memory,
      "scratch crosses via host, like runField"). So the payoff is keeping the matrices device-side,
      not shaving launches. That is exactly what Stage 5's unified datapath + the device-resident
      loop do ⇒ **no separate work item; fold this measurement into the Stage 5 justification.**
      NB `DATAFLOW.md` says "~8 XRT kernel-launches/iter" — the real count is 12, and the sentence
      credits the resident loop with saving launch overhead when the bigger win is the DMA.
- [ ] **Operator skipping** — Xplace skips the density-gradient operator most iterations early in
      placement, when `|density_grad|/|wirelength_grad| < 0.01` and `iteration < 100`. Check whether
      sw_only's early iterations already have a near-zero density term (γ/λ schedule) and would
      benefit from literally skipping the density gradient computation on those iterations rather
      than computing and discarding a near-zero result — potential real iteration-time win on sw_only,
      and worth carrying into `pl_algo`'s `density_manager`/`iteration_update` modules from the start.

---

## #7 — Investigate two more Xplace contributions (opened 2026-07-29)

From the same Xplace summary (2026-07-29 conversation), items 2 and 4 of the contributions list —
distinct from the operator-level optimizations in #6, worth a look independently:

- [x] **CLOSED 2026-08-06 against TODO #19b — it was already implemented, on the wrong quantity.**
      The skip_update gate, the ×3 throttle, the (0.5, 0.95) window and the `<50` warmup are all
      present in `Schedule.cpp::updateSchedule` and faithful. What was wrong is that the window was
      tested against `density_force_fraction` (a gradient-norm ratio) rather than XPlace's
      `weighted_weight` (the preconditioner mass ratio, now `precond_kappa`) — and because the
      gradient ratio is not monotone in λ, the throttle stayed engaged through the endgame and λ
      ramped ~6× slower than XPlace's. Fixed; see #19. Original text below.
      <details><summary>original item</summary>
      Xplace defines a "precondition weighted
      ratio" κ(η) = |H_D| / |H_W + H_D| from the existing preconditioner terms (net-degree term H_W,
      cell-area term H_D) to classify each iteration into wirelength-dominated / spreading /
      final-overlap phases, then deliberately slows the γ/λ parameter update during the spreading
      phase (0.5 < κ < 0.95) to trade some runtime for better final quality. sw_only already has a
      preconditioner (`dff_force_ratio`/`precond_raw_area`, see memory
      `mms-faithful-field-ab-result`) and a γ/λ schedule (kept host-side per `AIEplace/CLAUDE.md`) —
      check whether κ(η) is cheap to compute from what we already track, and whether slowing the
      schedule during spreading helps our known-hard designs (adaptec5/newblue4/newblue5, see memory
      `mms-hard-spreading-three-diseases`) without hurting runtime elsewhere.
      </details>
- [ ] **Xplace-Route (detailed-routability-driven placement)** — Xplace extends beyond global-router
      validation to actual detailed-routability: PG-rail/I/O-pin density penalties, a GPU pattern
      router for a live congestion map, congestion-driven cell inflation with historical inflation
      ratio, and a pin-accessibility post-refinement pass. This is a much bigger scope addition than
      #6/the scheduling item — AIEplace has no router or DP-stage routability handling today. Treat
      as a research question first: is detailed-routability in scope for this project's placement
      goal at all, or is HPWL/overflow-vs-XPlace (the current success metric) the right target to stay
      focused on? Needs a scoping conversation before any implementation work.

---

## #9 — User Friendliness (opened 2026-07-30)

- [x] **STEP 1 DONE 2026-08-04 — the silent fork is gone: `host/src/common/` extracted.** All 15
      shared files now exist ONCE. sw_only's versions were promoted (they were strictly newer —
      pl_algo's copies were a 2026-06-15/07-10 snapshot) and pl_algo's were deleted; `lib/*.a` moved
      with them, so pl_algo no longer reaches into `sw_only/`. `pl_algo/include/` is now empty and
      gone. See `host/src/common/README.md` for the boundary rule (nothing in `common/` may include
      `AIEplace.h`, `Visualizer.h`, or anything from `pl/` — verified, and it holds today).

      **Build wiring:** `common.mk` gains `HOST_COMMON_DIR`; `host/Makefile` gains a second pattern
      rule for `$(HOST_COMMON_DIR)/src`; each `makeflags.mk` lists the shared files in `COMMON_SRCS`.
      There is deliberately **no library target** — the variants need different flags (sw_only
      `-O2 -fopenmp`, pl_algo `-O0` + the mixed `_GLIBCXX_USE_CXX11_ABI` for the XRT TU), so each
      compiles the shared sources into its own `obj/`. The OpenMP pragmas are inert without
      `-fopenmp` and nothing calls the OpenMP runtime API, so pl_algo just runs them serially.

      **Dead markv1 residue deleted with pl_algo's copies** (none of it was reachable from pl_algo's
      own sources — checked by grep before deleting): the AIE `Packet`/`PacketIndex` structs,
      `initializePacketContents` / `prepareNetGroup` / `storeNetGroup`, `sortPositionsMaxMinX/Y`,
      `Net::tally`, `Node::m_mutex`/`lock`/`unlock`, `Node::m_is_large`/`checkIfLarge`/`isLarge`,
      `get_index(thread::id)`, and the `VEC_SIZE`/`LCM_BUFFSIZE`/`PARTIALS_GRAPH_COUNT`/
      `BINS_PER_ROW` constants. (`DATAFLOW.md`'s open-formats list still says "mirror sw_only
      `prepareNetGroup`" — that function no longer exists anywhere; the AIE HPWL packet grouping
      has to be specified from scratch.)

      **Two pl_algo call sites had to change**, both because pl_algo was frozen against an older API:
      `Packer.cpp` `pin.node` → `pin.node_p`, and `DensityVerify.cpp`
      `Grid::computeBinOverlaps(n)` → `computeNodeOverlaps(n, false)` + `depositNodeOverlaps(n)`
      (the deterministic split; it does not depend on whether the host was built with OpenMP).

      **⚠ THE MERGE EXPOSED A LIVE BUG — `make run-density` has been checking against a stale
      golden.** pl_algo's frozen `Grid.cpp` had **no √2 density clamp**; the PL gained it
      2026-07-05 (`node_footprint.hpp`, commit `0237e57`). So every `--density` sw_emu run since
      then compared a *clamped* device rho against an *unclamped* software rho. **Any PASS recorded
      in that window is void — re-run `make run-density`.** This is exactly the failure mode this
      TODO predicted, and it had already happened.

      **Verified (no build available — another session held the CPU):**
      - `g++ -fsyntax-only` clean on all 17 sw_only TUs (incl. `-DCREATE_VISUALIZATION`) and all
        12 pl_algo TUs (incl. `-DUSE_XILINX_XRT`, and `Driver.cpp` under the new ABI).
      - `make -B -n` for `HOST=sw_only`, `HOST=pl_algo`, and `HOST=pl_algo BUILD_XRT=1`: every
        source resolves through the right pattern rule, and the pl_algo link line picks up
        `-L .../host/src/common/lib`.
      - **NOT verified: linking, running, or any numerical result.** Do a `make host` on both
        variants and one `verify_swonly.sh` before trusting anything.

- [ ] **STEP 1b — re-run `make run-density`** and record the number. See the ⚠ above. While there:
      the software `computeNodeFootprint` and the PL `node_footprint` still differ on **one** thing —
      the PL shifts an overhanging footprint back on-grid, the software golden does not (it relies on
      `Placer::enforceDieBoundaries`, which does not run in the verify harness). Affects only cells
      within ~√2 bins of the die edge. Decide whether the golden should shift too, or whether the
      harness should project first; do not change synthesizable HLS to chase it blind.

- [ ] **STEP 2 — collapse the two hosts into ONE binary** (the original target: one host that runs
      the iteration on the CPU, or offloads to the VCK5000 when a card/xclbin is available). Now
      unblocked, and deliberately NOT attempted 2026-08-04: it means merging `Placer` with
      `Driver`/`Placement.hpp` and collapsing the compute-method dispatch (`partials_method` /
      `density_method`) into a CPU-vs-accelerated selector — an 18-site refactor of hardware-driving
      code (see #10's `KernelSession` item) that cannot be signed off without a real build + an
      sw_emu re-verify. Step 1 was the part that was safe to do by inspection.

      **Historical context — why it needed doing.** The two trees were a silent fork, not a shared
      base. 15 files existed in both `host/src/sw_only/` and `host/src/pl_algo/` under the same class
      names and namespace, with the diffs already large:
      | file | diff lines |
      |---|---|
      | `src/DataBase.cpp` | 886 |
      | `src/Grid.cpp` / `src/Net.cpp` / `src/Logger.cpp` | 114 / 94 / 80 |
      | `include/DataBase.h` / `include/Bin.h` | 111 / 101 |
      | `include/Logger.h` / `include/Net.h` / `include/Common.h` | 44 / 41 / 40 |
      | `include/Node.h` / `include/MacroClass.h` / `include/Grid.h` / `include/IOPad.h` / `include/Component.h` | 33 / 19 / 17 / 15 / 13 |
      | `src/Common.cpp` | 8 |
      A parser or geometry fix landed in one tree did not exist in the other, and nothing caught it
      — the density-clamp bug above is the proof.

- [x] **DONE 2026-08-05 (Mark's call) — Limbo is a REAL git submodule; zero `.a` tracked anywhere.**
      Mark: *"They should be added to the github project as a gitmodule... not have multiple copies
      of a library in our github."* The state was worse than "multiple copies":
      - `.gitmodules` **already declared** `third_party/Limbo` a submodule pointing at
        `https://github.com/limbo018/Limbo.git`. It had been de-submodularized at some point —
        commit `1d23609 "Rename Limbot to Limbo"` looks like a plain `mv` + `git add` over the
        top of `3682a4b "Add third_party/Limbo submodule"`, which turned the gitlink into 4195
        ordinary files. `git submodule status` listed only `Vitis_Libraries`; `.git/config` had
        no submodule sections at all.
      - **76.4 MB of a 103 MB repo (74%) was that one vendored dependency**, including
        **three** duplicate sets of the same static libraries: `Limbo/build/**/*.a` (25),
        `Limbo/lib/*.a` (25), and the 5 the host actually linked.

      **Which upstream commit we were on — determined, not guessed.** Cloned upstream and diffed
      our tree against every candidate ref: tag **3.5.2 (`81b64433`)** is the match, with exactly
      one source difference — `limbo/parsers/gdsii/gdsdb/GdsObjectHelpers.h` gains `std::round`
      in the SREF rotate/magnify helpers. Upstream has the semantically identical fix in
      `0ce68951` but on a divergent gdsdb lineage (11 files apart from us), and `git log -S` finds
      that exact code nowhere, so it is a **local patch**. Preserved (tracked, with the reasoning)
      at `third_party/patches/limbo-3.5.2-gdsdb-round.patch`; deliberately NOT re-applied — see
      that directory's README. It cannot affect AIEplace: we link only
      lefparseradapt / defparseradapt / bookshelfparser / gzstream, and include only
      `gdsii/stream/GdsWriter.h`, never gdsdb.

      **What landed:** `git rm -r --cached third_party/Limbo` → real `git submodule add` pinned
      to 3.5.2; `git rm -f` the 5 `common/lib/*.a`; both `makeflags.mk` take headers from the
      submodule (`-I third_party/Limbo`) and libs from an **out-of-tree** build
      (`-L third_party/limbo_install/lib`). Out-of-tree on purpose: building *inside* the
      submodule works (Limbo's own `.gitignore` covers `build/`/`lib/`/`include/`/`bin/`) but
      leaves `?? share/` dirty, and a submodule that is ever dirty will eventually be committed
      dirty. `*.a` is now in `.gitignore` alongside `*.o` so this cannot recur silently.
      New: `tools/bootstrap_third_party.sh` (+ a rewritten `host/README.md` explaining what a
      submodule is and why a fresh clone needs the step).

      **Verified by running, not inspection:**
      - Limbo builds from a clean submodule checkout; all 25 libs install; the submodule is
        `git status`-clean afterwards (0 entries) — asserted by the script itself.
      - Clean `make host HOST=sw_only` against the submodule: links.
      - Same config/seed on mgc_pci_bridge32_a **before vs after** the swap: `converged` at
        iteration **617**, Final HPWL **3.368e+08**, exact **3.376e+08**, smoothed overflow
        **6.170e-02**, exact+filler **1.978e-01** — identical in every digit. The freshly built
        parsers are behaviourally indistinguishable from the retired `.a`.
      - **`-DBoost_NO_BOOST_CMAKE=ON` is REQUIRED on this box**, not cosmetic: a stray
        `/usr/local/lib/cmake/Boost-1.80.0/BoostConfig.cmake` advertises Boost 1.80 while the
        real system Boost is 1.71, so CMake's config mode dies on
        `Could not find a configuration file for package "boost_graph" ... version "1.80.0"`.
        Both the script and the README say so.
      - `bootstrap_third_party.sh --clean` run end to end from a wiped build: all 25 libs
        produced, submodule still 0 dirty entries, host relinks against the result.

      ⚠ **This does NOT shrink the existing clone.** Untracking removes the 76 MB from HEAD, not
      from history — every past commit still references those blobs, so the pack stays ~103 MB.
      Only a `git filter-repo`/`filter-branch` rewrite would reclaim it, and that **rewrites every
      commit hash**, breaking every existing clone and any pushed branch. Mark's call, separate
      job, not started.

- [x] **DONE 2026-08-05 — the rest of `third_party/`, per Mark: "I want the clone to pull third
      party repos as submodules."**
      - **`tabulate` is now a submodule too** (`p-ranav/tabulate`, pinned `3a58301`). Identified
        the same way as Limbo: diffed our copy against every candidate ref — `3a58301` matches
        **byte-for-byte, no local patch**, so this is behaviour-identical. Header-only, so no
        build step; the include path `third_party/tabulate/include` is unchanged. FYI upstream
        master is 2 commits ahead and carries a locale-restoration fix in `table_internal.hpp`
        (`3fba623`) that we do not have — taking it is a deliberate choice, not done blind.
      - **`CImg-3.2.6` deleted** (Mark: *"Yes remove CImg. That was a possible alternative"* —
        to cairo). 14.6 MB / 72 files, referenced by nothing.
      - **`third_party/cairo` LEFT ALONE, but it is dead too** — 15 headers copied out of
        `/usr/include/cairo`, referenced by no makefile (the visualizer uses the *system* cairo
        via `-lcairo` and `<cairo/cairo.h>`). 0.2 MB. Not removed because it was not asked for;
        say the word.
      - Bootstrap names its submodules instead of `--init --recursive`, so it cannot drag in
        `vck5000/aie/lib/Vitis_Libraries` (gigabytes, AIE builds only, still uninitialized).

- [x] **DONE 2026-08-05 — Boost reconciled (Mark: "I've had plenty of trouble with boost versions
      in the past").** There are **two** Boosts on this box and three different version claims
      were in play. Measured, not assumed:
      | | version | state |
      |---|---|---|
      | `/usr/include` | 1.71 (apt) | complete — headers + every `.so` |
      | `/usr/local/include` | 1.80 (source build) | headers complete, **only some `.so`** (no `graph`, no `regex`) |
      | `$HOME/local/boost_1_82_0` | "1.82" | **does not exist** |

      gcc searches `/usr/local/include` first, so **everything here actually compiles against
      1.80** — while `sw_only/makeflags.mk` carried `-I${HOME}/local/boost_1_82_0/` (a
      nonexistent path implying 1.82) and Limbo's CMake reported 1.71. Worse, with
      `-DBoost_NO_BOOST_CMAKE=ON` CMake pairs **1.80 headers with the 1.71 `libboost_graph.so`**
      — a real ABI bug.
      - The dead `-I` is **deleted**, replaced by a comment explaining why there must be no `-I`
        at all (gcc de-duplicates `-I` against its own system dirs, so it could never reorder
        `/usr/local/include` ahead of `/usr/include` anyway — it could only misdescribe reality).
      - **Why the header/lib split is harmless here, now asserted rather than believed:** Boost
        is header-only across everything we link. `bootstrap_third_party.sh` checks on every run
        that (a) the host's Boost version, (b) the version Limbo was configured with, and (c) the
        count of undefined `boost::` symbols in the four archives we link (must be 0) all agree.
        Currently: `host 1_80 / Limbo 1_80 / 0 symbols`. If it ever fires, the fix is a decision
        about this machine — complete the 1.80 install or remove it so 1.71 wins — not a repo flag.
      - Full writeup in `host/README.md` under "Boost — read this before debugging a Boost problem".

- [ ] **Dependencies** — `vck5000/requirements.txt` was just added (JSON→TOML config migration
      session) covering `tomlkit`, `numpy`, `matplotlib`, `SALib`, `Pillow`, `pyunpack`, `patool`.
      `pyunpack`/`patool` (used only by `host/benchmarks/ispd2005_2015.py`/`ispd2019.py`, the
      benchmark-download scripts) were NOT installed in this environment and so were never actually
      exercised against the new file — verify `pip install -r requirements.txt` succeeds clean and
      a benchmark download still works (patool may also need a system `unrar`/`7z` on PATH for some
      archive formats). Keep the file in sync as new scripts/imports are added.

---

## #10 — pl_algo cleanup & clarity (opened 2026-07-30) — **MOSTLY DONE 2026-08-02**

From the 2026-07-30 fresh-eyes codebase review. Clarity/hygiene, not blockers. Worked 2026-08-02
under a no-CPU constraint (another session held the box for sweeps), so everything below was
verified by inspection / `g++ -fsyntax-only` / `make -n` — **no build, no synthesis, no emulation**.
Report: `2_ARTIFACTS/_NEW_pl_algo_cleanup_20260802.md`.

### Stale docs that actively mislead (cheapest, highest value) — ALL DONE
- [x] DONE **`pl/src/pl_algo/README.md` rewritten.** Status now reflects Gate-1-passed + verified
      datapath; Layout lists the modules that actually exist, split into datapath / control
      (built-not-wired) / PL-only alternates; adds `model/`, `host_interface.hpp`, the `run-*` target
      list, and a line naming `DATAFLOW.md` as authoritative where the two disagree.
- [x] DONE **Root `CLAUDE.md` "pl_algo current state" refreshed and re-pointed.** It now defers to
      `DATAFLOW.md` explicitly ("update it, not this section") and keeps only the stable summary.
      Also fixed the Verification-references paths — the golden functions moved to `src/placer/`.
- [x] DONE **`Driver.cpp` file header** replaced: describes the real one-kernel/`mode` contract and
      points at `host_interface.hpp` `top_mode` + `top.cpp` as the authorities.
- [x] DONE **`CHECKPOINT.md` archived** → `docs/archive/pl_algo_CHECKPOINT_history.md` (via `git mv`,
      history preserved) with a HISTORICAL banner that names the 4 known-stale headline claims
      (BB clamp, `dct_normalize_inverse`, the other retired flags, "GP only") and points at the
      current sources. Nothing but TODO.md referenced the old path.

### Dead / unwired code — ALL DONE
- [x] DONE **`src/modules/density_manager.hpp` deleted** (`git rm`). The three prose references to
      it (`density_bin.hpp`, `host_interface.hpp`, the two `model/*.cpp` headers) were retargeted to
      "the density solve" so nobody hunts for the file.
- [x] DONE **`bb_reduce` + `param_scheduler` built-but-not-wired is now explicit** — a blockquote in
      `DATAFLOW.md` Status saying it is deliberate, what *does* exercise them (`synth_check.tcl`,
      `sched_verify.cpp`), and "do not re-derive these". Echoed in README + CLAUDE.md.
      Also un-staled `DATAFLOW.md`'s open-decisions list: IDXST is implemented, not deferred.

### Structural
- [x] DONE **`run-*` targets folded.** `STAGE4_RUN` → `EMU_RUN` (gained an optional `$(2)` for
      trailing args), and all 16 `run-*` targets now go through it; ~90 lines of copy-paste gone.
      **Verified:** `make -n` before/after for all 16 targets — every emitted shell command is
      byte-identical modulo line-continuation joining. (The review said "20+ / ~14 duplicates";
      the real count was 9 duplicates + 7 already on the define.)
- [~] PARTIAL **Port aliasing in `top.cpp`.** Added a compact **PORT-ALIAS TABLE** above `top_mode`
      in `host_interface.hpp` — one scannable grid of mode × gmem port, plus the scalar aliases, so
      the mapping no longer has to be reconstructed from 40 lines of prose. The *code-level* fix
      (per-mode struct of named references / `static_assert`-able aliases) was NOT attempted: it
      changes a synthesizable kernel and cannot be honestly signed off without HLS C-synthesis, which
      the CPU constraint ruled out. Stage 5's unified datapath still supersedes this.
- [ ] **`Driver.cpp` is 18× the same XRT boilerplate** (~1188 lines): open device → load xclbin →
      alloc bo per arg → memcpy → sync-to-device → run → sync-back, once per `run*()`. A small
      `KernelSession` helper (device/uuid/kernel + a `bind(idx, ptr, bytes)`) would cut it hard and
      make the port-aliasing above visible in one place. **Not attempted 2026-08-02** — an 18-site
      refactor of hardware-driving code needs a real build + an sw_emu re-verify, not a syntax check.

### Repo hygiene (pl_algo-scoped)
- [x] DONE **Build artifacts untracked.** `git rm --cached` on `pl/src/pl_algo/_x/` (9 files) and the
      two model ELFs; `_x/` added to `pl/src/pl_algo/.gitignore` and all five `model/` binary names to
      `model/.gitignore` (verified with `git check-ignore -v`). **Staged, NOT committed** — the
      working tree also holds another session's unrelated edits, so the commit is Mark's call.
- [ ] **`common.mk` defaults point at the dead variant:** `AIE ?= markv1`, `PL ?= markv1`. A bare
      `make` builds the legacy partial-offload design, not `pl_algo`. Flip once pl_algo is the
      primary target (`HOST ?= sw_only` should probably stay until the #9 merge lands).
      **Left alone 2026-08-02 on purpose** — "is pl_algo the primary target now" is Mark's call, not a
      cleanup; note that flipping it also means updating the `CLAUDE.md` "What this is" line that
      documents the current defaults.

---

## #11 — XPlace density-footprint faithfulness gaps (opened 2026-07-30)

> ### CLEANUP DONE 2026-07-31 — asymmetric, on purpose (Mark's call)
>
> **#11a `xplace_die_projection` — toggle DELETED, projection now unconditional.** Its toggle
> guarded the *legacy* path, which is the one that is NOT XPlace-faithful, so there was no
> faithfulness argument for keeping it, and the A/B measured it neutral (±0.7%). Removed:
> the member + config key, the `if (xplace_die_projection)` guards in `Step.cpp`
> (`enforceDieBoundaries`) and `Setup.cpp` (`initializePlacement`), the deposit-time shift in
> `Grid.cpp::computeNodeFootprint`, and `FootprintConfig::shift_in_die` /
> `Grid::setShiftFootprintInDie` / `m_shift_footprint_in_die`. The in-die correction now lives
> in exactly one place (the position) instead of two.
> *Safety condition verified before deleting the shift:* fillers ARE in `mv_movable_nodes`
> (`DataBase.cpp:342`) and `enforceDieBoundaries` clamps BOTH `node_pos` and `probe_pos`
> (`Step.cpp`) — and `computeNodeFootprint` reads the probe — so every non-FIXED node's footprint
> is legal by construction. Fixed nodes are geometrically clipped by the caller as before.
>
> **#11b `macro_td_expand_ratio` — toggle KEPT, default false.** Reverses the report's
> "delete the losing branch" next-step, and resolves its contradiction with the note below
> (which already said the toggle "stays in the code as a documented, deliberate divergence").
> Two reasons:
> 1. It is XPlace-FAITHFUL (`database.py:921-923`). Per CLAUDE.md a divergence must be deliberate
>    and documented; the toggle *is* that documentation, and it keeps the divergence reversible.
> 2. **Its rejection is CONFOUNDED with the stop criterion.** The A/B report itself records that
>    every `true` arm "halted 25–65 iterations early on a deflated signal" — macros depositing at
>    `target_density` push less mass into the smoothed overflow that drives convergence. So the
>    +5.2% HPWL is partly "this arm stopped sooner", not purely "this arm places worse".
>    **Re-test #11b once the stop criterion is fixed (TODO #4).** Deleting it now would mean
>    re-implementing it to run that test.
>
> **UNBLOCKED 2026-08-07.** The stop criterion was fixed by **#19**, not by #4 (whose premise was
> retracted — see `history.md`). The re-test this bullet is waiting on can now be run.
> `tagMovableMacros()` / `Node::m_is_movable_macro` therefore STAY — they exist to serve #11b.
>
> **VERIFIED bit-identical.** adaptec1, 60 iters, `random_seed = 42`: HEAD (`56d8a16`, pre-removal)
> and this tree both give restored-best HPWL `60353676.000000`, overflow `0.826129`. Three
> consecutive runs of each are also identical to themselves, so `deterministic = true` holds.
>
> ⚠️ **Gotcha that cost time here — `run_config.toml` does NOT set `random_seed`.** It defaults to
> `-1`, which `Setup.cpp::initializePlacement` turns into `std::srand(std::time(nullptr))` — a
> time-based seed, so a bare template run randomizes the initial placement and looks
> nondeterministic. Sweeps are unaffected (they pin `random_seed` explicitly, e.g.
> `gen_footprint_ab_configs.py`). **Any manual A/B must pin `random_seed`** or it measures
> seed-to-seed variation, not the change under test.
>
> **DEFAULTS APPLIED 2026-07-31.** `xplace_die_projection = true`, `macro_td_expand_ratio = false`
> is now the default in both `run_config.toml` and the `AIEplace.h` member-initializer fallback (so
> the two agree whether or not a config file names the keys). Rebuilt clean; confirmed the new
> default changes the trajectory vs. the pre-#11 golden (as expected, since #11a is not a no-op) via
> the header-fallback path — `ef1c8f29…` vs. the old legacy `8019a989…`. **Any goldens captured
> before today no longer match** — that's intentional per the A/B, not a regression; recapture
> deliberately rather than diffing against old references. #11b confirmed faithful to XPlace too
> (its `database.py:921-923` / `density_map_cuda_kernel.cu:28` overwrite `expand_ratio` with
> `target_density` for movable macros, breaking area conservation on XPlace's own side) — rejected
> on results, not on faithfulness grounds; the toggle stays in the code as a documented, deliberate
> divergence rather than an oversight. Toggles NOT yet deleted (see cleanup steps below).
>
> **RESULT 2026-07-31 — A/B DONE (46/48 runs). Report:
> `1_REVIEW/_NEW_REPORT_footprint_ab_20260731.md`.**
> **#11a ADOPT** — exactly neutral (mean +0.0% HPWL / 15 designs, worst 0.7%); take it for
> faithfulness + deleting the deposit-time-shift branch. **#11b REJECT** — mean +5.2% HPWL worse
> / 7 designs, and worst (+17.9%, +11.2%, +6.1%) on adaptec5/newblue5/newblue4, the exact designs
> it was meant to help. Caveat: a rebuild landed mid-sweep (00:05 Jul 31, the TODO #12 threading
> work); newblue7's two arms ran on the new binary and are excluded — no conclusion mixes binaries.
> Cleanup steps listed at the end of the report.
>
> **STATUS 2026-07-30: both implemented behind runtime toggles; A/B prepped, NOT yet launched.**
> `xplace_die_projection` (#11a) and `macro_td_expand_ratio` (#11b) in `run_config.toml`, both
> default `false` = legacy. Legacy path verified bit-identical to the pre-change golden. Harness:
> `2_ARTIFACTS/gen_footprint_ab_configs.py` (48 configs), `run_footprint_ab.sh`,
> `analyze_footprint_ab.py`. Launch with:
> ```
> python3 2_ARTIFACTS/gen_footprint_ab_configs.py
> nohup bash 2_ARTIFACTS/run_footprint_ab.sh > /tmp/fp_ab/runner.log 2>&1 &
> ```
> Decide from the scorecard, then **delete the losing branch** — these toggles are temporary.

Found by verifying `computeNodeFootprint()` line-by-line against XPlace source (not comments).
**Most of the footprint is faithful** — √2 clamp, area-conserving weight, centering, macros
unaffected, fixed nodes deposited at exact size and geometrically clipped, fixed density capped at
`target_density`. Two genuine divergences fell out, neither previously documented. Per CLAUDE.md,
divergence must be *deliberate and documented* — right now these are accidental.

### (a) The in-die shift is applied to the wrong thing, with the wrong size
**XPlace** (`run_placement_nesterov.py:5-11`, applied in `calculator.py:27` on **every** gradient
evaluation) constrains the **position**:
```python
node_pos_lb = mov_node_size / 2 + data.die_ll + 1e-4
node_pos_ub = data.die_ur - mov_node_size / 2 + data.die_ll - 1e-4
x.data.clamp_(min=node_pos_lb, max=node_pos_ub)
```
`mov_node_size` here is the **EXPANDED (√2-clamped)** size (`get_mov_node_info` returns
`mov_node_size = clamp_mov_node_size`), and `node_pos` is the node **CENTER** (confirmed:
`GPDatabase::getNodeCPosTensor` = `Lx + Width/2`; the CUDA kernel forms `node_pos ± node_size/2`).
So XPlace projects the optimization variable so the *expanded* footprint is always fully in-die.
The cell itself moves.

**sw_only** does it in two disconnected places, and neither matches:
- `enforceDieBoundaries()` (`Step.cpp`) clamps `node_pos`/`probe_pos` using the **RAW** size
  (`max_x = die_w - getXsize()`), so the expanded footprint can still hang off the edge.
- `computeNodeFootprint()` then applies a **second, non-persisted** shift of the expanded footprint
  at deposit time.

Net effect: both keep the deposit fully in-die (no lost mass), but for any cell within
`(cw-w)/2` of the edge XPlace moves the *cell*, while sw_only leaves the cell and displaces only
the phantom footprint — so the deposited mass sits off-centre from the cell it belongs to. The force
gather is self-consistent (it reads the same shifted `BinOverlaps`), so this isn't a bug, but it is
not XPlace's projected-gradient formulation and it biases edge cells differently.

- [ ] Decide: adopt XPlace's form (clamp position with the expanded size in `enforceDieBoundaries`,
      drop the shift from `computeNodeFootprint`), or keep ours and document why. Adopting it makes
      `computeNodeFootprint`'s movable branch disappear entirely, which is a simplification.
      **Not behavior-preserving — needs a suite re-baseline either way.**

### (b) Movable macros: XPlace overrides expand_ratio with target_density
`database.py:921-923`, applied *after* the area-conserving ratio, so it **overwrites** it:
```python
if args.target_density < 1.0:
    expand_ratio[mov_lhs:mov_rhs].masked_fill_(self.is_mov_macro[mov_lhs:mov_rhs], args.target_density)
```
A movable macro's deposited density is forced to `target_density` rather than `real/clamped` area.
sw_only has **no equivalent** — movable macros always get `weight = 1` (they exceed the √2 clamp).

Only fires when `target_density < 1.0`, so ISPD2005 (all target_density 1.0) is unaffected — but it
fires on exactly the ISPD2015/MMS designs with movable macros, target_density 0.42–0.9. That is the
same population as the known MMS under-spreading problem (#4, and auto-memories
`mms-hard-spreading-three-diseases` / `overflow-metric-grid-faithfulness`).

- [x] **DONE — stale checkbox.** This landed 2026-08-02 as `macro_deposits_target_density` and the
      toggle was deleted the same day; the XPlace-faithful branch is unconditional in
      `Grid.cpp::computeNodeFootprint`. It is a large part of the actual adaptec5 fix (removes the
      overflow floor that starved λ's feedback loop) — see the #8 banner and #4's closed adaptec5
      entry. The `[ ]` here survived the landing; the answer to "does it close the residual
      under-spread" is **yes, in combination with phase 2**.

---

## #14 — Zoomable Visualizer window (opened 2026-07-31)

The Visualizer only ever renders the whole die. At MMS scale that is 200k–1M cells in a few
thousand pixels, so everything below the macro scale is a grey wash — we have never actually
*looked* at the placement at the resolution the algorithm operates on.

Motivating observation (from the TODO #11/#13 filler work): global placement is easy to reason
about as a continuous density field and forget that the target is a row-based standard-cell
layout. sw_only sized fillers from a trimmed mean of cell heights rather than the row height for
exactly that reason. A zoomed view would make the row structure, the filler distribution, and the
macro edges visible, which is the kind of thing that produces insight rather than another metric.

- [x] **DONE 2026-08-04 — configurable zoom window.** `Visualizer` now renders a `ViewWindow`
      (a rectangle in die coordinates) instead of hard-coding the die, through four helpers
      `mapX/mapY/mapW/mapH`. The full-die view is the same window with `xl=yl=0`, so it computes
      the pre-zoom expression exactly — deliberately anchored at the ORIGIN rather than at the die
      box's lower-left, because that is what every coordinate map here always did. (One caveat:
      `drawIOPad` used to divide-then-multiply where everything else multiplied-then-divided;
      routing it through the same helper normalizes that, so IO-pad rectangles can shift by up to
      1 ULP of a double — far below a pixel, but it is not literally byte-identical.)
      Config: `output.zoom` / `zoom_center_x` / `zoom_center_y` / `zoom_span`, next to
      `visualize` / `iterations_per_export`. Centre and span are **fractions of the die** (span
      of the *shorter* dimension) rather than absolute coordinates, so one setting means the same
      magnification on every benchmark — MMS die sizes span two orders of magnitude. The window
      is not clamped to the die: seeing the die edge is one of the things worth looking at.
- [x] **DONE 2026-08-04 — the detail layers, all four, and all zoom-only.** Row pitch (from the
      new `DataBase::getRowHeight()`) and density-bin boundaries under the cells; per-cell
      outlines and grey-vs-blue filler/cell separation over them. A layer denser than
      `MAX_DETAIL_LINES` (256) is dropped rather than drawn as a solid wash. Geometry is clipped
      to the window **only when zoomed** — in the full-die view, FIXED terminals legitimately sit
      in the margin outside the core-row die (TODO #4) and clipping would silently hide them.
      Row lines assume a uniform pitch from the origin: that is the row grid sw_only implicitly
      targets, and 11 of 16 MMS designs actually have a ragged core (TODO #3) — so this drawing
      is the *placer's* view of the rows, and a design where the real rows disagree is itself
      worth seeing.
- [x] **DONE 2026-08-04 — GIF path.** Zoom frames go to `placement_zoom/` with the same
      `iter_<N>.png` naming, so `gif_builder.py` needs no change; `Output.cpp` builds
      `zoom_placement.gif` next to `full_placement.gif`. `tools/make_viz_gifs.py --zoom`
      (optionally `--zoom CX,CY,SPAN`) turns it on. The one-off `best_solution` frame shares the
      run directory and takes a `_zoom` filename suffix instead.

- [x] **DONE 2026-08-05 (Mark's call) — the y axis is no longer mirrored.** Cairo's user-space y
      grows DOWNWARD while die y grows upward, so every frame this renderer has ever produced was
      vertically flipped. Fixed in `mapY` (plus `scaleY` for the E-field overlay) rather than with
      a `cairo_scale(1,-1)` transform, because a transform would mirror the overlay TEXT as well.
      Two consequences that are easy to get wrong and are handled explicitly:
      - `cairo_rectangle` grows downward from its anchor, so a die-space box must now be anchored
        at its **top** edge: `mapRectTop(die_y, die_ysize)`. Used by `drawComponent`,
        `drawIOPad`, and the focus-net bounding box.
      - `drawElectricField`'s arrows take `-y_mag`, so a field pushing cells toward larger die y
        draws as an arrow pointing UP the image.
      ⚠ **Every PNG/GIF produced before this is mirrored relative to every one produced after.**
      That includes `2_ARTIFACTS/newblue5_placement.gif` and the whole `GIFS_*` pile. Do not
      compare an old frame against a new one and conclude the placement moved.

**Verified by rendering** on `mgc_pci_bridge32_a` (converged, 617 iters): zoom + full-die frames
and both GIFs produced, caption fits, y-up confirmed against the DEF/LEF to within 6 px on all
four fixed macros. **Not** yet run on a full MMS design —
`python3 tools/make_viz_gifs.py --designs newblue1 --zoom --every 50` is the next check. Watch
for whether `MIN_SIZE` (0.001 of canvas) still floors anything at zoom, where it should not.

### Follow-ups opened 2026-08-05 (Mark)

- [ ] **Zoom window locked to a NODE, not a fixed region.** Give the window a target — a node
      name, or "the node with the largest movement this iteration", or a macro — and re-centre it
      every frame so the animation tracks that cell through the run instead of watching cells
      drift through a static box. This is the version that answers "what is the optimizer *doing*
      to this cell", which a fixed window cannot. Needs the window to be recomputed per frame,
      so it lands naturally in the new offline tool (below), not in the host.
- [ ] **Multiple zoom levels / regions per run.** Today `output.zoom*` is a single window fixed
      at setup, and changing it means re-running the placement. Same conclusion: this belongs in
      an offline tool.
- [→] **MOVED TO THE VIZ REWORK (below):** both of the above, plus the existing zoom config keys.
      Handoff: `1_REVIEW/handoffs/_NEW_HANDOFF_viz_offline_tool_20260805.md`.

---

## #15 — Net-local coordinate frames for the wirelength gradient (opened 2026-08-03)

**Motivation is PL, not sw_only:** a net whose pins are tightly clustered but sit at large absolute
coordinates (top-right of the die) burns precision for nothing. Store each net's pin coordinates
**relative to that net's own min** — so every net has a pin at x=0 and a pin at y=0 — and the
absolute die offset never enters the gradient arithmetic at all. Smaller data types then become
viable on PL, which is the real payoff.

### The enabling fact: the WA gradient is translation-invariant
Shift by any per-net constant `k` (`u_i = x_i − k`, `C' = C − k·B`) and the `k/γ` terms cancel
exactly:
```
(1 + u_i/γ)·B₊ − C'₊/γ  =  (1 + x_i/γ − k/γ)·B₊ − C₊/γ + k·B₊/γ  =  (1 + x_i/γ)·B₊ − C₊/γ
```
Same on the minus side with `k = x_min`. So this is a **reframing, not an approximation** — the
computed gradient is the same function, and because a gradient is already frame-independent the
result needs no un-shifting before it accumulates onto the node.

### What today's code actually does
`Partials.cpp:243-246` shifts **only the exponent** (`exp((p.x - max_x) * inv_gamma)`), purely to
stop `exp()` overflowing. The `x_i` in `C` (`Partials.cpp:258-261`) and in the `(1 ± x_i/γ)` factor
(`Partials.cpp:288-296`) stay **absolute**. So the bracket subtracts two O(`x_max/γ`·B) quantities
to produce an O(B) result — a cancellation of exactly `x_max/γ`. XPlace does the same thing
(`wa_wirelength_hpwl_cuda_kernel.cu`: it forms `s_x = C₊/B₊` first, but then computes
`grad_const + x_coeff*cur_x`, re-incurring the identical cancellation), so adopting this is a
**deliberate divergence** and must be documented as one per the root CLAUDE.md rule.

### Measured (float32 vs float64 reference on *identical* float32-rounded inputs)
Harness: `2_ARTIFACTS/net_local_frame_precision_test.cpp` (`g++ -O2`, self-contained, ~1 s).
Error is normalized by the net's largest |partial|. Gammas are the real schedule endpoints
(`base_gamma` 167 adaptec1 / 433 bigblue3, late `γ = 0.1·base`).

| case | `x_max/γ` | current | net-local | gain |
|---|---|---|---|---|
| adaptec1 early (γ=10·base) | 7 | 6.3e-08 | 2.5e-08 | 3× |
| adaptec1 late (γ=0.1·base) | 694 | 7.3e-07 | 2.4e-09 | **301×** |
| adaptec1 late, tight nets | 311 | 4.4e-06 | 2.0e-08 | 218× |
| bigblue3 late | 641 | 7.5e-07 | 2.6e-09 | 283× |
| offset frame (origin 1e6) | 60574 | 1.1e-04 | 2.4e-09 | **44536×** |

The current form's error tracks `x_max/γ` linearly; the net-local form sits at machine epsilon
**independent of coordinate magnitude**. Note the real MMS frames are only mildly offset
(`newblue4.scl SubrowOrigin 7728 / NumSites 10239`, `newblue3 7839 / 30757`) — same order as the
span, so ~1.75× amplification, *not* the 1e6 row. That row is illustrative, not representative.

### ⚠️ The trap: `C` and the `(1 ± x_i/γ)` factor must move TOGETHER
Shifting `C` alone leaves a residue of `(x_max/γ)·(a_i₊/B₊)` — measured mean error **62× the
signal**, max **6.94e+02** = exactly `x_max/γ`. It does not NaN; it silently points the gradient the
wrong way. Any implementation needs a test that would catch a half-applied shift.

### Why this matters on PL specifically
- Bookshelf coords are **integers** — do the per-net min-subtract in integer (exact), then convert
  the small offset to a narrow float/fixed. Precision is never lost in the first place.
- Offsets are bounded by the net bbox span, and pins with `|u| ≫ γ` underflow `exp()` to 0 anyway,
  so the range can be **hard-clamped** (e.g. `u/γ < −20 ⇒ a_i = 0`). Bounding the dynamic range is
  exactly what makes a narrow `ap_fixed` feasible — today you'd need the range to span `x_max/γ`
  ≈ 60k *above* an O(1) result.
- The streaming datapath already wants per-net pin arrays, so the layout may be close to free there.

### Open questions before implementing
- [ ] **Layout cost.** A node sits on many nets, so a per-net frame means per-net-pin duplication of
      coordinates rather than one position per node. Check what `pl_algo`'s pin streaming already
      materializes — this may cost nothing, or it may be the whole expense.
- [ ] **Scope boundary.** Only the *wirelength* gradient is translation-invariant. The density/field
      path needs absolute die coordinates for bin indexing. Define exactly where the frame converts
      back, and make sure nothing downstream of `probe_grad` assumes a shared frame.
- [ ] **Does sw_only change too?** On the CPU the current error is ~1e-6 relative — well below what
      BB / the line search reacts to, so expect **no HPWL movement**; do not sell this as a quality
      fix for sw_only. Changing it there is only worth it to keep the golden aligned with the PL
      datapath. If PL shifts and sw_only does not, the sw_emu partials tolerance must not be set
      tighter than ~1e-6 or it will chase a phantom.

Parked at Mark's request 2026-08-03 — analysis done, no implementation.

---

## Parked (not cleanup) — open technical follow-ups

- [x] **MMS faithful-field re-baseline** — DONE 2026-07-26 (full 16-design `false`-vs-`true` A/B). See
      report + `2_ARTIFACTS/mms_dct_ab_*`.
- [x] **XPlace-metric overflow re-measurement** — DONE 2026-07-26 (all 16, GPU). Confirmed the under-read
      → folded into **#4** (now in `history.md`; ⚠️ its filler conclusion was reversed by #19, so
      re-read the banner before citing this). The vs-XPlace "wins" on the 9 macro-heavy designs are
      under-spread artifacts; the 7 faithful designs' A/B numbers stand.
- [ ] **pl_algo initial-step mirror** — implemented in `host/src/pl_algo/src/Driver.cpp`
      (`estimate_initial_step`), compiles, but UNVERIFIED — needs Geert's card or sw_emu to check
      against the sw_only golden.
- [ ] **(optional) init_step_seed narrow-range Morris** — [0.005, 0.05] screen to positively
      confirm μ* collapses; mechanism already understood, so low priority.

**Relocated here 2026-08-07 when their parent task was archived** (see `history.md`):

- [ ] **SoA layout for the hot per-node/per-bin fields** (from #12). The next real threading win is
      layout, not more threads: `computeOverlaps`/`combineGradients`/`recordIterationResults` are
      memory-bound over pointer-chased `Node`/`Bin` objects and go flat by 4 threads. `Bin` is ~64+
      bytes (including a `std::vector<Node*> overlapping_nodes` that only the dead
      `Bin::computeOverlap` ever fills) but the density scatter touches only its 4-byte
      `total_overlap`. Big change, deserves its own task.
- [ ] **Logger cosmetics** (from #5) — all four are cosmetic or unrelated, none blocking:
      the two summary tables render with a **double border** because the callers nest a `Table`
      inside a title-only outer `Table` (`DataBase::printInfo`, `Placer::exportSummaryReports`);
      the welcome banner still goes straight to `cout` via `banner.print(cout)`, the last source of
      trailing whitespace (11 lines); `run.log` is written for **every** run including DSE sweeps
      (~250 KB/run at 1200 iterations, ~125 MB per 500-run sweep — gate on `quiet` if it bites);
      and "Algorithm time (s) | 0.000" in the Run Statistics table looks wrong (`algo_time` never
      accumulated?), noticed in passing and never investigated.

---

## #17 — sw_only has no automated tests (opened 2026-08-05, **BUILT 2026-08-05**)

**Built. `vck5000/test/regress/`, run with `make test-regress`.** Read
`vck5000/test/regress/README.md` before touching a baseline — it is the authoritative doc; this
entry records only the decisions and what is still open.

```bash
cd vck5000 && make test-regress          # 2 ISPD-2015 designs, ~12 s
cd vck5000 && make test-regress-slow     # + mms/adaptec1 (phase 2 + LP legalization), ~3 min
```

Per design it asserts two things, both **exact** (no tolerance): `iterations.dat` matches the
committed baseline row for row, and the `sha256` of the output `.def` (every final cell position)
matches. The trajectory says *when* behaviour changed; the hash catches drift below the 4
printed significant figures.

### Open decisions — all resolved by measurement 2026-08-05
- [x] **Which benchmark → `ispd2015/mgc_pci_bridge32_b` + `ispd2015/mgc_fft_a`.**
      **ISPD-2019 is unusable**, which settles the "unverified whether sw_only handles it"
      question: `DataBase::readDEF()` (`host/src/common/src/DataBase.cpp:199`) accepts *only* a
      file literally named `floorplan.def`, and ISPD-2019 ships `<design>.input.def`, so the DEF
      list is non-empty but `def_file` stays default-constructed and the parse fails with an
      empty path. `BENCHMARKS.md` independently rules ISPD-2019 out of scope (no XPlace data).
      ISPD-2015 is also the only suite shipping `placement.constraints`, so the fast tier cannot
      fall into the `td = 1.0` trap at all.
      **Two designs, not one** — the second is ~5 s and is not redundant: auto-sized grid 128 vs
      256, target density 0.143 vs 0.5, so grid sizing and the filler path differ.
- [x] **How many iterations → run to natural convergence.** Post-threading (#12) these are far
      cheaper than the pre-threading figures in `BENCHMARKS.md` suggest: full converged runs are
      **668 iters / ~5 s** and **616 iters / ~9 s**. No iteration cap needed, so the convergence
      criterion is itself under test and the iteration count is an assertion.
- [x] **What to assert → both.** Trajectory *and* the final-position hash. The hash costs one
      `sha256sum`, and the two fail differently enough to be worth having separately.
- [x] **MMS caveat →** `configs/slow/mms_adaptec1.toml` states `bins_per_row = 512` and
      `maximum_utilization = 1.0` explicitly (XPlace's tuned pair, `tools/benchmarks.py::_ROWS`).

### Decisions made while building, worth not re-deriving
- **The configs are frozen snapshots, not live copies of `run_config.toml`.** Editing
  `run_config.toml` does not affect this test. It is a tripwire on the **code**; a test whose
  expected output moves whenever a hyperparameter is tuned is one nobody can keep green.
- **`random_seed = 42` is pinned in every frozen config and is load-bearing.** Measured: two
  unpinned `mgc_pci_bridge32_b` runs took **668 vs 662** iterations and ended in different
  positions. `deterministic = true` alone is not enough (auto-memory `pin-random-seed-in-manual-ab`).
- **Determinism re-verified for this test, not assumed.** Identical trajectory *and* identical
  final-position hash across repeat runs and `OMP_NUM_THREADS` = 1, 4, unset, on both fast designs.
- **Baseline regeneration requires `--reason`**, which is written into the baseline header so it
  lands in the git diff beside the numbers it explains.
- **Measured sensitivity:** `init_gamma` 4 → 4.000001 (**2.5e-7 relative**) moves the printed
  trajectory at iteration **65** and changes the position hash. The failure paths were exercised,
  not assumed — perturbed input, corrupted hash with intact trajectory, missing `--reason`,
  unknown design, missing executable.
- **First real result, unplanned:** TODO #16 (commit 77d16ea, renderer moved out of the placer)
  is **behaviour-preserving for sw_only** — all three designs give identical trajectories and
  identical position hashes on the pre- and post-rebuild binaries. The frozen configs were then
  regenerated from the rewritten `run_config.toml` and every baseline still passed, which also
  confirms the deleted renderer keys (`visualize`, `zoom*`, `rand_focus_*`) were genuinely inert.
- **A slow tier exists because no ISPD-2015 design has movable macros** (all four candidates
  measured `num_movable_macros = 0`), so the fast tier cannot reach phase 2. `mms/adaptec1` has
  62 movable macros, 1373 iterations across both phases, ~150 s.

### Still open
- [ ] **Nothing checks quality, only stability.** A reproducibly *wrong* sw_only passes. Guarding
      the XPlace ratio would need committed reference numbers and a tolerance — a separate job.
*(A side finding from building this — `init_step_seed`'s second-order behaviour — is recorded
below rather than left open, since it answers itself.)*

### Side finding: `estimateInitialStep()` is second-order in `init_step_seed` (measured)
Noticed while probing the test's sensitivity, then pinned down. Perturbing `init_step_seed` on
`mgc_pci_bridge32_b` (all vs the committed baseline):

| relative change | result |
|---|---|
| 1e-5 | **bit-identical** |
| 1e-4 | **bit-identical** |
| 1e-3 | differs from iteration 2 |
| 1e-2 | differs from iteration 2 |
| 2× (0.02) | differs from iteration 1 |
| 0.1× (0.001) | differs; **2135 iterations** instead of 668 |

The seed cancels to *first* order in α = ‖Δpos‖/‖Δgrad‖ (Δpos ∝ seed), so a relative
perturbation `r` moves α by ~`r²`. That vanishes into float32 rounding until `r² > eps ≈ 1.2e-7`,
i.e. `r > 3.5e-4` — which is exactly where the measured threshold sits, between 1e-4 and 1e-3.

So this **validates** the self-calibrating initial-step refactor: the seed genuinely stops
mattering once it is anywhere near right, and only bites when it is off by orders of magnitude
(the 0.001 row). It is not dead config (`Setup.cpp:239`, read as `float`). Relevant to auto-memory
`init_step_seed_sa_payoff`, whose predicted Morris μ*/σ collapse did not happen: a screen over a
*narrow* range would see nothing at all here, which is consistent with that finding being
interaction-driven over a wide seed range.
- [ ] **`readDEF()`'s `floorplan.def` hardcoding is a latent bug**, not just an ISPD-2019
      inconvenience: a benchmark dir with `.def` files but none named `floorplan.def` fails with
      an empty path in the error message rather than saying what it wanted. Noticed here, not
      fixed — out of scope for #17.

---

## #19 — Two XPlace faithfulness gaps in the overflow metric and the schedule gate (opened 2026-08-06)

> ### ✅ CONFIRMED on all 16 MMS designs, 2026-08-06. Both found by reading XPlace source.
> Between them they explained why 10 of 16 MMS designs stopped on `divergence_guard` instead of
> converging, and why our phase-2 overflow descent was 3–6× slower than XPlace's.
>
> | | before | after |
> |---|---|---|
> | **post-DP HPWL vs XPlace** (**all 16** designs) | +1.15% | **+0.74%** |
> | post-GP HPWL vs XPlace (16 designs) | +2.06% | **+1.19%** |
> | **runs that `converged`** (rest = `divergence_guard`) | 6/16 | **15/16** |
> | post-DP density vs XPlace (8 measurable) | parity | **parity, unchanged** |
>
> *(Updated 2026-08-07: adaptec3 was missing from the original 15-design figure because our LG/DP
> harness crashed XPlace's legalizer — a bug in OUR input generation, now fixed; see #3. With it
> in, both arms move: 15-design +1.23%→+0.97% becomes 16-design **+1.15%→+0.74%**. adaptec3 is the
> single best design in the suite at **−2.69%** vs XPlace, up from −0.05%.)*
>
> **The post-DP gain is understated by that table.** The "before" column is flattered by
> newblue3's −4.35%, which was a *fake* win — it guard-stopped at 1087 iterations, under-spread.
> It now converges and reads +0.95%, an honest number replacing an artifact. Excluding newblue3
> from both sides: **+1.63% → +0.97%**.
>
> Worst case tightened more than the mean: the three worst post-DP designs went
> adaptec5 +4.38 / adaptec4 +3.59 / bigblue4 +3.20 → adaptec5 +3.17 / bigblue3 +2.39 /
> bigblue4 +1.64. Biggest single gains post-GP: adaptec4 +5.50→+2.00, bigblue4 +4.76→+2.87,
> newblue6 +4.35→+2.27, bigblue2 +2.50→+0.19, newblue7 +3.42→+1.57.
>
> **Direction of travel is exactly the diagnosis:** our exact overflow *rose* toward XPlace's on
> nearly every design (e.g. bigblue4 0.0833→0.1073 vs XPlace 0.1134) — we had been **over**-spreading
> and paying wirelength for it, because a filler-inflated stop signal kept a throttled λ grinding.
>
> Data: `2_ARTIFACTS/faithful_suite_results.tsv` (GP) and `2_ARTIFACTS/lgdp_faithful_results.tsv`
> (LG/DP), against `rebase_suite_results.tsv` / `lgdp_rebase_results.tsv`.
>
> **Remaining outlier: newblue4.** The only design still stopping on `divergence_guard`, and the
> only one whose overflow ended *above* XPlace's (0.2026 vs 0.1840). Post-DP +1.33%, so it is not
> a quality problem — but it is the one design the throttle fix did not convert, and worth its own
> look. bigblue1 also now sits slightly above XPlace on overflow (0.1260 vs 0.1178) while gaining
> HPWL (+1.34%→+0.49%).

### (a) RETRACTION — every XPlace overflow metric EXCLUDES fillers. We included them.

`convergence_include_fillers` was forced **true in phase 2** on 2026-08-02 (TODO #13) citing
*"XPlace's own std-cell-fixed-macro GP has no toggle for this; it always counts fillers."*
**That is wrong on all three XPlace code paths**, and so was the matching claim that its reported
overflow is `sharp/+filler`:

| XPlace path | what it does |
|---|---|
| `direct_calc_overflow` (`electronic_density_layer.py:272-292`) — the per-iteration + `GP Stop!` signal | slices pos/size/weight/expand_ratio to `[mov_lhs:mov_rhs]`, `num_mov_nodes = mov_rhs - mov_lhs` |
| `ElectronicDensityLayer.forward` (same file, 36-50) | computes `overflow` from `[mov_lhs:mov_rhs]`, and only THEN adds `filler_density_map` (from `[mov_rhs:]`) to build the map used for the **force** |
| `get_obj_overflow` (`evaluator.py:26-50`) — the reported "exact Overflow" | same slice, denominator `total_mov_area_without_filler` |

Fillers really are past `mov_rhs`: `get_mov_node_info` appends them (`database.py:901-904`).
XPlace's own comment (`evaluator.py:55-56`) says the only difference between its two overflow
metrics is **clamp-vs-exact node size — not fillers**. This also settles the contradiction TODO #8
left open ("code says excluded, the newblue2 calibration says included"): **excluded**, and the
`.claude/skills/xplace-compare` Trap-3 table already said so.

- [x] `Placer::convergenceIncludesFillers()` no longer forces true in phase 2 — both phases read
      the config key, whose default (`false`) is the faithful setting.
- [x] Headline `final_overflow` and `Phase2.cpp`'s phase-1 `overflow_exact` switched to
      filler-EXCLUDED; the summary rows are relabelled "(exact, no fillers)". The row-prefix
      `Final Overflow (exact` that `run_footprint_ab.sh` / `run_mms_ab.sh` grep is preserved.
- [x] `logOverflowDiagnostics`' legend corrected (was "XPlace GP stop = clamp/+filler, XPlace
      report = sharp/+filler").
- ⚠️ **Any overflow number recorded between 2026-07-31 and 2026-08-06 is filler-INCLUDED** and
      reads roughly 2× high against anything XPlace prints. That includes the "Final Overflow
      (exact, +fillers)" column of `2_ARTIFACTS/rebase_suite_results.tsv`.

### (b) The schedule throttle gates on the WRONG QUANTITY — this is the big one

XPlace freezes λ, γ and `precond_coef` together on 2 of every 3 iterations while
`weighted_weight ∈ (0.5, 0.95)` or `iter < 50` (`param_scheduler.py:284-289`), where

```python
alpha_1 = mov_node_to_num_pins                              # pin count
alpha_2 = precond_coef * density_weight * mov_node_area     # area, carries lambda LINEARLY
weighted_weight = ||alpha_2||_1 / (||alpha_1||_1 + ||alpha_2||_1)     # param_scheduler.py:386
```

sw_only computed `a1_norm`/`a2_norm` correctly in `updatePrecondWeights` — and then gated on
`density_force_fraction`, a **gradient-norm** ratio `‖λ∇den‖₁/(‖∇wl‖₁+‖λ∇den‖₁)`, while the
doc-comment above it claimed to be computing XPlace's `weighted_weight`. Different functions:

- **κ carries λ linearly ⇒ monotone.** It crosses (0.5, 0.95) **once**, in ~50 iterations, then
  sits at ~1.0 and the throttle is off for the whole endgame.
- **The gradient ratio is not monotone in λ** — ∇den *falls* as cells spread — so it drifts
  *into* the window late and holds the 3× throttle on exactly when λ needs to ramp.

Measured, MMS adaptec1 (td 1.0, grid 512, seed 42), phase-2 iteration 1163:
`kappa=0.999 force_frac=0.534 throttled=yes` — XPlace would be running at full rate.
Endgame λ growth per 100 iterations: **XPlace ×68 · legacy gate ×11 · κ gate ×20→×105.**

- [x] `Placer::scheduleGateMetric()` + config **`schedule_gate_metric`**, default
      **`"precond_kappa"`** (faithful); `"force_fraction"` keeps the legacy quantity so the A/B
      stays reproducible. `precond_kappa` is now a first-class member; a `[GATE]` detail line
      every 50 iterations prints **both** candidates and whether the throttle fired, so this can
      never again be invisible.

### Measured so far — MMS adaptec1, td 1.0 / grid 512 / seed 42
Metric is **exact HPWL (all nets)** and **Macro-Excluded Overflow (exact, no fillers)**, i.e. the
two rows `.claude/skills/xplace-compare` Trap 3 names. XPlace reference = `_XPLACE_MMS_FINAL`
(post-phase-2 `After GP, best solution eval`).

| arm | HPWL | exact overflow | iters | stop |
|---|---|---|---|---|
| baseline (filler-incl conv, force_fraction) | 6.385e7 | 0.1092 | 1373 | converged |
| + filler fix only | 6.397e7 | 0.1350 | 1189 | converged |
| **+ filler fix + κ gate (both faithful)** | **6.368e7** | **0.1115** | **1325** | converged |
| XPlace | 6.457e7 | 0.1146 | 1344 | GP Stop |

The faithful pair lands within **19 iterations** of XPlace's total and at essentially its
overflow, for **−1.4% HPWL**. Note the filler fix ALONE is a regression (it stops 184 iterations
early); the two are a pair, because the filler-inflated signal was partly compensating for the
throttled λ.

- [x] **DONE — MMS 16-design suite on the faithful pair** (launched 2026-08-06 18:04, landed
      22:56; `/tmp/faithful/`, configs copied from `/tmp/rebase/configs`, binary md5 in
      `/tmp/faithful/binary.md5`). All 16 rows in `2_ARTIFACTS/faithful_suite_results.tsv`.
      Compared against `2_ARTIFACTS/rebase_suite_results.tsv` (same configs, pre-change binary,
      verified bit-reproducible at HEAD on 2026-08-06). Headline is in the ✅ CONFIRMED banner at
      the top of this task — **post-DP +1.15% → +0.74% vs XPlace over all 16**, 15/16 converge
      (was 6/16), density parity unchanged. newblue4 is the one design the fix did not convert.
      Do not quote the older 15-design pair (+1.23% → +0.97%): that predates adaptec3 joining the
      table when TODO #3's LG/DP-harness bug was fixed.
      > **Provenance — a rebuild DID land mid-sweep** (the trap that cost TODO #11 its newblue7
      > arms), at ~18:14, two designs in: `59d65125…` -> `03791bef…`. **Verified benign, not
      > assumed:** the edits were comment-only (`Schedule.cpp` stale-TODO note, `Output.cpp`
      > filler comment, plus `default_config.toml`, which the sweep does not read), and re-running
      > adaptec1 on the *new* binary reproduces the sweep's own adaptec1 **bit-identically** —
      > same `iterations.dat`, same DEF md5 `30f9e41b524b…`. The suite is one generation.
- [x] **DONE — LG/DP re-run on the new placements** (`2_ARTIFACTS/lgdp_faithful_results.tsv`,
      `/tmp/lgdp3/`). The harness now takes `LGDP_{PL,LOG,RES,PROG}` env overrides and
      `analyze_lgdp_suite.py` takes `LGDP_OURS_GLOB`, so both suites are reproducible side by side.
      ⚠️ `gen_lgdp_inputs.py` must be run to completion in the FOREGROUND before launching the
      runner — backgrounding the whole `gen && run` chain kills generation partway (it produced 3
      of 16 `.pl` files, silently).
- [x] **DONE — regression baselines regenerated** (all three; the κ gate affects phase 1, so the
      two ISPD2015 designs moved too: 616→731 and 668→751 iterations, both still `converged`).
      `--reason` records the change and the confirming numbers in each baseline header.
      `make test-regress` and `--slow` are green again.
- [x] **DONE 2026-08-07 (Mark): BOTH toggles retired**, per TODO #2's settled-toggle pattern —
      "if there is no reason to use the knob, remove it and keep the best default."
      `schedule_gate_metric` and `convergence_include_fillers` are gone as config keys, along with
      `Placer::scheduleGateMetric()` and `Placer::convergenceIncludesFillers()`; the faithful
      behaviour (κ gate, filler-excluded overflow) is now unconditional. The reasoning and the
      measured numbers moved into the surviving code comments at `Schedule.cpp::updateSchedule`
      and `Output.cpp::recordIterationResults`, so the record survives the deletion.
      **Verified behaviour-neutral:** `make test-regress` and `--slow` bit-identical on all three
      baselines (731 / 751 / 1325 iterations).
      `density_force_fraction` is still computed and printed on the `[GATE]` line — it is just no
      longer the gate, and having both visible is what makes the divergence observable.
- [x] **newblue4 — CLOSED as good enough (Mark, 2026-08-07).** Still the one design stopping on
      `divergence_guard`, and the only one whose exact overflow ends above XPlace's (0.2026 vs
      0.1840), but post-DP is +1.33% and the density is at parity, so there is no quality problem
      to chase. Not worth the time. If it is ever revisited, the `[GATE]` trace in the run log
      makes the mechanism directly observable now.

### 🔑 pl_algo was RIGHT all along — and its own test has been reporting the mismatch for weeks

`pl/src/pl_algo/src/modules/param_scheduler.hpp:76` computes the gate quantity in closed form:
`sched_dff(λ, c) = c·λ/(1+c·λ)`, `c = precond_coef·K/total_pins` (K = Σ areas). Substitute
XPlace's definition and that **is** κ exactly:

```
κ = ‖α₂‖₁/(‖α₁‖₁+‖α₂‖₁) = (pcoef·λ·ΣA)/(ΣP + pcoef·λ·ΣA) = c·λ/(1+c·λ),  c = pcoef·ΣA/ΣP
```

So pl_algo implemented the **right function under the wrong name** (`density_force_fraction`),
while sw_only implemented the **wrong function under the right name**. Verified numerically on the
new adaptec1 run: `c = κ/((1−κ)·λ)` is constant to ~0.4% within a fixed `precond_coef`
(121.28 · 121.59 · 121.43 · 121.20 · 121.62 · 121.13 · 121.49 · 121.35 · 121.20), and steps by
exactly ×2 at each escalation (242.6 → 487.5), resetting to 70.2 at the phase-2 restart and
holding constant again. That is the signature of the closed form, and it did not hold before.

**`test/sched_verify.cpp` derives `dff_coef` from the trace and its own comment says "its
constancy across the run is itself a check on the closed form" (lines 11-12, 61-62).** On the old
golden that check reads `min=2.34 max=1.48e6`, a **633,000× spread** — and the harness *prints*
it, takes the median anyway, and passes. It also feeds the golden's `dff` straight into the
scheduler ("to isolate the lambda-trend logic", line 93), so the choice of gate quantity is
structurally outside what it can test.

- [ ] **Make `sched_verify` assert dff_coef constancy** (within a `precond_coef` plateau; allow the
      ×2 steps). This is the single highest-value test change in the repo right now: it is an
      already-designed check that was left as a print, and asserting it would have caught #19b on
      day one from the pl_algo side.
- [ ] **Regenerate the fixture trace from the post-#19 sw_only** — but note TODO #20: it needs
      `dumpScheduleTrace()` restored first (deleted as dead code 2026-07-28). Until then the
      fixture is a 2026-08-05 capture of the OLD gate quantity.
- [ ] **Rename pl_algo's `dff`/`dff_coef` to `kappa`/`kappa_coef`** to match sw_only's
      `precond_kappa` and XPlace's `weighted_weight`. The name is what hid this.

### This reframes TODO #7's first bullet
"Placement-stage-aware parameter scheduling / κ(η)" was filed as *not implemented*. It **was**
implemented — the skip_update gate, the ×3 throttle, the (0.5, 0.95) window and the `<50` warmup
are all present and faithful. Only the quantity being tested was wrong. What remains of #7 item 1
is nothing; close it against this section.

---

## #20 — pl_algo Stage 5: wire the full device design, on the CURRENT sw_only algorithm (opened 2026-08-06)

Full assessment: `1_REVIEW/reports/_NEW_REPORT_pl_algo_stage5_assessment_20260806.md` (UNREAD).
`DATAFLOW.md` stays the authority on the dataflow itself; this item is the work plan.

**The problem is not "compose the resident loop".** pl_algo's algorithm is pinned to the
**2026-07-14** sw_only (`param_scheduler.hpp`, commit `3a42098`), 20 sw_only commits + the
uncommitted #19 ago. Composing Stage 5 on top of that hardens a three-week-old algorithm.

Three findings that are not recorded anywhere else:

- **`Placer::dumpScheduleTrace()` was deleted from sw_only as dead code** (`44612cc`, 2026-07-28).
  It was the only producer of the golden `test/sched_verify.cpp` replays, and that consumer lives in
  another variant and names it by *filename* — nothing in the build could see the coupling. So the
  fixture **cannot be regenerated**, and `make test`'s green `sched_verify` validates the on-device
  scheduler against **sw_only as of 2026-07-18** and will keep passing forever. Its companion config
  is still `.json` — it predates the TOML migration.
- **`sched_dff` in `param_scheduler.hpp` already computes XPlace's κ**, not the
  `density_force_fraction` its comments claim — i.e. pl_algo has #19b's fix by accident. Proven from
  the committed fixture (which carries `precond_a1_norm`/`precond_a2_norm`): fitting `q/(1-q)=c·λ`
  gives **1.12 % spread for κ vs 2136 % for dff**. Two catches: `dff_coef` is a fixed scalar but
  κ's `c` carries `precond_coef`, which escalates ×2/20 iters to 1024 (the fixture never exercises
  it — `precond_coef ≡ 1.0` for all 692 rows); and `sched_verify` feeds the trace's **dff** column
  in, so it verifies against the wrong quantity either way. The harness's
  `closed-form dff max rel err: 1.608` line was measuring exactly this and `fixtures/README.md`
  explains it away as a precond-on artifact.
- **Tier-1 covers 3 modules of 17.** Only `fft_pl`, `field_solve_pl`, `param_scheduler` are
  `#include`d by a harness; `density_bin_model.cpp` holds its **own copy** of `node_footprint`, and
  the two are stale together. Every module Stage 5 must change is uncovered, so today those edits
  can only be checked through a full Vitis + sw_emu cycle.

Datapath divergences from the golden (all small, all currently unverifiable — see step 3):
`node_footprint.hpp` still does the in-die shift #11a **deleted**; it lacks #11b's movable-macro
`weight = target_density` (needs a macro tag in `NodeBox`); `iteration_update.hpp` clamps to
`[0, die−w]` where sw_only clamps to the √2-expanded box; and **pl_algo has no fillers at all**
(`Packer.cpp` walks `getComponents()`, which excludes `getFillers()`).

Structural, decide before composing: the convergence overflow needs a **second, movable-only**
density map (sw_only rebuilds it independently; overflow is nonlinear so it cannot be subtracted
out — adopt XPlace's mov-map + filler-map decomposition); backtracking is a re-entrant inner loop
around the whole datapath; best-solution **positions** need an M-sized DDR buffer (the scheduler
tracks only the metrics); phase 2 is host-side LP work that makes the device loop re-enterable
rather than run-once; `GRID` is a compile-time 1024 vs sw_only's per-design formula grid.

### Steps (cheap and load-bearing first; 1–4 need no Vitis and no free CPU)
- [x] **0. The build is broken on arrival for a boring reason.** `make host HOST=pl_algo` fails with
      `No rule to make target '.../host/src/pl_algo/src/DataBase.cpp'`, which reads like the
      `host/src/common/` extraction (#9) broke pl_algo. It did not: `build/hw/host/pl_algo/obj/
      DataBase.d` is dated 2026-07-10 and still names the pre-move path, and `host/Makefile` does
      `-include $(HOST_DEPS)`. **`make clean HOST=pl_algo` once** — verified, builds and links clean.
      Worth a README line; it is the first thing a returning session hits and it accuses the wrong
      commit.
- [ ] **1. Restore `dumpScheduleTrace()` in sw_only** with today's columns (`precond_kappa`,
      `precond_coef`, phase, `phaseIteration`, stop reason, `backtrack_steps`) and regenerate the
      adaptec1 fixture + its `config_used.toml`. Verify: `make test-regress` bit-identical before and
      after (the dump is config-gated, so it MUST be a no-op). Do this **before** touching pl_algo —
      it is the instrument every later step is measured with.
- [ ] **2. Re-verify `param_scheduler` against the new trace**, feeding **κ**, not dff. Fix what
      falls out: escalating `dff_coef` (`precond_coef_k·K/total_pins`), the missing
      `overflow rising` conjunct on the coarse divergence test, phase-relative counters, jolt
      params read from config instead of hardcoded. Verify: `make test`.
- [ ] **3. Tier-1 harnesses for the uncovered modules** — `node_footprint`, `density_bin` (include
      the real header; delete the copy in `density_bin_model`), `iteration_update`, `bb_reduce`,
      `metrics`, `force_gather` — each against its named sw_only golden. This is what makes 4–6
      safe.
- [ ] **4. Close the datapath divergences** above, under that coverage.
- [ ] **5. Fillers** — packer, uniform-random initial placement (not centre-clustered), the λ-init
      balance that counts them, and the movable/filler split the two density maps need. Largest
      single change; largest quality lever.
- [ ] **6. Compose the resident loop (Stage 5 proper)** with the second density map, the
      best-position buffer, and phase-2 re-entrancy designed in from the start.

### Open questions for Mark (in the report's §10)
1. Does "the exact same algorithm" include **phase 2** and the **backtracking line search**, or is
   v1 "phase-1 GP, device-resident, bit-comparable"?
2. Step 1 touches sw_only while #19 is uncommitted and its suite is running — go, or wait?
3. **Pin pl_algo to a named sw_only commit** (recommend: the one that lands #19) rather than chasing
   HEAD? Chasing HEAD is what produced this drift.
4. Grid: pin sw_only to 1024 for the A/B, or build pl_algo per-design with `-DPL_GRID`?

---

## #21 — Repo restructure: one host at the top level, `vck5000/` for PL+AIE only (opened 2026-08-07)

Full assessment: `1_REVIEW/handoffs/_NEW_HANDOFF_repo_restructure_20260807.md` (UNREAD).

Target shape, as stated by Mark:

```
AIEplace/host/src/     ONE host — runs the algorithm software-only, PL-only, or PL+AIE
AIEplace/vck5000/pl/   PL kernels for the VCK5000
AIEplace/vck5000/aie/  AIE kernels for the VCK5000
```

**This is two changes and only the first is cheap.** Keep them separate; A is shippable alone.

- **A — the move.** `vck5000/host/` → `host/`, rewire the build. ~1 day, mechanical, low risk.
- **B — one host.** Collapse `sw_only` + `pl_algo` (+ decide about `v2`) into one host with a
  backend switch. Days. **B is #20 wearing a different hat** — "one host with three backends" is
  the same sentence as "stop maintaining a second copy of the ePlace schedule in
  `host/src/pl_algo/src/Placement.hpp`". It therefore inherits **all** of #20's preconditions,
  including *do not touch the pl_algo algorithm until `dumpScheduleTrace()` and tier-1 coverage
  are restored* (#20 steps 1–3). Without those there is no way to show a unified host still
  computes what sw_only computes.

### The merge-safety finding (this is the reason to act now, not later)

`git merge-tree --write-tree --name-only HEAD origin/geert`, run 2026-08-07 (read-only):

> **one conflict — `.gitignore`, two independent appends to the tail.** `vck5000/aie/Makefile`
> auto-merges. Nothing else.

Since the fork (`a006500`, 2026-03-20) Geert changed **42 files**, Mark changed **4493**, and the
intersection is **exactly those 2**. Geert's work is almost entirely *new* files under
`vck5000/host/src/v2/**` and `vck5000/aie/src/v2/**`. Reproduce:

```bash
cd /home/msears/phd/AIEplace && git diff --name-only a006500 origin/geert | sort > /tmp/g.txt && git diff --name-only a006500 HEAD | sort > /tmp/m.txt && comm -12 /tmp/g.txt /tmp/m.txt
```

**Merge `origin/geert` BEFORE restructuring.** After the move, his 25 `host/src/v2/**` files
become adds-into-a-deleted-directory; git 2.50 flags that (`merge.directoryRenames=conflict`)
rather than misplacing them, but it turns a no-op into a manual relocation of 25 files. Merging
first makes the restructure a rename over a tree that already contains v2. **Tell Geert before
merging his branch.**

His active work-front is safe either way: his last four commits (through 2026-07-10) are all
`vck5000/aie/src/v2/` — which this proposal does not move. His host work has been dormant since
2026-06-19. `origin/geert` is also already 15 behind `origin/main`, independent of this.

### ⚠️ The actual risk is semantic, not textual

**`vck5000/host/src/v2/` is Geert's own from-scratch host rewrite**: its own `DataBase.cpp`,
`Parsers.cpp` (**Limbo removed**, hand-written LEF/DEF), `Library.cpp` (`ComponentTypeLibrary`,
FPGA-targeted), `Placer.cpp`, `FPGADriver.cpp`, JSON config + vendored `json.h`. His README says
`performGradientStep()` / `computeMomentumStep()` are **stubs** — a scaffold, well behind sw_only.

So the repo holds **two independent, mutually unaware consolidations of the same component**:
`host/src/common/` (Mark's, 2026-08-04, TODO #9 — Limbo/ASIC/TOML) and `host/src/v2/` (Geert's,
2026-06-19 — no-Limbo/FPGA/JSON). Git merges them happily forever because they never share a file.
**That is the danger, not a safeguard** — same failure mode as auto-memory
`cross-variant-coupling-is-invisible`. "One host" cannot mean four hosts. **Whether v2 is the
future data model is a decision for Mark and Geert, and it gates B, not A.**

### Steps

- [ ] **1. Merge `origin/geert`.** Resolve `.gitignore` (keep both appends). Talk to Geert first.
      → verify: `make test` + `make test-regress` green, `make host HOST=v2` builds.
- [ ] **2. PURE-RENAME commit.** `git mv vck5000/host host`. **Zero content edits.** The build is
      broken at this commit; that is intended. → verify: `git show --stat` is 100 % renames and
      nothing else. *Git detects renames by per-file similarity — a commit that moves and edits
      can drop below threshold, and then every change Geert made to that file becomes a
      delete/modify conflict he resolves by hand.*
- [ ] **3. BUILD-REWIRE commit.** Split `REPO_ROOT` (git root) from `PROJECT_ROOT` (the vck5000
      platform dir) in `common.mk`; `HOST_ROOT = $(REPO_ROOT)/host`;
      `THIRD_PARTY = $(REPO_ROOT)/third_party` (retires the `$(PROJECT_ROOT)/../` climb in all
      three `makeflags.mk`). **Keep the `HOST=` selector working throughout the transition.**
      → verify: `make host` builds for `HOST=sw_only`, `pl_algo`, **and `v2`**.
- [ ] **4. SWEEP commit.** The **92** path references across ~20 dirs
      (`git grep -In -e 'vck5000/host' -e 'host/src' -e 'HOST_DIR' -e 'HOST=' -- . ':(exclude)third_party'`).
      Concentrated in `tools/` (8 files), `test/regress/run_regress.sh`, `pl/src/pl_algo/`,
      `CLAUDE.md`, `.gitignore`.
      → verify: `make test` and `make test-regress` green **without regenerating any baseline**.
      *If a baseline needs regenerating, stop* — the three frozen configs hold paths relative to
      `vck5000/`, so a baseline change here means something moved that should not have. **Recommend
      `host/benchmarks/` does NOT move**: it is data, and re-baselining to accommodate a directory
      move destroys the tripwire for exactly the change it should be catching.

**Steps 1–4 are change A and ship on their own. Everything below is B.**

- [ ] **5. Decide what `v2` is** (Mark + Geert). Blocks 7.
- [ ] **6. #20 steps 1–3** — restore `dumpScheduleTrace()`, regenerate the `sched_verify` fixture,
      raise tier-1 coverage above 3-of-17. **Non-negotiable prerequisite for 7.**
- [ ] **7. Collapse `Placement.hpp` into the sw_only schedule** behind a backend interface.

### Decisions needed before anyone starts

1. **Which "three modes"?** compile-time (`-D`, one binary per backend) / **link-time (recommended
   — one algorithm, three executors, and XRT never enters the CPU golden's link line; the existing
   old-ABI-parser / new-ABI-`Driver.o` seam with `PackedDesign` as the neutral boundary is already
   in the right place)** / run-time (`--backend=`, forces XRT into every build). **The answer
   changes `common.mk`, so settle it in step 3, not step 7.**
2. Does `benchmarks/` move with `host/`? (Recommend no — see step 4.)
3. Does `vck5000/test/` split? `regress/` tests the host, `test/*.cpp` tests the PL. **Recommend
   leaving both under `vck5000/test/` for change A** — splitting breaks `make test` /
   `make test-regress`, which are written down in CLAUDE.md, both skills, and most of `1_REVIEW/`.

### Pre-existing breakage this will expose (fix in passing, do not port)

- `aie/src/markv1/makeflags.mk:11` → `-include="$(PROJECT_ROOT)/host/src/include"`. **That
  directory does not exist**; the flag is silently a no-op today.
- `vck5000/.git/` is a stray dir containing only an empty `info/`. Not a repo, not tracked.
  Delete it — it will confuse anyone running `git` from inside `vck5000/`.

### The flag divergence B must reconcile (not blockers, but do not silently pick one)

`sw_only` is `-O2 -fopenmp` (the `-O2` is deliberate: `-O0` perturbs the golden's low bits, and
OpenMP is TODO #12); `pl_algo` is `-O0`, no OpenMP, with `Driver.o` alone forced to
`_GLIBCXX_USE_CXX11_ABI=1` for XRT while everything else stays at `0` for Limbo. Keep that per-TU
override — it is the pattern a unified host needs, already working. Don't re-derive it.

---

## #22 — 8 ISPD2015 designs have no XPlace reference: the `ispd2015_fix` dataset (opened 2026-08-07)

**Decided 2026-08-07 (Mark): SKIP for now.** The 44-design snapshot ships with **36 of 44**
carrying an XPlace reference; these 8 appear with our own numbers and no ratio. This entry records
what the fix would take, so it is not re-derived.

### Why they are blocked
`Xplace/main.py:94-96` silently rewrites `--dataset ispd2015` to `ispd2015_fix`:
```python
if args.dataset == "ispd2015":
    print("We haven't yet support fence region in ispd2015, use ispd2015_fix instead")
    args.dataset = "ispd2015_fix"
```
and `~/phd/Xplace/data/raw/ispd2015_fix/` holds exactly **one** design (`mgc_pci_bridge32_b`), so
every other design dies with `Design Name X should in ['mgc_pci_bridge32_b']`. That rewrite is why
a first attempt at `--dataset ispd2015` failed on all 20.

**Workaround used for the other 12** (`2_ARTIFACTS/run_xplace_ref_2015.sh`): `--custom_path` is
checked by `find_design_params` **before** the dataset dispatch, so it bypasses the rewrite and
reads our own `tech.lef`/`cells.lef`/`floorplan.def`:
```
--custom_path "tech_lef:<p>/tech.lef,cell_lef:<p>/cells.lef,def:<p>/floorplan.def,design_name:<d>,benchmark:ispd2015"
```
**Applied ONLY to the 11 designs whose `floorplan.def` has no `REGIONS`/`GROUPS`.** For the other
8, XPlace's own message says it cannot handle fence regions; forcing them through the guard would
yield a number it cannot compute correctly, and a silently-wrong reference is worse than a missing
one. They are recorded `blocked_fence_region` in `2_ARTIFACTS/xplace_ref_ispd.tsv`.

**The 8:** `mgc_des_perf_a`, `mgc_des_perf_b`, `mgc_edit_dist_a`, `mgc_matrix_mult_b`,
`mgc_matrix_mult_c`, `mgc_pci_bridge32_a`, `mgc_superblue11_a`, `mgc_superblue16_a`.

### Option 1 (preferred) — obtain the official `ispd2015_fix` dataset
Distributed by the Xplace / DREAMPlace community. Drop each design into
`~/phd/Xplace/data/raw/ispd2015_fix/<design>/` and re-run
`2_ARTIFACTS/run_xplace_ref_2015.sh` — it skips rows already `done` and would pick these up via
the plain `--dataset ispd2015` path. No code change needed.

### Option 2 — construct `_fix` ourselves, and VALIDATE it before trusting it
Measured layout difference, `mgc_pci_bridge32_b`, the one design we have both variants of:

| | plain `ispd2015` | `ispd2015_fix` |
|---|---|---|
| LEF | `tech.lef` (21,878 B) + `cells.lef` (165,460 B) | single `<design>.lef` (225,963 B) |
| DEF | `floorplan.def` (4,949,004 B) | `<design>.def` (4,214,705 B) |
| REGIONS / GROUPS | 1 / 1 | **0 / 0** |

So the transform is (a) merge tech+cells into one `<design>.lef` and (b) strip `REGIONS`/`GROUPS`
from the DEF. **It is NOT a plain concatenation** — 21,878 + 165,460 = 187,338 ≠ 225,963, so the
merged LEF is reformatted, not appended. Reverse-engineering that is the risky part.

**The validation is free and must be done first:** we hold both variants of `mgc_pci_bridge32_b`,
so run the constructed one through XPlace and check it reproduces the shipped one's numbers —
`gp_hpwl_exact 3.432114E+06 · gp_ovfl 0.2431 · lg 3.495595E+06 · dp 3.477053E+06 · 722 iters`
(`2_ARTIFACTS/xplace_ref_ispd.tsv`). If a constructed `_fix` does not reproduce those, the
transform is wrong and none of the other 8 can be trusted.

### ⚠️ Comparability caveat that applies even today
`mgc_pci_bridge32_b`'s reference came from the **`_fix`** data (fence regions *stripped*), while
sw_only places the region-bearing `floorplan.def`. For that one design the two tools solved
slightly different problems. One row, flagged rather than silently averaged in.

---

## #23 — `init_step_seed = 0.01` underflows: 5 ISPD2015 designs are DEAD ON ARRIVAL (opened 2026-08-07)

> **The five:** `mgc_superblue11_a`, `mgc_superblue12`, `mgc_superblue14`, `mgc_superblue16_a`,
> and **`mgc_des_perf_b`**. All five stop `nan_metrics` with an initial step of exactly 0.
> `mgc_superblue19` is the only superblue that works.
>
> ⚠️ **It is NOT simply "the largest designs".** `mgc_des_perf_b`'s HPWL is 3.7e8 — two orders of
> magnitude below superblue's 3.9e10 — and it fails identically. So the trigger is the ratio
> between the probe displacement and the design's coordinate/gradient scale, not raw size. Do not
> assume a size threshold; the only reliable detector is `α == 0` itself.
>
> Separately, `mgc_edit_dist_a` exits `diverged_hpwl` at 1371 iterations, but that one is NOT this
> bug — its initial step is healthy (5.583e4), it spreads normally, and its restored-best HPWL
> (4.224e9) matches the 2026-07-08 snapshot's 4.228e9 to 0.1%. It is a guard-stop, not a failure.

**Found by the 44-design snapshot — this is exactly the blind spot that suite was built to expose.**
Nothing else covers ISPD2015: `make test-regress` runs two small mgc designs, and the MMS suite is a
different tier. These four have been broken and invisible.

### Symptom
`mgc_superblue11_a`, `mgc_superblue12`, `mgc_superblue14`, `mgc_superblue16_a` all stop with
`nan_metrics` at ~2133 iterations and report an HPWL 1.6–2.2× worse than the 2026-07-08 snapshot.
`mgc_superblue19` is fine. The trajectory is unmistakable — **nothing ever moves**:

```
iter, HPWL,      OVFW,      step_len,  density_weight
001,  3.906e+10, 9.997e-01, 0.000e+00, 1.501e-14     <- step is ZERO from iteration 1
1500, 3.906e+10, 9.997e-01, 0.000e+00, 2.091e+15     <- identical HPWL 1500 iterations later
2132, 3.906e+10, 9.997e-01, 0.000e+00, 1.031e+29
2133, -nan,      9.997e-01, -nan,      1.082e+29     <- lambda finally overflows
```
Overflow pinned at 0.9997 = the initial pile, never spread. λ ramps unbounded for 2133 iterations
because overflow never improves, then NaNs. The reported HPWL is the untouched initial placement.

### Root cause — CONFIRMED, not inferred
`Estimated initial step_length (BB): 0  (seed 0.01)`. `estimateInitialStep()` takes one trial step
of `init_step_seed`, then sets α = ‖Δpos‖/‖Δgrad‖. On these designs the trial displacement is below
one float32 ULP of their coordinate magnitudes, so **Δpos is exactly 0 ⇒ α = 0**, and a zero step
is self-sustaining: nothing moves, so the next Δpos is also 0.

Measured on `mgc_superblue14` (5-iteration probes, `/tmp/stepdiag`):

| `init_step_seed` | initial step | iteration 1 |
|---|---|---|
| **0.01** (default) | **0** | overflow 0.9997, step 0 — dead |
| 1.0 | 189647 | overflow 0.9667, step 1.929e5 — spreading |
| 100.0 | 191441 | overflow 0.9664, step 1.930e5 — spreading |

Two orders of magnitude of seed change the result by nothing once it works (189647 vs 191441,
0.9%) — the estimator *is* self-calibrating, as documented. It just cannot calibrate from a probe
that rounds to zero. `default_config.toml` calls the seed "a robust internal default; rarely needs
tuning now that the first step self-calibrates per design" — **false for the largest designs.**

### This is the large-coordinate precision problem, concretely
Same family as TODO #15 (net-local frames: error tracks `x_max/γ`) and the float-vs-double item
under *Improvements*. Those are filed as PL-precision enablers with "expect no HPWL movement on
CPU"; this is a case where float32 at large absolute coordinates **silently kills a run on the CPU
golden**. Worth re-reading both in that light.

### Not fixed, and deliberately not papered over
- [ ] **Decide the fix.** Options, roughly in order of preference:
      (a) make the probe **relative** — scale the trial displacement by the die span or the current
      position magnitude, so it can never round to zero (this is the real fix, and it removes the
      seed's design-sensitivity entirely);
      (b) detect `α == 0` and retry with a geometrically larger seed until Δpos ≠ 0;
      (c) raise the default seed — cheapest, but it only moves the cliff rather than removing it,
      and it changes every design's trajectory, so it needs a full re-baseline.
- [ ] **Assert it, do not just fix it.** `α == 0` (or `step_length == 0` at iteration 1) should be
      a hard error with a clear message, not a silent 2133-iteration no-op. Same rule as
      `CLAUDE.md` § *A test asserts*: this ran to "completion" and wrote a plausible-looking HPWL.
- [ ] **Add one large design to `make test-regress`.** The tripwire's two mgc designs are small
      enough that the probe never underflows, so it cannot see this class of bug.
- [ ] The 4 designs are **excluded from the 44-design snapshot headline** and marked
      `nan_metrics` there. Re-run them once this is fixed.

**Do NOT "fix" the snapshot by re-running these four with a hand-tuned seed.** That would be a
per-design hyperparameter, not comparable to the other 40, and it would hide the defect.

---

# Improvements

Algorithmic ideas (beyond faithfulness cleanup) — hypotheses to try, not yet scoped.

- [ ] **Data type precision sweep (float vs double).** Placement algorithms are numerically intensive
      (transcendentals in DCT, accumulating gradients and forces over thousands of nets and bins),
      and single-precision vs double-precision could have effects on convergence speed, overflow
      accuracy, and final HPWL. Current codebase uses a mix (e.g., `float` for density grids to save
      bandwidth, `double` elsewhere). Sweep sw_only systematically: measure wall-clock time, HPWL,
      and convergence trajectory under `float` only, `double` only, and the current hybrid on a
      representative MMS subset (e.g., adaptec1, newblue3, newblue5). Priority: understand whether
      precision limits solution quality or merely the speed to solution.

- [ ] **Smoothing schedule (density footprint √2 inflation ramped down over the run).** The convergence
      metric is smoothed (each cell's footprint inflated to ≥√2·bin), which lets GP stop at smoothed
      overflow ~0.07 while the *exact* physical overflow is still 0.12–0.28 (hotspots) on the hard
      macro-heavy designs (adaptec5, newblue4, newblue5). Idea: start with heavy smoothing (helps early
      spreading / smooth gradients) and gradually reduce the inflation toward 1·bin (sharp) as GP
      progresses, so late convergence tracks the true physical density and the placer keeps spreading
      out the sub-bin hotspots instead of declaring victory early. Candidate remedy for the residual
      under-spread the `convergence_include_fillers` fix did NOT close (see #4). Don't implement yet —
      first diagnose *why* those designs won't spread (see the handoff / GIFs).

---
## #2 — Consolidate the preconditioner / field-faithful code (validated 2026-07-26)

The full MMS A/B (`1_REVIEW/NEW_mms_dct_ab_20260726.md`) definitively validated the
faithful-field + preconditioner set (`dct_normalize_inverse=false`, `precond_raw_area=true`,
`dff_force_ratio=true`): faithful beats legacy **16/16 on MMS, mean −9.6% HPWL**, confirming the
preconditioner works as intended. Now clean up the experiment scaffolding it was developed behind:
- [x] DONE 2026-07-27 (commit 1f84d8d): retired ALL 5 settled legacy toggles (Mark decided "delete all").
      Deleted the flags, config keys, dead branches + oversized comments: `dct_normalize_inverse` (the
      double-normalized inverse was an *error*, not a frame — inverse transforms now unconditionally
      unnormalized), `precond_raw_area=false`, `dff_force_ratio=false`, `precond_density_scale` (no-op
      EXPERIMENT), and `enable_pin_offsets` (per-pin offsets always on; dropped DataBase ctor param +
      member + 2 collapse-to-origin guards). Behavior-preserving (run_config already used every kept
      value); builds clean, smoke-verified within RNG noise. Net −94/+29 lines.
- [x] precond_density_scale: retired (part of the above).
- [x] Misleading defaults + comment blocks: DONE (bb7904b single-source dedup + 1f84d8d comment pruning).
      run_config.json fully committed (the `enable_pin_offsets` breadcrumb is gone with the key).
- [x] `updatePrecondWeights` simplified (dead ternaries + dff if/else removed). Kept `precond_a1_norm`/
      `precond_a2_norm` (still used by the trace) and the auto-enable/escalation logic (unchanged — not cruft).
- [x] DONE 2026-08-02: mirrored the faithful-only simplification into pl_algo. No retired flag *names*
      survived in pl_algo source (they were only in `CHECKPOINT.md`, now archived — see #10); the real
      residue was the `avg_area` normalization still threaded through the preconditioner. Removed it:
      `Placement.hpp::updatePrecondWeights` now takes raw area (`w = max(1, pins + pcoef*λ*area)`,
      matching sw_only `Schedule.cpp` / XPlace `alpha_2`), and the dead `avg_area` parameter is gone
      from `runPlacement` (`Driver.hpp`/`Driver.cpp`) and from `main.cpp`, which no longer computes it.
      **Behavior-identical** — the only call site already passed a hardcoded `1.0f`. Verified by
      `g++ -fsyntax-only` on `Driver.cpp` + `main.cpp` (no full build: another session held the CPU).

---

## #4 — Fix convergence to include filler density (opened 2026-07-30, ⛔ RETRACTED 2026-08-06)

> ## ⛔ CONCLUSION REVERSED — read this before citing anything below
>
> **Retracted 2026-08-06 by TODO #19(a).** This task's premise — that sw_only's convergence
> overflow should *include* filler density to match XPlace — is **wrong on all three XPlace code
> paths**. Every XPlace overflow metric **excludes** fillers (`direct_calc_overflow`,
> `ElectronicDensityLayer.forward`, `get_obj_overflow`); fillers sit past `mov_rhs`, and XPlace's
> own comment says its two overflow metrics differ by clamp-vs-exact node size, *not* fillers.
> #19 removed the phase-2 force-to-true, and on **2026-08-07 the `convergence_include_fillers`
> config key was retired entirely** (with `Placer::convergenceIncludesFillers()`) — the faithful
> behaviour, filler-EXCLUDED, is now unconditional. There is no knob to set.
>
> **Any overflow number recorded between 2026-07-31 and 2026-08-06 is filler-INCLUDED** and reads
> roughly 2× high against anything XPlace prints. The newblue2 calibration this task leaned on is
> still unexplained. See memory `overflow-metric-grid-faithfulness` and TODO #19(a).
>
> Kept in full because the reasoning, the XPlace source reading and the reporting work in Step 1
> are still the record of how the metric was arrived at — but the verdict is inverted.

> ### ⚠️ REFRAMED 2026-07-31 — the premise below was measured against the wrong XPlace number
>
> **XPlace's mixed-size flow has TWO GP phases; sw_only implements only the first.** From
> `~/phd/Xplace/result/2026-07-17-23:03:11_adaptec5/log/test.log`: Mixed-GP (macros movable) →
> `Start running Macro Legalization` (LP/cbc) → `Reset optimizer` → `Re-run std cell placement
> with fixed macros`, a second full GP pass. The familiar `GP Stop!` line is the end of **phase 2**.
> Every "vs XPlace" MMS number we have ever quoted — including the −22%/−25% under-spread figures
> below — compared our phase-1 result against XPlace's phase-2 result.
>
> Phase-1 reference now recorded in `tools/benchmarks.py::_XPLACE_MMS_MIXED_GP`. Two things change:
> - **XPlace's Mixed-GP ends at 0.10–0.18 exact overflow on 15 of 16 MMS designs** (newblue3 is the
>   lone outlier at 0.040). It never reaches the 0.07 stop threshold either. "We don't spread to
>   0.07 with macros movable" is therefore NOT by itself a defect.
> - Against the phase-1 reference the footprint-A/B base arm is **mean +3.5% HPWL over 16 designs**,
>   within +0.7…+4% on 11 of them. The bad ones are adaptec5 (+7.0), newblue4 (+8.4), newblue7
>   (+6.7), newblue5 (+16.4), and newblue3 (−13.4, a *fake* win — it stopped at iter 375).
>
> **The real defect is the stop criterion, and it splits 8/8 by target_density** (stop reasons
> grepped from the A/B base-arm logs in `/tmp/fp_ab/logs`):
> - all 8 `target_density = 1.0` designs → clean overflow countdown
> - newblue1–6 → `divergence guard exhausted` (the plateau branch, `Schedule.cpp`
>   `checkFineDivergenceGuard`, `life -= MAX_LIFE` = instant kill) — an ad-hoc heuristic XPlace
>   does not have, firing while overflow is still descending
> - adaptec5, newblue7 → genuine 2× HPWL blowup
>
> Not one `td < 1.0` design reaches the threshold, so the guard is the *de facto* stop rule on
> exactly the designs we care about. XPlace runs 1300–2280 iterations; we die at 375–907 under a
> `convergence_max_iterations = 1200` ceiling.
>
> **Re-validate the "premature stop → under-spread" premise against the phase-1 reference before
> spending another sweep on it.** The filler fix is still right on faithfulness grounds (below),
> but its *predicted benefit* was derived from the phase-2 comparison.

### Step 1 — comparability, DONE 2026-07-31 (reporting only; no algorithm change)
- [x] **Headline exact overflow now includes fillers** — `computeFinalMetrics` uses
      `computeOverflow(false, nullptr, true)`, matching XPlace's `evaluate_placement` convention;
      row relabelled "Final Overflow (exact, +fillers)". The filler-excluded number read ~2× low
      and was never comparable to anything XPlace prints.
- [x] **Smoothed overflow follows the convergence flag** (`convergenceIncludesFillers()`, shared
      with `recordIterationResults`), so the reported number is the signal that actually stopped
      the run; label says which policy is in force.
- [x] **Machine-readable stop reason** — `Placer::StopReason` + one `[STOP] reason=<token>
      iteration=<n>` line, plus a "Stop reason" row in run_summary. Tokens: `converged`,
      `max_iterations`, `divergence_guard`, `diverged_hpwl`, `nan_metrics`, `nan_partials`.
      Sweeps can now group by termination mode instead of regexing prose. (NOT plumbed into
      results.csv — that schema is the DSE path, left alone deliberately.)
- [x] **XPlace Mixed-GP reference in `tools/benchmarks.py`** + surfaced in `BENCHMARKS.md`.
- Note: these change the *reported* overflow numbers, so **any golden captured before this no
  longer matches**. HPWL, iteration counts and the trajectory are untouched.

### Step 3 — OUTCOME: only (c) shipped. (a)+(b) are an inseparable pair that needs TODO #13 first.
**Final state 2026-07-31, verified against `2_ARTIFACTS/mms_baseline_20260731.tsv`:**
newblue1 6.156e7 @ 695 it, newblue4 2.426e8 @ 670 it — **baseline stopping fully restored**,
plus the safer divergence test. ispd2005/adaptec1 bit-identical throughout.

- **(a) doubled stop overflow — NOT applied.** Reverted; `applyMixedSizeStopPolicy` documents why
  and `mixed_size_mode` stays wired for #13.
- **(b) plateau kill off in mixed-size — NOT applied.** Reverted; documented deliberate divergence
  in `checkFineDivergenceGuard`.
- **(c) coarse-divergence overflow conjunct — APPLIED, and it is very nearly a NO-OP.**
  Validated 2026-07-31 on 6 designs against the corrected baseline, single binary
  (md5 `d4462bd6…`, hashed before and after the sweep — no mid-sweep rebuild this time):
  newblue1 / newblue4 / adaptec5 / newblue5 / newblue3 are **bit-identical** (same total
  iterations, same sharp/+filler overflow, same HPWL). Only newblue7 moved: 716 -> 718 iterations,
  overflow 0.420 -> 0.415, HPWL unchanged. Keep it — it costs nothing and is faithful — but do not
  attribute any result to it.
  > **Earlier claim in this file was WRONG:** "(c) made adaptec5 run 649 -> 1163 iterations."
  > adaptec5's baseline total was ALREADY 1163. 649 was the RESTORED-BEST iteration, read from
  > `footprint_ab_results.tsv`'s `iter` column, which is `iters_best`, not `iters_total`. The two
  > differ by hundreds of iterations. `2_ARTIFACTS/mms_baseline_20260731.tsv` now carries BOTH
  > columns explicitly to stop this recurring.

**Net effect of step 3 on results: zero.** That is the honest headline, and it is consistent with
the session's conclusion — phase-1 stop tuning cannot move MMS quality, because phase 1's std-cell
placement is discarded anyway. The gap is TODO #13.

**Why (a) and (b) cannot be separated.** Doubling `stop_overflow` also doubles the guard-arm band
(`overflow < stop_overflow*5`, 0.35 -> 0.70). XPlace can afford that ONLY because it simultaneously
disables the plateau kill in this phase. Applying (a) alone arms the guard during the early plateau
every design has before the density weight ramps: **newblue1 died at iteration 258, overflow still
0.70, HPWL 2.39e7 vs 6.15e7** — an unspread placement. Applying (b) alone removes our only phase-1
exit and grinds to `max_iterations`. Re-enable BOTH with #13.

### Step 3 — the analysis: THREE faithfulness gaps, all in XPlace's `include_macros` phase
Found by reading `Xplace/src/param_scheduler.py` (`need_to_early_stop`, `set_mixsize_init_param`)
rather than tuning our heuristics. XPlace runs its mixed-size phase under `include_macros = True`
and **loosens the stop rules while it holds**; sw_only applied the std-cell-phase rules throughout.
That is the whole explanation for the 8/8 split — it was never a per-design pathology.

- [x] **(a) Stop overflow is DOUBLED in mixed-size.** XPlace: `self.stop_overflow =
      args.stop_overflow * 2.0` when `enable_mixed_size and not zero_macro_grad`. So its Mixed-GP
      target is **0.14**, not 0.07 — which is exactly why its Mixed-GP ends at 0.10–0.18. We were
      chasing 0.07 with macros in the field, a target XPlace never asks for and that is likely
      unreachable in phase 1. Implemented in `Setup.cpp::applyMixedSizeStopPolicy` by doubling the
      `overflow_threshold` member once (mirroring XPlace, which mutates `stop_overflow` in one
      place), so the countdown, the guard-arm band and best-solution tracking all move together.
- [x] **(b) The overflow-plateau kill is DISABLED in mixed-size.** XPlace gates it on
      `not self.include_macros` (`param_scheduler.py:467`); we had no equivalent. Mixed-size
      overflow descends slowly enough to look flat over the 50-iteration window, so the guard read
      a healthy run as converged and killed it — this is what ended newblue1–6.
- [x] **(c) Coarse divergence was missing a conjunct.** XPlace requires
      `overflow[ptr] > overflow[ptr-1] AND hpwl[ptr] > best*2`; we tested the HPWL term alone.
      HPWL legitimately climbs past 2× best while cells spread, so this fired on healthy
      macro-heavy runs (adaptec5, newblue7) that were still descending in overflow.

`mixed_size_mode = (num_movable_macros > 0)` is the `include_macros` equivalent — sw_only only ever
runs phase 1 (no macro legalization + fixed-macro second pass), so the flag is simply "has movable
macros". **Non-mixed-size designs are unaffected: ispd2005/adaptec1 verified bit-identical**
(HPWL 60353676.000000, overflow 0.826129, seed 42).

**MEASURED 2026-07-31** (seed 42, XPlace grid + td, vs `2_ARTIFACTS/mms_baseline_20260731.tsv`):
| design | baseline | after step 3 | XPlace Mixed-GP |
|---|---|---|---|
| newblue1 | 6.156e7 @ 0.189, 673 it, `divergence_guard` | 6.156e7 @ 0.190, 661 it, **`converged`** | 5.946e7 @ 0.136 |
| newblue4 | 2.426e8 @ 0.355, 670 it, `divergence_guard` | 2.447e8 @ 0.337, 1200 it, **`max_iterations`** | 2.238e8 @ 0.182 |

Step 3 fixes *why* runs stop, not yet *how well* they place. newblue1's guard was firing about
where it would have converged anyway. newblue4 now runs to the ceiling and is still descending —
too slowly to reach 0.182.

- [x] **`convergence_max_iterations`: DECIDED 2026-07-31 — leave at 1200, do NOT raise.**
      XPlace's cap is `args.inner_iter`, **default 10000**, and `for iteration in range(args.inner_iter)`
      spans BOTH phases — observed totals 1344–2280, so it never binds. It is a runaway backstop,
      not a schedule; 1200 vs 2000 is the wrong axis. Raising ours would be actively harmful:
      newblue4's trajectory shows it is **stuck, not slow**. After ~iter 750 overflow is pinned at
      0.289 while `density_weight` runs away 60,000x (0.55 -> 3.3e4), `step_length` 6e2 -> 3.6e5,
      and HPWL inflates 45% (2.42e8 -> 3.52e8). More iterations = more waste. (The reported
      2.447e8 survives only because best-solution tracking restores an earlier placement.)
- [x] **Density-weight runaway — INVESTIGATED AND CLOSED 2026-08-04. It was a SYMPTOM of #11b,
      and it is already gone; no clamp was added.**
      Confirmed first that the runaway was real and that it mattered. On the pre-#11b adaptec5,
      iterations 592 -> 1163 held overflow pinned at 0.423 while lambda went 0.0504 -> 2.84e+04
      (**x564,000**) and HPWL doubled 3.02e8 -> 6.07e8. That tripped the coarse "HPWL > 2x best"
      test, whose stop reason `diverged_hpwl` is one `Phase2.cpp`'s eligibility gate REFUSES — so
      the runaway is precisely why adaptec5 was the only design denied a phase 2, and why it
      carried the suite's only bad post-DP density (+14.0% vs XPlace, see #3).
      Decisive comparison: newblue4's phase-1 best was 2 iterations before its stop (overflow
      0.435); adaptec5's was **571** before its stop (overflow 0.4275) — the same state, but only
      newblue4 tripped an *eligible* guard, got its phase 2, and landed +1.8% HPWL at equal density.
      **Root cause is not the missing clamp.** Making the movable-macro deposit unconditional
      (#11b, landed 2026-08-02) removes the overflow floor that starved lambda's feedback loop.
      Measured on the current tree: adaptec5's phase 1 now **converges at iteration 649** and
      enters phase 2, versus `diverged_hpwl` at 1163 before. TODO #8 already recorded that "#11b
      buys the convergence" — the suite simply was never re-run after it landed.
      **Two things were built, measured, and deliberately NOT kept** (both documented in
      `Schedule.cpp` so they are not re-derived):
      - *A lambda freeze while overflow is flat.* It is a trap: freezing lambda on a plateau is
        self-reinforcing, because lambda is what ends the plateau. adaptec5 deadlocked and exited
        phase 1 at iteration 401 with exact overflow **0.89**, completely unspread. Do not re-add
        without an escape mechanism; the 2x jolt is the sanctioned one and is confined to the
        high-overflow band on purpose.
      - *A `stuck_plateau_window` phase-1 exit* for runs stuck above the guard's arming band. The
        window was sized against real trajectories (400 = 1.38x the 289-iteration longest flat
        stretch any healthy MMS design shows, and it fired on adaptec5 and nothing else in 16).
        Removed anyway: with #11b in, adaptec5 converges and the mechanism never fires, so it
        would have been unexercised code altering a stop criterion. Re-add from this note if the
        re-baseline turns up a genuinely stuck design.
      **Real finding this exposed:** `checkFineDivergenceGuard` gates BOTH its checks on
      `overflow < 5*overflow_threshold`, but XPlace gates only the plateau kill that way — its
      `check_divergence` life-drain has no band (`param_scheduler.py:459-474`). An accidental
      divergence, currently near-inert (the drain also needs a converged `best_primary`, which a
      stuck run does not have). Left alone rather than changed blind; worth its own item.

> ### ⚠️ CORRECTION 2026-07-31 — the guard firing in phase 1 is XPlace's PHASE TRANSITION
> `run_placement_nesterov.py:167-172`: `need_to_early_stop()` sets `terminate_signal`, and when
> mixed-size is still in phase 1 that signal **triggers macro legalization and the optimizer reset**,
> then does `terminate_signal = False` and CONTINUES. It does not end the run. XPlace's own newblue4
> phase 1 ended at 0.182 — above its own 0.14 target — i.e. it too left phase 1 on the guard, not by
> converging.
>
> So sw_only's original `divergence_guard` stop on newblue1-6 was closer to CORRECT than step 3
> assumed: it was finding the right phase-1 exit point. What is missing is the phase 2 that should
> follow (**TODO #13**), not a better guard.
>
> **This puts step 3 change (b) in question.** Disabling the plateau kill is faithful to XPlace's
> gating (`not include_macros`), but without phase 2 there is nowhere for the run to go, so it
> grinds to the iteration cap — newblue4 and newblue5 both now stop at `max_iterations` 1200
> instead of exiting at ~670. Changes (a) doubled stop overflow and (c) coarse-divergence conjunct
> are unaffected and still right. Options: revert (b) until #13 lands (runs stop at the phase-1
> exit, as before, and that becomes the transition trigger later), or keep it and accept wasted
> iterations. NOT yet decided.
- [x] **Filler-area faithfulness gap — CONFIRMED AND FIXED 2026-07-31.** It was not "fewer fillers
      than XPlace"; on the macro-heavy low-td designs it was **ZERO fillers**.
      XPlace: `total_filler_area = target_density * stdcell_placeable_area - mov_stdcell_area`
      where BOTH terms exclude movable-macro area (`database.py` compute_filler_without_fence).
      sw_only had `available*target_util - m_total_movable_area`, macro area in both terms — a
      shortfall of exactly `M*(1-td)`. Measured: **adaptec5 0 → 310,073 fillers, newblue4 0 →
      205,682, newblue1 100,375 → 181,724.** adaptec5 now matches XPlace's own log
      (`#Fillers: 310073 Filler size: (1.0795e+01, 1.2000e+01)`) exactly on count and size.
      Landed together with five other divergences in the same function (macro exclusion from the
      size sample; filler height = ROW height, not a mean; the `target_density` raise to
      `stdcell_util`; a warning when zero fillers result). ISPD2005 verified bit-identical.
      **Quality impact UNMEASURED** — needs the MMS re-baseline. Full writeup + sweep instructions:
      `1_REVIEW/_NEW_HANDOFF_filler_faithfulness_20260731.md`.

Measured 2026-07-26 (report `1_REVIEW/NEW_mms_overflow_faithfulness_20260726.md`, data
`2_ARTIFACTS/mms_xplace_overflow.tsv`). **Root cause = FILLERS, not grid, not deposit formula.** sw_only's
own `[OVFW-DIAG]` line (newblue2): `sharp/+filler=0.143` ≈ **XPlace 0.145** — the overflow formula is
faithful; my earlier "gap" compared sw's *no-filler* (0.060) to XPlace's *with-filler* number. XPlace
counts filler density in BOTH its convergence and its "exact" report. sw_only converges on the
**filler-EXCLUDED** smoothed overflow (`convergence_include_fillers` defaults **false**, Output.cpp:697),
so it stops at no-filler 0.044<0.07 while the real (with-filler) overflow is still 0.102 → premature stop
→ cells+fillers clumped → under-spread → artificially low HPWL on the 9 macro-heavy designs.
**VALIDATED 2026-07-27** (flag on, 9 designs; data `2_ARTIFACTS/mms_fillconv_results.tsv`): correct fix but
PARTIAL — not the one-line silver bullet. newblue3 cleanly fixed (−12.7%→−1.7% vs XPlace); newblue6/2,
adaptec2, newblue7 within ~±3%. BUT adaptec5 (−22%) & newblue4 (−25%) ran longer yet stayed under-spread
(sharp/+filler 0.14–0.18); newblue5 DIVERGED (sharp 0.61). Residual: convergence is SMOOTHED (√2 inflation,
XPlace does this too), so "converged" ≠ fully spread on the hardest designs.
- [→] **MOVED TO #13 (2026-07-31): adopt `convergence_include_fillers=true`.** Still the right
      change, but it is a *phase-2* change, not a phase-1 one — see "Convergence metric" under
      TODO #13. Do not do it standalone.
- [x] **newblue4 spreading — RESOLVED (verified 2026-08-04).** Phase 2 (#13) is the fix: it now
      exits phase 1 on the eligible guard at 645, takes its phase 2, and lands **post-DP HPWL +1.80%
      vs XPlace at IDENTICAL post-DP density** (overflow 0.3332 both sides, `tools/post_dp_density.py`).
      Nothing further owed on this design.
- [x] **adaptec5 spreading — CLOSED 2026-08-06. Confirmed from the re-baseline, which had in fact
      finished on 2026-08-04 22:14** (`/tmp/rebase/progress.txt`, `2_ARTIFACTS/rebase_suite_results.tsv`);
      this entry simply was never updated. Both predicted effects are measured:
      - **Phase 1 converges** — `[STOP] reason=converged iteration=1436`, versus `diverged_hpwl`
        at 1163 before. HPWL 3.020e8 -> 3.248e8; the *lower* old number was the under-spread
        artifact, not a better result (macro-excluded exact overflow 0.104 -> 0.140).
      - **The post-DP density penalty is gone: +14.0% -> +0.2% vs XPlace** (ours 0.3834 vs XPlace
        0.3828 at td 0.5, `tools/post_dp_density.py` over both tools' own written post-DP `.pl`).
        adaptec5 no longer needs excluding from the headline on density grounds — though
        `2_ARTIFACTS/analyze_lgdp_suite.py` still prints a stale "phase-1 divergence -> under-spread
        GP" rationale for excluding it, which should go.
      Re-baseline verified HEAD-representative: mms/adaptec1 re-run on 2026-08-06 is **bit-identical**
      (`iterations.dat` and `RowBasedPlacement.def` md5 `b6d64d8a1fb9…`) to the 08-04 run.
- [x] **adaptec5 + newblue4 spreading (original scope)** — superseded by the two entries above.
      > ### ⚠️ The "#11b RULED OUT" verdict that used to live here was WRONG and is retracted
      > It read: *"RULED OUT 2026-07-31: TODO #11b is not the fix — the MMS A/B hit exactly these
      > three designs as its three worst results (+17.9%, +6.1%, +11.2% HPWL) ... the wirelength
      > cost is far too large to call it a fix. Rejected; defaults false."*
      >
      > That A/B ran on **zero-filler arms** (the `addFillers` bug, fixed 2026-07-31), which is
      > recorded under #8 as a double confound. Re-run with correct fillers on 2026-08-02
      > (`_NEW_REPORT_footprint_ab_20260802.md`): **mean +0.61% HPWL over 8 macro-heavy designs,
      > −0.38% excluding adaptec5**, and `on` converts a phase-1 divergence into a clean converged
      > run. The toggle was deleted the same day and the XPlace-faithful branch made unconditional.
      > #11b is now understood to be a large part of the actual fix for adaptec5 (2026-08-04):
      > it removes the overflow floor that starved λ's feedback loop.
      >
      > Kept verbatim because this verdict was cited for four days and someone will meet it again.
- [x] **newblue5 divergence — RESOLVED (verified 2026-08-04).** Phase 2 fixed it; re-run today on
      the current tree **converges at 1486 iterations**, post-DP HPWL **+1.44%** vs XPlace at
      **+0.3%** post-DP density. The old "sharp/+filler 0.61" reading came from a diverged
      single-phase run and no longer occurs. See also memories `phase2-implemented-newblue5-converges`
      and `newblue5-config-confound` (state td and grid on any newblue5 claim).
- [→] **MOVED TO #13: always include fillers in the CONVERGENCE signal.** (The *reporting* half of
      this is done — see Step 1.) Verified XPlace counts fillers in its GP-stop metric `overflow_fn`.
- [x] **Report the EXACT (sharp) + filler overflow as the headline number** — DONE in Step 1.
- [x] **DONE 2026-08-04: renamed `clamp` → `smooth`** in `computeOverflow` (declaration, definition,
      doc comment, and the `dumpBinDensity` loop variable). Pure rename, no behavior change. Left
      `clamp` alone where it means the *footprint* clamp and not the metric — `clamp_node`,
      `clampFixedDensity`, "√2 clamp" — those are a different concept and renaming them would be
      the wrong edit.
- [ ] (No grid or deposit-formula change needed — both already match XPlace.)

---

## #5 — Logger cleanup (DONE 2026-07-30 for sw_only)

**Summary:** Refactored sw_only's console logging and run-report system to match the governing principle:
the console shows the algorithm's user *useful* output and nothing more; nuisance detail belongs in a
report file. `interactive = false` → bare minimum. `quiet = true` → nothing but errors.

### Completed work

- [x] **Ordered severity scale** replaces the flat `Logger::keys` set (`Logger.h`): a single
      `LogLevel` threshold, `TRACE < DEBUG < DETAIL < ITER < INFO < WARNING < ERROR < CRITICAL`.
      Two orderings are deliberate and load-bearing, not alphabetical accidents:
      * `TRACE`/`DEBUG` sit **below** `DETAIL` because the run report captures `DETAIL`+, so the
        two developer-dump levels stay opt-in and never bloat it.
      * `ITER` sits just **below** `INFO` so `interactive=false` drops the live-status line while
        keeping every INFO message — one threshold, no second flag.
      Custom named channels (`profiling`) survive as an opt-in set alongside the scale, for output
      that isn't more or less severe than anything (`Logger::log_key`). `dbinfo` was dead — removed.
- [x] **Singleton + mutex dropped.** `iLogger`, `getLogger()`, `getMutex()` (declared, never
      defined), `iMutex` and the private ctor are gone; Logger is a plain static utility. This also
      removes the inconsistent `updateFunctionStats` lock. Revisit only if the merged host (#9) is
      actually threaded — then lock `function_stats_map` on *all three* accessors, not one.
      **UPDATE 2026-07-31 (#12):** sw_only IS threaded now. No race today — every `TIME_FUNCTION`/
      `TIME_BLOCK` sits at function or pass scope, outside every parallel region, and `Logger.h`
      now says so at the macro. But the "revisit if threaded" condition has been met, so the next
      person who wants a timer inside a parallel loop must add the lock (all three accessors) first.
- [x] **Renderer rewritten** (`Logger::emit`). Was: one `tabulate::Table` per log line. That padded
      every line to the cell width (trailing whitespace on all 130 lines of a piped log) and emitted
      **8 ANSI escapes per line** on a TTY. Now plain stream writes: zero trailing whitespace, one
      escape pair per line, and colour only when `isatty(stdout)`.
- [x] **Run report** — `<run_dir>/run.log`, everything at `DETAIL`+, written regardless of console
      verbosity. Lines logged before the run dir exists are held in a backlog and flushed by
      `Logger::openReport` (called from `createRunOutputStructure`).
- [x] **`interactive` follows the stream** — defaults to `isatty(stdout)`, so a piped/DSE run is
      automatically non-interactive. `output.interactive` in the config still forces either way; it
      is commented out in `run_config.toml` so the default applies.
- [x] **Message re-triage** — nuisance `INFO` → `DETAIL` across DataBase/Setup/Output/Schedule/
      Step/Partials/Visualizer. `logStepDiagnostics` was gated on the `DEBUG` key but logged its
      20 lines at `log_info` — exactly the drift the ordered scale prevents; now `log_debug`
      throughout. Deprecated-config notice promoted `INFO` → `WARNING`.
- [x] **Table consolidation** — the fixed/movable/filler counts were four loose load-time lines;
      they now live as rows in the Benchmark info table and the loose lines are `DETAIL`.
- [x] **`X=10` throttle** → config `output.iterations_per_status`.

**Verified on adaptec1 (25 iters):** quiet = 0 lines; piped = 84 (68 of them the two tables, 11 message
lines); TTY = 103 (adds banner + live status). `run.log` = 192 lines, 0 trailing-whitespace, 0 ANSI.
Config force-on-in-a-pipe and force-off-on-a-TTY both verified, as is a non-default cadence.

### Follow-ups — resolved or relocated 2026-08-07

- **pl_algo's old singleton Logger** — DONE 2026-08-04 via #9 step 1, exactly as planned: it was
  deleted rather than ported, and both hosts now build the one rewritten `Logger` from
  `host/src/common/`. pl_algo's own sources never touched the Logger (only the shared parser did),
  so there were no call sites to update. Two behaviour notes for pl_algo: it does not call
  `setup_logging`, so the console threshold is the `INFO` default (`db.printInfo()` still prints;
  the parser's `DETAIL` chatter no longer does), and it never calls `openReport`, so report lines
  accumulate in `report_backlog` instead of a file — bounded, since pl_algo only logs during parse.
- The four remaining items were **cosmetic or unrelated** (double-bordered tables, the banner
  bypassing the Logger, `run.log` written for sweep runs, the `algo_time = 0.000` oddity). They
  moved to **TODO.md "Parked"** on 2026-08-07 rather than keeping a completed task open.

---

## #8 — Investigate zero-area interior `terminal` nodes in newblue5 (opened 2026-07-29)

> ### ✅ ANSWERED 2026-07-31 — the terminals are INERT. Report:
> ### `1_MARK_TO_REVIEW/_NEW_REPORT_newblue5_todo8_20260731.md`
>
> **The terminal-node theory is dead, structurally — not merely "no evidence found".** All 4790 are
> 0×0, all FIXED, and the zero-area set and the `terminal` set **coincide exactly** (0 zero-area
> movable nodes anywhere). Every terminal pin offset is exactly **(0,0)** — so the "scrubbed macro
> left its pin geometry behind" theory is falsified (the 4 real movable macros, by contrast, carry
> 795/796/413/413 pins spread across their full footprint). Decisively: newblue5's total fixed area
> is **exactly 0.0** (confirmed by XPlace's own log, `FixArea: 0.000E+00`), so a terminal contributes
> zero to the density map, zero to the overflow numerator and zero to the filler budget. **A
> zero-area node cannot form a density hotspot**, so the planned per-region probe was unnecessary
> and deliberately not built.
>
> **What is actually wrong (canonical config: grid 1024, td 0.5, seed 42):**
> 1. **newblue5 places with ZERO fillers.** 51.4% of its movable area is movable macros (172.0M of
>    334.3M; 26.4% of the die). HEAD's `addFillers` counts macro area in *both* terms, driving the
>    budget to −9.2M. XPlace's std-cell frame gives **632,490** fillers — my independent arithmetic
>    from the bookshelf files matched XPlace's log digit-for-digit (`#Fillers: 632490`, size
>    `(1.0112e+01, 1.2000e+01)`).
> 2. **There is no "explosion".** HPWL rises smoothly (ordinary spreading). The real failure is an
>    overflow **floor** plus λ runaway: iterations 674→1044 move overflow 0.3544→0.3500 (flat) while
>    λ goes 0.147→102.5 (**×700**) and HPWL inflates **+17%**. That is TODO #4's unowned
>    "density-weight runaway"; newblue5 is a clean witness for it.
> 3. **The floor is structural** and XPlace hits it too — its Mixed-GP ends at **0.1697**, above its
>    own doubled 0.14 target, reaching 0.0452 only *after* macro legalization (phase 2).
>
> **2×2 measured** (both binaries verified bit-identical on adaptec1):
> | arm | fillers | #11b | HPWL | vs XPlace | sharp/+filler | stop |
> |---|---|---|---|---|---|---|
> | A baseline | 0 | off | 4.399e8 | +16.0% | 0.350 | `divergence_guard`, fallback |
> | B fillers | 632k | off | **3.994e8** | **+5.3%** | 0.429 | `divergence_guard`, fallback |
> | C #11b | 0 | on | 4.892e8 | +29.0% | 0.338 | **converged** |
> | D both | 632k | on | **4.003e8** | **+5.6%** | 0.394 | **converged** |
> | XPlace | 632,490 | on | 3.792e8 | — | **0.1697** | phase-1 handoff |
>
> Fillers buy the **HPWL** (−9.2%); `macro_td_expand_ratio` buys the **convergence**. Arm C
> reproduces the footprint A/B's newblue5 figure exactly (+11.2%), confirming the setup matches the
> record.
>
> ### ⚠️ CORRECTION 2026-08-01 — "2–2.5× under-spread vs XPlace" was WRONG (my error)
> It compared **our macro-INCLUDED** overflow to **XPlace's macro-EXCLUDED** one.
> `run_placement_nesterov.py:173` sets `ps.zero_macro_grad = True` **before** the Mixed-GP
> `evaluate_placement` at line 182, and `evaluator.py:30` then drops `is_mov_macro`. **So the 0.1697
> in `tools/benchmarks.py::_XPLACE_MMS_MIXED_GP` EXCLUDES movable macros** — this affects every MMS
> overflow comparison, not just newblue5. Confirmed three ways: code order; arithmetic (newblue5's
> macros alone contribute ≈0.257, so 0.1697 cannot include them); and XPlace's own
> `--global_placement False` run, where `zero_macro_grad` is never set, reporting **0.4836**.
>
> Recomputed like-for-like from the DEFs (`/tmp/t8/overflow_variants.py`, validated against
> sw_only's own `sharp/no-filler`): **A 0.0964 · B 0.1244 · D 0.0940 vs XPlace 0.1697.**
> **sw_only's std-cell spreading matches or beats XPlace.** The whole "under-spread" signal was the
> movable macros — exactly what phase 2 legalizes. Strengthens the case for **#13**.
>
> **Unresolved:** the filler axis. Code says XPlace's exact overflow also excludes fillers
> (`get_mov_node_info` appends them after `mov_rhs`; `get_obj_overflow` slices `[mov_lhs:mov_rhs]`),
> but the newblue2 calibration in `overflow-metric-grid-faithfulness` says included. Those
> contradict — resolve before treating the numbers above as final.
>
> **Follow-up actions this creates (not done):**
> - [x] **DONE (verified 2026-08-04): the overflow column of `tools/benchmarks.py::_XPLACE_MMS_MIXED_GP`
>       is labelled macro-EXCLUDED** — the comment above the table spells out both the filler and the
>       macro exclusion and names the sw_only number to compare against, and `BENCHMARKS.md` carries
>       the same note. The HPWL column is unaffected (`get_obj_hpwl` has no such exclusion).
> - [ ] **Re-read TODO #4's reframing in this light.** Its "XPlace's Mixed-GP ends at 0.10–0.18 on
>       15 of 16 MMS designs" figures are macro-EXCLUDED, so they describe XPlace's *std-cell*
>       spread. Ours on the same basis is ~0.09–0.12 — i.e. comparable or better, which changes
>       what "we don't spread" means across the whole suite.
> - [ ] **Add a macro-excluded variant to `computeOverflow`** so this is a first-class number
>       rather than a post-hoc DEF script. This is the config-gated diagnostic originally scoped
>       here — now justified by evidence rather than speculation.
>
> **`sharp/+filler` IS XPlace's "exact Overflow"** — verified in `src/evaluator.py`:
> `get_obj_overflow` uses exact node size with `node_weight = ones` (no `expand_ratio`, no macro
> fill) over `total_mov_area_without_filler`. So confining #11b to the clamp branch in `Grid.cpp` is
> *faithful*, and the comparison holds in every arm.
>
> ### ⚠️ CORRECTION 2026-08-01 (Mark) — "#11b blinds the stop metric" was WRONG
> I originally wrote that #11b makes macros "contribute zero to the convergence signal", framing it
> as a symptomatic fix. **Wrong.** A macro depositing at exactly `target_density` fills a covered
> bin to **exactly capacity** (`bin_area × td`), excess 0 — which is *correct*: a bin taken up
> solely by one large macro is full at the target density, not overflowed. The macro has not
> vanished; it has consumed the bin's entire budget, so **anything overlapping it overflows
> immediately** (`density = cap + cell_area ⇒ excess = cell_area`). Macros generate overflow
> exactly when other nodes overlap them — the desired behaviour.
>
> The real defect is the reverse: with #11b **off**, every movable macro emits `area × (1 − td)` of
> overflow **permanently, whether or not anything overlaps it** — irreducible by any movement, on a
> design where macros are 26.4% of the die. **#11b is a correctness fix, not a symptomatic one.**
>
> What survives: the headline `Final Overflow (exact, +fillers)` deposits macros at weight 1 (the
> #11b branch is clamp-only), so it still carries that spurious term on mixed-size designs — arm D
> reads sharp 0.394 vs clamp 0.0676. **The REPORTED number is the misleading one, not the
> convergence signal.** XPlace avoids this by excluding macros from its phase-1 eval
> (`zero_macro_grad`) — same conclusion as the macro-exclusion correction above, reached
> independently.
>
> ### ✅ DECIDED (Mark, 2026-08-01): `macro_td_expand_ratio` → expected to be LOCKED `true`
> Keep it a toggle **for now**, while testing continues; the expectation is that it becomes
> unconditional and the legacy branch is deleted. It is XPlace-faithful *and* correct on the
> density accounting. The only thing holding it back is the 16-design A/B's −5.2% mean HPWL — and
> that A/B ran on **zero-filler arms**, so it must be redone after the filler change lands.
> - [x] DONE 2026-08-02: re-ran the #11b A/B with correct fillers, mean +0.61% HPWL over 8
>       macro-heavy designs (−0.38% excluding adaptec5), where `on` also fixes a phase-1
>       divergence into a clean converged run. `1_REVIEW/_NEW_REPORT_footprint_ab_20260802.md`.
> - [x] DONE 2026-08-02: toggle + legacy branch removed (as #11a was), per TODO #2's
>       retire-settled-toggles pattern. `macro_deposits_target_density` is no longer a config key;
>       the XPlace-faithful branch is unconditional in `Grid.cpp::computeNodeFootprint`.
>
> The real fix for the residual gap is **TODO #13 phase 2** — newblue5 is the suite's strongest
> case for it.
>
> ⚠️ **Corrections to the record (see report §5):**
> - `_NEW_HANDOFF_filler_faithfulness_20260731.md` §4 is **wrong** that the memories
>   `mms-hard-spreading-three-diseases` / `overflow-metric-grid-faithfulness` were derived on a
>   *zero-filler* newblue4/adaptec5. Those runs were at **td = 1.0**, where the macro term cancels:
>   measured **adaptec5 1,509,741 · newblue4 1,104,300 · newblue5 2,602,944** fillers. (Mark caught
>   this from the GIFs.) The memories stand; the handoff's inference about them does not.
> - **Three newblue5 configurations are conflated across the notes** — td 1.0/auto (converged,
>   3.246e8 @ 0.053), td 1.0/2048 + `convergence_include_fillers` (the GIF that opened this TODO),
>   and canonical td 0.5/1024. **The GIF was not the canonical config.** State td and grid on any
>   future newblue5 claim.
> - **TODO #11b's rejection is confounded twice**, not once: with the stop criterion (already noted)
>   *and* with zero fillers. #11b alone is +11.2% HPWL; #11b with correct fillers is −9.0%.

Triggered by `2_ARTIFACTS/newblue5_placement.gif`: placement looks fine, then diverges/explodes
starting around iter ~400. Visually there are large *movable* macros (correctly parsed — verified,
not a fixed/movable bug, see below) plus a scatter of small dark-red squares in the die interior
that turned out to be the benchmark's zero-area `terminal` nodes, not misclassified macros.

**Established so far:**
- `newblue5.nodes`/`.pl`: 4790 nodes carry the literal `terminal` keyword, **all exactly 0×0** in
  size, and are marked `FIXED` in `.pl`. Confirmed via the parser (`DataBase.cpp` in
  `host/src/sw_only/src/`) that fixed-vs-movable classification is purely keyword/`.pl`-driven —
  no size-based logic anywhere flips a movable node to fixed (`isLarge()`/`m_is_large` is
  bookkeeping-only, never touches `m_status`). The 4 genuinely huge macros (e.g. `o1228259`–
  `o1228262`, up to 10260×3036) are correctly movable. Visualizer (`Visualizer.cpp`) draws FIXED
  as dark red w/ border, movable macros as bright red w/o border, and floors tiny sizes up to a
  min render size — so the 4790 zero-area FIXED terminals are what render as the scattered small
  dark-red squares. **Not a classification bug in our code.**
- XPlace (`~/phd/Xplace`) handles these identically: `terminal` alone doesn't set fixed (only the
  `.pl` `/FIXED` suffix does — `file_bkshf_db.cpp:711-788,1078-1111`), and it has zero position/
  size-based special-casing in `GPDatabase::setupNodes`. XPlace *does* have a real placement-
  blockage/keepout concept (`Database::placeBlockages`, distinct `"Blkg"` node type,
  `GPDatabase.cpp:75-91`), but it's only populated from DEF `PLACEMENT` blockage sections or a
  DAC/ICCAD-2012-only irregular-shape bookshelf extension (`file_bkshf_db.cpp:611-618`, explicitly
  commented as ICCAD/DAC2012-only) — **neither applies to newblue5** (plain ISPD05 bookshelf,
  `.aux` lists only `nodes/nets/wts/pl/scl`, no shapes file). So XPlace creates **zero** keepout
  geometry for these nodes either — same blind spot in both tools, not a divergence between them.
- Working theory (Mark's, plausible but unconfirmed): these are pin stubs left behind after IBM
  scrubbed proprietary hard macros out of the original design for the ISPD05 release — real
  connectivity, zero remaining footprint. Supporting evidence pulled from `newblue5.nets`: 4790
  terminals, avg net-degree 17.2, several groups of terminals sharing an *exact* degree (8 terminals
  at degree 313, 2 at 298, 2 at 88, 2 at 74/64, etc. — plausible repeated/mirrored macro instances),
  many as 2–3-pin nets fanning out to nearby small cells (i.e. plausible former intra-macro nets
  redirected to the macro's boundary pin). Terminal y-coordinates span the *whole* die height
  (p10=5635 to p90=19675 out of a ~67–25615 core range) — not clustered at the periphery like
  classic IO pads — which favors the removed-macro theory over "these are just boundary IO pads"
  for most of the 4790, though the highest-degree cluster (313-degree group) sits at a fairly
  narrow y-band (~1400–1700) which could equally be a pad row or a cluster of related macro pins.
- Checked `results/mms_suite_precondON/newblue5/newblue5/20260717_234925_203_cpu_cpu/schedule_trace.csv`
  around iter 400: `grad_norm_sq` and `pos_norm_sq` are already climbing *smoothly* well before 400
  (e.g. iter 395: grad_norm_sq=4.58e4 → iter 410: 1.21e5, steady ~exponential-ish growth, no sudden
  kink at exactly 400) — consistent with the existing `mms-hard-spreading-three-diseases` memory's
  characterization of newblue5 as a genuine λ-starved/under-damped-macro divergence, not a single
  discrete trigger event. Doesn't yet prove or rule out the terminal hub theory as a contributor.

**Original next steps — ALL THREE RESOLVED 2026-07-31 (see the banner above):**
- [x] ~~Per-node / per-region density data around the hub clusters.~~ **Not needed and deliberately
      not built.** The terminals are 0×0 and newblue5's total fixed area is *exactly 0.0*, so they
      deposit nothing — a zero-area node cannot produce a density hotspot. Building the probe would
      have been speculative (CLAUDE.md rule 2).
- [x] **Gradient/preconditioner path audited against a degree-313 zero-area node — clean.**
      `addNet` is called once per *pin* (`DataBase.cpp:626`), so `getNets().size()` really is the pin
      count and matches the per-pin gradient accumulation; `updatePrecondWeights` matches XPlace
      exactly (`alpha_1 = mov_node_to_num_pins`, `param_scheduler.py:372`); the WA gradient's
      max-shift is algebraically exact; nothing divides by node width/height. No hub-node or
      zero-area assumption anywhere.
      *Incidental (benign, unfixed):* `computeHpwlPartials` clears `probe_grad` only for movable +
      IOPad nodes, but the accumulation loop writes it for **every** pin including FIXED components,
      so a fixed node's `probe_grad` accrues across iterations. Never read for stepping (only
      `getMovableNodes()` steps), magnitudes stay O(1e5) — wasted work, not a bug. Worth a one-line
      skip if anyone is in there anyway.
- [x] **XPlace corroboration done from existing logs — no new GPU run needed.**
      `~/phd/Xplace/result/2026-07-17-23:11:24_newblue5` is the canonical Mixed-GP reference
      (3.792e8 @ 0.1697 → macro legalization → phase 2 `GP Stop! #Iters 2010 … overflow: 0.0452`).
      Confirmed newblue5's td=0.5 / grid=1024 originate in `utils/setup_dataset.py` (so
      `args.target_density < 1.0` holds and **#11b is active in the reference**), and that XPlace
      produces exactly 632,490 fillers. **Not a shared blind spot on the terminals** — the question
      is moot, since neither tool deposits anything for a zero-area node.

---

## #12 — Multithread sw_only (opened 2026-07-30) — **DONE 2026-07-31**

sw_only was single-threaded: one placement run used one core of the 8-core box, so interactive
turnaround on a big MMS design stayed at tens of minutes no matter what. Now threaded with OpenMP.
Commits: `e2f039c` (profile + DCT tables), `372861d` (threading). Report:
`1_REVIEW/_NEW_multithread_swonly_20260731.md`.

### Step 0 — profile: the premise of this task was WRONG, and that changed the plan

The original table here (`computeElectricFields` 74%, DCT 87% of that, HPWL partials only 8%) came
from `mgc_fft_1` **forced** to a 512 grid — a 32k-cell design on 262k bins, 0.12 cells/bin. Real
designs auto-size the grid so cells/bin stays near 1, and then the picture inverts. Share of
`performIteration`, 20 iterations (`tools/profile_swonly.sh`):

| | adaptec1@512 | superblue11@1024 | newblue3@2048 | mgc_fft_1@512 |
|---|---|---|---|---|
| iteration time | 7.0 s | 36.9 s | 78.8 s | 2.8 s |
| **density DCT** | **20%** | **18%** | **43%** | **65%** |
| computeOverlaps | 22% | 25% | 24% | 10% |
| combineGradients | 12% | 15% | 16% | 5% |
| computeHpwlPartials | 15% | 14% | 4% | 8% |
| recordIterationResults | 12% | 10% | 3% | 5% |
| step/reset/BB/precond | 16% | 15% | 10% | 7% |

**No single function exceeds 25% on a real design**, so "parallelize the transforms" would have
capped at ~1.25x. The whole iteration had to be threaded, which is what was done.

### Step 1 — data layout: the targeted half was worth it, the full refactor is NOT
- [x] `DCT_fft`/`IDCT_fft` evaluated one `std::polar` (a libm sin+cos) **per output element** —
      ~6N² transcendental calls per iteration, 12.6M at a 1024 grid, the dominant term inside
      `dct_rowpass`. Now cached per N with the FFT stage roots and the bit-reversal permutation.
      Bit-exact (same argument, same `std::polar` call, just computed once). ~1.8x on that block
      alone, single-threaded.
- [x] The FFT forms take a caller-owned output buffer instead of returning a vector (two heap
      blocks per call, ~25k allocations/iteration at 2048 — the allocator traffic that would have
      stopped the row loops from scaling).
- [~] **Flat row-major matrices: NOT DONE, and measured as not justified.** After the above,
      `dct_transpose` is 1–4% of the iteration. Revisit only if a bigger grid changes that.

### Step 2/3 — threading (OpenMP; TBB stayed closed)
- [x] Transform row passes, transposes, spectral scaling, grid I/O, all per-node loops, per-net
      loops, `computeOverlaps`, `computeHpwlPartials`, `combineGradients`, `computeOverflow`,
      `computeTotalWirelength`.
- [x] `DataBase::buildNodeIndex()` — flat index-addressable views of the component/net maps
      (a `std::map` cannot drive an `omp parallel for`, and walking its tree ~10x per iteration
      pointer-chased 200k–1M entries for nothing).

### Reproducibility — solved, not traded away
`params.deterministic` (default **true**). Only the two *scatter* reductions (cell area into
shared bins, net gradients onto shared nodes) are switched by it; disjoint-write loops and scalar
reductions are threaded unconditionally and bit-exact by construction (`OrderedReduce` in
`Common.h` sums in index order). Under `true` the per-item work still runs threaded and only the
shared add is replayed in the original order.

**Verified bit-identical to the pre-threading serial binary** — `iterations.dat` and all 211447
adaptec1 cell positions — at `OMP_NUM_THREADS` = 1, 3, 8. So sw_only is still a valid golden.

### Thread count — one CPU is deliberately left free
No config key (Mark's call: "always all cores"). OpenMP default **minus one CPU**;
`OMP_NUM_THREADS` overrides.
- [x] **Re-measured on an idle box 2026-07-31 — my first explanation was WRONG and is corrected
      in the code.** There is no intrinsic cliff at the CPU count. Loop of small parallel regions
      separated by serial work, threads 1/2/4/6/7/8: idle = 0.68/0.68/0.69/0.71/0.72/**0.73** s;
      with one other job running = -/-/0.70/-/0.73/**5.89** s. The 8x collapse I first measured was
      contention with the footprint sweep, not "team size == CPU count". Reserving one CPU is
      still right — it costs nothing idle and avoids the collapse when the box is shared, which
      here is the normal case — but for that reason, not the one first written down.
      `OMP_WAIT_POLICY=passive` also avoids the collapse but is ~20% *slower* idle (0.88 vs
      0.72 s) and cannot be set from inside the program anyway (libgomp reads its environment in
      a library constructor — a `setenv()` in `main()` is verifiably too late).

### Measured speedup (idle box, `performIteration` over 40 iterations, `tools/bench_swonly.sh`)

| design | pre-#12 | deterministic | atomics | det | fast |
|---|---|---|---|---|---|
| adaptec1 @512 | 11.03 s | 5.44 s | 4.16 s | **2.03x** | **2.65x** |
| adaptec1 @1024 | 19.58 s | 8.31 s | 7.28 s | **2.36x** | **2.69x** |
| mgc_matrix_mult_1 @128 | 5.45 s | 2.67 s | 1.93 s | **2.04x** | **2.83x** |
| superblue11_a @1024 | 53.48 s | 28.31 s | 22.54 s | **1.89x** | **2.37x** |
| newblue3 @2048 | 111.44 s | 54.32 s | 46.73 s | **2.05x** | **2.38x** |

Deterministic ~2x, atomics ~2.4–2.8x, consistent across design size and grid. Roughly half the
deterministic win is single-threaded (twiddle tables + flat node index + no per-call allocation):
1 -> 7 threads alone is 1.67x on adaptec1, and 1.67 x ~1.2 ≈ 2.03.

Bit-exactness re-confirmed on the **full** set (adds superblue11_a and newblue3).

End to end, adaptec1 run to convergence (**total wall clock**, incl. the serial LEF/DEF read):
pre-#12 136.6 s / 688 iters / HPWL 7.057e+07 / ovfl 1.335e-01; deterministic **69.7 s (1.96x)**,
identical iteration count and identical result to every printed digit; atomics **50.5 s (2.71x)**,
696 iters, HPWL 7.056e+07, ovfl 1.323e-01 — the FP noise stays well inside seed-to-seed variation
and does not cost quality.

### Where the remaining headroom is (measured, not guessed)
Thread scaling on adaptec1, 1 -> 7 threads. Atomics mode: `dct_rowpass` **5.2x**,
`computeHpwlPartials` **3.9x** (both compute-bound) — but `computeOverlaps` **2.4x**,
`combineGradients` **2.0x**, `recordIterationResults` **1.3x**, all flat by 4 threads. Those are
**memory-bound over pointer-chased `Node`/`Bin` objects**, not contention-bound. Deterministic
mode on an idle box, same 1 -> 7: whole iteration 1.67x, `dct_rowpass` 4.6x, `computeHpwlPartials`
2.1x, `combineGradients` 1.9x, `computeOverlaps` 1.24x (its deposit half is serial by design),
`recordIterationResults` 1.17x.
- [ ] The next real win is an SoA layout for the hot per-node/per-bin fields, not more threads.
      `Bin` is ~64+ bytes (including a `std::vector<Node*> overlapping_nodes` that only the dead
      `Bin::computeOverlap` ever fills) but the density scatter touches only its 4-byte
      `total_overlap`. Big change, own task — do not fold into #12.

### Sweep concurrency — SETTLED 2026-07-31: sequential, one design at a time
Each run now takes ~7 threads, so the old phase-tiered concurrency (4/2/1) would have been 28
threads on 8 CPUs. Rather than cap it with `OMP_NUM_THREADS` math per phase, measured which
policy actually gives better sweep throughput (Mark's ask — direct A/B, not more modeling):
8-design mix (ISPD2005 + ISPD2015, `2_ARTIFACTS/run_thread_throughput_ab.sh`), one run each:
- **A** — sequential, one design at a time, placer default threads (all-but-one core): **873 s**
- **B** — all 8 concurrent, 1 thread each: **886 s**

**A tie (1.5% apart).** Per-run thread scaling is sublinear (~1.67x at 7 threads, measured
earlier), and running 8 single-threaded jobs at once gives some of that back to L3/memory-
bandwidth contention across the concurrent processes — the two effects roughly cancel. Every
one of the 8 designs also produced bit-identical HPWL and iteration count between A and B,
confirming `deterministic=true` holds under real concurrent scheduling, not just the isolated
thread-count microbenchmark.

Decision: **sequential everywhere, `OMP_NUM_THREADS` left unset (placer default).** Same
throughput as tiered concurrency, none of the memory-budget bookkeeping or oversubscription
risk. Applied to `tools/dse.py` (`MAX_PARALLEL = 1`) and `2_ARTIFACTS/run_footprint_ab.sh`
(phases collapsed to one sequential loop). `2_ARTIFACTS/run_mms_ab.sh` has the same
phase-tiered pattern and needs the identical simplification next time it's touched — not done
yet, just noted.

---

## #13 — Implement mixed-size PHASE 2: macro legalization + fixed-macro std-cell GP (opened 2026-07-31)

> ### ✅ IMPLEMENTED 2026-08-01 — newblue5 CONVERGES. Report:
> ### `1_REVIEW/NEW_REPORT_phase2_implemented_20260801.md`
> Built in worktree `/home/msears/phd/AIEplace_t8` on `caa8f2b` **+ the uncommitted filler work**
> (copied in; verified 632,490 fillers = XPlace exactly). **Mark's tree untouched.**
>
> | newblue5 (grid 1024, td 0.5, seed 42) | phase-1 only | **phase 2** | **+ LP legalization** | XPlace |
> |---|---|---|---|---|
> | iterations | 1070 | 1926 | 1912 | 2010 |
> | HPWL | 4.891e8 | **3.984e8** | **3.987e8** | 3.833e8 |
> | stop | `divergence_guard`, fallback | **`converged`**, primary | **`converged`**, primary | GP Stop |
> | clamp/+filler | 0.350 | **0.0844** | **0.0842** | — |
> | sharp/+filler | 0.429 | **0.229** | **0.229** | — |
> | macro-excluded (XPlace-comparable) | 0.1257 | **0.0756** | **0.0756** | **0.0452** |
>
> **The divergence TODO #8 chased is gone** — `converged` with a *primary* best. −9.4% HPWL and
> −35% exact overflow vs the original baseline; **+3.9% HPWL** from XPlace's final.
>
> **Hypothesis confirmed: freezing the macros, not legalizing them, is the mechanism.** Legalization
> moved HPWL 0.08%. newblue5's phase 1 already leaves the macros nearly legal — **1 overlapping pair
> of 91, displacement 8.85** (XPlace's own moves 2484). Stage 2 buys legality, not quality, which
> vindicates doing stage 3 first.
>
> **Built:** P3 phase-relative counter (`phaseIteration()`, XPlace `init_iter`) · P2 macro-definition
> unification · S3 `placer/Phase2.cpp` (freeze + rebuild fillers + re-seed + reset) ·
> S2 `placer/MacroLegalize.cpp` (constraint graph + LP solved by the **bundled CBC**, longest-path
> fallback). New keys `enable_phase2` / `macro_legalization` / `macro_lp_solver`;
> `enable_phase2 = false` restores the old single-phase run exactly.
> **P3 and P2 are verified NO-OPS** — adaptec1 *and* newblue5 bit-identical on `iterations.dat`
> *and* the placement DEF.
>
> **Bugs caught in review before they produced a wrong number:** (1) frozen macros kept phase 1's
> stale `probe_pos`, and `computeNodeFootprint` deposits at the PROBE position — every macro's
> density would have landed where it no longer was; (2) the best-solution restore was unguarded
> against a phase 1 that never recorded one. Plus one caught by the run: `legalizeMacros()` ran
> before the freeze and saw an empty set.
>
> ### ✅ `convergence_max_iterations` 1200 -> 10000 (DECIDED, Mark 2026-08-01)
> The first phase-2 run was **starved**: phase 1 ended at 1069, phase 2 got 131 iterations and
> stopped at `max_iterations` with overflow 0.756 (completely unspread). Now set to **10000**,
> matching XPlace's `args.inner_iter` default — which likewise spans both phases and never binds
> (its observed MMS totals are 1344-2280).
>
> This **supersedes TODO #4's "leave at 1200, do NOT raise"** for the two-phase flow. That
> decision remains correct about what it was actually reasoning over: a single-phase run that is
> *stuck* does not benefit from more iterations, and newblue4's λ-runaway trajectory proved it.
> What changed is that the cap must now cover BOTH phases to be the runaway backstop it is
> documented as, rather than a de-facto schedule. The density-weight-runaway defect #4 identified
> is unaffected and still open — a bigger cap makes bounding λ *more* important, not less.
>
> ### Not done
> - [x] DONE 2026-08-02: **longest-path refinement ported** (`MacroLegalize.cpp`'s
>       `runLongestPathRefinement`/`markEdgeToMove`/L-R node slack). Fired for real on adaptec2
>       (total negative slack -3.53e3 -> 0, then 42/42 overlaps resolved) — the other 14 designs
>       tested didn't need it, matching newblue5. `1_REVIEW/NEW_REPORT_phase2_mms_suite_20260802.md`.
> - `macro_legalization_xy` / `_ilp` variants and the retry driver; **site/row alignment** after
>   legalization. Still not ported.
> - [x] DONE 2026-08-02: **all 16 MMS designs now run** (15 more +
>       `1_REVIEW/NEW_REPORT_phase2_mms_suite_20260802.md`). Correction to this section's own
>       framing: MMS's td=1.0 designs are NOT macro-free (62-959 movable macros measured) — 14 of
>       15 entered phase 2; only adaptec5 stayed in phase 1 (genuine `diverged_hpwl`, correctly
>       excluded by the eligibility gate, not a regression).
> - [x] DONE 2026-08-02: **phase-1 numbers now in run_summary.md and results.csv** (`Phase 1
>       Iterations/HPWL/Overflow.../Stop reason` rows; `Phase1 Iters/HPWL/OVFW.../Stop Reason`
>       columns).

**sw_only implements only phase 1 of XPlace's mixed-size flow.** XPlace runs three stages
(`run_placement_nesterov.py:167-230`, log evidence in
`~/phd/Xplace/result/2026-07-17-23:03:11_adaptec5/log/test.log`):

1. **Mixed-GP** — macros movable, `include_macros = True`, `stop_overflow = args.stop_overflow*2`.
   Ends at `After Mixed-GP, best solution eval`. **This is all sw_only does.**
2. **Macro legalization** — `macro_legalization_main` (`detail_placement.py:347`): an LP solve
   (logs `Use cbc to solve LP`, variants `macro_legalization_mix` then `macro_legalization_xy`,
   picks the lower-displacement result), then site/row alignment + an overlap check via the
   compiled `gpudp.macroLegalization` op, with a retry at a longer time limit if macros still
   overlap. adaptec5: 76 macros, total displacement 10255, 0.56 s.
3. **Reset optimizer → re-run std-cell placement with macros FIXED** — `include_macros = False`
   (so `stop_overflow` returns to 0.07 and the plateau kill re-enables), density map re-built with
   the legalized macros as fixed obstacles, filler area recomputed from
   `__total_mov_area_without_filler__` (macro area excluded), std cells re-initialised
   `randn_center` while legalized macro positions are kept, macro gradients zeroed via
   `cache_node_weight[macros] = -1.0`, initial LR re-estimated, best-solution tracker reset.

### Why it matters — this is where XPlace's quality actually comes from
Phase 2 buys a **2–4× overflow improvement for ~0–3% HPWL** (from the local XPlace MMS logs;
phase-1 numbers are `tools/benchmarks.py::_XPLACE_MMS_MIXED_GP`):

| design | Mixed-GP (phase 1) | final (after phase 2) | Δ |
|---|---|---|---|
| adaptec1 | 6.238e7 @ 0.131 | 6.453e7 @ 0.042 | +3.4% HPWL, 3.1× overflow |
| adaptec5 | 3.035e8 @ 0.148 | 3.093e8 @ 0.047 | +1.9% HPWL, 3.1× overflow |
| newblue1 | 5.946e7 @ 0.136 | 5.832e7 @ 0.045 | **−1.9% HPWL**, 3.0× overflow |
| newblue4 | 2.238e8 @ 0.182 | 2.299e8 @ 0.094 | +2.7% HPWL, 1.9× overflow |
| newblue5 | 3.792e8 @ 0.170 | 3.833e8 @ 0.045 | +1.1% HPWL, 3.8× overflow |

Consequences of not having it:
- **Our GP output has illegal macro positions** (overlapping, off-site), so any downstream
  LG/DP has to absorb that. Ties directly to TODO #3's post-DP-HPWL item.
- **We can never match XPlace's headline number**, only its phase-1 endpoint. Every published
  XPlace MMS figure is post-phase-2.
- Phase 1 cannot spread past ~0.14 by design, so chasing lower overflow *in phase 1* is chasing
  the wrong target — see the newblue4 evidence in TODO #4 step 3 (1200 iterations, still 0.337).

### ⚠️ Phase 1's only durable output is MACRO POSITIONS
`get_mov_node_info(init_method="randn_center")` (`database.py:889`) **re-randomizes every movable
cell** to a Gaussian at the die centre (scale = 0.1% of die span); the legalized macro positions are
then copied back over them. The std-cell placement phase 1 worked so hard on is **thrown away**.
Log confirms it: overflow jumps 0.1485 -> 0.8604 across the restart.

This recontextualises every MMS comparison we have made:
- **Phase-1 HPWL is not the deliverable.** Our "+3.5% mean HPWL vs XPlace Mixed-GP" measures a
  placement XPlace discards. It is a sanity signal, not a quality target.
- **What phase 1 must get right is where the macros end up.** That is the input to legalization and
  the fixed obstacle set for phase 2. We currently have no metric for macro-placement quality at
  all — worth adding (macro displacement vs XPlace's legalized positions, macro overlap area).
- It also explains why chasing low phase-1 overflow is misdirected effort (TODO #4 step 3).

### Convergence metric — `convergence_include_fillers` (absorbed from TODO #4 "step 4", 2026-07-31)
Adopting `convergence_include_fillers = true` is XPlace-faithful (`overflow_fn`, its GP-stop signal,
counts filler density) and is still the right change — but it belongs HERE, not as a standalone
phase-1 fix. Three reasons, all measured today:

1. **Phase 1 is not where fillers matter.** Phase 2 re-randomizes std cells and rebuilds the filler
   set from `__total_mov_area_without_filler__` (macro area excluded) — so the filler population
   the convergence signal should count is the *phase-2* one, and it is a different population.
2. ~~**It is a NO-OP on exactly the designs we care about.**~~ **INVALIDATED 2026-07-31 — this
   premise was a BUG, now fixed.** newblue4 read `clamp/no-filler = clamp/+filler = 0.289` because
   at td=0.5 it had **literally zero fillers** — not a property of the design, but the filler-area
   gap in `addFillers` (see #4, now fixed). newblue4 now has 205,682 fillers and adaptec5 310,073,
   so the filler-inclusive convergence signal is no longer degenerate on them. **Re-evaluate
   whether `convergence_include_fillers` still belongs in phase 2 rather than phase 1** once the
   MMS re-baseline lands — this reason for deferring it is gone, and reasons 1 and 3 below should
   be re-read in that light.
3. **Where it DOES bite, the stop target also changes.** adaptec4 stopped on
   `clamp/no-filler = 0.046` while `clamp/+filler = 0.244` — a signal reading 5x low. But in phase 2
   `include_macros = False`, so `stop_overflow` returns to 0.07 (no doubling) and the plateau kill
   re-enables. Flipping fillers on under phase-1 rules changes both the signal AND which rules apply.
   Evaluating it before phase 2 exists would measure a configuration we will never ship.

- [x] DONE 2026-08-02: `convergence_include_fillers` is now unconditionally true in phase 2
      (`Placer::convergenceIncludesFillers`, `Output.cpp`) — the two reasons blocking it as a
      phase-1 fix (filler population belongs to phase 2; phase-1's doubled-threshold/plateau-kill
      rules) don't apply once macros are frozen and `mixed_size_mode` reverts to false. Phase 1's
      own default is unchanged (config-controlled, still false) pending the per-design
      `clamp/no-filler` vs `clamp/+filler` split — not yet measured.

### Prerequisites (agreed 2026-07-31: land these BEFORE stage 3)
Three faithfulness/structure gaps that phase 2 would otherwise inherit. Ordered by blast radius.

- [x] **P1 — filler sizing in the standard-cell frame. DONE 2026-07-31 (code), sweep pending.**
      Six divergences from `compute_filler_without_fence` fixed in one landing; adaptec5 went from
      0 to 310,073 fillers, matching XPlace exactly. Details in #4 above and in
      `1_REVIEW/_NEW_HANDOFF_filler_faithfulness_20260731.md`. **MMS re-baseline still owed
      — the quality effect is unmeasured.** Divergence D (overlapping fixed macros double-counted
      in placeable area) deliberately NOT implemented: no test design exercises it.
- [x] **P2 — unify the two macro definitions. DONE (verified in code 2026-08-06; this checkbox was
      stale — the IMPLEMENTED banner at the top of this section already listed it).**
      `analyzeDesignArea` now classifies with `Node::isMovableMacro()`, the single definition set by
      `tagMovableMacros()` from XPlace's `is_mov_macro` rule; the die-area 0.02% heuristic is gone.
      `Setup.cpp:295-301` records the measurement the entry below asked for: the two rules disagreed
      on **7 of 16 MMS designs** (worst newblue1, 64 vs 53), but unifying is behaviour-preserving in
      practice — every functional use is a `> 0` test, and the ePlace grid is identical on all 16
      because the power-of-2 rounding absorbs the difference.
- [x] **P3 — phase-relative iteration counter. DONE (stale checkbox, same as P2).** `phaseIteration()`
      exists and is used by all six equivalents (`Schedule.cpp` warmup/`%3`/μ decay/jolt warmup/
      precond `%20`/guard arming, `Output.cpp` `BEST_SOL_MIN_ITER`); `reachedMaxIterations` correctly
      stays absolute. Verified a no-op at the time on adaptec1 + newblue5.
      <details><summary>original P3 text</summary>
      XPlace's `set_init_param` resets
      `init_iter = iter`, and every schedule term is `iter - init_iter`: skip_update (`%3`, `<50`),
      the μ decay `0.9999^(iter-init_iter)`, precond escalation (`%20`), `need_to_early_stop`'s
      `<100` arming, `check_plateau`. sw_only uses raw `iteration` in the six equivalents
      (`Schedule.cpp` warmup/`%3`, μ decay, jolt warmup, precond `%20`, guard arming;
      `Output.cpp` `BEST_SOL_MIN_ITER`). Add a `phaseIteration()` offset. `reachedMaxIterations`
      stays ABSOLUTE — XPlace's `args.inner_iter` spans both phases. **Provably a no-op while
      there is one phase**, so it is verifiable with `tools/verify_swonly.sh` before phase 2
      exists — do it first.
      </details>

### Scope / how hard
- **Stage 3 is nearly free**: it is the GP loop we already have, with macros treated as fixed and
  a re-initialised solver. The pieces (fixed-node density deposit, filler sizing, LR estimate,
  best-solution tracking) all exist; this is control flow + a re-init path.
- **Stage 2 is the real work**: an LP-based macro legalizer. No LP solver is currently linked into
  sw_only. Options: (a) port XPlace's formulation and link an LP solver, (b) a simpler
  displacement-minimising legalizer (macros are few — 25–959 per MMS design), (c) short term,
  call XPlace's legalizer externally to unblock measurement, reusing the 07-17 flow
  (`tools/def_to_bookshelf_pl.py` + `--global_placement False --given_solution`).
- **Hardware relevance (pl_algo):** stage 3 needs NO new PL/AIE blocks — it is the same iteration
  with a different fixed/movable partition. Stage 2 is an LP solve, i.e. host/CPU work, not PL.
  So this grows the host control flow, not the accelerator scope.

- [x] **Stage-2 approach DECIDED 2026-07-31 (Mark): port XPlace's LP formulation and link an LP
      solver.** i.e. option (a) — constraint-graph construction + longest-path refinement + the
      min-total-displacement LP, per `Xplace/src/core/macro_legalization.py`. Rejected: the
      simpler bespoke legalizer (b) and the permanent external XPlace call (c). Note XPlace's own
      file warns the LP gets slow above ~500 macros (newblue7 has 959) and that pulp/CBC is the
      bottleneck, so a C++ port is expected to help rather than merely match.
- [x] DONE 2026-08-01 (`placer/Phase2.cpp`, see the IMPLEMENTED banner at the top of this section).
- [x] DONE 2026-08-02 for 16 of 16 MMS designs: `1_REVIEW/NEW_REPORT_phase2_mms_suite_20260802.md`
      (15 designs) + `NEW_REPORT_phase2_implemented_20260801.md` (newblue5).
- [x] **CORRECTED 2026-08-04 — the XPlace phase-2 reference DID already exist for all 16.**
      That report's §5 ("no local XPlace phase-2 `GP Stop!` reference exists for these 15 designs
      ... needs 15 more XPlace runs") is **wrong**. The `2026-07-17-23:0x_*` batch in
      `~/phd/Xplace/result/` is `--dataset mms --mixed_size True --seed 42` and every log carries the
      whole flow: Mixed-GP → macro legalization → phase-2 `GP Stop!` → LG → DP. Now recorded as
      `_XPLACE_MMS_FINAL` in `tools/benchmarks.py` (post-GP HPWL + exact overflow, post-LG HPWL,
      post-DP HPWL) and surfaced in `BENCHMARKS.md`. **Zero new GPU runs were needed.**
- [x] **Also corrected 2026-08-04: `GP Stop!`'s overflow is the MASKED (smoothed) number**, not the
      exact one. XPlace's exact figure is the next line, `After GP, best solution eval, exact HPWL:
      ... exact Overflow: ...`, and reads 0.106-0.184 where `GP Stop!` prints 0.042-0.094. Any
      "XPlace ends at ~0.045, we end at 0.07-0.18" comparison in the notes above made that
      substitution and so overstates the gap by 2-3x.

---

## #16 — Move visualization OUT of the placer into an offline tool (opened 2026-08-05)

**Full plan: `1_REVIEW/handoffs/_NEW_HANDOFF_viz_offline_tool_20260805.md`. Build in a new session.**

Mark, 2026-08-05: *"we should rethink how visualizations are made... I might want to generate
multiple gifs of different zoom levels, or perhaps locked to a particular node. So let's move all
of the visualization creation to a separate tool, which can be run repeatedly without rerunning
the full placement."*

Shape of it:
- The host stops rendering. `output.visualize` becomes **`output.export_iterations`** (an
  interval, 0 = off): every N iterations it dumps node positions. The `zoom*` keys move to the new
  tool; `focus_nets` / `rand_*` are deleted outright.
- A new tool reads that dump and produces images/GIFs — any window, any zoom, any node-locked
  view, re-runnable in seconds against a placement that took an hour.
- `Visualizer.cpp` / `Visualizer.h` and the cairo dependency leave the host build.

**Defaults decided 2026-08-05 (Mark)** — handoff §7 is now RESOLVED, do not re-litigate:
1. Frame cadence **20** in `make_viz_gifs.py --every`; `run_config.toml` ships
   `export_iterations = 0` so DSE runs never dump.
2. **Fillers stay exported**, paid for by **uint16 quantization** of positions instead of by an
   `export_fillers` opt-out — making fillers opt-in would reintroduce the re-run cost this TODO
   exists to remove. Encoding is over a 2×-inflated die box with a per-frame clamp count, so a
   cell that escapes the die is still visible rather than silently pinned to the edge (handoff §4.5).
3. Dump lives **inside the run dir** (`<run_dir>/viz/`). Bulk prune glob `results/*/*/viz` →
   hand to **#1**.
4. **`--focus-net` dropped.** The tool then has no netlist dependency at all and never parses a
   benchmark. `rand_focus_IO` / `rand_focus_nodes` were already dead keys ("not yet implemented").

**Sizing context measured 2026-08-05:** `vck5000/results` is **87 G**, disk at **96 % / 40 G free**.
Decisions 1+2 together put adaptec1 at ~96 MB and bigblue4 at ~480 MB per run.

### Step 1 — DONE 2026-08-05 (host export path; cairo renderer untouched)

New `host/src/sw_only/src/placer/PositionDump.cpp` writes `<run_dir>/viz/{manifest.json,
nodes_gen<N>.bin, frames_gen<N>.bin}`. Hooked into `printIterationResults` (cadence),
`printFinalResults` (a `best_solution`-tagged frame taken from the same restored placement the DEF
is written from), and both phase-2 boundaries. Deliberately **outside** `CREATE_VISUALIZATION` —
this path is what survives when cairo is deleted in step 5.

Config as built (Mark's call, differs from the handoff's §3 sketch — a bool gate, not interval-0):
```toml
dump_positions      = false   # master gate; nothing large is written unless toggled on
iterations_per_dump = 20      # cadence, SEPARATE from the renderer's iterations_per_export
```
`dse.py` pins `dump_positions = False` alongside its existing `visualize = False`.

**Gate PASSED on both a single-phase and a two-phase design** —
`2_ARTIFACTS/check_position_dump.py <run_dir>` compares the last frame against the output DEF:

| design | frames | gens | nodes | max marginal err | LSB |
|---|---|---|---|---|---|
| `ispd2015/mgc_pci_bridge32_a` (no movable macros) | 33 | 1 | 29 521 | 6.61 | 12.21 |
| `mms/adaptec1` (**phase 2**, 62 movable macros) | 73 | 3 | 211 447 | 0.213 | 0.326 |

Both at half an LSB with no systematic offset. The MMS run also proves the generation split:
gen 0 `mixed_size` → gen 1 (single `legalized` frame, 371033 → 370971 nodes as 62 macros freeze)
→ gen 2 `stdcell_fixed_macro`. `clamped` = 0 throughout.

**One real format gap this shook out** (the other three findings were checker bugs — see the
handoff): **`die_shift` was missing from the manifest and is NOT zero on MMS.** Bookshelf inputs
are translated so the die lower-left sits at the origin, and the DEF adds the shift back — 459
units on both axes for `mms/adaptec1`. `die.x0/y0` reads 0 on that path, so it is `die_shift`,
not `die.x0/y0`, that carries the offset. Added `DataBase::getDieShift()` (one getter in
`common/include/DataBase.h`) and `manifest.die_shift`. Also worth knowing: the quantization LSB
is **per-axis** — dies are not square, and a square test die hides the bug completely.

### Step 2 — DONE 2026-08-05 (`tools/generate_viz.py`, full-die view)

Mark named it `generate_viz.py`; the handoff's `place_viz.py` does not exist. Python + numpy +
Pillow, no new dependencies.

```bash
python3 tools/generate_viz.py <run_dir> [--iters A:B:S] [--out DIR] [--canvas N] [--gif]
```

Geometry, layer order and colours are ported from `Visualizer.cpp`; output is named `iter_<N>.png`
to match it. Rasterization is vectorized (cells ≤8 px painted by broadcasting, macros by slicing):
**~0.4 s per 371k-node MMS frame** vs ~4 s/frame for cairo inside the placement loop.

**Gate PASSED** — `2_ARTIFACTS/compare_viz_frames.py <run_dir>` compares per-column/row ink
profiles against the cairo frames of the same run:

| run | frames | worst corr | worst emd |
|---|---|---|---|
| `mgc_pci_bridge32_a` (focus nets ON in cairo) | 32 | 0.9945 | 0.00125 |
| `mms/adaptec1` (**phase 2**, focus off) | 70 | 0.9973 | 0.00052 |

Thresholds 0.99 / 0.002. `check_viz_orientation.py` pointed at the *port's* output independently
confirms y-up: 5 px worst corner error vs DEF/LEF ground truth (tolerance 20 px).

**The trap: cairo centres a stroke on its path.** Filling the die-boundary rectangle inward
instead offset that saturated 8 px line by half its width and — because the comparison crop began
exactly on it — dragged frame correlation to **0.93**. Fixed both ends: strokes now straddle the
path, and the comparison insets 1% past the die edge so boundary decoration cannot dominate a
metric about where the cells are.

Deliberate differences from cairo: no focus-net overlay (decision 4), phase banner gated on
generation count rather than `enable_phase2`, and boundary frames named `iter_N_legalized` vs
cairo's `iter_N_a_legalized` (so the comparison skips rather than pairs them).

### Step 3 — DONE 2026-08-05 (zoom view + detail layers)

```bash
python3 tools/generate_viz.py <run_dir> --view zoom --center 0.5,0.5 --span 0.05
```

`--center`/`--span` mirror `output.zoom_center_*` / `zoom_span`. Adds clipping (zoom only), row
pitch, bin grid, per-cell outlines, the zoom overlay line, and the `MAX_DETAIL_LINES = 256` drop
rule — verified at its boundary on MMS (span 0.3 = 267 rows/154 bins drops rows, keeps bins;
span 0.6 drops both).

**Gate PASSED** on every frame available: mgc zoom 32/32 (worst corr 0.9956), mgc full 32/32
(0.9995), MMS zoom 1/1, MMS full 1/1.

**⚠ The cairo zoom renderer is impractical at MMS scale.** On `mms/adaptec1`, **one cairo zoom
frame took 23 min 58 s** (file mtimes, cleanly isolated — nothing between the two writes but that
render); the port does the same window in **3.6 s/frame**. Two causes, and only one is cairo's:
`Visualizer.cpp` never pre-filters by window (it hands cairo all 371k rectangles and lets the clip
sort it out, which does not avoid tessellation — the port drops 99.75% of the design first), and
`outlineIfZoomed()` *strokes* that whole path, which is far dearer than filling. So the honest
framing is that the **C++ zoom implementation is unoptimized**, not that cairo is unusable — a
window pre-filter would close most of the gap. Doesn't change the conclusion: moving rendering
offline removes the cost from the placement loop rather than tuning it.
**Do NOT quote a cairo full-die per-frame number** — an earlier draft said 9 s, but that gap also
contained nine placement iterations, so the full-die render was never isolated. Known instead: run
135926 did 1398 iterations + 141 full-die renders in 454 s, so the full-die path is not the problem.
A 140-frame MMS zoom GIF would be ~56 hours in cairo — which is why TODO #14's zoom was only
ever shown on the 29.5k-cell mgc design. It is **slow, not broken**: a 1-iteration MMS run with
zoom under `/usr/bin/time -v` gives exit status 0, 0 signals, 482 MB peak RSS — pure CPU time in
path tessellation, no memory problem. **Consequence: the MMS zoom has exactly one cairo reference
frame** (salvaged from a run abandoned mid-render); the 32-frame mgc zoom gate is what proves the
window arithmetic. Do not plan on more — they cost a day each. This is also the strongest argument
yet for the whole TODO.

**Antialiasing added** (`--supersample`, default 2 full / 4 zoom): rasterize at N× and
box-downsample, which *is* the coverage fraction cairo antialiases with. Lifted the step-2
full-die numbers 0.9945 → 0.9995. Zoom needs the higher factor because it is the only view with
per-cell outlines: at 2× two neighbouring cell edges merge onto one raster pixel and the frame
loses 4–6% of its ink.

**Three cairo stroke conventions found by the gate, all fixed:** strokes are *centred* on the path;
grid-line widths must stay *fractional* (rounding width onto a rounded centre quantizes upward —
a 1.638 px row line became 1.75 px, 7% too much ink, identified because the residual
autocorrelated at lag 164 px = exactly the row pitch); rectangle outlines had the same bug.

**Gate metric changed:** `emd` → `lag` + `ink`. emd normalizes each profile, so the zoom's
uniformly spaced grid-line ink reads as a translation; on mgc iter_220 it showed 0.0033 with corr
0.999 and zero real displacement, and converged to 0.0019 purely by raising supersampling 4×→8×.
`lag` (cross-correlation offset in px) measures translation directly. Also added a `flat`-frame
guard: a centred zoom window on iteration 1 is solid cells, and correlating two constants reads
0.24 while the frames are in fact identical.

Minor: the 3-line header (benchmark+phase+zoom) overlaps the die box top edge at y=0.11 —
faithful to cairo, worth fixing whenever step 4 (below) is picked up.

### Step 5 — DONE 2026-08-05 (delete the cairo renderer)

Removed `Visualizer.{h,cpp}`, `BUILD_VIZ`, `CREATE_VISUALIZATION`, `-lcairo` from
`makeflags.mk`/`host/Makefile`; the `visualize`, `iterations_per_export`, `zoom*`, `focus_nets`,
`rand_focus_*`, `rand_macro_nets` keys from `run_config.toml`; every call site
(`initializeVisualization`, `initializeZoomView`, `initializeFocus` + the four `addRandom*`
helpers, `exportIterationVisualization`, `exportPhaseBoundaryVisualization`, `drawPlacementViews`,
`exportVisualizationArtifacts`) from `AIEplace.h`/`Setup.cpp`/`Output.cpp`/`Phase2.cpp`/`main.cpp`;
and the now-dead `mv_focus_nets`/`mv_focus_nodes` + `addFocusNet`/`addFocusNode`/`getFocusNets`/
`getFocusNodes` plumbing from `common/include/DataBase.h` (Visualizer.cpp was their only
consumer — nothing in pl_algo ever called them). `tools/make_viz_gifs.py` repointed at
`dump_positions`/`iterations_per_dump` + `generate_viz.py --gif` instead of relying on the exe's
own auto-GIF; `tools/dse.py`'s dead `visualize = False` pin dropped. New `docs/visualization.md`
(repo root, alongside `vck5000/`) documents the resulting workflow end to end.

**Gap found and closed — not in the original plan.** `Visualizer.h` also held `CairoPlotter`, a
second and functionally unrelated class rendering the per-run convergence-history charts
(`graphs/*.png` — TODO #18), which neither this handoff's reuse-map (§2) nor its deletion list
(§3) ever mentions. Deleting `Visualizer.h` wholesale as originally specified would have silently
dropped that output with nothing replacing it, hours after TODO #18 shipped it at Mark's explicit
direction ("I like your decisions"). Ported to `tools/plot_histories.py` (matplotlib, reads
`iterations.dat`, matches TODO #18's design: same 5 PNGs, per-metric identity colors, log-scale
density weight, shared-x-axis stacked overview) before deleting the C++ version — see TODO #18's
note.

Gate: `make host HOST=sw_only` builds clean with no cairo on the link line; `cd vck5000 && make
test` passes; a real run's `iterations.dat` fed through `plot_histories.py` reproduces all 5 PNGs
(spot-checked visually against the `overview.png` layout above).

**Next action: handoff step 4 (node-lock, multi-view) is still open** — nothing in this session
touched it. `check_viz_orientation.py` still wants promoting to `tools/` and generalizing off its
hardcoded ISPD2015 paths (handoff §5.4).

### Legibility + naming pass — DONE 2026-08-06 (Mark's review of the first adaptec1 zoom GIF)

Five things, all cosmetic-or-naming except the third. Gate: `make test` and `make test-regress`
both pass; a zoom re-render of the adaptec1 run was inspected frame by frame.

- [x] **Zoom caption reads a normalized CENTRE, not the window's lower-left in die units.**
      Was `@ (2.676e+03, 0.000e+00)`, now `@ (0.500, 0.250)` — the same number `--center` takes,
      so a frame can be reproduced from its own caption. Absolute die coordinates were
      uninterpretable: the caption never carried the die size needed to normalize them. The
      non-quiet stdout line got the same treatment. New `View.center_frac()`.
- [x] **The footer (iter / HPWL / OVFW / alpha / lambda) is now identical on every frame.**
      It was already drawn in *both* views — the full-die view was never missing it. The real gap
      was narrower: `if not tag:` suppressed alpha and lambda on **tagged** frames, so
      `best_solution`, `legalized` and `reseeded` — the frames most worth reading those two off —
      were the only ones without them, because the tag was printed in alpha's slot.
      The tag now rides the `Benchmark:` line as `[best_solution]`.

      > ⚠️ Tried it as its own header line first; it lands at y=0.11 and `DIE_START = 0.10`, so it
      > renders *inside* the die box, illegible over the cells. Same trap as the 3-line overlap
      > already noted under step 3 — **the header has room for 3 lines, not 4.** That overlap
      > (benchmark + phase + zoom, i.e. a two-phase MMS zoom render) is still unfixed.

- [x] **`<run_dir>/viz/` → `<run_dir>/coord_dump/`.** Says what it holds; `viz` was easy to confuse
      with the *rendered* `viz_render/` next to it. One line of behaviour (`PositionDump.cpp`
      `output_dir / "coord_dump"`), the rest comments/docs: `generate_viz.py::load_run`,
      `2_ARTIFACTS/check_position_dump.py`, `make_viz_gifs.py`, `docs/visualization.md`,
      `default_config.toml`, the `viz-gif` skill. **No compatibility fallback was added** — there
      were only 4 dumps on this box and they were renamed in place, so a reader for the old name
      would have been dead code from the day it was written. `viz_render/` keeps its name (it is
      renders, not coordinates).
- [x] **`host/src/sw_only/run_config.toml` → `default_config.toml`** (`git mv`, so history follows).
      It is the *default*, not the config any given run used — that one is `config_used.toml` in the
      run dir, and the old name blurred the two. Updated: `common.mk`, `main.cpp`'s fallback path,
      `Makefile` help text, `dse.py`, `make_viz_gifs.py`, `verify_swonly.sh`, `bench_swonly.sh`,
      `profile_swonly.sh`, both `2_ARTIFACTS/gen_*_configs.py`, `run_thread_throughput_ab.sh`,
      `host/src/sw_only/README.md`, `docs/visualization.md`, the `viz-gif` skill.
      **Deliberately NOT rewritten:** historical entries in this file and in `1_REVIEW/` (they
      record what was true then), and `test/regress/configs/*.toml` (frozen snapshots — the name
      appears only in a comment there).
- [x] **`0_placement.gif` + `1_best_solution_iter<N>.png` sort to the top of `viz_render/<view>/`**,
      above the `iter_<N>.png` trajectory (digits precede letters). Only `best_solution` is
      hoisted; `legalized`/`reseeded` keep `iter_<N>_<tag>.png` so the phase boundary still reads
      in place in the listing.

      > ⚠️ **This broke the GIF's frame order and the fix is the interesting part.** `gif_builder.py`
      > natural-sorts the directory, which recovered trajectory order *only* while every frame was
      > named `iter_<N>` — so `1_best_solution_*` jumped from last frame to first. `gif_builder.py`
      > now takes `--frames` (an explicit ordered list, `input_dir` still works standalone) and
      > `generate_viz.py` passes the order it already knows. The naming→ordering coupling is gone
      > rather than re-tuned; renaming a frame can no longer silently reorder an animation.

---

## #18 — Overhaul the per-run `graphs/` output (opened 2026-08-05, DONE 2026-08-05)

Mark, on a live Chart.js mockup built blind (without reading this code): *"I like your decisions.
Colors look clear and vibrant. Stacking them and sharing the X-axis is efficient and clear. The
current ones just seemed too plain. No axis data. Boring colors and font."* Also wants a
**density_weight graph added** — currently absent entirely.

**Shipped, per Mark's decisions** (4 individual PNGs + 1 stacked `overview.png`; combined_history
dropped; log scale reworked): `CairoPlotter` in `Visualizer.h` was rewritten around a `Series`
struct (data pointer, title, y-label, color, `log_scale` flag) with two entry points —
`plotHistory(Series)` for one full-canvas chart, and static `plotStacked(vector<Series>, title,
filename)` for N vertically-stacked panels sharing one x-axis, builds/saves its own surface.
`density_weight_history` reads back the existing `density_weight` column of `iterations.dat`
(`readDensityWeightHistory`, `Output.cpp`) rather than adding a 4th `push_back` to the hot loop —
`appendIterationLog` already writes it every iteration in lockstep with the other three history
vectors, so the parsed vector is guaranteed the same length/order for the shared x-axis. Colors:
dataviz-skill categorical palette, first three fixed-order slots (blue/orange/aqua) plus violet
(slot 7) for density weight in place of slot 4 (yellow), whose contrast against a white PNG
background was measured too low (~2.1:1) for a legible line. Font swapped `"Arial"` →
`"DejaVu Sans"` — confirmed a one-line family-name swap (no FreeType/file loading needed): cairo
on this box links the fontconfig backend (`cairo-fc`), which resolves the family name the same
way `tools/generate_viz.py`'s `load_font()` fallback chain does. Log scale: mapping and 10%
padding both done in log10 space (padding in linear space would pad the wrong end), ticks snap to
whole decades (`1e-9`, `1e-7`, …) rather than fractional-decade values. Y-axis label overlap fixed
by measuring actual tick-label width (`cairo_text_extents`) and sizing the left margin from that,
instead of a fixed 15px offset. X-axis now labels 1-based iteration number (array index + 1)
instead of the raw index. Verified by building (`make host`, clean, no warnings) and running a
real 400-iteration adaptec1 placement end-to-end — all 5 PNGs inspected visually, log scale/ticks/
colors/shared-x-axis alignment all correct. Scratch verification run discarded after inspection.

**Not matplotlib** (the initial guess was wrong) — `graphs/` is hand-drawn C++ via Cairo's C API:
class `CairoPlotter`, header-only, `host/src/sw_only/include/Visualizer.h:134-427`. Driven by
`Placer::plotHistories()` (`Output.cpp:228-254`), called once unconditionally at end-of-run from
`main.cpp:16`, compiled in whenever `BUILD_VIZ=1` (default) — independent of the per-iteration
`output.visualize`/`dump_positions` toggles. **Distinct from #16's `viz/` pipeline** — that one
renders cell layouts (positions → PNG/GIF), this one renders scalar-metric line charts
(HPWL/overflow/step_length → PNG). Don't conflate the two when picking this up.

### Confirmed problems (verified against the code and the actual PNGs, not just taste) — ALL FIXED
- [x] **No density_weight graph, and no history to plot it from.** Fixed by reading it back from
      `iterations.dat` at end-of-run (`readDensityWeightHistory`) instead of adding a 4th
      in-memory history vector — Mark's call, see the DONE note above.
- [x] **`combined_history.png` has no tick labels on either axis.** Moot: `plotDualHistory` /
      `combined_history.png` were deleted outright (Mark's decision — see below), not patched.
- [x] **Y-axis label overlaps the bottom tick value.** Fixed properly, not nudged: the left
      margin is now sized from the actually-measured tick-label width
      (`cairo_text_extents`) instead of a fixed 15px offset.
- [x] **Colors are two hardcoded literals reused everywhere.** Fixed: each metric has a fixed
      identity color from the dataviz-skill categorical palette (see DONE note).
- [x] **Font is Cairo's "toy" API guessing `"Arial"`.** Fixed: swapped to `"DejaVu Sans"`,
      confirmed a one-line family-name swap since cairo here links the fontconfig backend — no
      FreeType/file-path loading needed, resolving the open question below.
- [x] **No log-scale support anywhere in `CairoPlotter`.** Fixed: log10-space mapping + padding +
      whole-decade ticks, used by the density_weight panel.
- [x] **Four separate 800×600 files, no shared x-axis.** Fixed: kept the 4 individual PNGs
      (Mark's call) AND added `overview.png`, 4 panels stacked on one shared x-axis.
      `combined_history.png`'s normalize-and-overlay approach is gone, not patched.

### Direction discussed with Claude — not yet decided, needs Mark's sign-off before implementing
A live mockup (Chart.js, deliberately built without reading the code above, fed real data pulled
from a run's `iterations.dat`) proposed **stacked single-axis panels sharing one x-axis** (HPWL /
density_weight-log / overflow) in place of the normalize-and-overlay approach, on the grounds that
overlaying differently-scaled series (dual-axis or normalized) can visually manufacture a
correlation that isn't really there — a bad property for a chart used to debug the algorithm.
Mark's reaction was positive on the direction (panel layout, real per-metric colors, actual axis
data). **This means matching the mockup's information design in Cairo, not switching plotting
stacks** — Cairo can do everything the mockup did (log scale is a coordinate-mapping change,
stacked panels are one taller surface with sub-plot rects, better colors are just better literals).
Structurally this is a **new `CairoPlotter` method** (`plotStackedHistories` or similar) — the
existing class draws exactly one full-surface chart per instance; there's no code path today that
splits one surface into multiple axis regions.

### Open decisions — ALL RESOLVED 2026-08-05 (Mark)
- [x] **Keep 4 separate PNGs, or consolidate?** Both: kept all 4 individual PNGs (now including
      `density_weight_history.png`) AND added `overview.png` stacking all 4 (HPWL, Overflow, Step
      Length, Density Weight) on one shared x-axis. `step_length_history` joins as the 4th panel.
- [x] **Does `combined_history.png` survive?** No — deleted outright (Mark: "Remove combined
      history (tried to place 2 y-axis on same graph -- confusing)"). `plotDualHistory` removed
      from `CairoPlotter` entirely, not left dead.
- [x] **Font file + fallback list** — resolved without needing PIL's file-path chain: cairo links
      the fontconfig backend here, so `"DejaVu Sans"` as a *family name* (toy API, no
      `cairo_ft_font_face`/FreeType) resolves the same font `generate_viz.py` uses.
- [x] **Color assignment** — fixed per-metric identity color, dataviz-skill categorical palette
      (Mark: "best judgment... distinct and easy to see" — see DONE note for the exact hexes and
      why density_weight departs from the fixed slot order).
- [x] **Log-scale mapping** — padding and the min/max search both now happen in log10 space.
- [x] **X-axis ticks are array index, not iteration number** — now labeled index+1 (1-based
      iteration count since run start). Deliberately NOT `iterations.dat`'s own `Iter` field,
      which is phase-relative and would go non-monotonic across a phase-2 restart; the history
      vectors are appended once per call across the whole run regardless of phase, so index+1
      stays monotonic and correct as "iterations since run start."

Verified: `make host` builds clean, and a real 400-iteration adaptec1 run produced all 5 PNGs
with correct log-scale ticks, per-panel colors, aligned shared x-axis, and no label overlap.

**2026-08-05, later the same day — moved to Python, `CairoPlotter` deleted.** TODO #16 step 5
removed visualization from the host build entirely, including `Visualizer.h` — which is where
`CairoPlotter` lived. That was a gap in step 5's own plan (it never accounts for this class; see
the note there), caught while auditing what would be lost. Ported to `tools/plot_histories.py`
(matplotlib) before deletion, preserving every decision recorded above verbatim: same 5 filenames,
same per-metric identity colors, log-scale density weight, shared-x-axis `overview.png`, 1-based
iteration x-axis. Reads `iterations.dat` exactly as `readDensityWeightHistory` did. Not run
automatically at end-of-run anymore (nothing is, post-#16) — invoke it by hand:
`python3 tools/plot_histories.py <run_dir>`.
