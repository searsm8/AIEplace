# HANDOFF — #24 best-solution buffer, and the 44-design re-baseline in flight

*Written 2026-08-10 ~15:55 EDT. Branch `pl_algo`. Session started from a question about the
XPlace paper's benchmark tables and ended somewhere else entirely.*

---

## TL;DR for whoever picks this up

1. **A re-baseline is running right now** (28 ISPD designs, sw_only GP). It was started because
   the previous snapshot's GP inputs predate the #23 fix by three days. **It is not finished** —
   see *State of the run in flight* for exactly how to check and what to do next.
2. **#23 is confirmed working** on a genuinely dead design: `mgc_superblue14` went from frozen
   (`nan_metrics`, 2133 iters, HPWL 3.906e+10) to **converged** in 782 iters at 2.305e+10.
3. **New defect found — #24.** The `Restored … from iteration N` log line names a placement that
   is not the one shipped. Headline numbers are fine; the provenance line is not.
4. **`summary.md`'s "median HPWL ratio 1.0090 over 33 scored designs" should not be quoted** until
   the re-baseline lands. Its evidence file is missing and its inputs were stale. Details below.

---

## 1. What was committed

`ba0ce6a` — *sw_only: init_step_seed is in site widths, not raw DBU (closes TODO #23)*

Scoped deliberately to the #23 fix: `DataBase.h`/`.cpp` (adds `getSiteWidth()`), `Step.cpp`,
`Setup.cpp`, `default_config.toml`, and `test/regress/` (regenerated ISPD2015 baselines plus the
`baselines/ → golden/` rename, which had to ride along or `run_regress.sh` breaks mid-commit).
`make test-regress` passes bit-identical.

⚠️ **The working tree still holds ~19 other modified files** that were already there when this
session started — pl_algo (`Driver.cpp`, `Placement.hpp`, `main.cpp`, `param_scheduler.hpp`,
`DATAFLOW.md`), `sched_verify.cpp`, `synth_check.cpp`, `CLAUDE.md`, `summary.md`, `tasks.md`, and
two deleted `footprint_ab` reports. **None of it was touched.** It is Mark's to sort; do not assume
it belongs with #23.

One thing rode along unavoidably: `DataBase.cpp` also carries the TODO #17 `readDEF` diagnostic
(same file, already-closed work). Noted in the commit message.

---

## 2. Why the re-baseline was necessary (the part that is easy to miss)

`/tmp/full44/results` **survived** from 2026-08-07 — which is exactly the trap. Every GP solution
in it is dated 08-07, three days *before* the #23 fix. `run_lgdp44.sh` consumes that tree.

**So re-running only stage 2 would have re-legalized the same frozen placements and produced the
same garbage with a fresh timestamp.** That is a two-stage pipeline and only stage 2 is cheap:

```
stage 1  run_suite.sh   sw_only GP           -> /tmp/full44_v2/results/**/*.def   (hours)
stage 2  run_lgdp44.sh  XPlace's own LG+DP   -> lgdp44 tsv                        (minutes)
```

It also explains the numbers that looked insane in `lgdp44_results.tsv`: `mgc_superblue12`'s
7.05e+09 post-DP HPWL was not a bad placement, it was XPlace's legalizer being handed a pile of
cells stacked at die centre.

**MMS was deliberately skipped.** Bookshelf carries `Sitewidth = 1`, so #23 cannot change those
trajectories, and `mms_adaptec1`'s baseline is bit-identical proving it. Re-running 16 MMS designs
for provably identical output buys nothing. The MMS side of any combined score must therefore come
from the existing `lgdp_suite_results.tsv`.

### Two gotchas that cost time here

- `run_suite.sh` resolves the config's `benchmark = "host/benchmarks/…"` **relative to cwd**. Launch
  it from `vck5000/`, not the repo root, or all 28 designs abort instantly with `ec=134`.
- `run_suite.sh` and `run_lgdp44.sh` both still default their output to `$REPO/2_ARTIFACTS/`, i.e.
  `vck5000/2_ARTIFACTS/` — which no longer exists; the workflow dirs moved to `AIEplace/.claude/`.
  Pass `SUITE_RES` / `LGDP44_RES` explicitly. Worth fixing in the scripts.

---

## 3. State of the run in flight

Launched 14:49 EDT, sequential, seed 42, `deterministic = true`.

```
configs   /tmp/full44_v2/configs        (28: 8 ispd2005 + 20 ispd2015)
results   /tmp/full44_v2/results        (the .def tree stage 2 needs)
logs      /tmp/full44_v2/logs
progress  /tmp/full44_v2/progress.txt
tsv       .claude/2_ARTIFACTS/full44_v2_suite_results.tsv
```

**23 of 28 done at time of writing.** Remaining: `mgc_superblue16_a`, `mgc_superblue11_a`,
`mgc_superblue12`, `bigblue3`, `bigblue4`. Three of those four superblues are previously-dead #23
designs, so they are the substantive part of the test — do not draw conclusions before they land.

```bash
grep -c '' /home/msears/phd/AIEplace/.claude/2_ARTIFACTS/full44_v2_suite_results.tsv   # rows+1
tail -3 /tmp/full44_v2/progress.txt
```

The runner is **resumable** — a design already marked `done` in the TSV is skipped, so if it died
just relaunch it from `vck5000/` with the same env vars.

### Then run stage 2

```bash
cd /home/msears/phd/AIEplace/vck5000
LGDP44_GP=/tmp/full44_v2/results \
LGDP44_OUT=/tmp/lgdp44_v2 \
LGDP44_RES=/home/msears/phd/AIEplace/.claude/2_ARTIFACTS/lgdp44_v2_results.tsv \
  bash /home/msears/phd/AIEplace/.claude/2_ARTIFACTS/run_lgdp44.sh
```

Expect the 6 `exit1_nodp` designs (`mgc_des_perf_a`, `mgc_edit_dist_a`, `mgc_matrix_mult_b/c`,
`mgc_pci_bridge32_a/b`) to **still** fail — that is TODO #22 (fence regions), not #23. Do not read
it as a regression.

### Early results, for orientation only

Previously-dead designs, old (08-07) → new:

| design | before | after |
|---|---|---|
| `mgc_superblue14` | `nan_metrics`, 2133 it, 3.906e+10 | **converged**, 782 it, 2.305e+10 |
| `mgc_des_perf_b` | `nan_metrics`, 2173 it, 3.742e+08 (frozen) | `divergence_guard`, 889 it, 1.607e+09 |

Note `mgc_des_perf_b` **places now but does not converge** — `summary.md` claims it "converges in
825 iters". That claim is not reproducible under the manifest's own config; treat it as overstated
until someone re-checks which config produced it.

Unaffected designs moved by <1% (e.g. `mgc_fft_1` 4.041e8 → 4.065e8). That is expected and is the
fix working: ISPD2015 sites are 100–200 DBU, so `seed × site_width` perturbs the trial step on
*every* ISPD2015 design, not just the frozen five. MMS staying bit-identical is the control.

---

## 4. #24 — the finding

Full statement lives in `tasks.md` #24. The short version and the reasoning trail:

**One buffer, two writers.** `Node::best_solution_pos` (`AIEplace.cpp:107`) is the only snapshot
storage, and both `best_primary` and `best_fallback` write it through `snapshotBestPlacement()`
(`Output.cpp:670-685`). `restoreBestSolution()` (`Output.cpp:414`) picks by *metadata* priority and
logs that metadata, then restores whatever geometry is in the buffer.

**How it was caught.** adaptec1's log says it restored iteration 728 (overflow 0.069424), but the
run's own `Final Overflow (smoothed)` — recomputed on the restored positions at `Output.cpp:455` —
is `3.746e-02`, which is *exactly* iteration 757's value in `iterations.dat`. Reproduced on
`mgc_matrix_mult_c`.

**Correction to an earlier conclusion in this session, recorded because it is the useful part:**
the log line was first read at face value and taken as evidence that sw_only latches onto the
marginal solution at the overflow threshold and ships an under-spread placement to XPlace's
legalizer. **That conclusion was wrong.** The geometry shipped is the well-spread one; only the
label is wrong. The tell was that `Final Overflow` and the restore line disagreed — *always check
the recomputed metric against the provenance line before believing either.*

**What is still open and genuinely XPlace-divergent** (defect 2 in the TODO): we have no
`best_sol_aux` equivalent, and `best_fallback`'s accept rule is inverted against XPlace's. Real,
but **defect 1 confounds measuring it**, so fix the buffer first.

---

## 5. `summary.md` needs correcting

- It cites `[[_NEW_REPORT_performance_snapshot_20260807.md]]` as the evidence for "median HPWL ratio
  1.0090 over 33 scored designs". **That file does not exist** in `.claude/1_REVIEW/reports/`. The
  number could not be audited this session — which of the 33 were scored, or which single design
  inflated the mean to 1.087, is unrecoverable from the notes.
- The underlying `lgdp44_results.tsv` was computed from pre-#23 GP inputs with roughly a third of
  the ISPD2015 tier dead, so the figure is stale regardless.
- Its #23 bullet ("`mgc_des_perf_b` converges in 825 iters") does not reproduce — see above.

**Do not delete the 1.0090 line — retract it in place** per the tasks.md procedure, once the
re-baseline gives a replacement.

---

## 6. Suggested order of work

1. Wait out stage 1; run stage 2. Score it. Write the report `summary.md` is missing.
2. Fix #24 defect 1 (per-tracker buffers), re-run one design, confirm the restore line and
   `Final Overflow` agree.
3. Only then look at #24's XPlace `best_sol_aux` gap.
4. Unrelated but noticed: **6 of 7 early ISPD2015 designs stop on `divergence_guard`, not
   `converged`** — and did so on 08-07 too, so it is not a #23 artifact. Nobody has explained it.
   It is a bigger question mark over ISPD2015 scores than the five dead designs were.

## Reference — what the paper actually reports

Exported verbatim to `.claude/2_ARTIFACTS/xplace_results/` (Tables I/II/III from
Liu et al., TCAD, DOI 10.1109/TCAD.2023.3346291): benchmark statistics, ISPD2005 HPWL/runtime, and
ISPD2015 HPWL/overflow/runtime for 5 methods. The `README.md` there records the stage caveat —
paper numbers are post-GP+LG+DP, and the ISPD suite is **not** the mixed-size MMS suite sw_only is
tuned on. Our own 2026-08-07 XPlace reproduction matches the published table within ~0.1%, so it is
a trustworthy stand-in where the paper does not cover a design.
