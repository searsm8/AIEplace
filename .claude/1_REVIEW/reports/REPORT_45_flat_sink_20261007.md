# #45 step 3 — `FlatSink`: one parse into flat arrays (2026-10-07)

Plan: [[PLAN_45_integration_20261006.md]] step 3. Decisions taken (Mark, 2026-10-07):
**DEF COVER is movable, as in DataBase**, and **the packer's macro rule is sw_only's
`tagMovableMacros`**, computed after the parse.

**Result:** `make -C host/sw_flat check-sink` → **ALL PASS (47 designs: 44 benchmarks + 3 fixtures)**.
That covers every node, net, pin and offset, bit-equal to sw_only's `DataBase`. Macro tags equal
sw_only's on all 47. Every difference from the legacy packer reader is accounted for. **16/16
mutants caught.** Wall time about 60 s (4 jobs).

| block | files | verdict |
|---|---|---|
| 1. The flat model + the sink | `flat_design.hpp`, `flat_sink.{hpp,cpp}` | — (verified by 2) |
| 2. `[database]`: vs sw_only's DataBase | `test/flat_sink_check.cpp` | 47/47 bit-exact |
| 3. `[macros]` + `[legacy]`: what the packer is fed | `flat_netlist.hpp`, `test/native_netlist.hpp` | 47/47; 44/44 legacy |
| 4. Does the check bite? | `.claude/2_ARTIFACTS/scratch_45/sink_mutants.sh` | 16/16 caught |

Full per-design output: `.claude/2_ARTIFACTS/scratch_45/check_sink_44.txt`; mutants: `…/sink_mutants.txt`.

---

## Block 1 — `FlatDesign` and `FlatSink`

**What.** `FlatDesign` (`host/sw_flat/flat_design.hpp`) holds only facts from the files:
- the header: name, units, die, die shift, row height, site width, and `maximum_utilization` from
  placement.constraints (added because sw_only's Setup reads it as the target density);
- per node: name, kind (`CELL` / `FIXED` / `IO_PAD`), position and size;
- nets as CSR arrays (each net's pins contiguous), with a pin's node and its x/y offsets.

`FlatSink` (`flat_sink.{hpp,cpp}`) is a `DesignSink` that fills it in one read with the host
reader. No `DataBase` objects are built. `read_flat_design(dir, sink, why)` copies
`DataBase::readInput`'s file discovery: every `.lef`, then `floorplan.def`, falling back to the
first `.aux`.

**How.** Each DataBase callback is copied rule for rule, down to the float operation order. The
non-obvious rules this forced:
- **LEF sizes stay in microns until `finish_lefdef`.** DataBase scales macro sizes and the CORE
  site only after both LEF and DEF have read, and only if both succeeded. DEF pin offsets are scaled
  at net time, unconditionally.
- **LEF pin centre `(float)(xl + xh) / 2.0f`**: the sum is done in double and then rounded. A
  repeated PIN name overwrites, and POWER/GROUND pins are skipped.
- **A repeated MACRO name keeps its first body.** The second body is parsed into a throwaway, as
  DataBase's `emplace` + orphan does. Repeated component and pad names also keep the first.
- **FIXED** is DataBase's FIXED:
  - Bookshelf `.nodes` terminal, or `.pl` `/FIXED`;
  - DEF `FIXED`, or DEF `PLACED` whose LEF class is neither exactly `CORE` nor `BLOCK`, so
    `CORE SPACER` → FIXED.
  - **COVER stays `CELL`** (decision 1).
- **Bookshelf die shift**: `x += -shift` in float, which is DataBase's `translate`. The no-rows
  fallback die keeps DataBase's `int` truncation of the largest fixed `.pl` coordinate.
- **An IO pad's position is the DEF PIN point; its size is the PIN bbox, which is relative to that
  point.** The old header said "lower-left" for every node. That was wrong for pads, and the header
  now says so.
- Bookshelf orientation is not kept: in sw_only it only feeds the DEF writer.

For consumers that used other rules, the sink lists the nodes whose FIXED status is not the bare
status word: `cover_nodes`, `placed_fixed_nodes` (PLACED → FIXED) and `pl_fixed_nodes` (`.pl`
`/FIXED` on a non-terminal).

**Where DataBase would crash, FlatSink returns an error instead:** a net naming an unknown node or
PIN, a component of an undefined LEF macro, or a `.pl` line for an unknown node.

## Block 2 — `[database]`: FlatSink vs sw_only's DataBase, bit for bit

**What it checks** (`host/sw_flat/test/flat_sink_check.cpp`, per design, both parses in one process):
- the header: name, units, die, shift, row height, site width, max utilization;
- the node count, i.e. components plus IO pads;
- for every node, looked up by name in DataBase's component or IO-pad map: fixed status, x, y,
  width, height;
- every net in file order (DataBase's `mv_nets`): name, degree, each pin's node (by pointer
  identity), and x/y offset.

Floats are compared by their bits, with the `%a` value printed on a mismatch.

**Result: 47/47.** The 44 benchmarks in `vck5000/tools/benchmarks.py` plus three fixtures:
- `vck5000/test/parser/edge_cases/{lefdef,bookshelf}`, the hand-written #43 fixtures;
- **new** `host/sw_flat/test/fixtures/pl_fixed`: the edge Bookshelf design with movable `o10`
  set to `/FIXED` in `.pl`.

**The rules no benchmark exercises.** On all 44 designs the COVER, PLACED → FIXED and `.pl`-fixed
lists are **empty**. Only the fixtures exercise those rules:
- edge LEF/DEF: 1 COVER, 3 PLACED → FIXED (the `CORE SPACER` / `COVER`-class components);
- `pl_fixed`: 1 `.pl`-fixed.

That is why the fixtures run in `check-sink`. Without them a mutant that drops any of these rules
would pass on every real design.

**Still untested by any input** (the code copies DataBase, but nothing exercises it):
- repeated component, macro or pad names (first wins);
- the Bookshelf no-rows die fallback;
- a LEF/DEF pair that reads partially and then falls back to Bookshelf.

## Block 3 — what the packer is fed: `[macros]` and `[legacy]`

**What.** `flat_netlist.hpp`:
- `tag_movable_macros(FlatDesign)` is sw_only's `Placer::tagMovableMacros` (Setup.cpp:229) over
  the components: taller than 2.01 × the mean movable height, larger than 10 × the trimmed mean
  area, and non-zero size.
- `to_netlist(FlatDesign, tags, name)` → `packer::Netlist` in FlatDesign's node ids:
  - movable = `CELL`;
  - nets of degree 2..`IGNORE_NET_DEGREE` (96);
  - offsets in DataBase's convention (DBU, from the lower-left).

**`[macros]`: 47/47 equal to sw_only's tag set.** sw_only sums `height_sum` in `std::map` (name)
order, while `tag_movable_macros` sums in file order. Float addition does not commute in general,
so this was a real risk; it makes no difference on any input. Movable macros:
- **0** on all 8 ISPD2005 and all 20 ISPD2015 designs;
- **25–959** on MMS (bigblue2 959, bigblue4 199, newblue7 161, adaptec2 127, newblue2 25).

**`[legacy]`: 44/44** against the legacy reader (`native_netlist.hpp`, copied unchanged into
`test/` as a reference from 4c46739). It maps node ids explicitly: components in file order, then
DEF IO pins in first-reference order. Then:
- **Kept nets: identical** on all 44: same nets, same pins, same nodes.
- **Movable flags: identical** on all 44. The rule allows a difference only on the nodes FlatSink
  lists, and there are none.
- **Offsets: equal once the convention change is undone.** Bookshelf: bit-exact as `w/2 + legacy`.
  DEF: `legacy × units`, with **1–2 ulp on many pin-axes** (counted, not failed), because the
  legacy reader rounds the LEF pin centre as `((float)xl + (float)xh)/2`. Counts per pin-axis:
  - 1,984 on mgc_fft_1/2;
  - 2.37 M on mgc_superblue12;
  - 0 on every Bookshelf design.

  FlatSink matches DataBase, which `[database]` checks bit for bit, so the flat value is the
  right one.
- **The legacy reader rejects the edge LEF/DEF fixture outright** ("no LEF pin filler1/x"), where
  DataBase gives offset 0. Fixtures therefore run with `--no-legacy`.

**[info]: the macro rule change (decision 2) and what it moves.** Movable in both readers, tagged
by one rule only:

| design | both | sw_only only | legacy only |
|---|---|---|---|
| ispd2005/bigblue3 | 0 | 0 | **2,480** |
| mms/bigblue3 | 69 | 0 | **2,480** |
| mms/newblue2 | 25 | 0 | **3,723** |
| mms/newblue1 | 53 | 0 | 11 |
| mms/adaptec1, mms/bigblue1 | 62 / 31 | 0 | 1 / 1 |
| other 38 | = | 0 | 0 |

The legacy rule was "taller than the most common movable height" (Bookshelf) or LEF `BLOCK`.
Under sw_only's rule those nodes become plain `CELL`s in the packer: they keep their own pins
instead of being rewritten to `MACRO_PIN` slots. ⚠️ **Step 4's verification needs restating.** The
plan says chunk counts and external slots must be "unchanged from today's `encode_chunked` on all
44". That cannot hold on these designs, by decision. They need a measured before/after instead
(bigblue3 and newblue2 especially; mms/bigblue3 is one of the 8 chunked designs).

**[info]: offset tables under the new convention** (distinct values per axis, flat vs legacy):
- **Bookshelf mostly shrinks.** adaptec1 x 308 vs 557; bigblue4 x 2,261 vs 3,369. Cell pins tend
  to sit at a few positions relative to the lower-left.
- A few axes grow: mms/newblue2 x 160 vs 140; ispd2005/bigblue3 y 3,690 vs 3,635.
- DEF shrinks slightly; the superblues are equal.

Wider tables could change `offset_bits`, and so the record width. Step 4 measures that with the
encoder.

## Block 4 — does the check bite? (mutants)

`.claude/2_ARTIFACTS/scratch_45/sink_mutants.sh` breaks one rule at a time in a temporary copy
(`host/.sw_flat_mutant`, deleted on exit). Each mutant runs on a design that exercises the rule,
and the check must exit non-zero. **16/16 caught** (14 at first, plus 2 for the parallel path), each by the check that should catch it:

| mutant | caught by |
|---|---|
| Bookshelf offset not shifted to the lower-left | `[database]` pin offset |
| DEF offset not scaled to DBU | `[database]` pin offset |
| die shift sign flipped | `[database]` node x |
| row height not scaled / CORE site not preferred | `[database]` row_height |
| max utilization dropped | `[database]` maximum_utilization |
| IO pad width from the wrong bbox corner | `[database]` node width |
| PLACED non-CORE/BLOCK not promoted (edge DEF) | `[database]` fixed status |
| COVER treated as fixed (the legacy rule; edge DEF) | `[database]` fixed status |
| `.pl` `/FIXED` ignored (`pl_fixed` fixture) | `[database]` fixed status |
| POWER/GROUND pins kept (edge DEF) | `[database]` pin offset |
| macro rule without the height test (mms/adaptec1) | `[macros]` 63 vs 62 |
| net mask at 100, not 96 (ispd2005/bigblue2) | `[legacy]` kept nets |
| fixed nodes marked movable in the netlist | `[legacy]` movable, unexplained |
| parallel path: pin offset written to the wrong slot | `[database]` pin offset |
| parallel path: a net batch's CSR rows not offset by earlier batches | `[database]` net degree |

The net-mask mutant first **survived** on mms/adaptec1, which has no nets of 97–100 pins. It was
moved to bigblue2, which has 23. These are the Bookshelf counts of 97..100-pin nets, part of #46's
first item:
- **ISPD2005:** bigblue2 23, bigblue3 1, bigblue4 87;
- **MMS:** adaptec5 9, bigblue2 23, bigblue3 1, bigblue4 87, newblue3 8, newblue4 6, newblue5 47,
  newblue7 23;
- **0** on the other ISPD2005/MMS designs.

The ISPD2015 designs were not counted.

## Findings and follow-ups

1. **The one parse is exact.** `FlatDesign` is sw_only's DataBase in flat form on every input we
   have. Step 4 can build the slot layout on it.
2. **Parse time: FlatSink is now 2–3× faster than DataBase** (Mark asked for it to run in parallel,
   same day). The first version used a serial `unordered_map<string>` and was ~1.5× slower. The
   reader itself takes 0.2–0.6 s warm; the rest was callbacks.

   It now has:
   - `NodeIndex`, a flat open-addressing name → id table on FlatDesign's name array (NameIndex's
     design);
   - per-net split hash / prefetch / find (`find_all`, DataBase's `findAll` pattern);
   - every net batch resolved and written in parallel (OpenMP) into arrays sized up front;
   - `.pl` lookups in parallel;
   - transparent `std::map` lookups for LEF macros and pins, so a lookup no longer allocates.

   What stays serial, because order is the semantics: first-wins node registration, the `.pl`
   apply, and the error report. That report is the first failing net in file order, via a parallel
   `min` reduction.

   Warm cache, 8 threads, one design at a time, before → after:

   | design | DataBase | FlatSink (serial map) | FlatSink (parallel) |
   |---|---|---|---|
   | ispd2005/bigblue4 | 2.52 s | 3.66 s | **1.18 s** |
   | mms/newblue7 | 2.86 s | 5.21 s | **1.32 s** |
   | ispd2015/mgc_superblue12 | 1.44 s | 2.25 s | **0.54 s** |

   With `OMP_NUM_THREADS=1` it still beats DataBase (bigblue4 2.30 vs 3.93 s), so most of the gain
   is the flat index. `check-sink` is still 47/47 bit-exact, and a single-thread run gives the same
   PASS. The mutant set has two new ones for the parallel path, a pin offset written to the wrong
   slot and net batches not chained; both are caught (16/16).
3. **Step 4's verification needs restating** for the macro-rule change (Block 3 table).
4. **No sw_only, `common/` or `pl/` code changed**, so `make test-regress` and `make -C pl test`
   are unaffected.
5. `test/native_netlist.hpp` is a test-only copy of a legacy file, to be deleted with `vck5000/`
   in step 9 along with the `[legacy]` check.

## Reproduce
```bash
cd host/sw_flat && make check-sink                         # 47 designs, ~60 s, exit 0
bash .claude/2_ARTIFACTS/scratch_45/sink_mutants.sh        # 16 mutants, ~1 min, exit 0
```
