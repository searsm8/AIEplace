# #42 — Chunk partitioning deep dive: baseline vs FM vs multilevel vs KaHyPar (2026-10-05)

**Question (Mark):** with the current BFS cut as the baseline, how much do better partitioners
buy, judged end to end (host start-up + device)? Candidates: (0) baseline, (1) improved baseline,
(2) k-way FM, (3) multilevel FM; KaHyPar as a quality oracle (reference only, never shipped).
Follows [[_NEW_REPORT_42_mailbox_widening_20261002.md]] (same cycle model, 300 MHz, 1000 iterations,
2 axes; density/FFT time excluded — it is identical for every partition, so it cannot change a
comparison between them, only the absolute seconds).

## Answer
1. **Quality ranking is clear and large.** Geometric-mean external slots vs the baseline over the 8
   chunked designs: improved 1.02, **FM 1 pass 0.38**, FM 3 passes 0.30, FM 8 passes 0.29,
   **multilevel 0.062**, **KaHyPar 0.026**. Every FM/multilevel/KaHyPar partition fits the minimum
   K; the baseline misses it on 3 of 8 (bigblue4 ×2, newblue7 need K=4 instead of 3).
2. **End to end, partition runtime decides, and it depends on the mailbox:**
   - **1-float mailbox (today, or with only the burst fix):** FM 1 pass cuts end-to-end by
     **32% geo-mean** (bigblue4 84.2 → 46.4 s) and pays for itself after **95–260 iterations**.
   - **Widened mailbox:** the mailbox is already ≤ 12%, so a better partition saves 0.3–2.5 ms of
     device time per iteration. FM 1 pass is **neutral** at 1000 iterations (geo-mean 1.007;
     wins 6 designs by 3–6%, loses bigblue4 ×2 by ~9% because repairing K=4 → 3 costs 8 s);
     break-even 1,800–3,300 iterations.
   - **Multilevel and KaHyPar never pay back at 1000 iterations** on either mailbox: multilevel takes
     16–105 s, KaHyPar 63–724 s single-threaded (break-even 2.6 K–170 K iterations even with the
     1-float mailbox).
3. **Recommendation:** make **FM, 1 pass, from the baseline cut** the packer's partitioner. It always
   fits K_min (a robustness win: the baseline's K=4 designs drop to K=3), costs 1–8 s, and is the best
   or near-best end-to-end choice under both mailboxes. More passes buy little (0.38 → 0.29) and cost
   3×. Keep multilevel as the quality path for long runs; it needs a contracted coarse hypergraph to
   be fast (below).
4. **The "improved baseline" is not an improvement:** a different BFS order is a coin flip (bigblue3
   +22% externals, newblue5 −43%), and the load-balanced cut only matters when capacity binds (it
   gets mms/bigblue4 and newblue7 to K=3 at ~1 s, but with 41% / 15% externals).

## Setup
- **Objective = exact external slots.** A node costs one external slot in every non-owner chunk that
  homes a net containing it (nets homed by majority, ties to the lowest chunk — `build_chunks`' own
  rule). Equivalently, connectivity−1 of the dual hypergraph (nets as vertices, nodes as edges).
- **Constraint:** per chunk, owned + external + fixed-copy slots ≤ 1,048,576. The objective consumes
  the capacity it is constrained by — which is why no off-the-shelf partitioner models it directly.
- **Scoring:** every partition goes through `build_chunks_from_owner` (the real chunk builder) and
  `check_chunked`; K is the smallest that builds. Externals, cycles and ledger come from the built
  chunks, never from a partitioner's own estimate.
- **Timing:** native methods run one process at a time on an idle box (`final/`). KaHyPar re-timed
  one job at a time (`kahypar_timing/`); its partitions are byte-identical to the scored ones (seed 42).

## Methods (`vck5000/bring_up/beat_packer/partition.hpp`)
- **PartitionState** tracks the exact model: per-net per-chunk owned counts, majority homes,
  per-node per-chunk presence, per-chunk slot load. `evaluate(unit, target)` returns the exact change
  in externals and every load in O(nets of the unit + nodes of nets whose home flips); `apply` commits.
- **0 baseline:** `locality_order` + K equal runs (unchanged; `build_chunks` now wraps
  `build_chunks_from_owner`, verified bit-identical by device-array hash on 3 designs).
- **1 improved:** BFS over move units through every homed net (large nets too), each component started
  from a pseudo-peripheral unit; cut points moved until the chunks' exact loads balance.
- **2 k-way FM:** a pass seeds a lazy max-heap with every boundary unit's best (target, gain); pops the
  best, re-checks it exactly (stale → re-push), applies, locks, refreshes unlocked neighbors; negative
  moves allowed; stops after 2% of units moved without a new best; rolls back to the best prefix
  (least overflow, then fewest externals). While any chunk is over capacity, gain = −(Δexternals +
  100·Δoverflow); once feasible, capacity is hard. Move unit = a cell, or a macro with its pins.
- **3 multilevel:** heavy-edge matching (rating Σ 1/(|net|−1), nets > 32 skipped, clusters ≤ 2% of
  capacity) to ~2000·K units (11–14 levels); improved-style cut of the coarsest level; then FM
  (4 passes) at every level, coarsest to finest. Every level is scored on the FLAT netlist (exact).
- **KaHyPar 1.3.6** (`kahypar_partition.py`): direct k-way, km1 objective, sea20 preset, ε=0.03, on
  the primal hypergraph (move units, weight = slots). It optimizes km1 under vertex-weight balance,
  not our objective — and is still the best by far. **kahypar+fm** = its output refined by our FM.

## Verification
`vck5000/test/partition_test.cpp` (in `make test`, 6 s), golden = `build_chunks_from_owner`:
[1] model == builder (externals; per-chunk load == slot nodes held) for the baseline and 3 random
owners; [2] 3000 random moves: every predicted delta == applied change, final state == fresh init;
[3] FM ends at its best prefix, no worse than it started, consistent, builder agrees; [4] multilevel
same + macro pins with their macro; [5] coverage (macros, large nets, K=5, FM and multilevel strictly
beat the baseline). **Mutants 5/5 caught:** home tie to highest, owner not moved, fixed nodes counted
as external, rollback removed (caught only after [3] gained the best-prefix assertion), unit weight
dropped from the load.

## Results — summary over the 8 chunked designs
| method | externals vs baseline (geo-mean) | fits K_min | mean partition s | end-to-end vs baseline, widened | end-to-end vs baseline, 1-float |
|---|---|---|---|---|---|
| baseline | 1.000 | 5/8 | 0.5 | 1.000 | 1.000 |
| improved | 1.022 | 7/8 | 0.8 | 1.016 | 1.011 |
| **FM, 1 pass** | **0.382** | **8/8** | **3.5** | **1.007** | **0.676** |
| FM+ (from improved), 1 pass | 0.400 | 8/8 | 4.0 | 1.026 | 0.693 |
| FM, 3 passes | 0.300 | 8/8 | 5.9 | 1.100 | 0.677 |
| FM, 8 passes | 0.285 | 8/8 | 11.6 | 1.390 | 0.806 |
| multilevel | 0.062 | 8/8 | 53.2 | 3.431 | 1.676 |
| KaHyPar (incl. its 63–724 s) | 0.026 | 8/8 | 230 | 11.9 | 5.72 |
| KaHyPar + FM | 0.024 | 8/8 | — | — | — |

## Results — per design (externals; K; partition s; end-to-end 1-float / widened s)
| design | baseline | FM 1 pass | FM 3 passes | multilevel | KaHyPar |
|---|---|---|---|---|---|
| bigblue3 | 117,176; K2; 0.3; 19.5/11.7 | 51,456; K2; 1.0; 14.5/11.1 | 41,032; K2; 1.5; 14.2/11.5 | 8,589; K2; 17.4; 28.4/27.8 | 4,455; K2; 63; 73.5/73.2 |
| bigblue4 | 868,470; **K4**; 0.7; 84.2/26.2 | 266,623; K3; 7.9; 46.4/28.6 | 206,571; K3; 13.8; 47.9/34.1 | 23,580; K3; 81.9; 104/102 | 10,025; K3; 165; 186/186 |
| superblue12 | 65,219; K2; 0.5; 15.5/11.2 | 29,782; K2; 0.9; 12.9/10.9 | 23,429; K2; 1.2; 12.8/11.2 | 7,776; K2; 59.7; 70.7/70.2 | 6,033; K2; 724; 735/734 |
| mms/bigblue3 | 126,201; K2; 0.3; 20.2/11.8 | 56,031; K2; 1.0; 15.0/11.3 | 44,402; K2; 1.6; 14.8/11.8 | 7,050; K2; 16.3; 27.6/27.1 | 4,730; K2; 143; 155/154 |
| mms/bigblue4 | 850,202; **K4**; 0.7; 83.9/27.1 | 279,131; K3; 8.0; 48.3/29.6 | 216,509; K3; 14.4; 50.1/35.6 | 41,783; K3; 105; 130/127 | 13,834; K3; 215; 237/237 |
| mms/newblue5 | 196,273; K2; 0.3; 26.2/13.1 | 83,832; K2; 1.7; 18.8/13.2 | 65,681; K2; 3.0; 18.6/14.2 | 10,835; K2; 29.7; 42.1/41.4 | 5,645; K2; 112; 125/124 |
| mms/newblue6 | 169,758; K2; 0.4; 25.4/14.1 | 71,163; K2; 1.5; 18.1/13.3 | 55,385; K2; 2.4; 17.8/14.1 | 18,790; K2; 20.5; 34.3/33.1 | 3,539; K2; 209; 222/221 |
| mms/newblue7 | 664,354; **K4**; 0.8; 73.1/28.7 | 186,760; K3; 5.9; 42.8/30.3 | 147,101; K3; 9.5; 43.5/33.7 | 34,222; K3; 94.5; 121/119 | 9,461; K3; 211; 236/235 |

Full tables with FM+ (from improved), FM 8 passes, kahypar+fm, mailbox shares and break-even
iterations: `.claude/2_ARTIFACTS/partition_42/final_tables.md`.

## Findings
- **FM's value is front-loaded.** bigblue3: externals 117 K → 51 K in pass 1 (1.3 s), then 7 more
  passes for 51 K → 39.6 K. On bigblue4 / mms/bigblue4, pass 1 also repairs K=4 → 3 (the baseline cut
  at K=3 overflows), which is why those take 8 s.
- **Starting point matters little for FM** (FM vs FM+ within ±20% either way, design-dependent).
- **Multilevel's quality comes from its coarse levels, and so does its runtime.** superblue12: the
  coarsest level (4,750 clusters) takes 19 s, the next three 8 / 5.5 / 3.8 s, every fine level < 1 s.
  Skipping FM on levels below 50 K units cuts the time ~2× but loses most of the quality (bigblue3
  8.6 K → 20.9 K). Smaller cluster caps did not help. Cause: each level is evaluated exactly on the
  flat netlist, so a coarse move costs all its member nodes' nets, and every neighbor cluster is
  re-evaluated. Real multilevel tools contract the hypergraph (merged parallel nets, internal nets
  dropped) so a coarse move is cheap. **That is the fix** — exact scoring only at the finest level.
- **Our multilevel is within 2.4× of KaHyPar's externals** (0.062 vs 0.026) at ~4× less time, and
  KaHyPar+FM gains another ~7% over raw KaHyPar — i.e. KaHyPar's km1 optimum is close to, but not
  exactly, our objective's.
- **Headroom under widening is small.** Even KaHyPar quality cuts bigblue4's widened device cycles only
  1.49 M → 1.34 M per axis vs FM 1 pass (−10%). Partition quality is mostly a 1-float-mailbox story,
  plus getting K down.

## Half-capacity (512 K slots) study
Why: the resident loop's URAM budget (256 of 463 per axis; both axes do not fit) may force a smaller
slot capacity. All 44 designs, baseline / improved / FM 1 pass / FM+ 1 pass / multilevel, one process
at a time (`half/`). **17 designs need chunks at 512 K** (vs 8 at 1 M).

| method (14 designs where the baseline fits within K_min+2) | externals vs baseline (geo-mean) | fits K_min | mean partition s | end-to-end widened | 1-float |
|---|---|---|---|---|---|
| baseline | 1.000 | 10/14 | 0.2 | 1.000 | 1.000 |
| improved | 0.948 | 13/14 | 0.3 | 0.998 | 0.971 |
| FM 1 pass | 0.395 | 14/14 | 1.3 | 1.046 | 0.724 |
| FM+ 1 pass | 0.383 | 14/14 | 1.2 | 1.033 | 0.706 |
| multilevel | 0.060 | 14/14 | 24.9 | 3.333 | 1.705 |

**The baseline breaks down on the three largest designs.** It fits nothing up to K_min+2, and
`encode_chunked` ends at K=9 (bigblue4 ×2, 78% externals) and K=10 (newblue7, 67%).

| design (K_min = 5) | baseline (`encode_chunked`) | FM 1 pass | multilevel |
|---|---|---|---|
| bigblue4 | K9, 78.2%; 148.0 / 34.9 s | K6, 26.6%; 28 s; 89.0 / 50.6 s | **K5**, 3.6%; 209 s; 233 / 228 s |
| mms/bigblue4 | K9, 77.9%; 147.8 / 35.0 s | K6, 27.4%; 29 s; 93.3 / 53.6 s | **K5**, 2.7%; 201 s; 226 / 222 s |
| mms/newblue7 | K10, 67.2%; 151.5 / 40.2 s | K6, 18.4%; 21 s; 78.1 / 47.7 s | K6, 2.6%; 171 s; 199 / 195 s |

(Cells read: K, externals %; partition s; end-to-end 1-float / widened s.)

What this changes:
- **The partition matters more as capacity shrinks.** It decides how many chunks a design needs, not
  just how much mailbox traffic it makes.
- **Only multilevel reaches K_min on the biggest designs.**
- **FM's single pass is slow when its starting cut is badly over capacity** (28 s on bigblue4 at
  K=6). Starting FM from a capacity-feasible cut would fix that; the load-balanced cut alone does not
  get there here. Under the widened mailbox, a fast baseline at K=9 still wins end to end. So at
  small capacities, a fast partitioner that is also good is the open problem, and contracted
  multilevel is the candidate.

## Not done / next
- **Contracted multilevel** (coarse hypergraph with merged nets; exact objective only at level 0):
  the path to multilevel quality at FM-like cost.
- **Parallel partitioning** (Mark: the host has 8 cores). Not explored: native code is single-threaded
  for comparability. Mt-KaHyPar (parallel KaHyPar) would show what 8 threads buy; needs an install
  (Mark's OK — only KaHyPar was approved).
- **Integration:** `encode_chunked` still uses the baseline. Making FM 1 pass the default is a small
  change (it would add `make_partition_graph` + `fm_refine` after `order_owner`), but it changes every
  chunked design's device arrays — Mark's call, and #41's owner should know.

## Files
- New: `bring_up/beat_packer/partition.hpp`, `partition_study.cpp`, `cycle_model.hpp` (moved out of
  `chunk_profile.cpp`), `kahypar_partition.py`; `test/partition_test.cpp` (+ `test/Makefile`).
- Changed: `beat_packer.hpp` (`build_chunks` → `order_owner` + `build_chunks_from_owner`, bit-identical).
- Data: `.claude/2_ARTIFACTS/partition_42/` — `final/` (authoritative), `kahypar/` + `kahypar_timing/`,
  `tuning/`, `maxw/`, `minref/` (contended timings: quality only), `hgr/` (KaHyPar inputs), scripts.
- KaHyPar: `pip3 install --user kahypar` (1.3.6); presets in `~/aieplace_tmp/kahypar/` (from the
  KaHyPar repo's `config/`).
