# REPORT #48: RTL co-sim of the 64-bank deposit: forwarding distance 4, depth 6 has 2 to spare

*2026-10-29. Branch `pl_algo`. Was the handoff of the same name (the brief is kept below).*

## Result

**The 64-bank drain needs 4 forwarding registers; `BANKED_FORWARD_DEPTH` = 6, so it has 2 to spare.** Left at 6
(no change, so no new csynth or Vivado run). RTL co-sim of `pl/synth/bin_density_banked/cosim/` (Vitis HLS 2022.2,
`PL_DENSITY_GRID_MAX=64`, two calls: grid 64 and grid 60):

| `BANKED_FORWARD_DEPTH` | 0 | 1 | 2 | 3 | **4** | 5 | 6 (code) |
|---|---|---|---|---|---|---|---|
| C simulation | FAIL (95 / 118 words) | PASS | PASS | PASS | PASS | PASS | PASS |
| RTL, grid 64 / 60: words that differ (of 4096) | not run | **FAIL** 124 / 138 | **FAIL** 108 / 117 | **FAIL** 101 / 107 | **PASS** 0 / 0 | **PASS** 0 / 0 | **PASS** 0 / 0 |

- **The schedule predicts 4, and RTL agrees.** From `drain_banks_Pipeline_drain.verbose.sched.rpt` (pipeline
  depth 6, II = 1), iteration t issues its URAM read in state 2 (cycle t+1). The data and the forward/add are in
  state 5 (t+4). The deferred store for update t is issued by iteration t+1, in its state 5 (t+5), and lands in
  state 6 (t+6). A same-word update d iterations later reads at t+d+1, so it reads stale data for d <= 4. Its add
  finds update t in forwarding slot d-1, which needs depth >= d. **Minimum 4.** Depth 3 failing shows the bench
  does create distance-4 hazards.
- **Depth 0 now fails in C as well.** The write-back stores the previous iteration's sum after this iteration's
  read, so even in-order C sees a distance-1 hazard unless slot 0 is compared. Co-sim stops when the C bench
  fails, so **depth 1 is the RTL negative control**. `run_depths.sh` defaults to 1..6.
- **The deferred write did NOT lengthen the distance relative to the 16-bank CU (5).** Different pipelines:
  the drain has a 6-stage schedule, with the read issued one state after the pick.
- **Termination and deadlock are clean.** The unequal slices (700 / 440 / 550 / 400 / 620 / 490 / **0** / **3**
  cells) end with every word bit-exact, including the last deferred write. Both transactions finish (latency
  7,682..7,865 cycles each, of which 4,096 are write-out). The HLS deadlock detector was built in and never
  fired. Words past the grid at 60 (fixed pitch) stay 0.

## The bench (`cosim_tb.cpp`)

- **Distance 1:** producers 0..5 put 64 cells each at one position.
- **Distance k = 2..8:** 48 cells each cycle through k positions 8 bins apart (same banks, different addresses),
  phase-shifted per producer.
- **Producer slots:** producer 0 fills every slot. It has priority, so its banks see unbroken distance-k streams.
  Producer p >= 1 leaves every (p+1)th slot NO_DEPOSIT, so later sections interleave producers 1..5.
- **Random tail:** unequal lengths, NO_DEPOSIT slots, cells clipped at the die edge.
- **Golden:** a plain uint64 deposit, compared bit for bit over all 4096 raw words.
- **A first bench was too weak.** It left NO_DEPOSIT gaps in producer 0 too. Depth 1 failed and depth 4 passed,
  but it never ran depths 2-3. The final bench above was re-run at every depth 1..6.

Files: `cosim_top.cpp` (`banked_cosim_top`: `#include "../banked_top.cpp"` plus bench-sized m_axi `depth=`),
`cosim_tb.cpp`, `cosim.tcl`, `run_depths.sh`. Logs: `cosim/cosim_run_d{1..6}.log` (gitignored), and the
first bench's `*_oldbench.log`. A run takes ~21 min (csynth 11-13 + co-sim 8-10), 3 in parallel on this box.

**Next (#48):** build `banked_top` (Vivado already met 3.33 ns). Macro tiles: [[HANDOFF_48_macro_tiles_20261022.md]].

---

## The brief (as handed off)

*2026-10-29. Branch `pl_algo`. Mark: "the next #48" after [[REPORT_48_drain_timing_20261028.md]] (drain meets
3.33 ns in Vivado, 2.928 ns). Pattern to copy: the 16-bank co-sim, `pl/synth/bin_density_CU/cosim/`
([[REPORT_48_bin_density_fixed_point_20261019.md]], re-run in [[REPORT_48_bank_address_multiplies_20261028.md]]).*

## Why this is needed

`drain_banks` (`pl/synth/bin_density_banked/banked_top.cpp`) reads a URAM word, adds, and writes it back
several cycles later. If the same bank gets the same address again inside that window, the new read sees a
stale value unless the forwarding registers supply the fresh sum. `#pragma HLS DEPENDENCE ... inter false`
tells HLS to ignore that hazard, so **correctness rests entirely on `BANKED_FORWARD_DEPTH` (6) covering the
scheduled read→write distance**.

C simulation cannot check this: it runs each iteration to completion, so it passes at any depth (even 0).
Only RTL co-sim runs the real pipeline. The 16-bank CU was proven this way (distance 5: depths 0/4 FAIL,
5/6 PASS). The 64-bank drain has **never** been co-simmed. And the timing work just changed its pipeline:
the write-back is now deferred one cycle (it stores the PREVIOUS cycle's sum from forwarding slot 0), so the
distance may have grown. Until this runs, depth 6 is a guess.

The co-sim also checks three things only RTL can show:
- **Real FIFO interleaving.** In C simulation the 8 producers run one after another; in RTL they run
  together, so one bank gets updates from different producers on consecutive cycles, in an order C never
  produces.
- **Termination.** The exit is now two cycles late, and the last deferred write must still land. The
  comment claims every real update is followed by at least one more cycle; the co-sim proves it.
- **Dataflow deadlock** across 512 FIFOs of depth 8 (see the noteToSelf entry on FIFO bounds and skew).

## Plan

1. `pl/synth/bin_density_banked/cosim/`: `cosim_top.cpp` (banked_top with `depth=` on every m_axi port,
   sized to the bench), `cosim_tb.cpp`, `cosim.tcl`, `run_depths.sh`, all modelled on the 16-bank ones.
   Shrink the grid with `-DPL_DENSITY_GRID_MAX=64`, so the URAMs are 64 words per bank and RTL sim is
   fast; the pipeline is the same. Pass `-DBANKED_FORWARD_DEPTH=$depth`.
2. **The stream must collide on purpose, ACROSS producers.** With 8 producers, a bank sees the same address
   back to back when several producers hit the same bins at once. Sections, at least:
   - all 8 producers depositing at ONE position for many cells (distance 1, every bank, every cycle);
   - distance k = 2..8: positions cycling with period k, so each bank re-hits an address every k updates;
   - random cells, NO_DEPOSIT slots, and slices of unequal length (one producer finishing long before the
     others, to exercise termination with `last`s arriving at different times);
   - at least one run at `grid_bins` < GRID_MAX, so the fixed bank pitch is exercised too.
   Golden: a plain one-bin-at-a-time deposit in the bench (uint64, no banks), as `banked_check.cpp` does.
   Compare every word bit for bit; words past the grid must be 0. Exit 0 = match.
3. `run_depths.sh`: depths **0** (negative control, must FAIL), then upward until it passes, plus one above.
   The answer is the smallest passing depth. Record each verdict and the failing-bin count.
4. If the smallest passing depth is **> 6**: raise `BANKED_FORWARD_DEPTH`, re-run `banked_check`, csynth
   and (portal permitting) Vivado, since more forwarding comparators touch the timing just closed. If it is
   ≤ 6, leave 6 and say what margin it has.
5. Read the URAM read and write states from `.autopilot/db/drain_banks*.verbose.sched.rpt` and state the
   predicted distance next to the measured one; they should agree.

Watch out: co-sim needs `LIBRARY_PATH=/usr/lib/x86_64-linux-gnu` and `depth=` on every m_axi pragma with
bench buffers at least that large (noteToSelf). `bins` is a SystemVerilog keyword, so no port may be named
that. Long runs: use the Bash tool's `run_in_background`, not `nohup &` inside `wsl -e`.

Out of scope: the `(ow·w)·oh` reorder, macro tiles, folding the banked design into `bin_density_CU`.

## Done means

Depth 0 FAILs, the minimal passing depth is known and ≤ `BANKED_FORWARD_DEPTH`, the predicted distance from
the schedule matches it, and termination/deadlock are clean on the unequal-slice section. tasks.md #48 and
the Active Task updated; this file `git mv`'d to `REPORT_...`; commit file by file (never `git add -A`) and
push `pl_algo`.
