# journal.md — the dated narrative

Append-only, most-recent-first. This is the **story** of the project: superseded status
snapshots (the retraction trail `summary.md` sheds under its soft cap) and dated notes on how
the numbers and state evolved. Read it chronologically.

Boundary with its neighbours — don't let this become a second history.md:
- **[[summary.md]]** holds only CURRENT state, soft-capped. When a snapshot there is superseded,
  it moves **here** rather than growing a `<details>` inside summary forever.
- **[[history.md]]** is the **task-indexed archive** — full completed-task sections, looked up by
  `#n`. journal.md is **chronological and cross-cutting**; a closed task's formal record still
  goes to history.md, but the day's status-level narration (headline numbers, "we thought X, now
  Y") belongs here.

Nothing here is injected into sessions — it's the archive you consult, not the always-loaded
status. **Never rewrite an entry; annotate.** The retraction trail is the point.

---

## 2026-10-02 — evicted from summary.md: #40's DONE bullet (closed; record in history.md), to make room for #42

> - **#40 — `hpwl_computer` DONE (2026-09-22): built, tier-1-verified, C-synthesizes at II=1.**
>   `bring_up/hpwl_computer/`: beat-granularity degree resolution → `dhar_tree<Op>` (Dhar Fig. 6/7,
>   now a template over the combining operator) → 16-lane selector → one packed `OutBeat`/beat. II=1
>   took 4 synthesis iterations (fixed-width write loop, then `ARRAY_PARTITION` on two arrays, then
>   the wide-output-beat fix) — full table in the report. → [[REPORT_40_hpwl_computer_20260921.md]]

---

## 2026-09-22 — evicted from summary.md: #40's mid-build `hpwl_computer` snapshot, superseded by DONE

> - **#40 — v2 rewrite's bbox/HPWL milestone hit II=1 compute / II=8 DDR-port-bound load** (2026-09-21);
>   spawned **`hpwl_computer`** (`bring_up/hpwl_computer/`, not yet built): `LANES=16`, nets packed
>   `floor(16/degree)`-per-beat (Dhar Method 1), one beat/cycle feeds compute directly (no separate
>   beat-parsing controller). Compute reuses Dhar's Fig. 6/7 tree+selector with comparators instead of
>   adders. Design settled, no code yet. → [[_NEW_HANDOFF_40_hpwl_computer_20260921.md]]
> - **#41 — `hpwl_gradient_computer`**, opened 2026-09-21, blocked on #40: extends `hpwl_computer` with
>   Dhar's term-gen/LUT/adder-trees/combiner to get the actual per-pin gradient, not just HPWL.

Superseded same-day: the module was built, tier-1-verified and C-synthesized to II=1 within the same
session this snapshot was written in — see summary.md's current #40 line and
[[REPORT_40_hpwl_computer_20260921.md]] (the handoff this pointed at, now converted to that report).

---

## 2026-09-21 — evicted from summary.md: #40's pre-v2-rewrite snapshot, superseded by the `hpwl_computer` spawn

> - **#40 — `hpwl_gradient_dhar` fails P&R timing, AND its 16-pin net cap costs +12.4% post-DP HPWL**
>   (28/28 worse, ISPD2005 +30.6%; 2026-09-18). Plan: exact ⌈d/16⌉-block chunking, folded into the
>   lane-narrowing timing fix. → [[_NEW_REPORT_40_net_degree_cap16_20260918.md]]

Superseded by the 2026-09-18 v2 rewrite ([[_NEW_HANDOFF_40_dhar_v2_rewrite_20260918.md]]), which
replaced the timing-closure/16-pin-cap problem this snapshot describes with a from-scratch module;
that milestone in turn spawned `hpwl_computer` (2026-09-21, see summary.md's current #40 line).

---

## 2026-08-28 — evicted from summary.md: #14, #32, #39 closed-task narration, stale under the soft cap

All three closed **before** 2026-08-27 and have full task-indexed records in [[history.md]] (search
`#14`, `#32`, `#39` there); this was the still-inline detailed narration summary.md carried past its
usefulness. Moved verbatim, one warning kept behind (see below).

> - **#14 — zoomable visualizer: CLOSED 2026-08-17**, archived to [[history.md]]. Node-lock
>   (`generate_viz.py --lock <name>|index:N|most-moved`) re-centres the window on one tracked cell
>   every frame — verified at **0.0000 px** from the reticle across all 31 newblue1 frames and all
>   three generations. `--add-view` renders N windows in one pass (byte-identical to N separate
>   invocations). `MIN_SIZE` cleared: at zoom it floors **0%** of std cells, fillers and macros; the
>   only nodes floored are 337 **zero-area** bookshelf terminals, where that is correct.
>   ⚠️ **The dump format grew a file**: `names_gen<N>.txt` (sparse `<index> <name>`, no fillers),
>   written per generation because the phase-2 boundary reshuffles indices. Dumps made before
>   2026-08-17 have no names and `--lock` will refuse them — re-run the placement.
>
> *Rewritten 2026-08-17. This section was headed "Newly open" and 4 of its 6 entries (#25, #28, #29,
> #31) were **closed and already archived to [[history.md]]** — the most-read file in the repo was
> advertising finished work as open. Their full text is in history.md; only live items are below.*
>
> - **#32 — CLOSED 2026-08-17**, archived to [[history.md]]. All three items done. **The u-vs-v
>   question is settled: we track on `v`.** `snapshotBestPlacement()` stores `probe_pos` and HPWL is
>   measured there too (new `at_probe` arg on `computeTotalWirelength`/`computeWirelength_HPWL`), so
>   HPWL, overflow and the stored solution describe **one** position — XPlace's single `p`/`v_k`.
>   `BEST_SOL_MIN_ITER` is phase-relative. `syncProbeToCommitted()` deleted, folded into
>   `restoreBestPlacement()` (restores both halves) after its blocking comment's claimed perturbation
>   measured **bit-exact identical** — retracted in the code.
>   **A/B settled: KEEP 1.005** (`DSE_20260818_113716`, 28 designs × 2 arms, 56/56, 159.6 min).
>   1.005 → 1.0097 / 1.0126; 1.010 → 1.0097 / 1.0128 (median / mean DP).
>   ⚠️ **The real finding is that the knob barely binds: 1 design of 28 selects differently**
>   (ISPD2005 byte-identical across arms). So the effective n is **1, not 28**, and widening the
>   design set cannot help — the set was already everything. Where it binds (`mgc_des_perf_a`) 1.005
>   wins by 0.71 pp post-DP, and **DP amplified the penalty rather than absorbing it**, reversing the
>   #24 report §5 story that "more spread legalizes better". Unexplained: the three designs that
>   flipped in the 2026-08-10 A/B no longer do — plausibly #31's grid cap moving the overflow gate,
>   but that is a hypothesis, untested.
>   ⚠️ **pl_algo inherits the u-vs-v decision** — flagged in tasks.md #20 step 6, with the specific
>   trap: `sched_verify` checks the schedule, not the geometry, so it cannot catch a wrong choice.

The u-vs-v warning is kept live in summary.md's pl_algo section (not just here) since it bears on
the still-open #20 step 6.

> - **#39 — position dump format v2: CLOSED 2026-08-27**, archived to [[history.md]]. The dump now
>   carries what a GIF needs to show *mechanism*: `net_degree`, the Nesterov probe `v_k`, the
>   solver's own bin density (+ opt-in field, box-averaged to ≤256×256), and the per-node
>   wirelength/density gradient split + preconditioner — that last one captured inside
>   `combineGradients()`, the only place the two terms ever exist separately. `generate_viz.py`
>   reads them via `--underlay density` / `--color-by force|precond` / `--positions probe`, and
>   `tools/check_viz_dump.py` (new) asserts the dump against four structural invariants.
>   **Bit-identical with every channel ON**, verified on both a single-phase and a 3-generation
>   mixed-size run against their committed baselines — stronger than `make test-regress`, whose
>   frozen configs dump nothing. ⚠️ **Disk, not CPU, is the constraint**: forces are 20 B/node/frame,
>   so the 14-design suite at cadence 5 is **32 GB all-channels vs 9.6 GB with `dump_forces = false`**
>   against ~40 GB free. ⚠️ **Half of the original request was already built** — the "positions only"
>   comment in `default_config.toml` was three weeks stale (now fixed); check `PositionDump.cpp`
>   before scoping dump work. → [[_NEW_REPORT_39_dump_v2_channels_20260827.md]]

## 2026-08-27 — evicted from summary.md: the `#3` cap→scale entry, WRONG since 2026-08-25

Found while landing #37. summary.md still carried `#3` as closed-in-favour-of-the-**scale**, which
`#35` reverted eight days earlier — and summary.md is injected into every session, so this was the
one always-loaded file actively contradicting `CLAUDE.md`'s divergence registry, `tasks.md`, and
the code. A session trusting it would have "restored XPlace faithfulness" by reverting a closed,
measured, Mark-authorized decision. Exactly the failure mode the one-in-one-out rule exists to
prevent. Replaced in summary.md with a pointer to the registry; original verbatim:

> - **#3 — fixed-density cap-vs-scale: CLOSED 2026-08-17.** Now a scale (`min(ρ,1)·td`), matching
>   `initializer.py:82`; was a cap (`min(ρ,td)`). Bundled into the same suite re-run as #32's 7a/7b
>   (Mark's call). Provably a no-op at td=1, so all 8 ISPD2005 designs are untouched by it —
>   `mms_adaptec1` re-baselined bit-identical. The remaining open item in #3 is the **per-row site
>   model** (ragged cores on 11 of 16 MMS designs), unrelated. See tasks.md #3.

**Current truth:** the fixed-density treatment is a per-bin **CAP**, `min(ρ, td)` — `#35`,
2026-08-25, the project's first registered deliberate divergence from XPlace, worth −2.38 pp of MMS
mean. `#36` (08-26) collapsed the two host copies onto `capFixedDensity`. See the journal entries
of 2026-08-25 and the divergence registry in `CLAUDE.md`.

## 2026-08-27 — #37: the "macro-excluded" overflow was never macro-excluded

Superseded number, recorded because it was quoted. `computeOverflow(exclude_macros=true)` was
called at the END of the run, after `freezeMovableMacros()` had made every movable macro FIXED — so
the flag matched nothing and the "macro-excluded" row was the plain exact overflow wearing a label.
`tools/benchmarks.py` pointed the XPlace Mixed-GP comparison straight at it.

On mms/adaptec1 the correction is **0.118 → 0.0702** against XPlace's 0.1306. The old number read
as "spread slightly better than XPlace at the handoff"; the true one says substantially better.
Any tier-3 flag in history.md derived from the macro-excluded column predates this and was computed
on the wrong quantity — **retracted, not merely stale**; see tasks.md #37.

The fix also moved `reportPhaseSummary()` after the phase-1 restore, so Phase 1 HPWL / Overflow
(smoothed) / Overflow (exact) now describe the placement phase 1 *ships* rather than its last
iterated one — XPlace's own Mixed-GP checkpoint. Those three values shift on every MMS run_summary
from this date. Reporting only: `test-regress` and `test-regress-slow` bit-identical, mms_adaptec1
(phase 2) included.

## 2026-08-17 (later) — evicted from summary.md when #32 and #14 closed

Moved verbatim under the soft cap. These closed on 2026-08-12; their task-indexed records are
in [[history.md]]. Kept here for the trail — the `tools/` triage in particular is the reason an
unlisted tool is now a visible defect rather than an unknown.

## Closed 2026-08-12
- **`tools/` triaged; every survivor now carries a status** (`331f1df`, closes a #1 bullet). The dir
  had grown past the point where useful and stale were distinguishable by inspection. 5 stale tools
  deleted — `xplace_gp_ref.py`, `collate_mms.py`, `make_scorecard.py`, `legalize_swonly_mms.sh`,
  `bench_swonly.sh` — each with zero references from any code, Makefile or skill, and each
  superseded by a named replacement. 2.1 MB of `adaptec1_*.png` run output was **moved, not
  destroyed**, to `.claude/2_ARTIFACTS/legacy_density_heatmaps/`. `tools/README.md` now has a
  **live / dormant** row for all 38 survivors, checked mechanically, so an unlisted tool is a
  visible defect. Kept as *dormant* rather than deleted: the OpenROAD opendp island (independent
  legalizer, binary still installed), `eval_overflow_xplace.sh`, `vcd_to_svg.py`.
- **#26 — fence regions** (details above). Decision: ignore them, document it, keep both benchmark
  variants. Guards against re-deriving this: the operational rules in `CLAUDE.md`, a warning in every
  run log, and both ISPD2015 harnesses now failing loudly (with the regeneration command) when
  `ispd2015_fix` is missing or older than the raw data.
- **The scoring pipeline is now TRACKED, in `vck5000/tools/`** — 10 scripts moved out of the
  gitignored `.claude/2_ARTIFACTS/`, where the guards above would have protected exactly one machine.
  `tools/README.md` has the run order and the rule for what belongs there: *anything that produces a
  number we quote is tracked; one-off experiment runners stay with the output.* Code moved, results
  did not — every runner writes to `$ARTIFACTS`, still defaulting to `.claude/2_ARTIFACTS/`.
  The move surfaced three live path bugs, all fixed: `run_suite.sh` and `run_lgdp_suite.sh` defaulted
  to `vck5000/2_ARTIFACTS/` (gone since 08-07), and `run_xplace_ref.sh` wrote ISPD2005 references to
  a **different file** than `run_xplace_ref_2015.sh` wrote the ISPD2015 ones. `analyze_full44.py` now
  exits loudly on a missing artifacts dir instead of printing a table of dashes.

## 2026-08-17 — evicted from summary.md under the soft cap

Moved verbatim when #32 (7a/7b) and #14 landed and summary.md went over ~200 lines. These were
closed on 2026-08-11 and are no longer live context; their task-indexed records are in
[[history.md]]. Kept here because the retraction trail is the point — #27 in particular is the
"check a design's inputs before reading its response to an algorithm change" lesson.

## Closed 2026-08-11
- **#22 — the 8 designs with no XPlace reference.** Resolved by #26: neither of that entry's two
  options was the answer — `ispd2015_fix` is generated by XPlace's own `data/fix_ispd2015_route.py`,
  not downloaded and not hand-built. All 8 now have a reference; the suite is 28 of 28.
- **#27 — `mgc_matrix_mult_a` was a stray space, not an algorithm failure.** Its
  `placement.constraints` was 25 bytes — `maximum_utilization=60% \n`, one trailing space more than
  every other design's. `readPlacementConstraints` tests `back() == '%'` to decide whether to divide
  by 100; the space defeats it, `stof("60% ")` returns **60.0**, and the design placed at
  `target_density = 60`: **29,779,040 fillers** for 149,650 movable cells, a filler area ~20× the
  die. **Fixed by deleting the space** (file now byte-identical to `mgc_matrix_mult_b`'s), and
  `readPlacementConstraints` now hard-errors on any value outside (0, 1].
  **3.2669 → 1.0171**; `divergence_guard` at 271 → **converged** at 715.
  ⚠️ **The benchmark fix is NOT tracked** — `benchmarks/.gitignore` ignores `ispd2015`, so a fresh
  clone or re-download reintroduces the bad file. Check:
  `wc -c vck5000/host/benchmarks/ispd2015/mgc_matrix_mult_a/placement.constraints` must be **24**.
  The new hard-error is what makes that recoverable rather than silently wrong.
  ⚠️ Its apparent +7.71% response to the preconditioner change was **noise** — a broken landscape
  reshuffling. Check a design's inputs before reading its response to an algorithm change.
  → [[_NEW_REPORT_27_matrix_mult_a_stray_space_20260811.md]]
- **Preconditioner always on + escalation unthrottled** (`3c70b38`). Two coupled faithfulness fixes,
  both about `precond_coef` — which feeds the per-node `precond_weight` **and** `precond_kappa`, and
  κ gates the every-3rd-iteration γ/λ throttle for every design.
  **(A)** `precond_coef` escalation hoisted out of `updateDensityWeight()` into `updatePrecondCoef()`,
  called outside the `perform_update` gate. XPlace's `step_precond_coef` is the one member of its
  `step()` trio with **no** `skip_update` guard — deliberately, since it is what ends the throttle.
  Gated, our `%20` grid could only fire where it met `%3`: **every 60 iterations, not 20**.
  **(B)** `auto_enable_preconditioning` removed (above).
  Suite effect: median **1.0113 → 1.0095**, ISPD2005 mean 1.0111 → **1.0052**, ISPD2015 ~0.4% worse,
  `divergence_guard` 10/28 → 8/28. `bigblue3` **−4.30%** (1.0565 → 1.0111, and it now *converges*);
  `mgc_des_perf_1` also recovered and now beats XPlace by 1.9%.
  ⚠️ **Not a uniform win.** Three designs moved the wrong way — `mgc_matrix_mult_a` +7.71% (but see
  #27, it is a parser bug), `mgc_superblue19` +2.26%, `mgc_superblue14` +0.79%.
  ⚠️ (A) is provably a **no-op** when the preconditioner is off — verified by reproducing a frozen
  baseline bit-for-bit with `enable_preconditioning = false` on the new binary.
  → [[_NEW_REPORT_26_precond_always_on_20260811.md]]

## 2026-08-25 (later) — D LANDED; `#35` closed; first registered deliberate divergence from XPlace

Mark's call: land D. "2.38 pp is noticeable, even though MMS isn't our main focus." So the
fixed-density formula is now the cap `min(ρ,td)` on the frozen binary — reverted from the faithful
scale in all four sites (`Grid::clampFixedDensity` canonical, `Density.cpp::computeOverflow`,
`density_bin.hpp`, `density_bin_model.cpp`), each carrying a comment that names it a deliberate
divergence, not a bug. Regress baselines regenerated for the two td<1 designs (reason recorded
in-file); the landing reproduces D's final positions bit-for-bit (`pci_bridge32_b` sha
`43fa7e73a889`). td=1 `mms_adaptec1` re-ran bit-identical at 1274 iters — the no-op-at-td=1 claim,
verified, so nothing on ISPD or the td=1 half moved.

**This is the first entry in a new `CLAUDE.md` section, "Deliberate divergences from XPlace."**
`CLAUDE.md` already carried the rule that divergences must be *deliberate and documented*, but had
no registry of the ones actually taken — so a divergence lived only in scattered code comments and
was one "let me make this faithful again" away from silently regressing. The registry is the
always-loaded backstop; the four code comments are the point-of-divergence claims. Where I'd been
wrong earlier in the day and Mark corrected me (fillers idempotent) is now moot; the one lead left
un-chased is the fixed-node √2-field-inflation divergence, recorded in #35 as the not-taken
keep-faithful path.

Landing commits on top of the doc-only `24c500b`. sw_only was frozen; this is a Mark-authorized
change to the freeze, which is the only thing that updates the frozen binary.

## 2026-08-25 — experiment D: `#3` IS the whole MMS regression, and reverting it beats even pre-`#3`

The missing cell from the #35 handoff (state "D": `#3`'s formula reverted to `min(ρ,td)` in all
four sites, **everything else held at HEAD**) is now run — `DSE_20260824_161319`, 16/16, 245 min.
D isolates `#3` exactly, where the 2026-08-14 baseline confounded it with `#32`'s 7a/7b. Result:

- **D vs C (HEAD) = `#3` alone = −2.38 pp of MMS mean** (1.0347 → 1.0110). The td-split is
  perfectly clean: all 8 td=1 designs flat (±0.04 pp, `#3` is a provable no-op there), all 8 td<1
  recover, magnitude tracking (1−td). adaptec5 −14.57 pp (recovers the +15.02 it regressed under
  `#3`), newblue1 −8.47, newblue4/5 −5.2. Internal consistency exact — adaptec5 lands at 1.0501,
  ~its pre-`#3` 1.0456.
- **D (1.0110) beats even pre-`#3` A (1.0161)** by 0.52 pp, because D keeps 7a/7b (bigblue3 −8.12)
  and the rest of HEAD that the 2026-08-14 A lacked. So reverting `#3` while holding everything
  else at HEAD is the best MMS result on record.

The handoff's decision gate ("if D ≈ 1.0161, `#3` alone is the whole regression") is resolved
past its own threshold: `#3` alone is the whole thing, cleanly.

**Two of the handoff's three leads died in static reads, and I was wrong to weight lead 2.** Mark
called it: recreating the fillers at the phase boundary is idempotent — `freezeMovableMacros` →
`computeAreaBreakdown` moves macro area movable→fixed, so `addFillers`'s inputs are invariant and
the td-raise never fires (confirmed in the D logs). Lead 1 (macro deposit weight) is also dead:
the area-conserving weight is exactly 1.0 for any node ≥ √2 bins, and XPlace zeroes the frozen
macro out of the movable field just as we do. **One live faithful-vs-us divergence remains,
untested:** our density *field* runs FIXED nodes through the √2 inflation; XPlace's
`init_density_map` deposits them at raw size. That is the lever for a keep-`#3`-and-fix path.

**Tree state:** D was a throwaway — source reverted (`git apply -R`), regress bit-identical, so
**frozen HEAD is unchanged**; running D landed nothing on the frozen binary. What DID land
(`289b45d`): the two stale `clampFixedDensity` comments in `Density.cpp` corrected to name
saturate-then-scale, and the row-site-model improvement brief. The `#35` decision — accept D
(−2.38 pp but un-faithful, overriding `CLAUDE.md`'s prefer-XPlace rule, needs Mark's call) vs
keep `#3` and fix the fixed-node field divergence — is now sharply posed and still Mark's, with
sw_only frozen. `scratch/experiment_D.patch` reproduces D on demand.

## 2026-08-21 -- evicted from summary.md: the 2026-08-17 golden and 2026-08-19 MMS-regression narration

Superseded by TODO #34's fix (`02464d0`) and the 2026-08-21 re-runs. Kept verbatim -- both blocks
were the live "where things stand" text in summary.md between 2026-08-17 and 2026-08-21 and the
retraction trail is the point. Current state: `.claude/2_ARTIFACTS/results/
GOLDEN_sw_only_frozen_20260821/` (ISPD) and `.claude/2_ARTIFACTS/results/MMS_sw_only_frozen_20260821/`
(MMS, not golden -- see TODO #35).

> - GOLDEN -- median HPWL ratio 1.0097, mean 1.0126, over ALL 28 ISPD designs (legal-vs-legal,
>   2026-08-17, 28/28 in 80.6 min). 22/28 within +-2%, better than XPlace on 6. Archived 2026-08-19
>   to `GOLDEN_sw_only_frozen_20260817/` (later renamed `SUPERSEDED_sw_only_20260817_pre34/`). Data
>   produced at `821a9c8`; frozen commit `64cfa0e`, believed behaviour-neutral at the time -- this
>   later turned out to still be a two-commit gap on top of a THIRD problem (#34), not the neutral
>   commit itself. 2026-08-15 -> 2026-08-17 table: ISPD2005 1.0053/1.0052 -> 1.0057/1.0057,
>   ISPD2015 1.0113/1.0138 -> 1.0101/1.0153, all-28 1.0095/1.0113 -> 1.0097/1.0126. Attributed to
>   #32's 7a/7b + #3's cap->scale bundled into one run; ISPD2005's +0.05pp isolated as 7a/7b alone
>   since #3 is a provable no-op at td=1.
> - MMS (16 designs) IS UNMEASURED ON THE FROZEN BINARY -- the 2026-08-14 numbers (median 1.0138 /
>   mean 1.0161) predate #32/7a-7b and #3. RUN LANDED 2026-08-19 (`DSE_20260819_152124`) and
>   REGRESSED: median 1.0137 -> 1.0192, mean 1.0161 -> 1.0351. Split by td: td<1 (8 designs) all
>   worse, mean +4.99pp; td=1 (8 designs) mean -1.19pp, i.e. 7a/7b alone is a WIN on MMS. Found a
>   real inconsistency while checking it -- `computeOverflow` still used the pre-#3 cap under a
>   comment claiming it mirrored `clampFixedDensity`. Opened as TODO #34. Also closed #30's
>   never-done tier-3 spot-check (16/16 references matched `_XPLACE_MMS_FINAL`).
>
> **What actually happened next (TODO #34/#35), for continuity:** fixing the metric inconsistency
> (`02464d0`) improved ISPD (mean 1.0126 -> 1.0112) but moved MMS's mean by only 0.04pp (1.0351 ->
> 1.0347) -- FALSIFYING the hypothesis that the stale metric was driving the MMS regression. The
> regression is intrinsic to `#3`'s solver-field formula itself on macro-heavy/mixed-size designs,
> a mechanism still under investigation as #35.

## 2026-08-17 — sw_only FROZEN; evicted "Closed 2026-08-09" from summary.md

Mark's call: sw_only functionality is frozen at parity (median 1.0096 / mean 1.0113 legal-vs-legal,
28 ISPD designs), and pl_algo (#20) becomes the active thread. The reasoning that decided it:
pl_algo's algorithm is pinned to the 2026-07-14 sw_only, so every further sw_only change was
another port — freezing is what makes #20 bounded, not a pause.

Evicted from summary.md the same day under the one-in-one-out rule (verbatim, none of it is a
standing decision a session must not re-derive — all three are recorded in history.md by task):

> ## Closed 2026-08-09 (three low-risk items, both test suites green)
> - **#19** — pl_algo's `dff`/`dff_coef` renamed to `kappa`/`kappa_coef`; `make test` numbers
>   byte-identical, so it is a pure rename. **New:** `host/src/pl_algo/` still gates on the *real* dff —
>   the pre-#19 bug, still live there, now tracked under #19.
> - **#11** — the self-contradicting `macro_td_expand_ratio` entry resolved from the code: the toggle is
>   gone, the faithful branch is unconditional, and the "re-test unblocked by #19" note is moot as
>   written (re-testing means re-adding the branch). Whether that is worth doing is Mark's call.
> - **#17** — `readDEF()` names the file it wanted instead of printing an empty path. Diagnosis only;
>   the `floorplan.def` hardcoding stands.

The #19 "still live there" bullet is **not** dead with this eviction — it was re-filed the same day
as **↪ pl_algo** in tasks.md #19 and folds into #20 step 2.

Also rewritten in summary.md that day: the section headed **"Newly open"**, in which 4 of 6 entries
(#25, #28, #29, #31) were closed and already archived to history.md. The most-read file in the repo
was advertising finished work as open.

---

## Superseded headline HPWL-ratio snapshots (sw_only vs XPlace)

### 2026-08-10 — "median 1.0113, mean 1.1218" (pre-`3c70b38`)
> **Median HPWL ratio 1.0113 vs XPlace** over **19 scored ISPD designs** (legal-vs-legal, re-run
> 2026-08-10 on the post-#23 binary). 12/19 within ±2%, better than XPlace on 4. **Quote the
> median** — the mean (1.1218) is one broken design, `mgc_matrix_mult_a` at 3.03× (GP dies at
> iteration 290); excluding it the mean is 1.0159. 9 designs unscored (#22 fence regions).
> → [[_NEW_REPORT_performance_snapshot_20260810.md]]

Not withdrawn — **directly comparable**, same 19 designs, same two-stage method, same references.
The delta is exactly one commit. That report's §2 method section still governs.

### 2026-08-07 — "1.0090 over 33 scored designs"
> **Median HPWL ratio 1.0090 vs XPlace** over 33 scored designs (44-design suite, legal-vs-legal,
> 2026-08-07). 25/33 within ±2%, better than XPlace on 7. **Quote the median** — the mean (1.087)
> is one broken design. → `[[_NEW_REPORT_performance_snapshot_20260807.md]]`

**Withdrawn, not corrected — the two are not comparable.** That figure spanned all three tiers
(33 of 44, including 16 MMS); the new one is ISPD-only (19 of 28). Two independent reasons it
could not stand: its cited report **never existed** in `1_REVIEW/reports/`, so which designs were
scored and which inflated its mean were unrecoverable; and its stage-1 GP inputs predated the #23
fix, with a third of the ISPD2015 tier frozen (`mgc_superblue12` carried a 7.05e+09 post-DP HPWL
— XPlace's legalizer fed cells stacked at die centre). **Do not average the old and new numbers.**
The MMS side still rests on `lgdp_suite_results.tsv`, valid but scored under a different harness.

---

## Dated closed narration, evicted from summary.md's soft cap

### 2026-08-10 — #24 "code DONE", #23 baselines
> - **#24 code DONE** — best-solution tracking rebuilt to match XPlace: three trackers
>   (`best_primary`/`best_aux`/`best_rollback`), **each with its own geometry buffer**, one shared
>   `selectBestSolution()` (`get_best_solution`, param_scheduler.py:540-577). Two defects, only one
>   of which was known: the shared buffer (17 of 29 runs shipped a placement the log did not name),
>   and a **torn restore** — density deposits at `probe_pos`, which the restore never touched, so
>   every reported overflow described the *last iteration* rather than the shipped placement. That
>   second one produced #24's original evidence, so the ticket's stated proof was misattributed.
>   All three suites green; 3 baselines regenerated with `--reason`.
>   ⚠️ **The rule does not always ship the spread-out placement** — aux 8 / primary 11 over 29
>   traces, and the *bug* shipped spread nearly always. A/B on the 0.5% budget says **keep XPlace's
>   1.005**: 1.010 buys ~35% less overflow for ~0.5–0.7% GP HPWL and DP recovers only 41–74% of it.
>   **Two decisions still open** (fix (B)'s scope → possible MMS re-run; n=2 on the A/B).
>   → [[_NEW_REPORT_24_best_solution_trackers_20260810.md]]
> - **#23 the fix** — `init_step_seed` scaled by site width. Regress baselines for
>   `mgc_fft_a` / `mgc_pci_bridge32_b` regenerated with `--reason`; `mms_adaptec1` untouched
>   (⚠️ superseded — #24's fix (B) changed `mms_adaptec1`, see above).
> - **#23 bullet 2** — `estimateInitialStep()` now hard-errors on a zero/NaN BB step instead of
>   no-opping to max_iterations. `make test-regress` bit-identical; error path exercised directly.

**Superseded 2026-08-17 by #24's close.** Both "decisions still open" resolved: fix (B) was scoped
to the final restore only (`syncProbeToCommitted()`), and its premise ("scoping it leaves MMS
bit-identical") was itself wrong — MMS moves under #24's *selection* fix regardless of (B)'s scope,
via `beginFixedMacroPhase`. The MMS suite was re-run 2026-08-14 (16/16, DP ratio median 1.0138).
The A/B's n=2 and the two further faithfulness gaps found while auditing the close (u-vs-v
snapshot position, `BEST_SOL_MIN_ITER` absolute-vs-phase-relative) carry forward as **#32**. Full
record: `history.md` #24.


## 2026-08-27 — evicted from summary.md (one in, one out)

Displaced by #39's entry. Verbatim as it stood; the full record is `history.md` #24, and the
narration it itself superseded is above under the 2026-08-17 entry.

## Closed 2026-08-17
- **#24 CLOSED** — best-solution tracking now matches XPlace's `get_best_solution`: three trackers
  (`best_primary`/`best_aux`/`best_rollback`), each with its own geometry buffer, one shared
  selection rule. Fixed a shared-buffer defect (17/29 runs shipped a placement the log didn't name)
  and a torn-restore defect (reported overflow described the last iteration, not the shipped one).
  MMS suite re-run 2026-08-14: 16/16, DP ratio median 1.0138 / mean 1.0161. Two remaining
  faithfulness gaps (snapshot position u-vs-v; `BEST_SOL_MIN_ITER` absolute-vs-phase-relative) and
  the A/B's n=2 spun off to **#32** rather than left open here.
  → [[_NEW_REPORT_24_best_solution_trackers_20260810.md]]. Superseded prior narration: [[journal.md]].

