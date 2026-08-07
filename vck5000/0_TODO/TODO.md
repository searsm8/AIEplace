# TODO

Cross-session task parking. New workflow (2026-07-25): park work here, not only in loose reports.
Deliverables are named `_NEW_<TYPE>_<description>_<YYYYMMDD>.md` until Mark has read them (then he
drops the `_NEW_`). 

**A finished task leaves this file.** Numbers are never reused, so a `#N` reference always
resolves — if it is not here, it is in `history.md`. Archived 2026-08-07:
**#2, #4 (⛔ its conclusion was later reversed — read the banner), #5, #8, #12, #13, #16, #18.**
Procedure: repo-root `CLAUDE.md`, "Keeping TODO.md from bloating".

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
