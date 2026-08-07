# HANDOFF — filler-sizing faithfulness (TODO #13 "P1"), 2026-07-31

**Your job: run the MMS re-baseline for the change described below, and report whether it moves
placement quality.** The code is written, builds clean, and its *filler population* is verified
against XPlace. What is NOT known is whether it improves HPWL/overflow. That is the whole task.

---

## 0. State of the tree — READ FIRST

- Branch `pl_algo`, HEAD **`caa8f2b`**.
- **The work is UNCOMMITTED**: 4 modified files under `vck5000/`. Do not `git stash`,
  `git reset`, or `git checkout` those paths.
  - `host/src/sw_only/include/AIEplace.h`
  - `host/src/sw_only/include/DataBase.h`
  - `host/src/sw_only/src/DataBase.cpp`
  - `host/src/sw_only/src/placer/Setup.cpp`
- **Recommend committing before you start the sweep**, so the run has a pinned provenance. The
  `mms_baseline_20260731.tsv` header treats binary provenance as load-bearing, and the previous
  commit hashed the binary either side of its sweep. Follow that practice.
- `CLAUDE.md` also shows a modification that is **not part of this work** (a "always read
  TODO.md first" section, from another session). Leave it out of any commit of this change.
- Ephemeral, will not survive a reboot — rebuild if missing:
  - `/tmp/ref_swonly.exe` — reference binary built from `caa8f2b` (md5 `d4462bd617918f74a06b1bacbd8074c1`)
  - `/tmp/p1_filler.patch` — backup of the uncommitted diff
  - New binary md5 at time of writing: `6c2d0265d2d4d5eb44a5e9fe60c65671`

Build: `cd vck5000/host && make HOST=sw_only`
(exe lands at `vck5000/build/hw/host/sw_only/aieplace_sw_only.exe`)

---

## 1. What changed and why

`DataBase::addFillers` was written without the XPlace reference and diverged from
`compute_filler_without_fence` (`~/phd/Xplace/src/database.py:662`) in six ways. Five are now
fixed; all six were reviewed and approved by Mark on 2026-07-31.

| # | Divergence | Status |
|---|---|---|
| A | Movable macros were in the filler **size sample** | fixed — std cells only |
| B | Filler height was a trimmed mean of cell heights, not the **row height** | fixed — row height plumbed |
| C | Movable macro area was in **both** terms of the whitespace budget | fixed — std-cell frame |
| D | Overlapping fixed macros double-counted in placeable area | **NOT DONE** — see below |
| E | No **`target_density` raise** when the design is denser than its target | fixed |
| F | Zero fillers produced **silently** | fixed — warns |

Structural changes that came with it:

- `addFillers` now returns `float` (the **effective** target density) instead of an ignored
  `bool`. XPlace mutates `args.target_density` in place; returning it keeps the mutation visible
  at the call site. **The caller must adopt the return value.**
- `setupDesign` reordered: `tagMovableMacros()` now runs **before** the filler math (it has to —
  A/B/C all need the macro tag), and `createFillers()` was split out of `loadDesignDatabase()`.
  This is the minimal slice of the "unify the two macro definitions" work; `analyzeDesignArea`
  still uses its own die-area `num_movable_macros` heuristic for grid sizing. **Two macro
  definitions still coexist.** That full unification is still open (was "P2").
- Row height (`DataBase::m_row_height`) plumbed for both input formats: `.scl CoreRow Height`
  for Bookshelf, and the LEF **CORE `SITE`** for LEF/DEF (`lef_site_cbk` was an empty stub —
  `DefParser::Row` carries no height field, only an origin). Falls back to the old trimmed mean
  with a warning if the input supplies neither.
- All four `log_detail` calls removed; one `log_info` summary line replaces them:
  `Fillers: N at (w, h), effective target density D`.

### Why D was skipped
`computeAreaBreakdown` (`DataBase.cpp:103`) **already** clips fixed area to the die, so the only
residual gap vs XPlace is that two overlapping fixed macros count twice where XPlace's clamped
density map counts them once. No tested design has overlapping fixed geometry, so implementing it
would mean shipping untested code. Left as a documented, deliberate divergence. Revisit only if a
design turns up where it matters.

---

## 2. What is already verified

Method: build a reference binary from `caa8f2b`, run both binaries on the same configs, compare
`iterations.dat` and `RowBasedPlacement.def`.

| design | suite | old fillers | new fillers | result |
|---|---|---|---|---|
| adaptec1 | ispd2005 | 160,067 @ (14.4, 12) | identical | **bit-identical** trace + placement |
| adaptec1_g1024 | ispd2005 | identical | identical | **bit-identical** |
| mgc_fft_1 | ispd2015 | 105 @ (895, 2e3) | 105 | changed — row height (B) only |
| mgc_matrix_mult_1 | ispd2015 | 0 @ (757, 2e3) | 0 | changed — B + raise 0.802 → 0.802045 |
| **adaptec5** | mms td=0.5 | **0** | **310,073 @ (10.8, 12)** | **exact XPlace match** |
| newblue4 | mms td=0.5 | **0** | 205,682 @ (11.8, 12) | C, plus A (width 11.9 → 11.8) |
| newblue1 | mms td=0.8 | 100,375 | 181,724 | C |

XPlace's own adaptec5 log
(`~/phd/Xplace/result/2026-07-17-23:03:11_adaptec5/log/test.log`) reads
`#Fillers: 310073 Filler size: (1.0795e+01, 1.2000e+01)`. We now produce 310,073 at
(10.795, 12.000) — count and size exactly.

ISPD2005 being bit-identical is structural, not luck: its macros are FIXED, so A/C/E are no-ops,
and its row height (12) equals the mean movable cell height because every movable cell is one row
tall.

**The limit of this evidence:** the MMS runs above are **20 iterations**, where `density_weight`
is still ~1e-11 and fillers have not begun to act — both arms agree on HPWL and overflow to four
significant digits. This proves the filler *population* is now correct. It says **nothing** about
placement quality.

---

## 3. THE TASK — MMS re-baseline

Compare the new binary against `2_ARTIFACTS/mms_baseline_20260731.tsv` across the MMS tier.

Baseline TSV columns:
`design, target_density, hpwl_exact, ovfl_sharp_filler, iters_total, iters_best, stop_reason,
xp_mixedgp_hpwl, xp_mixedgp_ovfl`

Its provenance (from its own header): seed 42, XPlace per-design grid + target_density,
`xplace_die_projection=true`, `macro_td_expand_ratio=false` — i.e. the **proj** arm of the
footprint A/B, which is what today's unconditional defaults reproduce. It is the correct "before".

### Harness
Reuse the pattern that produced the baseline:

- Generator: `2_ARTIFACTS/gen_footprint_ab_configs.py` — writes TOML, and critically sets
  `bins_per_row`, `maximum_utilization` and `random_seed` per design from the MMS table.
- Runner: `2_ARTIFACTS/run_footprint_ab.sh` — sequential, resumable, one design at a time.
  (Sequential is deliberate and measured; see its header. Don't "optimize" it into concurrency.)

```bash
nohup bash 2_ARTIFACTS/run_footprint_ab.sh > /tmp/fp_ab/runner.log 2>&1 &
```

⚠️ **The generator is partly STALE.** It writes `params["xplace_die_projection"]`, a config key
that **no longer exists** — `caa8f2b` adopted #11a unconditionally and deleted the toggle. toml++
ignores unknown keys, so the `base` and `proj` arms would now be silently *identical*, and you'd
burn double the sweep time proving it. You want **one arm per design** (current defaults). Either
strip the arm machinery from a copy of the generator, or pass a single arm. Fix or fork it — do
not run it as-is.

Also note `run_mms_ab.sh` (a different, older runner) references `.json` configs; the generator
writes `.toml`. Don't mix them.

### Scope
Mark's open question, not yet decided: **all 16 MMS designs, or just the macro-heavy set?** The
designs where this change can possibly do anything are those with movable macros AND
`target_density < 1.0`:
**adaptec5, newblue1, newblue2, newblue3, newblue4, newblue5, newblue6, newblue7** (8 designs).
The eight `td = 1.0` designs are affected only through A and B (size sample and row height), which
on MMS Bookshelf is likely a no-op — worth one cheap confirmation run rather than eight full ones.
Confirm the scope with Mark before committing hours.

---

## 4. What to look for

The headline hypothesis, stated so it can be falsified:

> adaptec5 and newblue4 previously ran with **literally zero filler cells**. Fillers are the
> mechanism by which whitespace is represented in the density field, so a zero-filler run cannot
> spread properly. This may be a root cause of the MMS under-spreading tracked in TODO #4.

If that is right, expect on adaptec5 / newblue4 / newblue5: lower final overflow, HPWL rising
toward (not away from) the XPlace Mixed-GP reference, and possibly a different `stop_reason`.

If overflow does **not** improve, say so plainly — the change is still correct on faithfulness
grounds and stays, but the under-spreading story needs a different explanation.

### Notes that this change puts in question (do not edit them yet — wait for data)
- Auto-memory `overflow-metric-grid-faithfulness` and `mms-hard-spreading-three-diseases` were
  both derived on a **zero-filler** newblue4/adaptec5. Their observation that newblue4 reads
  `clamp/no-filler == clamp/+filler` is explained trivially by there being no fillers at all.
- TODO #4's `convergence_include_fillers` reasoning ("it is a NO-OP on exactly the designs we care
  about, because they have essentially no fillers") rests on the same premise. With 310k/206k
  fillers now present, **re-evaluate that argument** — it may no longer hold, which would change
  the TODO #13 plan that currently defers the flag to phase 2.

---

## 5. Traps

1. **MMS needs an explicit target density.** Bookshelf suites have no `placement.constraints`, so
   a config from the bare `run_config.toml` template runs at `maximum_utilization = 1.0`. This
   cost a run today: adaptec5 produced 1,509,741 fillers instead of 310,073. Worse, divergence C
   shifts the count by `movable_macro_area * (1 - target_density)` — **identically zero at
   td = 1.0** — so the experiment could not have detected its own effect. Always set BOTH
   `maximum_utilization` and `bins_per_row` from `tools/benchmarks.py::_ROWS`. Auto-memory:
   `mms-needs-explicit-target-density`.
2. **Pin `random_seed`.** The template omits it; it defaults to a time-based seed. Auto-memory:
   `pin-random-seed-in-manual-ab`.
3. **`tools/verify_swonly.sh` cannot be diffed the way its own docstring says.** It tells you to
   compare runs with `diff -r artifacts`, but it collects `function_statistics.md`, which contains
   wall-clock timings — so `diff -r` reports a difference on every design, every run, regardless
   of correctness. Diff `iterations.dat` and `RowBasedPlacement.def` directly. **Worth fixing**
   (drop the timing file from the artifact set, or exclude it); not done here to keep this change
   surgical.
4. **`iters_best` ≠ `iters_total`.** The baseline TSV carries both and its header warns they differ
   by hundreds (adaptec5: best 649, total 1163). `footprint_ab_results.tsv`'s `iter` column is
   iters_BEST. Comparing one against the other is a trap.
5. **Goldens captured before 2026-07-31 do not match** anything current — `caa8f2b` changed
   reported overflow conventions and adopted #11a. Use `mms_baseline_20260731.tsv`, nothing older.
6. **Check `ps` before trusting timings or killing anything** — Mark leaves multi-hour sweeps
   running. Auto-memory: `long-running-sweeps-on-this-box`.

---

## 6. Open decisions for Mark

- [ ] Sweep scope: 16 designs or the 8 macro-heavy ones?
- [ ] Commit the change before the sweep? (Recommended — pins provenance.)
- [ ] Keep the one `log_info` "Fillers:" line, or go silent? (Mark asked for the `log_detail` spam
      gone; this single line was proposed as a replacement and not explicitly ruled on. It is the
      number you need to interpret every result below, so it earns its place — but it is his call.)
- [ ] Fix `verify_swonly.sh`'s artifact set (trap 3)?
- [ ] Implement divergence D after all?

## 7. Where this sits in the larger plan

This is prerequisite **P1** of TODO #13 (mixed-size phase 2). Still open, in order:
- **P3** — phase-relative iteration counter (a `phaseIteration()` offset replacing raw `iteration`
  in ~6 schedule sites). Provably a no-op while there is one phase; do it before phase 2 exists.
- **P2** — unify the two macro definitions (`num_movable_macros` die-area heuristic vs
  `Node::m_is_movable_macro` XPlace rule). Partially started here (the tag now runs early).
- Then TODO #13 stage 3 (fixed-macro restart), then stage 2 (LP macro legalizer — Mark chose
  porting XPlace's formulation and linking an LP solver).

Mark's stated working preference: **review how code will read to a human before implementing.**
Show the shape of a change and get a nod before writing it.
