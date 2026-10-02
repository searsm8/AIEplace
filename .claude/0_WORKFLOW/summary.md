# Summary — project status at a glance
*Updated 2026-08-28. Branch `pl_algo`. If this file and the code disagree, the code wins — say so.*
> **Soft cap — one in, one out.** Current state only, ~2 screens. To add a line, remove one:
> superseded snapshots & dated "Closed" narration → [[journal.md]]; finished task sections → [[history.md]].
> If it's done and no longer live context, it isn't "where things stand" — evict it.

## Two threads
- **sw_only** — CPU golden reference; goal is to match XPlace. **FROZEN 2026-08-17 (Mark's call).**
  Parity reached; no further algorithm/behaviour changes without an explicit decision. Cleanup,
  tooling, docs and tests are NOT frozen. `make test-regress` bit-identical is now the contract.
- **pl_algo** — move the whole placement iteration onto the PL. **The active thread**, starting at
  #20 step 1 (restore `dumpScheduleTrace()`; `sched_verify` currently validates a 07-18 golden and
  always will). Why freezing came first: pl_algo's algorithm is pinned to the 2026-07-14 sw_only,
  so every further sw_only change was another port.

## Where sw_only stands (frozen — this is the final state, not a waypoint)
- 🏆 **GOLDEN (ISPD) — median 1.0096, mean 1.0115, over ALL 28 ISPD designs**, legal-vs-legal,
  2026-08-25, 28/28. 22/28 within ±2%, better than XPlace on 6. Nothing unscored (#26).
  Archived: `.claude/2_ARTIFACTS/results/GOLDEN_sw_only_frozen_20260825/` — **read its README
  before quoting it**. Frozen commit `271d024` (the #35 cap landing). Supersedes the 2026-08-21
  golden (`GOLDEN_sw_only_frozen_20260821/`, banner added), built before the cap; the cap moved
  ISPD only +0.03 pp (net-neutral — it is a no-op except on macro-bearing td<1 designs, where
  gains and losses cancel), while winning −2.38 pp on MMS.
- **MMS (16 designs) — #35 CLOSED 2026-08-25, regression fixed.** Root cause was `#3`'s faithful
  scale (`Grid::clampFixedDensity`, `min(ρ,1)·td`) hurting macro/mixed-size convergence. Fixed by
  **landing experiment D**: reverted the fixed-density formula to the cap `min(ρ,td)` in all four
  sites (the two host copies since unified into one `capFixedDensity`, #36 2026-08-26) — a
  **deliberate, Mark-authorized divergence from XPlace** (registered in `CLAUDE.md` under
  "Deliberate divergences from XPlace"), worth **+2.38 pp of MMS mean** (D vs HEAD: 1.0347 →
  **1.0110**, cleanly isolated; D beats even pre-`#3` 1.0161). Isolating run
  `vck5000/results/DSE_20260824_161319/`. Regress baselines regenerated; td=1 `mms_adaptec1`
  bit-identical (the cap is a provable no-op at td=1, so no ISPD/td=1 design moved). #34's earlier
  metric-consistency fix was falsified as the cause but kept on its own merits. The not-taken
  keep-faithful-and-fix-the-fixed-node-field alternative is recorded in #35.
  ⚠️ `.claude/2_ARTIFACTS/results/DSE_20260814_152306/` stays undeleted — the only surviving
  pre-#3 reference now that the intermediate 2026-08-19 broken run has been pruned.
- Landed: two-phase mixed-size flow + LP macro legalization; #19's two XPlace faithfulness fixes
  (overflow excludes fillers; the γ/λ throttle gates on preconditioner κ). Both toggles retired
  2026-08-07 — faithful behaviour is now unconditional.
- **The preconditioner is ON for every design as of `3c70b38`** — `auto_enable_preconditioning` is
  gone. It had been OFF on all 28 ISPD designs since 638b9a8, which also froze `precond_coef` at
  1.0, which is the *only* thing that carries κ out of the γ/λ throttle window. Setting
  `enable_preconditioning = false` is now a diagnostic only, and a trap.
- **#23 — FIXED 2026-08-10: `init_step_seed` is in SITE WIDTHS, not raw DBU.** Committed `ba0ce6a`.
  **4 of the 5 dead ISPD2015 designs now converge** (`mgc_superblue11_a` 842 it, `12` 921, `14` 782,
  `16_a` 772 — all previously frozen at ~2135 iterations of `nan_metrics`). The 5th,
  `mgc_des_perf_b`, **places but does not converge** — `divergence_guard` at 889. XPlace prescales
  all coordinates by site width, so its `args.lr = 0.01` always meant 0.01 *sites*; ours meant
  0.01 DBU and underflowed. **Bookshelf `Sitewidth = 1` ⇒ the whole MMS suite is bit-unchanged**;
  only the two ISPD2015 regress baselines were regenerated.
  ⚠️ Units, **not** precision — scaling coordinates buys nothing in float32; shifts do (that is #15).
  ⚠️ Rising HPWL on the recovered designs is **not** a regression: the old number was the untouched
  initial placement (cells stacked at centre, overflow 0.9998).
  → [[REPORT_23_site_width_seed_20260810.md]]. Re-run + re-score **DONE 2026-08-10**
  → [[_NEW_REPORT_performance_snapshot_20260810.md]]
  <details><summary>Superseded: "`mgc_des_perf_b` converges in 825 iters, `mgc_superblue11_a` in 849"</summary>

  `mgc_des_perf_b` **does not reproduce** as converging: under the manifest's own config
  (`gen_suite_configs.py`, seed 42) it reaches `divergence_guard` at 889 iterations. Verified twice
  — standalone and in the 28-design suite. Which config produced the 825-iteration claim is
  unknown. `mgc_superblue11_a`'s iteration count also differs (842, not 849).
  </details>
- **#26 — fence regions: scored, measured, priced, and CLOSED** (2026-08-12). **Decision (Mark): we
  do NOT implement fence regions — we ignore them, as XPlace does, and say so.** ⚠️ `CLAUDE.md`
  carries only the two *operational* rules (how to regenerate `ispd2015_fix`, keep the fenced
  originals); the **decision and its reasoning live in `history.md` #26 and the report**, so this
  bullet is the always-loaded statement of it. Three things worth carrying forward:
  - **`ispd2015_fix` is GENERATED, not downloaded** — `cd ~/phd/Xplace/data && python3
    fix_ispd2015_route.py` builds all 20 from the raw data (a symlink to our own benchmarks).
    The regenerated `mgc_pci_bridge32_b` DEF is byte-identical to the copy its reference came from.
  - **We violate the fences badly: 59–94% of constrained cells land outside their region.** The
    contest's own legalized solutions score 0 of 190,010 through the same checker
    (`vck5000/tools/fence_check.py --expect-legal`), which is what makes that a measurement.
  - **~10 pp of our margin on those 9 is the constraint, not the placer** — we beat the contest's
    legal solutions by 2.6% on the 11 unfenced designs and 12.5% on the fenced 9.
  → [[_NEW_REPORT_26_fence_regions_20260811.md]]

## Where pl_algo stands — THE ACTIVE THREAD as of 2026-08-28
- **#41 WA gradient on a static pin-record stream (2026-09-22/23, bring_up).** DDR carries only
  32-bit records (`node_slot | offset_idx`, 16 per beat); positions and gradients live in 32-bank
  URAM. All random access is on chip, and the host precomputes a bank-conflict-free,
  hazard-spaced packing.
  - Four modules: `hpwl_computer_v2` and `hpwl_gradient_computer`, plus chunked `_v3` / `_v2` for
    designs over 1 M slots.
  - Verification: tier-1 exact (HPWL bit-exact, gradient ~4e-7), and C-synthesis II=1 on every loop.
  - All 44 designs encode; 8 need chunks.
  - The gradient beat loop is 3 DATAFLOW stages (`pin_bbox`/`wa_sums`/`wa_gradient`, bit-identical).
  - 17..96-pin nets, **on by default** (2026-10-02): HPWL and gradient done, chunked designs too
    (external slots double on bigblue4 / newblue7). Per-net bbox/sums travel A→B→C on side streams;
    RTL co-sim proves the FIFO bound, depth ≥ extent − 1 + skew (32). 97..100 dropped by decision.
    Open: MMS `max_rel` (Mark's call), post-route timing, URAM. → [[_NEW_REPORT_41_large_nets_in_chunks_20261002.md]]
  → [[_NEW_REPORT_41_record_datapath_20260922.md]], protocol in `vck5000/bring_up/beat_packer/README.md`.
- All datapath modules written, HLS C-synthesis clean, each verified against the sw_only golden.
- **v1 scope DECIDED (Mark, 2026-08-28):** phase-1 GP, device-resident, bit-comparable. **No phase 2,
  no backtracking** (deferred until needed). **pl_algo pins to the frozen sw_only HEAD.** (Third §10
  question — grid-1024 A/B vs per-design `-DPL_GRID` — still open.)
- Items re-filed here from the sw_only list on 2026-08-17 (marked **↪ pl_algo** in tasks.md):
  **#15** entirely (net-local frames — PL precision, expects no sw_only HPWL movement), **#23**'s
  initial-step mirror, **#19**'s live pre-#19 dff gate in `host/src/pl_algo/`, and **#6c** operator
  skipping (now an *Improvements* bullet, but wanted during step 6, not after).
- **#20 — compose Stage 5 (the resident loop = v1) LAST, after tier-1 coverage.** Progress
  2026-08-28: **step 1 DONE** — `dumpScheduleTrace()` restored in sw_only (config-gated, `make
  test-regress` bit-identical = proven no-op), fixture regenerated from the frozen HEAD (adaptec1,
  652 iters), so `sched_verify` now validates CURRENT sw_only and feeds **κ** (not the pre-#19 dff
  hack); the escalating `precond_coef` ladder is exercised. **step 2 core DONE** (schedule +
  convergence bit-exact); its divergence-conjunct / phase-relative / jolt items need a diverging or
  mixed-size fixture. **step 3 DONE** — the `formats.hpp` wall is broken (`b4130e6`: HLS includes
  guarded behind `#ifndef PL_TIER1_STUB`, no-op for the real build; `tier1_stub.hpp` supplies a
  `std::deque` `hls::stream`), unblocking all four modules. `make test` = **12 harnesses, 11 of ~14
  real modules** (`6cdcc8d` node_footprint/density_bin/force_gather/metrics/iteration_update; `4c0546b`
  spectral). **step 4 geometry pair DONE** (`d095a9f`): `node_footprint` drops the in-die shift and
  `iteration_update` clamps to the √2-expanded box — one coupled contract matching sw_only
  `computeNodeFootprint`/`enforceDieBoundaries`, `bin_w=die/GRID` so no new ABI scalar.
  **Step 4 CLOSED for v1** (2026-08-29): the movable-macro weight override (#11b) is TABLED —
  latent on every std-cell design (num_movable_macros==0) and needs the same host→PL kind flag as
  fillers, so it's folded into step 5; `node_footprint_test` [4] is the tripwire.
  **Next: sw_emu trajectory A/B** for the geometry change (tier-1 proves the formulation, not the
  end-to-end match), then step 5 (fillers + macro weight), then step 6 (compose the resident loop =
  v1, LAST). → [[_NEW_HANDOFF_20_pl_algo_stage3_20260828.md]]
- **`hpwl_gradient` de-gathered (P1b+P2, 2026-08-28, `21adad6`/`ed25f1a`) — the main win of the #20
  step 3b optimization thread.** `NodePin` now carries the ABSOLUTE pin position (`{x,y}`, replacing
  per-node `{off_x,off_y}`); a new `refresh_pin_pos` module folds `v_k` in once per iteration
  (`MODE_REFRESH_PINS`, II=1). All three random `node_pos` gathers left the HPWL datapath (and
  `metrics::hpwl_sweep`'s went too) — burst log confirms `gmem0` no longer appears in the HPWL path;
  `sweep_bbox` pipeline depth **146 → 73**. LUT +2.4%, BRAM/DSP/timing unchanged, tier-1 bit-identical.
  ⚠️ **Not yet load-bearing on device**: `Driver.cpp`'s `eval_gradients` does not yet issue
  `MODE_REFRESH_PINS` before `MODE_HPWL_GRAD`/`MODE_METRICS` — until it does, an on-device run
  silently evaluates the gradient at stale pin positions. Small fix, do it before any sw_emu run of
  the HPWL path. → [[_NEW_REPORT_20_hpwl_gradient_opt_20260828.md]]
- `top.cpp` is still a mode-switch bring-up scaffold; the host owns the γ/λ schedule, one
  round-trip per iteration.
- `make host HOST=pl_algo` needs one `make clean HOST=pl_algo` first (stale `.d`, not a source break).
- ⚠️ **pl_algo inherits sw_only's u-vs-v decision (settled by #32, closed 2026-08-17): track on `v`.**
  HPWL, overflow and the stored solution all describe the lookahead `v_k`, not the committed `u` —
  flagged in tasks.md #20 step 6, with the specific trap that `sched_verify` checks the schedule,
  not the geometry, so it cannot catch a wrong choice there.

## Open
- **#33 — the aux ACCEPT budget is hardcoded and has never been swept** (opened 2026-08-17, from
  #32). XPlace has **two** 0.5% budgets: an accept rule in `update_best_sol`
  (`param_scheduler.py:436` — ours is a hardcoded `1.005f` in `Output.cpp`) and the preference test
  #32 just settled (`:568` — our `aux_select_hpwl_ratio`). They are independent knobs that share
  a literal upstream. Next step is a cheap diagnostic (how often does the accept rule fire?) before
  spending another suite on it. ⚠️ Do **not** collapse the two onto one config value — that asserts
  an equality XPlace does not.
- **#3 — fixed-density: the CAP `min(ρ,td)` is current, and it is a KNOWING divergence.** #3's
  cap→scale close (08-17) was reverted by **#35** (08-25, Mark-authorized, −2.38 pp MMS mean) and
  the two host copies collapsed onto `capFixedDensity` by **#36** (08-26). ⚠️ Do not "restore
  faithfulness" here — see `CLAUDE.md`'s divergence registry first. *(The old scale entry said the
  opposite and stood for 2 days after #35; evicted to journal.md 2026-08-27.)* #3's remaining open
  item is the **per-row site model** (ragged cores on 11 of 16 MMS designs), unrelated.
- **#37 — the "macro-excluded" overflow was never macro-excluded** (landed 2026-08-27). It ran
  after the phase-2 freeze, when the macros are FIXED and the flag matches nothing, so it equalled
  the plain exact overflow while `benchmarks.py` aimed the XPlace Mixed-GP comparison at it. Now
  measured at the phase-1 checkpoint, after the best-solution restore, like XPlace's. mms/adaptec1
  **0.118 → 0.0702** vs XPlace 0.1306. ⚠️ **Macro-excluded comparisons in history.md are retracted,
  not stale** — re-derive before quoting. Phase 1 HPWL/overflow rows also shift (they now describe
  the shipped placement, not the last iterated one). Bit-identical on all three regress designs.
- **#38 — is `MacroLegalize.cpp` (~600 lines) redundant?** XPlace re-legalizes our macros itself on
  every scored run (`detail_placement.py:374`, unconditional in `run_lg`), so it earns nothing at
  scoring time — but it runs *inside* phase 2 and conditions the GP result, so deleting it is not
  free. `macro_legalization = true|false` A/B over MMS decides it. See tasks.md #38.
- **#42 — does a better chunk partitioner pay off end to end (opened 2026-10-02)?** The current one is
  a BFS cut (not min-cut); external slots are 5–40% on the 8 chunked designs (large nets doubled them). Measure host
  start-up and the per-iteration mailbox + fold cycles first; close it if that share is small.
- **#41 — keep in mind for the resident loop (2026-10-01):** standalone, loading positions and
  draining gradients is >=26 K cycles vs a 51.5 K-cycle beat loop per axis (adaptec1); the resident
  loop must keep both in URAM (256 of 463 URAMs per axis). Chunked designs (8/44) keep per-chunk DDR
  traffic. m_axi bundles are grouped by width. → DATAFLOW.md, [[_NEW_REPORT_41_ddr_bundles_20261001.md]]

## Also open
- **#21 — repo restructure** (host to top level, one host binary). Proposal only, nothing started.
  **Merge `origin/geert` before anything else** — one `.gitignore` conflict today, 25 hand-moved
  files after.

