# Summary — project status at a glance
*Updated 2026-10-16. Branch `pl_algo`. If this file and the code disagree, the code wins — say so.*
> **Soft cap — one in, one out.** Current state only, ~2 screens. To add a line, remove one:
> superseded snapshots & dated "Closed" narration → [[journal.md]]; finished task sections → [[history.md]].
> If it's done and no longer live context, it isn't "where things stand" — evict it.

## Two threads
- **sw_only** — CPU golden reference; goal is to match XPlace. **FROZEN 2026-08-17 (Mark's call).**
  Parity reached; no further algorithm/behaviour changes without an explicit decision. Cleanup,
  tooling, docs and tests are NOT frozen. `make test-regress` bit-identical is now the contract.
  **One authorized exception (#49, Mark, 2026-10-16):** the HPWL metric sums in double (it read
  0.39% low on bigblue4 in float). Post-DP HPWL unchanged (+0.015% mean, 13 large designs); reported GP
  HPWL now correct, up to +0.46% on bigblue4. Regress baselines regenerated. [[REPORT_49_hpwl_double_sum_20261016.md]]
- **pl_algo** — move the whole placement iteration onto the PL. **The active thread**, now as
  **#45**: integration restarts in a new top-level `/AIEplace/pl`, slot order everywhere; the
  `vck5000/` tree becomes read-only legacy until it is deleted. Why freezing came first: pl_algo's
  algorithm is pinned to sw_only, so every further sw_only change was another port.

## Where sw_only stands (frozen — this is the final state, not a waypoint)
- 🏆 **GOLDEN (2026-10-19, `077b4c4`, nets ≤ 96 pins + #49's double HPWL) — ISPD median 1.0095 /
  mean 1.0116** (28/28, 23 within ±2%, better on 5); **MMS median 1.0136 / mean 1.0158** (16/16).
  `.claude/2_ARTIFACTS/results/GOLDEN_sw_only_frozen_20261019/` and `GOLDEN_mms_sw_only_frozen_20261019/`:
  **read the READMEs before quoting.** ISPD is net-neutral vs 08-25. MMS +0.48 pp is mms/adaptec2
  (+5 pp), which is noise: 1-ULP `init_gamma` nudges span 1.0274..1.0799 there. The 08-25 goldens
  (bannered) are in journal.md.
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

## Where pl_algo stands — THE ACTIVE THREAD as of 2026-10-06
- **#45 — integration in `/AIEplace/pl` (ACTIVE, opened 2026-10-06).** The native parser (#43),
  FM partitioning (#42) and the chunked record-stream gradient (#41) each verify alone; none feeds a
  placer. **Mark's decisions:** slot order (beat_packer format) for every per-node array; one parse
  feeds both the software route and the PL; one chunk-capable `hpwl_gradient_CU` (from
  `hpwl_gradient_computer_v2`; `hpwl_computer*` retired); FM and bare cut both kept behind
  `partition_fm_passes`; start fresh in `/AIEplace/pl` and delete `/vck5000` once moved; timing after
  integration. Gradient `max_rel` bound relaxed to 1e-3 (rel_rms 1e-5 unchanged) — done.
  **Second round (same day):** mailbox option A now, C later, B fallback; module suffix `_CU` (rule
  amended in `CLAUDE.md`); all software in `host/sw_flat/`, built new with sw_only's high-level
  structure (likely the final host); control on the CPU, a few scalars per iteration (#44 arm B);
  **no caching** — every run treats the design as unseen; netless nodes + fillers go in a
  density-only slot tail; #20 / #40 closed as superseded; `origin/geert` stays separate.
  **Steps 1–3 done** (skeleton; `hpwl_gradient_CU`; `FlatDatabase`, the one parse, bit-exact vs
  DataBase on all 44, [[REPORT_45_flat_sink_20261007.md]]) and **step 4** (slot layout: chunks
  back to back + filler tail, fillers bit-equal to sw_only's, 44/44; netless nodes stay in their
  chunks — plan deviation, [[REPORT_45_slot_layout_20261009.md]]) and **step 5** (`sw_flat_place`,
  CPU phase-1 GP on the slot layout, **bit-identical to sw_only** under D6: sums in sw_only's orders,
  `make -C host/sw_flat check-placer`, [[REPORT_45_sw_flat_placer_20261009.md]]). **Step 5b:** slot-order
  sums and the PL's exp table are `[sw_flat]` switches, off by default; their drift is within 2-ULP
  noise on 28/30 runs, so slot order hides no semantic difference
  ([[REPORT_45_slot_order_sums_20261014.md]]). **Step 5b part 2 (10-16):** `wa_sums`/`grad_merge`/`hpwl`
  switches make sw_flat's gradient + int64 HPWL **bit-exact to `hpwl_gradient_CU`** on 6/6 (chunked, macros);
  drift within noise apart from one best-solution flip. **Step 6's golden = sw_flat `pl+Bprime`.** It also found
  sw_only's float HPWL error, fixed by #49 ([[REPORT_45_pl_arithmetic_20261016.md]]). **Step 8 brought forward
(10-19):** `hpwl_gradient_CU` is a Vitis kernel at 250 MHz (`make -C pl xclbin`). Through XRT
(`make -C host/sw_flat hpwl-gradient-xrt`), sw_emu 4/4 and hw_emu 3/3 (synthetic with 16 chunks, pci_bridge32_b, fft_a) are bit-exact vs the g++ CU.
hw xclbin built at **200 MHz** (WNS +0.031 ns). **Clock work (10-27):** **230 MHz PASSES bit-exact on the card** (4 designs, bigblue4 7.9 ms/axis; WNS +0.001 ns; HLS at
400 MHz + unshared adders); on vck set XRT_DEVICE_BDF=0000:c1:00.1; 250 MHz best −0.178 ns ([[REPORT_45_hpwl_gradient_timing_20261027.md]]).
**Macro beats (10-27):** refresh/fold 16 pins per cycle, ~11–14× fewer macro-pin cycles on MMS, bit-exact in sw_emu and hw_emu; routed (build G) 230 MHz still closes and 250 MHz WNS is −0.119 ns, now limited by the `wa_*` DSP inputs; on the card at 230 MHz 4/4 bit-exact, adaptec1 0.8 → 0.7 ms per axis ([[REPORT_45_macro_beats_20261027.md]]).
**On the VCK5000 (2026-10-20, host `vck`): PASS bit-exact** on synthetic, fft_a, mms/adaptec1 and bigblue4 (3 chunks,
~8.4 ms wall-clock per axis) ([[REPORT_45_hpwl_gradient_hw_flow_20261019.md]]).
**Then:** step 6, density path.
  → [[PLAN_45_integration_20261006.md]]
- **Building blocks (#41/#42, closed 2026-10-06; history.md):** chunked gradient tier-1 exact, II=1,
  post-route 3.955 ns vs 3.33; one axis 256 of 463 URAM; FM 1 pass 0.38× externals.
- **#48 phase 2 — overflow map = B' (Mark, 2026-10-14; measured 10-15).** The device reduces the overflow
  from its own solver map after the cells, before the fillers, over an EXACT-size fixed base (XPlace's one
  deposit, no extra URAM). sw_flat switches `overflow_map`/`fixed_base`; B' == sw_only's metric (38/44
  bit-equal, rest rounding); B without the exact base reads 1.3-6.7% low on bigblue2/3/4. Trajectories
  within noise. **Device side landed 10-15:** `bin_density_CU` reduces between cells and fillers (tree
  sum, so within 3.9e-6 of a double sum, 44/44; C-synth `reduce` II=1, ~0.22 ms at 1024).
  → [[REPORT_48_overflow_map_20261015.md]]
- **#48 — `bin_density_CU` (renamed 10-19) accumulates in fixed point (Mark).** sw_flat models it:
  18.18 in one 36-bit URAM word per bin (8K × 36, 128 URAMs at 1024). Peak ρ 95,408 bins (mms/newblue4,
  iteration 4). Exact integer sums, so the map is order-independent and the overflow beats float; drift
  in noise on 5/5 (bigblue4 +0.002%). **10-20: F per design from the total-area bound (never overflows;
  F = 14..23), and one cell per cycle: 4×4 banks + forwarding, II = 1, bit-exact 44/44, forwarding proven in
  RTL co-sim (depth 4 fails, 5 passes).** → [[REPORT_48_bin_density_fixed_point_20261019.md]] **10-27: bin index
  by multiply** in golden and device (registered divergence; drift nil on 4/5, in noise on bigblue4; ~63 K LUT saved over 8
  producers) → [[REPORT_48_multiply_bin_index_20261027.md]] **10-28: banked deposit 656 DSP, drain timing met in
  csynth (+0.10 ns)**; co-sim must re-prove the forwarding depth → [[REPORT_48_drain_timing_20261028.md]]
- **#47 — the borrowed-copy exchange replaced the chunk mailbox (2026-10-09; "tail" renamed "borrowed"
  2026-10-26).** Rule C6: a macro pin is never borrowed (the macro is). Rule C7: a chunk's borrowed
  copies sit in its borrowed rows, one block per owner; owners write / read them through a 32 x 256 row
  buffer replaying host-scheduled ops (`borrowed_copies.hpp`). C8: shared-first owned rows. bigblue4 at
  1 M: 2.49 M -> 1.57 M cycles per axis. **Not built: B4 = A (Mark, 2026-10-09)**, which removes the
  CU's send and merge passes by folding them into `iteration_update` (modeled 0.98 M). Software only:
  the M2 golden, and since 2026-10-27 sw_flat's own loop (`initialization`, then gradient, field,
  `iteration_update`, which merges and sends; bit-identical). The device side waits on #45 step 6
  (loop skeleton: `host/sw_flat/device_loop.hpp`) ([[PLAN_47_borrowed_copies_status_20261026.md]]).
  **R (resident shared region) CLOSED 2026-10-19, measured, not chosen; B4 stays the plan.** Modeled 1–4 pp
  faster than B4 and global colouring is free, but the region is 25% of capacity at K=3 and ≥ 100% at
  K=6 (512 K run); a sliding window (first→last user chunk) equals it at K ≤ 3 and is 39–58% at K=6.
  Only per-chunk tails scale to K=6.
  → [[PLAN_47_mailbox_redesign_20261006.md]]
- **v1 scope DECIDED (Mark, 2026-08-28; control placement 2026-10-06):** phase-1 GP, data
  device-resident, control on the CPU (#44 arm B). **No phase 2, no backtracking** (deferred until needed). **pl_algo pins to the frozen sw_only HEAD.** (Third §10
  question — grid-1024 A/B vs per-design `-DPL_GRID` — still open.)
- Items re-filed here from the sw_only list on 2026-08-17 (marked **↪ pl_algo** in tasks.md):
  **#15** entirely (net-local frames — PL precision, expects no sw_only HPWL movement), **#23**'s
  initial-step mirror, **#19**'s live pre-#19 dff gate in `host/src/pl_algo/`, and **#6c** operator
  skipping (now an *Improvements* bullet, but wanted during step 6, not after).
- ⚠️ **pl_algo inherits sw_only's u-vs-v decision (settled by #32, closed 2026-08-17): track on `v`.**
  HPWL, overflow and the stored solution all describe the lookahead `v_k`, not the committed `u` —
  flagged in #20 step 6 (history.md; now #45 step 7), with the specific trap that `sched_verify` checks the schedule,
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

## Also open
- **#43 — native parser (CLOSED 2026-10-06; history.md).** `DesignReader.cpp` replaced Limbo (all 44
  parse 5.3× faster). Limbo itself removed 2026-10-22: `make test-parser` compares against its
  frozen dump hashes (#9). ⚠️ Touching `DesignReader.cpp` or a `DesignSink` (`DataBase`, beat_packer's
  `native_netlist.hpp`) → run `make test-parser` AND `make -C bring_up/beat_packer check-reader`.
- **#44 — host control vs PL control (opened 2026-10-05, Mark).** XPlace runs its schedule on the
  CPU and pays only ~3–4 scalar readbacks per iteration. Build both arms (resident `MODE_PLACE` vs
  per-iteration launch + host schedule) and A/B on real HW. Step 1: time the bare XRT round trip.
- **#21 — repo restructure.** Change A (host to top level) landed 2026-09-09 (`05b35bd`); #45 does
  the rest (`pl/` at top level, `vck5000/` retired). **`origin/geert` is still NOT merged** (12
  commits ahead, checked 2026-10-06); his `host/src/v2/` is a second from-scratch LEF/DEF host, which
  matters to #45's "one parse".

