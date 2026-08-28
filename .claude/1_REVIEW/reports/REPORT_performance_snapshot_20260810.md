# REPORT — sw_only vs XPlace performance snapshot, ISPD tiers (2026-08-10)

*Replaces the missing `_NEW_REPORT_performance_snapshot_20260807.md` that `summary.md` cited.
Supersedes the "median 1.0090 over 33 scored designs" headline — see §5 for the retraction.*

## 1. Headline

**Median post-DP HPWL ratio 1.0113 vs XPlace over 19 scored ISPD designs.**

| metric | value |
|---|---|
| median ratio (sw_only / XPlace) | **1.0113** |
| mean ratio | 1.1218 — **do not quote**, see §4 |
| within ±2% | 12/19 |
| better than XPlace | 4/19 |
| best / worst | 0.9847 (`mgc_fft_b`) / 3.0331 (`mgc_matrix_mult_a`) |
| unscored | 9/28 — fence regions, TODO #22 |

**Quote the median.** The mean is one broken design (§4).

## 2. Method — legal-vs-legal, same downstream tool on both sides

Two stages, both re-run today on the post-#23 binary (commit `ba0ce6a`):

```
stage 1  run_suite.sh    sw_only GP, 28 ISPD designs, seed 42, deterministic
                         -> /tmp/full44_v2/results
                         -> .claude/2_ARTIFACTS/full44_v2_suite_results.tsv
stage 2  run_lgdp44.sh   those GP solutions through XPlace's OWN legalizer + detailed placer
                         (--global_placement False --given_solution)
                         -> .claude/2_ARTIFACTS/lgdp44_v2_results.tsv
```

The reference column is our own XPlace run (`_XPLACE_ISPD_FINAL` in `tools/benchmarks.py`,
captured 2026-08-07, seed 42), which runs the *whole* pipeline itself. Both sides are therefore
post-DP and legalized by the same code — the only difference is whose global placement fed it.

⚠️ **ISPD2015 HPWL is in site units** on both sides; routed through
`benchmarks.xplace_hpwl_in_sw_frame()`. Ratios are unit-free so this cancels, but the absolute
columns below are converted to DBU. `mgc_superblue*` is site width 100, everything else 200.

**Why stage 1 had to be re-run, not just stage 2:** the previous snapshot's GP tree
(`/tmp/full44/results`) is dated 2026-08-07, three days before the #23 fix, and still held the
frozen initial placements of the five dead designs. Re-running only stage 2 would have
re-legalized those and reproduced the same garbage under a fresh timestamp. This is how
`mgc_superblue12` came to carry a 7.05e+09 post-DP HPWL in the old table — XPlace's legalizer
handed a pile of cells stacked at die centre.

## 3. Results

| design | sw_only → XPlace LG/DP | our XPlace (full) | paper (Table II/III) | vs ours | vs paper |
|---|---|---|---|---|---|
| adaptec1 | 7.2852e+07 | 7.3103e+07 | 7.3090e+07 | **0.9966** | 0.9967 |
| adaptec2 | 8.1880e+07 | 8.1318e+07 | 8.1300e+07 | 1.0069 | 1.0071 |
| adaptec3 | 1.9257e+08 | 1.9385e+08 | 1.9362e+08 | **0.9934** | 0.9946 |
| adaptec4 | 1.7531e+08 | 1.7335e+08 | 1.7336e+08 | 1.0113 | 1.0112 |
| bigblue1 | 8.9478e+07 | 8.9081e+07 | 8.9080e+07 | 1.0045 | 1.0045 |
| bigblue2 | 1.3781e+08 | 1.3697e+08 | 1.3691e+08 | 1.0062 | 1.0066 |
| bigblue3 | 3.2007e+08 | 3.0294e+08 | 3.0308e+08 | 1.0565 | 1.0561 |
| bigblue4 | 7.5229e+08 | 7.4255e+08 | 7.4219e+08 | 1.0131 | 1.0136 |
| mgc_des_perf_1 | 1.1092e+09 | 1.1265e+09 | 1.1065e+09 | **0.9847** | 1.0025 |
| mgc_fft_1 | 4.0706e+08 | 4.0488e+08 | 4.1150e+08 | 1.0054 | 0.9892 |
| mgc_fft_2 | 3.7185e+08 | 3.6208e+08 | 3.7410e+08 | 1.0270 | 0.9940 |
| mgc_fft_a | 6.2723e+08 | 6.1235e+08 | 6.2580e+08 | 1.0243 | 1.0023 |
| mgc_fft_b | 8.3277e+08 | 8.3639e+08 | 8.4560e+08 | **0.9957** | 0.9848 |
| mgc_matrix_mult_1 | 2.1289e+09 | 2.1004e+09 | 2.1163e+09 | 1.0135 | 1.0059 |
| mgc_matrix_mult_2 | 2.1537e+09 | 2.1401e+09 | 2.1527e+09 | 1.0063 | 1.0004 |
| mgc_matrix_mult_a | 9.2022e+09 | 3.0339e+09 | 3.0326e+09 | **3.0331** | 3.0344 |
| mgc_superblue12 | 2.7153e+10 | 2.5710e+10 | 2.5784e+10 | 1.0561 | 1.0531 |
| mgc_superblue14 | 2.3360e+10 | 2.2830e+10 | 2.2777e+10 | 1.0232 | 1.0256 |
| mgc_superblue19 | 1.6429e+10 | 1.5560e+10 | 1.5542e+10 | 1.0559 | 1.0571 |

Raw data: `.claude/2_ARTIFACTS/lgdp44_v2_results.tsv` (stage 2),
`.claude/2_ARTIFACTS/full44_v2_suite_results.tsv` (stage 1).
Paper values: `.claude/2_ARTIFACTS/xplace_results/` (Liu et al., TCAD, DOI 10.1109/TCAD.2023.3346291).

### Our XPlace reproduction is trustworthy — with a tier caveat

On **ISPD2005** our own run matches the published table to ~0.1% (adaptec1 +0.02%, bigblue1
+0.001%). On **ISPD2015** it is looser: `mgc_fft_2` −3.2%, `mgc_des_perf_1` +1.8%. For that tier
prefer our own run as the comparison target — same machine, same seed, same LG/DP — which is what
the "vs ours" column does.

## 4. The outliers

**`mgc_matrix_mult_a` = 3.03× — genuinely broken, and it is what destroys the mean.**
GP stopped on `divergence_guard` at iteration **290** (every healthy design runs 750–920), and
XPlace scores the handed-over solution at `gp_ovfl_in = 0.9009` — essentially unspread. Post-DP
9.20e+09 against a 3.03e+09 reference. Excluding it, the mean falls to 1.0159 and tracks the
median. **This one design is the whole justification for quoting the median**, exactly as the
previous (unverifiable) snapshot also claimed.

**Three real ~5.6% gaps, none catastrophic, none explained:** `bigblue3` (1.0565, GP also ends on
`divergence_guard`), `mgc_superblue12` (1.0561), `mgc_superblue19` (1.0559). `superblue19` was
never a #23-affected design and its gap is **unchanged** by the fix, confirming it is an
independent issue.

**9 designs unscored** (`exit1_nodp` — XPlace's legalizer rejects the input): `mgc_des_perf_a`,
`mgc_des_perf_b`, `mgc_edit_dist_a`, `mgc_matrix_mult_b`, `mgc_matrix_mult_c`,
`mgc_pci_bridge32_a`, `mgc_pci_bridge32_b`, `mgc_superblue11_a`, `mgc_superblue16_a`. Same set as
the 08-07 run — this is TODO #22 (fence regions / `REGIONS`+`GROUPS`), **not** a regression and not
caused by #23.

## 5. Retraction — "median 1.0090 over 33 scored designs"

`summary.md` carried that figure citing `_NEW_REPORT_performance_snapshot_20260807.md`, **which
does not exist in `.claude/1_REVIEW/reports/`.** It could not be audited: which 33 designs were
scored, and which single design inflated its mean to 1.087, are unrecoverable from the notes.

Independently, its inputs were stale — `lgdp44_results.tsv` was computed from pre-#23 GP solutions
with roughly a third of the ISPD2015 tier frozen and one design carrying a 7.05e+09 post-DP HPWL.

The 1.0090 figure is **withdrawn**, not corrected — the two are not comparable. It covered 33
designs across all three tiers (including 16 MMS); this covers 19 ISPD designs only. **A combined
all-tier number is not computed here** and should not be assembled by averaging the two: MMS's
side still comes from `lgdp_suite_results.tsv`, which is valid (bookshelf `Sitewidth = 1` means
#23 cannot have changed it) but was scored under a different harness invocation.

## 6. What #23 changed

Four of five previously-dead designs now converge:

| design | before (08-07) | after |
|---|---|---|
| `mgc_superblue11_a` | `nan_metrics`, 2135 it | converged, 842 it |
| `mgc_superblue12` | `nan_metrics`, 2153 it | converged, 921 it |
| `mgc_superblue14` | `nan_metrics`, 2133 it | converged, 782 it |
| `mgc_superblue16_a` | `nan_metrics`, 2133 it | converged, 772 it |
| `mgc_des_perf_b` | `nan_metrics`, 2173 it | places, but `divergence_guard`, 889 it |

⚠️ HPWL rising on these is **not** a regression — the old number was the untouched initial
placement (all cells at die centre, artificially short wires, overflow 0.9998).

⚠️ `summary.md`'s claim that `mgc_des_perf_b` "converges in 825 iters" **does not reproduce** under
the manifest's own config; it reaches `divergence_guard` at 889. Treat as overstated pending
someone identifying which config produced it.

Designs never affected by #23 moved <1% (`mgc_fft_1` 4.041e8 → 4.065e8). Expected: ISPD2015 sites
are 100–200 DBU, so `seed × site_width` perturbs the trial step on every ISPD2015 design. MMS
staying bit-identical is the control.

Suite-wide stop reasons: **20 converged, 8 `divergence_guard`** (was 14/14 on 08-07).

## 7. Open, in priority order

1. **`mgc_matrix_mult_a` dies at iteration 290.** Single biggest quality defect in the tier.
2. **TODO #24 — `best_solution_pos` is one buffer shared by two trackers.** The
   `Restored … from iteration N` log line names a placement that is not the one shipped. Headline
   numbers above are unaffected (they are recomputed on the restored geometry), but the provenance
   line is false. → [[HANDOFF_24_best_solution_buffer_20260810.md]]
3. **8 of 28 stop on `divergence_guard`, not `converged`** — and did so on 08-07 too, so it is not
   a #23 artifact. Unexplained. A bigger question mark over ISPD2015 scores than the dead designs
   were.
4. **The three ~5.6% gaps** (`bigblue3`, `superblue12`, `superblue19`).
5. **TODO #22** — 9 unscored fence-region designs.

## 8. Reproduce

```bash
cd /home/msears/phd/AIEplace/vck5000        # cwd matters: configs use relative benchmark paths
$HOME/anaconda3/bin/python ../.claude/2_ARTIFACTS/gen_suite_configs.py \
    --suites ispd2005 ispd2015 --outdir /tmp/full44_v2/configs --results-root /tmp/full44_v2/results
SUITE_CFG=/tmp/full44_v2/configs SUITE_LOG=/tmp/full44_v2/logs \
SUITE_RES=$PWD/../.claude/2_ARTIFACTS/full44_v2_suite_results.tsv \
SUITE_PROG=/tmp/full44_v2/progress.txt bash ../.claude/2_ARTIFACTS/run_suite.sh

LGDP44_GP=/tmp/full44_v2/results LGDP44_OUT=/tmp/lgdp44_v2 \
LGDP44_RES=$PWD/../.claude/2_ARTIFACTS/lgdp44_v2_results.tsv \
    bash ../.claude/2_ARTIFACTS/run_lgdp44.sh
```

⚠️ Both scripts still default their output to `$REPO/2_ARTIFACTS/`, a path that no longer exists
(workflow dirs moved to `AIEplace/.claude/`). Pass `SUITE_RES` / `LGDP44_RES` explicitly.
Stage 1 ~2 h, stage 2 ~9 min. Both are resumable — a design already `done` in the TSV is skipped.
