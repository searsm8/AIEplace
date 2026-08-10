# TODO #11 density-footprint A/B — results (2026-07-31)

MMS suite, seed 42, XPlace per-design grid + target density. Ran 18:37 Jul 30 → 00:52 Jul 31,
**46 of 48 runs**. Raw: `2_ARTIFACTS/footprint_ab_results.tsv`; scorecard:
`python3 2_ARTIFACTS/analyze_footprint_ab.py`.

## Verdict

| | outcome | recommendation |
|---|---|---|
| **#11a `xplace_die_projection`** | exactly neutral: mean **+0.0%** HPWL over 15 designs, worst case 0.7%, exact overflow unchanged | **ADOPT.** Free faithfulness + code deletion |
| **#11b `macro_td_expand_ratio`** | mean **+5.2%** HPWL *worse* over 7 designs, worse on 5/7, and worst exactly where it was meant to help | **REJECT.** Delete the branch |

`both` ≈ `macro` on every design (within 0.2%), confirming the toggles are independent and that
#11a contributes nothing on top of #11b.

## #11a — neutral, adopt

15 designs, mean +0.0%, range −0.7% (bigblue3) to +0.5% (adaptec3). Exact overflow moves by at most
0.003. This is the predicted result: the projection only changes cells within `(cw−w)/2` of the die
edge, a vanishing fraction of any real design.

So the decision rests on code, not numbers — and there it is a clear win. Adopting it lets us delete
the movable branch of `computeNodeFootprint` (the deposit-time shift) entirely, removes a documented
divergence from XPlace's `trunc_node_pos_fn`, and puts the in-die correction in one place (the
position) instead of two (position + deposit).

## #11b — harmful, reject

| design | td | base HPWL | macro HPWL | Δ HPWL | base ovfl_exact | macro ovfl_exact |
|---|---|---|---|---|---|---|
| adaptec5 | 0.5 | 3.246e8 | 3.828e8 | **+17.9%** | 0.413 | 0.401 |
| newblue5 | 0.5 | 4.412e8 | 4.908e8 | **+11.2%** | 0.352 | 0.337 |
| newblue4 | 0.5 | 2.425e8 | 2.572e8 | **+6.1%** | 0.356 | 0.316 |
| newblue1 | 0.8 | 6.152e7 | 6.386e7 | +3.8% | 0.168 | 0.177 |
| newblue2 | 0.9 | 1.562e8 | 1.600e8 | +2.4% | 0.102 | 0.129 |
| newblue6 | 0.8 | 4.158e8 | 4.123e8 | −0.8% | 0.128 | 0.156 |
| newblue3 | 0.8 | 2.449e8 | 2.339e8 | −4.5% | 0.236 | 0.255 |

**The three worst results are the three target-density-0.5 designs** — adaptec5, newblue5, newblue4
— which are precisely the macro-heavy under-spreaders from TODO #4 that this was hypothesised to
help. It makes them substantially worse.

There is a real effect underneath, just not a good trade. On those same three designs #11b *does*
improve physical spread (adaptec5 0.413→0.401, newblue4 0.356→0.316, newblue5 0.352→0.337) — letting
macros deposit less density does let the standard cells spread. But it buys 0.01–0.04 of overflow for
6–18% of wirelength. On the other four designs it loses on both axes.

Worth recording *why* it looked promising early and isn't: the smoothed overflow (the convergence
signal) drops dramatically under #11b — newblue1 0.127→0.031 — because macros deposit 0.8× their
area into the very metric that decides when to stop. Every #11b run halted 25–65 iterations early on
a deflated signal. Had the override been left applying to the exact-overflow diagnostic too (it was,
until I gated it inside the clamp branch during prep), this table would have shown a sweeping win on
overflow and the regression would have been invisible.

## Data quality — read before reusing this

**A rebuild landed mid-sweep.** `aieplace_sw_only.exe` was rebuilt at **00:05:05 Jul 31**, 47 minutes
before the sweep finished, by the session implementing the OpenMP threading (TODO #12). Two runs
started after that point and therefore used a *different binary* from the other 44:

- `newblue7_macro` (started 00:14), `newblue7_both` (started 00:33)

**newblue7 is excluded from every conclusion above** — its `base` arm is also one of the two missing
runs, so the analyzer had no baseline to compare against and dropped it automatically. No result in
this report mixes binaries. But the raw TSV does contain those two rows; do not use them.

**Missing (46/48):** `adaptec5_both`, `newblue7_base`. adaptec5 still has base/proj/macro, which is
what the verdict uses. The runner is resumable — re-running it fills only the gaps — but both gaps
would now execute on the threaded binary, so they are only comparable if the threading is confirmed
bit-identical to the pre-threading golden first.

**Process lesson:** the sweep pinned the design, seed, grid and config, but not the binary. A long
sweep should snapshot the executable it launches with (copy it aside, or record its hash per row).

## Broader observation

Only **6 of 15** designs reached an honest exact overflow (≤0.085), and all six are the
target-density-1.0 designs. Every td<1.0 design finished at 0.10–0.41 exact overflow — badly
under-spread — regardless of arm. That is TODO #4's open problem, and it dominates any effect either
toggle has. Neither of these toggles addresses it, and #11b's apparent overflow improvements on the
0.5-density designs are small next to how far from converged they all are.

## Next

1. Adopt #11a: make the projection unconditional, delete the deposit-time shift branch and the
   `xplace_die_projection` key.
2. Reject #11b: delete `macro_td_expand_ratio`, the `Grid` plumbing, and — check first — possibly
   `tagMovableMacros()` / `Node::m_is_movable_macro`, which exist only to serve it. That leaves
   `num_movable_macros` (auto-preconditioner) as the single macro rule again, closing the
   two-definitions note in TODO #11.
3. Re-verify the golden after both edits; #11a changes results by design, so the pre-#11 golden will
   not match — capture a new one deliberately rather than treating the diff as a regression.
