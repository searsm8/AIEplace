# REPORT #24 — one snapshot buffer, three trackers, and what the spread actually costs

*2026-08-10, branch `pl_algo`. Closes the code side of TODO #24. Two decisions left open at the
bottom — read those before building on this.*

---

## TL;DR

1. **#24 defect 1 is real and is fixed.** `Node::best_solution_pos` was one buffer written by two
   trackers; selection picked by *metadata* and restored whatever geometry was last written. Now
   there are three trackers, each with its own buffer, and one selection rule they all share.
2. **#24's original evidence was misattributed.** The "final overflow matches iteration 757" tell
   was a *different* bug — density is measured at `probe_pos`, which the restore never touched.
   The real evidence for defect 1 is the **HPWL** mismatch. Both bugs are fixed; only one was known.
3. **We now match XPlace's selection rule**, including the `best_sol_aux` preference and the
   `best_sol_rollback` net we never had. It ships the spread-out placement only when it is nearly
   free — 8 of 19 converged designs, not all of them.
4. **A/B on the 0.5% budget: 1.005 wins.** Loosening it to 1.010 buys ~35% lower overflow for
   ~0.5–0.7% GP HPWL; DP recovers 41–74% of that cost but never all of it. **Keep XPlace's 1.005.**
5. `make test`, `make test-regress`, `make test-regress-slow` all green. Three baselines
   regenerated with reasons.

---

## 1. What was actually broken

### Defect 1 — one buffer, two writers

`Node::best_solution_pos` was the only snapshot storage. Both `best_primary` and `best_fallback`
wrote it through `snapshotBestPlacement()`, so **last writer won**. `restoreBestSolution()` then
selected by metadata (primary > fallback), logged that metadata, and restored whatever geometry
happened to be in the buffer.

Measured across all 29 runs of the 2026-08-10 `full44_v2` suite:

| logged overflow | count | shipped == logged? |
|---|---|---|
| ≈0.069 (converged, at the threshold crossing) | 17 | **no** |
| >0.07 (never converged — `best_primary` never valid) | 11 | yes, by construction |
| no best restored | 1 | — |

**17 of 29 shipped a placement that was not the one logged, and they are exactly the converged
designs** — the ones that get scored. The mechanism: the primary latches at the threshold crossing
(~0.069) and never improves again because HPWL rises monotonically from there; the fallback keeps
improving overflow for another ~30 iterations and overwrites the shared buffer.

**Evidence (adaptec1, `full44_v2`).** `Final HPWL` is genuinely computed on the restored positions:

```
log:            Restored primary (converged) ... from iteration 728   (HPWL 7.035e+07)
reported:       Final HPWL 7.051e+07
iterations.dat: iter 751  HPWL 7.051e+07  OVFW 4.221e-02   <- exact match, and a new overflow low,
                                                              so best_fallback wrote the buffer here
```

### Defect 2 — a placement is two variables, and the snapshot copied one

This one was **not** in the original TODO, and it is what produced the TODO's stated evidence.

A node's state carries both a committed position and a lookahead, and the two metrics deliberately
read different halves ([`Net.h:18`](../../../vck5000/host/src/common/include/Net.h) says so):

- **HPWL** → `NetPin::getPos()` → `node_p->getPos()` → `next.node_pos`
- **density / overflow** → `computeNodeFootprint` → `getProbeX()/getProbeY()` → `next.probe_pos`
  ([`Grid.cpp:36`](../../../vck5000/host/src/common/src/Grid.cpp))

`snapshotBestPlacement`/`restoreBestPlacement` handled only `node_pos`. So after a restore the node
sat in a state **that existed at no point during the run** — committed position from iteration A,
lookahead from iteration B. A torn read. Every downstream metric then silently reported on whichever
half it depended on, which is why the symptom was so confusing: `Final HPWL` correct and
`Final Overflow` wrong, from the same object, neither obviously broken alone.

**This is why TODO #24's cited evidence does not prove what it says it proves:**

```
adaptec1: STOP iteration=757
          Final Overflow (smoothed) 3.746e-02  ==  iterations.dat iter 757, the LAST row
```

The overflow matched the final iteration because the *probe* positions were the final iteration's —
not because the fallback's geometry had been restored. The TODO's falsifier ("after the fix the
restore line and `Final Overflow` must agree") is therefore only meaningful on designs where the
selected solution is **not** the last iteration.

**Someone already found this bug and fixed it locally.** [`DataBase.cpp:396`](../../../vck5000/host/src/common/src/DataBase.cpp),
inside `freezeMovableMacros`:

> Collapse all four state fields onto the committed position. This is NOT bookkeeping:
> `restoreBestPlacement()` writes only `next.node_pos` … so `probe_pos` would keep phase 1's last
> lookahead value forever. `computeNodeFootprint` deposits at the PROBE position, so the macro's
> density would land somewhere it no longer is.

Correctly diagnosed, repaired for frozen macros only, never generalised. Fix (B) generalises it.

---

## 2. What landed

### Three trackers, mirroring XPlace 1:1

| ours | XPlace | gate | accept rule |
|---|---|---|---|
| `best_primary` | `best_sol` | `ovfl < stop` | lowest HPWL |
| `best_aux` | `best_sol_aux` | `ovfl < stop` | overflow must improve; HPWL may creep ≤0.5% per update |
| `best_rollback` | `best_sol_rollback` | `stop ≤ ovfl < 5*stop` **and** never converged | overflow improves; HPWL ≤1% |

Each has its own `Position` buffer in `Node`. `ever_converged` carries rollback's
free-on-first-convergence lifetime (XPlace spells this `life < max_life`,
`param_scheduler.py:396-405`) — deliberately **not** reused from our `life`, which is a different
quantity: a divergence-guard budget burned 6 at a time.

The old `best_fallback` was doing two of XPlace's three jobs at once — it had **no
`overflow < stop_overflow` gate**, so it was simultaneously the aux and the rollback. That is why
the priority question looked like "guard vs. real answer" when in XPlace it is not one.

### One selection rule, shared by every consumer

`selectBestSolution()` ports `get_best_solution` (`param_scheduler.py:540-577`) and returns the
solution *and* its slot in one struct, so metadata and geometry cannot diverge again. All four
consumers use it: the restore, the CSV row, the summary table, and the phase-2 macro freeze.

`bestReference()` is kept **separate and unchanged in role** — it is the divergence guards'
reference metric, it feeds stopping criteria, and widening or narrowing it would move trajectories.

### Fix (B) — restore leaves a coherent state

`restoreBestPlacement` now sets `probe_pos = node_pos`, matching what `freezeMovableMacros` already
did. The `.def` is written from `node_pos`, so the reported metrics now describe the placement
actually shipped.

**Verified on the case that exposed it** (`mgc_pci_bridge32_b`, restores iter 723 of 752):

```
              restored:  iter 723, overflow 0.069975
before (B):   Final Overflow (smoothed) = 6.086e-02    <- iteration 752, 29 iterations away
after  (B):   Final Overflow (smoothed) = 6.849e-02    <- the restored placement
```

The residual 0.0685 vs 0.0700 is the probe-vs-committed lookahead offset within one iteration —
expected, and small. `.def` hashes byte-identical across (B), confirming no geometry moved.

---

## 3. Verification

| check | result |
|---|---|
| `make test` (pl_algo tier-1) | PASS |
| `make test-regress` | PASS (baselines regenerated) |
| `make test-regress-slow` | PASS (baseline regenerated) |
| adaptec1 end-to-end | ships aux iter 757, `Final HPWL 7.056e+07` == trace row 757 exactly |
| `mgc_pci_bridge32_b` end-to-end | ships primary iter 723, `Final HPWL 6.534e+08` == its solution |

Defect 1 is verified on **both** branches of the selection rule — a design where the aux wins and a
design where the primary wins — not just the one that motivated the ticket.

**Trajectories are unchanged** on both ISPD regress designs (verified bit-identical *before* the
baselines were regenerated; only final positions moved). The one exception is MMS — see the open
decision below.

---

## 4. The rule discriminates — it does not always ship the spread solution

This is the part most likely to be misremembered, so it is stated plainly.

XPlace prefers the aux only when `aux_hpwl < best_hpwl*1.005` **and** `aux_ovfl*1.1 < best_ovfl`.
Projected over all 29 traces: **aux 8, primary 11, no converged solution 10.**

Two worked examples:

```
adaptec1            spread 0.069 -> 0.037 costs 0.23% HPWL   -> AUX   (nearly free)
mgc_pci_bridge32_b  spread 0.070 -> 0.061 costs 9.7%  HPWL   -> PRIMARY
```

Today's *bug* shipped the spread solution on essentially every converged design. The faithful rule
ships it on 8 of 19. So relative to the buggy behaviour, this change makes 11 designs **less**
spread — deliberately.

---

## 5. A/B: is XPlace's 0.5% budget right?

`best_aux_max_hpwl_ratio` was exposed as a config parameter (default 1.005 = XPlace's literal; read
with `value_or` so the frozen regress configs keep working) and swept via
`DSE_RUN_SET=best_sol_ab`, 8 designs × 2 arms.

Subset: 4 designs projected to flip at 1.010 plus 4 controls that must not move — including
`mgc_des_perf_1`, which holds still because it fails the *overflow* gate rather than the HPWL one,
pinning the other condition. **All four controls came back byte-identical across arms.**

### GP result

| design | 1.005 | 1.010 | ΔHPWL | ΔOVFW |
|---|---|---|---|---|
| `mgc_superblue19` | 757 · 0.0699 | 786 · 0.0456 | +0.702% | −34.8% |
| `mgc_superblue16_a` | 743 · 0.0696 | 772 · 0.0455 | +0.524% | −34.6% |
| `bigblue2` | 820 · 0.0696 | 849 · 0.0451 | +0.530% | −35.2% |
| `adaptec3` | 797 · 0.0412 | 797 · 0.0412 | — | — |

### After XPlace's own LG + DP — 1.010 loses

| design | arm | GP HPWL | after DP | |
|---|---|---|---|---|
| `bigblue2` | 1.005 | 1.331033e8 | **1.377351e8** | |
| | 1.010 | 1.338237e8 | 1.379256e8 | **+0.138%** |
| `mgc_superblue19` | 1.005 | 1.615452e8 | **1.636228e8** | |
| | 1.010 | 1.626703e8 | 1.642933e8 | **+0.410%** |

```
bigblue2         GP +0.541%  ->  DP +0.138%     74% of the cost recovered
mgc_superblue19  GP +0.696%  ->  DP +0.410%     41% recovered
```

**Verdict: keep 1.005.** "More spread legalizes better" is real and measurable — DP recovers most of
the penalty — but it never recovers all of it.

### Three caveats

1. **n = 2.** Only two designs both flipped *and* produced DP numbers. `mgc_superblue16_a` flipped
   but died `exit1_nodp` in both arms (fence regions, #22).
2. **The offline projection is unreliable near the budget.** `adaptec3` was projected to flip and
   did not: `iterations.dat` stores HPWL at 4 significant figures, so its ratio `1.893e8/1.883e8` is
   only known to lie in ~[1.0048, 1.0058] — it straddles the threshold. The projection is sound well
   away from 0.5% and worthless within ~±0.1% of it, which is exactly the interesting population.
   **Do not re-derive flippers from traces; run them.**
3. **Our overflow metric and XPlace's disagree on direction for one design.** On `mgc_superblue19`
   our `Best OVFW` says the 1.010 placement is 35% better spread (0.0699 → 0.0456) while XPlace's
   exact overflow on the *same* `.def` says slightly worse (0.1783 → 0.1816). `bigblue2` moves as
   expected (0.1321 → 0.1024), so it is not systematic. Not a reporting artifact — these `.def`s
   predate fix (B), and (B) changes no geometry. **Worth its own investigation before our overflow
   number is used to argue placement quality.**

---

## 6. Open decisions

### 6a. Fix (B)'s scope — an MMS re-run hangs on this

`restoreBestPlacement` also fires **mid-run**, at the phase-2 macro freeze. Fix (B) therefore
perturbs every mixed-size run. On `mms_adaptec1` the phase-2 restart moves in the fourth digit:

```
iter 667   step_len 2.565e+03 -> 2.566e+03    density_weight 1.442e-10 -> 1.443e-10
           HPWL and overflow identical
```

which then amplifies chaotically over ~620 remaining iterations:

| | before | after (B) |
|---|---|---|
| iterations | 1325 | 1288 |
| final HPWL | 6.367e+07 | 6.382e+07 (+0.24%) |
| final overflow | 4.002e-02 | 4.115e-02 (+2.8%) |

Chaotic divergence from a rounding-scale perturbation, not a quality regression — and n=1 on a
620-iteration chaotic phase cannot establish direction.

- **(a) Keep it everywhere** — *what is committed*. Simpler rule: a restore always leaves a coherent
  state. Cost: **the MMS suite must be re-run and re-baselined.** Note MMS was excluded from
  `full44_v2` because #23 provably could not affect it; (B) can, so that exclusion no longer holds.
- **(b) Confine the probe reset to the final restore** — MMS stays bit-identical, no MMS re-run, and
  the fix is provably scoped to what gets reported. One-line change.

**Recommendation: (b).** The whole justification for (B) is that *reported* metrics should describe
the shipped placement, which is a final-restore concern; mid-run the perturbation buys nothing
measurable and costs a re-baseline. (a) is committed only because it is what was literally asked
for. Reversal is one line plus regenerating `mms_adaptec1`.

### 6b. Widen the A/B?

n=2 is thin for a default that governs every design. Getting more usable flippers means running
~8–10 more designs blind, since the trace projection cannot identify them near the boundary.

---

## 7. Files

Code: `Node.h`, `AIEplace.h`, `AIEplace.cpp`, `Output.cpp`, `Phase2.cpp`, `Schedule.cpp`,
`Setup.cpp`, `default_config.toml`, `tools/dse.py` (adds `DSE_RUN_SET=best_sol_ab`).

Baselines regenerated with reasons: `mgc_fft_a`, `mgc_pci_bridge32_b` (selection change),
`mms_adaptec1` (fix (B), pending 6a).

Data, not committed (`/tmp`, will not survive): sweep at
`vck5000/results/DSE_20260810_173906/`, LG+DP at `/tmp/lgdp_ab/results_{1005,101}.tsv`.
**If the A/B is to be cited later, move those two TSVs into `.claude/2_ARTIFACTS/`.**

Related: [[_NEW_HANDOFF_24_best_solution_buffer_20260810.md]] (the handoff that opened this),
[[_NEW_REPORT_performance_snapshot_20260810.md]] (the re-baseline these numbers sit alongside).
