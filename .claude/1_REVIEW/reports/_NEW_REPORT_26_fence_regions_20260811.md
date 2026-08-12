# REPORT #26 — Fence regions: the 9 unscored designs, scored; and what ignoring the fence costs

*2026-08-11 23:40 EDT. Branch `pl_algo`. GP inputs from the v3 suite run on commit `3c70b38`.*

## 1. Summary

All five steps of [[#26]] are done.

- **The 9 designs are scored. The ISPD suite is now 28 of 28** (was 19 of 28). Their ratios are
  **unremarkable — median 1.0154, 7 of 9 within 2%, one better than XPlace** — which is itself the
  finding: nothing was hiding in the unscored set. The ISPD headline moves **median 1.0095 → 1.0106,
  mean 1.0151 → 1.0149** (both folding in [[#27]]'s corrected `matrix_mult_a`).
- **Step 2 was never an acquisition problem.** `ispd2015_fix` is not downloaded; XPlace *generates*
  it from the same raw files with `data/fix_ispd2015_route.py`. One command produced all 20 designs,
  and the regenerated `mgc_pci_bridge32_b` DEF is **byte-identical** to the 2026-07-13 copy that
  every existing reference for that design was measured on. #22's "construct it and validate"
  caveat is retired: this is XPlace's own script, not a hand-build.
- **Ignoring the fence is a real correctness gap, and now it is measured.** On the 9 designs,
  **59–94% of fence-constrained cells sit outside their region** in our placement. The measurement
  is controlled: the contest's own legalized solutions, run through the same checker, put
  **0 of 190,010** constrained cells outside.
- **Roughly 10 percentage points of our apparent advantage on those 9 is the constraint, not the
  placer.** Against the contest's legalized solutions we are 2.6% better on the 11 unfenced designs
  and 12.5% better on the fenced 9 — a **9.9 pp** difference-in-differences.
- **Recommendation on step 4: do not implement fence regions.** Document the gap instead. Reasoning
  and the counter-argument in §7. This one is Mark's call; everything else here is landed.

⚠️ **The XPlace comparison is unaffected by all of this.** XPlace strips fences too, and the 9 are
scored on the stripped variant on *both* sides. What is affected is any claim that our ISPD2015
numbers are legal contest solutions. They are not.

## 2. What the data actually is (step 2)

`Xplace/data/download_data.sh` ends its ISPD2015 section with:

```
echo "=== Preprocessing ispd2015 to generate ispd2015_fix ==="
python fix_ispd2015_route.py
```

`data/raw/ispd2015` is a symlink to our own `vck5000/host/benchmarks/ispd2015`, so the generator
reads the same bytes we place. It strips `REGIONS`/`GROUPS` (`removeDefFence = True`), and also does
routing-oriented surgery — SNet vias removed, `VIAS`/`NONDEFAULTRULES` moved into the LEF, a few
fixed cells snapped to the manufacturing grid.

Regenerated 2026-08-11: **20 designs, 702 MB, ~90 s.**

**Validation, in the order it should be read:**

| check | result |
|---|---|
| `mgc_pci_bridge32_b.def` vs the 2026-07-13 copy | **byte-identical** |
| `tech.lef`, `cells.lef`, `placement.constraints` vs same | **byte-identical** |
| merged `mgc_pci_bridge32_b.lef` vs same | **9 hunks differ** — see below |
| `COMPONENTS` count, all 20 designs, raw vs fix | identical |
| component *name set*, `edit_dist_a` + `fft_b` (the two whose COMPONENTS tail the script rewrites) | identical (md5 of the sorted names) |

The `.lef` difference is confined to `VIA` blocks and is a **nondeterminism in XPlace's generator**,
not a data problem: `generateLefViaFromDef` iterates a Python `set` to decide which layer is
`routeL1` vs `routeL2`, so the order varies run to run. It affects routing via definitions only —
no macro, pin, or site geometry — and the placement flow reads `tech.lef` + `cells.lef`, both
byte-identical. Worth knowing if anyone ever diffs this data again.

Two designs get placement-relevant edits from the generator, both tiny: `edit_dist_a` and `fft_b`
have 6 FIXED macros each moved to the manufacturing grid, **by at most 3 DBU** (e.g.
`55600 485307` → `55600 485305`).

## 3. Scoring the 9 (step 1)

Both harnesses now split on whether the DEF has `REGIONS`, and the split is recorded in the output:

- `run_xplace_ref_2015.sh` — fence designs run `--dataset ispd2015_fix --design_name <d>`; the rest
  keep `--custom_path` on the raw files. Its `blocked_fence_region` rows are gone.
- `run_lgdp44.sh` — same split, and **the template our placement is patched into becomes the `_fix`
  DEF** for those designs. Patching into a DEF the run will not read scores nothing.

Both TSVs gained a final `variant` column (`bookshelf` / `ispd2015` / `ispd2015_fix`), and
`analyze_full44.py` marks fence-stripped designs with **†**, the same mark and the same meaning as
the TCAD paper's Table III/V.

### 3a. The new XPlace references, cross-checked against the paper

The 8 missing references were produced locally (seed 42, 8 threads, RTX 3080; ~2 minutes for all 8).
Table III of the TCAD paper reports HPWL for the same designs, so this is a free check — convert
site units → DBU → microns:

| design | ours (×10³ µm) | paper Table III | ratio |
|---|---:|---:|---:|
| mgc_des_perf_a | 1979.5 | 1998.8 | 0.9903 |
| mgc_des_perf_b | 1573.1 | 1611.8 | 0.9760 |
| mgc_edit_dist_a | 4159.1 | 4198.3 | 0.9907 |
| mgc_matrix_mult_b | 2766.9 | 2762.6 | 1.0016 |
| mgc_matrix_mult_c | 2677.7 | 2674.6 | 1.0012 |
| mgc_pci_bridge32_a | 341.5 | 360.9 | 0.9463 |
| mgc_pci_bridge32_b | 695.4 | 714.0 | 0.9740 |
| mgc_superblue11_a | 33540.9 | 33521.3 | 1.0006 |
| mgc_superblue16_a | 25540.2 | 25491.0 | 1.0019 |

5 of 9 within 0.2%, worst 5.4%. Every local run is **equal or better** than the published number,
which is expected rather than suspicious: the paper's Table III HPWL is reported by *NTUplace4dr*
measuring XPlace's placement, not by XPlace itself.

### 3b. Our numbers on the 9

| design | iters | stop | our post-DP | XPlace post-DP | ratio |
|---|---:|---|---:|---:|---:|
| mgc_des_perf_a | 718 | converged | 1.0071e+07 | 9.8975e+06 | 1.0175 |
| mgc_des_perf_b | 838 | divergence_guard | 7.9869e+06 | 7.8657e+06 | 1.0154 |
| mgc_edit_dist_a | 782 | divergence_guard | 2.1032e+07 | 2.0796e+07 | 1.0114 |
| mgc_matrix_mult_b | 732 | converged | 1.3857e+07 | 1.3835e+07 | 1.0016 |
| mgc_matrix_mult_c | 723 | converged | 1.3422e+07 | 1.3388e+07 | 1.0025 |
| mgc_pci_bridge32_a | 738 | divergence_guard | 1.7999e+06 | 1.7076e+06 | 1.0540 |
| mgc_pci_bridge32_b | 737 | divergence_guard | 3.4225e+06 | 3.4771e+06 | **0.9843** |
| mgc_superblue11_a | 662 | converged | 3.4147e+08 | 3.3541e+08 | 1.0181 |
| mgc_superblue16_a | 671 | converged | 2.6237e+08 | 2.5540e+08 | 1.0273 |

### 3c. Suite effect

| set | n | median | mean | within 2% | better than XPlace |
|---|---:|---:|---:|---:|---:|
| ISPD2005 | 8 | 1.0053 | 1.0052 | 8 | 1 |
| ISPD2015, no fence | 11 | 1.0273 | 1.2268 | 5 | 2 |
| **ISPD2015, fence-stripped (new)** | **9** | **1.0154** | **1.0147** | **7** | **1** |
| ISPD2015 all | 20 | 1.0165 | 1.1313 | 12 | 3 |
| ISPD all | 28 | 1.0106 | 1.0953 | 20 | 4 |
| **ISPD2015 all, `matrix_mult_a` post-#27** | 20 | 1.0163 | **1.0189** | 13 | 3 |
| **ISPD all, `matrix_mult_a` post-#27** | **28** | **1.0106** | **1.0149** | **21** | **4** |

The two mean columns differ by one design. `matrix_mult_a` runs at 3.2669 in `lgdp44_v4_results.tsv`
because its GP input predates [[#27]]'s parser fix (`4ad8820`); that ticket's own re-run under the
identical suite config gives post-DP **1.543012e+07 = 1.0172** (REPORT_27 §5). The last two rows
substitute it. **The v4 TSV deliberately keeps the as-run row** — folding a differently-sourced
number into a suite TSV destroys its provenance, and the next full suite run will fold it in
naturally.

**The headline barely moves: median 1.0095 (19 designs) → 1.0106 (28), mean 1.0151 → 1.0149.**
Nine designs that had been unscored for four days turned out to hold no surprise at all.

**Determinism check, free and worth having:** re-running LG+DP for all 28 reproduced the 20
previously-scored rows **bit-identically** (`dp_hpwl` string-equal, 20/20). The only new information
in `lgdp44_v4_results.tsv` is the 9 rows that did not exist.

⚠️ `mgc_pci_bridge32_b` reads **1.6% better** than XPlace here, where the #26 ticket recorded "~5%
ahead" (3.310820e+06). Not a contradiction — the ticket's figure came from an ad-hoc placement, this
one from the v3 suite GP on `3c70b38`. **The suite number supersedes it.**

## 4. What ignoring the fence actually does (step 3)

`vck5000/tools/fence_check.py` (new) parses the DEF's `REGIONS`/`GROUPS`, matches each group's
wildcard patterns against instance names, and tests whether each constrained cell's placed origin
lands in any rectangle of its region.

| design | constrained cells | % of design | outside | % outside |
|---|---:|---:|---:|---:|
| mgc_des_perf_a | 11,970 | 11.1% | 11,199 | **93.6%** |
| mgc_des_perf_b | 16,586 | 14.7% | 14,939 | **90.1%** |
| mgc_edit_dist_a | 2,927 | 2.3% | 1,800 | 61.5% |
| mgc_matrix_mult_b | 14,535 | 9.9% | 12,543 | 86.3% |
| mgc_matrix_mult_c | 14,535 | 9.9% | 9,877 | 68.0% |
| mgc_pci_bridge32_a | 9,122 | 30.9% | 5,393 | 59.1% |
| mgc_pci_bridge32_b | 7,757 | 26.8% | 5,655 | 72.9% |
| mgc_superblue11_a | 61,286 | 6.6% | 54,427 | **88.8%** |
| mgc_superblue16_a | 51,292 | 7.5% | 46,492 | **90.6%** |

Displacements are not marginal: on `mgc_pci_bridge32_b`, cells assigned to region `r0` sit a mean of
127,299 DBU outside it on a die 800,000 DBU wide.

**The control is what makes this a measurement.** Every ISPD2015 design ships the contest's own
legalized solution as `after_legalized.ntup.fix.def`. Run through the same checker with
`--expect-legal`, all 9 report **zero** violations — 190,010 constrained cells, not one outside.
A parser that reports 0.0% on a legal placement and 59–94% on ours is measuring the right thing.
The check is one command per design and is documented in the tool's docstring; `--expect-legal`
exits non-zero, so it is an assertion, not a print.

### 4a. Step 3 as literally written has a null answer — proved, not argued

The ticket asked to place *both* variants with sw_only and compare. Done, on `mgc_pci_bridge32_b`
under its frozen regress config: `iterations.dat` is **identical** and the output DEF has the
**same md5**. Placing the fence-stripped data changes nothing, because the constraint is discarded
either way. There is no fence-carrying-vs-stripped HPWL gap to find — which is exactly why the
comparison below was needed instead.

### 4b. Separating "our placer is better" from "our problem is easier"

Comparing our HPWL to the contest's *legal* solution on the 9 conflates two effects. The 11 designs
with no fence regions calibrate the confound: there, any gap is placer quality alone. Both sides ran
through the same XPlace LG+DP path, so the frames match.

| group | n | median ours/contest | interpretation |
|---|---:|---:|---|
| unfenced | 11 | 0.9744 | we are 2.6% better — placer quality |
| **fenced** | **9** | **0.8753** | we are 12.5% better |

**Difference-in-differences: 9.9 percentage points.** The straightforward reading is that about ten
points of our margin on the fenced designs comes from solving an unconstrained problem.

Read it as an estimate, not a constant. It is a between-group comparison on different designs, the
contest solutions vary in quality (`mgc_fft_2` 0.78 unfenced is as extreme as anything in the fenced
group), and `mgc_pci_bridge32_b` runs the other way entirely — the contest solution beats us by 5%
there. `matrix_mult_a`'s 2.98 is our own [[#27]] bug and carries no information. What survives all
that is the direction and the rough size: the fence is not free, and we are not paying it.

## 5. What shipped (step 5)

1. **The placer says so.** `readDEF()` now warns when the DEF declares REGIONS/GROUPS: *"These are
   IGNORED: the design is placed unconstrained, so the result is NOT a legal ISPD2015 solution…"*.
   It lands in the run's `run.log`.
   ⚠️ **The warning is emitted in `readDEF()`, not in the `end_def_design()` callback where it
   belongs conceptually** — every DEF callback runs inside `runParserSilenced()`, which redirects
   stdout for the duration of the parse and swallowed it. Noted at both sites.
   ⚠️ Related trap, cost 20 minutes: **the placer writes nothing to a redirected stdout.**
   `test/regress/work/*.stdout` are all 0 bytes. Grep `run.log` in the run directory.
2. **The results carry the variant.** Both TSVs gained a `variant` column; `analyze_full44.py`
   prints † on fence-stripped designs and a legend naming all 9.
3. **`benchmarks.py` documents the split** at `_XPLACE_ISPD_FINAL`: which 11 came from raw data via
   `--custom_path`, which 9 from `ispd2015_fix`, how to regenerate, and the byte-identical
   validation.

`make test-regress` passes bit-identically before and after (all three designs), which is the
expected result for a change that only adds a log line.

## 6. Reproduce

```bash
cd ~/phd/Xplace/data && python3 fix_ispd2015_route.py          # regenerate ispd2015_fix (~90 s)
cd ~/phd/AIEplace/.claude/2_ARTIFACTS && XREF_OUT=/tmp/xref26 bash run_xplace_ref_2015.sh
LGDP44_GP=/tmp/full44_v3/results LGDP44_OUT=/tmp/lgdp26 \
  LGDP44_RES=$PWD/lgdp44_v4_results.tsv bash run_lgdp44.sh
python3 analyze_full44.py --gp full44_v3_suite_results.tsv --lgdp lgdp44_v4_results.tsv
python3 analyze_fence_cost.py lgdp44_v4_results.tsv contest_legal_lgdp_20260811.tsv
cd ~/phd/AIEplace/vck5000 && python3 tools/fence_check.py \
  host/benchmarks/ispd2015/mgc_pci_bridge32_b/floorplan.def \
  host/benchmarks/ispd2015/mgc_pci_bridge32_b/after_legalized.ntup.fix.def --expect-legal
```

Artifacts (all in `.claude/2_ARTIFACTS/`, untracked): `lgdp44_v4_results.tsv`,
`contest_legal_lgdp_20260811.tsv`, `fence_violation_20260811.csv`, `xplace_ref_ispd.tsv`.

## 7. Step 4 — should we implement fence regions?

> **DECIDED 2026-08-12 (Mark): no — document it.** We ignore fence regions, as XPlace does, and say
> so wherever the numbers appear. The rule is recorded in **`CLAUDE.md`** rather than only here,
> because an archived tasks.md entry is not loaded next session — which is exactly how #22's
> analysis went stale and #26 re-derived it four days later.

**Recommendation as written before that decision: no. Document it and move on.** Three reasons, in
order of weight:

1. **XPlace has no formulation to copy.** It raises `NotImplementedError` and its paper removes the
   constraint (footnote 2: *"We will address the fence region constraint in our future version"*).
   Per `CLAUDE.md`, anything we build here is **our own design decision**, not faithfulness work —
   per-region filler budgets and a per-region density objective, invented by us, on the one axis
   where we have deliberately chosen to track XPlace.
2. **It cannot improve any number we report.** Both sides of every ratio already strip the fence.
   Implementing it would make 9 of 28 designs *worse* by ~10% against a reference that does not
   honour the constraint — a real quality gain that reads as a regression in every table we keep.
3. **The gap is now visible rather than silent**, which was the actual defect: the run log says it,
   the scorecard daggers it, and `fence_check.py` quantifies it on demand.

**The counter-argument, stated fairly:** the ISPD2015 contest scored these designs *with* the
constraint, and a placement violating 90% of its fence assignments is not a placement anyone would
ship. If AIEplace is ever presented as an ISPD2015 result rather than an XPlace comparison, this
becomes a genuine correctness bug and §4b says it is worth about 10% of wirelength. The honest
framing for a paper is "ISPD2015 with fence-region constraints removed, as in [XPlace]" — the same
sentence the TCAD paper uses.

## 8. Left open

- **The next full suite run should fold in [[#27]]'s `matrix_mult_a`** so the v-series TSVs and the
  headline stop needing a footnote. The substituted numbers are in §3c; the raw row is not edited.
- **[[#25]] is untouched by this** — we and XPlace still use different `target_density` on ISPD2015,
  which affects all 20 including these 9.
- **`fence_check.py` is not wired into any suite.** `--expect-legal` on the 9 contest solutions is a
  ~20 s tripwire that would catch a DEF-parsing regression; it belongs in `make test-regress` if
  anyone touches the region/group parsing again.

## 9. Keeping this closed (added 2026-08-12)

The failure this ticket is most likely to repeat is not conceptual — it is **`ispd2015_fix` going
missing or stale** on a fresh box or after a benchmark re-download, after which the harnesses skip
9 designs and someone starts the investigation over. That is the exact shape of the #22 → #26 loop.
Both harnesses now **fail loudly on stdout** naming the regeneration command, and warn when the raw
`floorplan.def` is newer than the derived `_fix` DEF. Exercised against a synthetic missing-`_fix`
tree, not merely read.

**Keep the fenced originals — deleting them was considered and rejected.** They are the input
`fix_ispd2015_route.py` reads, so `_fix` becomes unregenerable without them, and they hold the only
copy of `after_legalized.ntup.fix.def`: the contest's own legal placement, which is the control for
every violation number in §4 and the only fence-legal reference we have. `_fix` carries neither that
file nor `design.v`. Deleting the raw data would additionally break both frozen regress configs, the
run and DSE configs, four `vck5000/tools/*.sh`, and XPlace's `data/raw/ispd2015` symlink — to
reclaim 1.8 GB that is already gitignored and re-downloadable.

One trap if anyone later tries to standardise on `_fix` alone: it nudges 6 fixed macros onto the
manufacturing grid in `edit_dist_a` and `fft_b` (≤3 DBU), so those designs would **not** reproduce
their current placements. `mgc_pci_bridge32_b` is bit-identical across the two variants (§4a) only
because it has no such nudges.
- **The scoring harnesses are untracked** (`.claude/2_ARTIFACTS/` is gitignored) even though they
  produce every headline number in this repo. `run_lgdp44.sh` still carried a stale default output
  path from the 2026-08-07 `.claude/` move, and so did `analyze_full44.py` — both fixed here, both
  invisible to git. Moving them next to `vck5000/tools/def_patch_placement.py`, which they call and
  which *is* tracked, is a 3-file change worth doing.
