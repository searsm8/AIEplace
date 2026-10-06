# #43 — Native design reader: Limbo-identical output, 5.3× faster, Limbo out of the host

*2026-10-06. Branch `pl_algo`. Mark's ask (2026-10-05): build a parser from scratch with the same
output as Limbo, and match or beat its parse time; fewer dependencies is a by-product.*

## TL;DR

- **Same output, proven, not assumed.** On 86 inputs (all 44 manifest designs + 40 extra DEFs + 2
  hand-written edge cases),
  a canonical dump of every field the parse sets in `DataBase` is **byte-identical** to what the
  same `DataBase` held when Limbo did the reading (floats compared as `%a`, i.e. bit-for-bit).
  `make test-regress-slow` is bit-identical on all 3 designs; `make test` passes.
- **Faster on every design.** All 44: **115.5 s → 21.9 s (5.3×)** with 8 threads, **34.9 s (3.3×)**
  on one. Worst case newblue7 13.1 → 2.3 s; bigblue4 11.2 → 2.0 s; superblue12 6.6 → 1.2 s.
- **Dependencies.** The host links no Limbo, Boost, zlib or gzstream any more, and drops
  `-D_GLIBCXX_USE_CXX11_ABI=0` (it existed only for Limbo's archives), and with it pl_algo's
  per-file ABI exception for the XRT driver. tabulate (header-only) is the one third-party
  dependency left. Limbo stays as a **test-only** reference, built on request.
- **Found on the way: the old host silently quit on some valid inputs.** Limbo's default
  `lef_nondefault_cbk` calls `exit(0)`, and the old host never overrode it, so any LEF with a
  `NONDEFAULTRULE` (all 20 `ispd2015_fix` designs) ended the run with status 0 and nothing placed;
  its message went to the stdout the host had redirected to `/dev/null`.

## What changed

| file | |
|---|---|
| `host/src/common/src/DesignReader.cpp` (new) | the LEF / DEF / Bookshelf readers |
| `host/src/common/include/ParseRecords.h` (new) | the plain records a reader hands `DataBase` (replace Limbo's structs) |
| `host/src/common/include/NameIndex.h` (new) | flat open-addressing name → node table for the parse |
| `host/src/common/{include,src}/DataBase.*` | no Limbo base classes; callbacks take `ParseRecords` (batched for the big sections); `parse*File` hooks so the harness can substitute Limbo |
| `host/src/{sw_only,pl_algo}/makeflags.mk` | Limbo/Boost/zlib flags and the old-ABI define removed |
| `vck5000/test/parser/` (new) | equivalence gate + timing harness — see its README |
| `vck5000/Makefile` | `make test-parser` |
| `vck5000/tools/bootstrap_third_party.sh` | default = tabulate only; `--with-limbo` builds Limbo for the gate |
| READMEs, `CLAUDE.md`/`AGENTS.md`, `PackedDesign.hpp` | stale Limbo/ABI statements updated |

`DataBase`'s callback **logic** is unchanged: each body does what it did, on a parser-neutral
record. Where Limbo had quirks that reach `DataBase`, the reader reproduces them and marks each
in the source — e.g. Si2 never clears a component's location, so a DEF component with no
placement inherits the previous one's; a Bookshelf `INTEGER` is `atoi`'d, so `-0` is `+0.0`;
`DIEAREA` keeps its first two points as written, not min/max.

## How "same output" is checked — `make test-parser`

`parse_bench` is built twice. The plain build reads with `DesignReader`. The other (`--limbo`)
reads through Limbo and a bridge that converts Limbo's structs into the same `ParseRecords` —
exactly what the old callbacks read out of them. Both dump the parsed `DataBase`; the gate
requires the dumps to be identical.

| inputs | count | lines compared | result |
|---|---|---|---|
| `tools/benchmarks.py` (ISPD2005, ISPD2015, MMS) | 44 | up to 15.2 M per design | identical |
| contest legal DEFs (`after_legalized.ntup.fix.def`, all cells PLACED) | 20 | | identical |
| XPlace `ispd2015_fix` DEFs + LEFs (a different writer) | 20 | | identical\* |

| hand-written edge cases (`test/parser/edge_cases/`) | 2 | | identical |

\* only after the bridge overrides `lef_nondefault_cbk` — without it Limbo `exit(0)`s mid-parse
(above).

The edge cases cover syntax and Limbo quirks no benchmark happens to use, each checked in the dump
to actually reach `DataBase`: a DEF component with no placement inheriting the previous one's
location (Si2 never clears it), and likewise a LEF MACRO without SIZE inheriting the previous
macro's size — the one quirk the edge cases *found* (fixed in the reader, 2026-10-06); COVER pins
(location, no status); a pin's first of several LAYERs; `+ SYNTHESIZED`; routed nets; quoted `;`;
a 4-point DIEAREA (Si2 keeps the first two points, so the die is degenerate — reproduced); 9-word
`.nets` pins; `FIXED_NI` (not fixed); lower-case keywords; a node placed twice (last wins).

**The gate fails when it should.** A mutant giving `UNPLACED` DEF components (0,0) instead of
(−1,−1) changed 32,281 lines on mgc_fft_1; one nudging `.nets` offsets by 1e-6 changed 41,071 on
mms/adaptec1. (A 1-ULP *double* nudge was correctly invisible: `DataBase` stores offsets as
float, so the parsed design really is unchanged.)

The gate was re-run after every optimisation below, each time all inputs identical.

## Speed

Wall time of `DataBase(dir)` — the whole read — best of 3, warm page cache, 8-core WSL box.
Full tables in `.claude/2_ARTIFACTS/parser_43_20261006/`.

| | all 44 | newblue7 | bigblue4 (ISPD05) | superblue12 | adaptec1 |
|---|---|---|---|---|---|
| Limbo (before) | 115.5 s | 13.13 | 11.20 | 6.60 | 1.05 |
| native, final, 8 threads | **21.9 s (5.3×)** | 2.31 | 2.01 | 1.20 | 0.21 |
| native, final, 1 thread | 34.9 s (3.3×) | 3.73 | 3.26 | 1.93 | 0.32 |

Per design the 8-thread gain is 4.1× (small ISPD2015) to 5.7× (newblue7).

**Where it came from** (newblue7, the largest; profiled with an in-process sampler since this box
has no `perf`). The reader itself was never the problem: tokenising newblue7's 10 M-pin `.nets`
takes ~0.5 s. Almost all the time was `DataBase` building its objects.

| step | newblue7 |
|---|---|
| Limbo | 13.1 s |
| native reader, Limbo-era `DataBase` callbacks | 8.5 |
| flat `NameIndex` instead of `std::map<string>` lookups (2 tree walks per pin) | 5.7 |
| prefetch all of a net's pins' slots before resolving any | 5.4 |
| `mm_components`/`mm_nets` built in one sorted pass, not millions of random inserts; `to_chars` macro names; no zero-fill of file buffers | 4.7 |
| net list of each cell starts at capacity 4 (was 3 reallocations each) | 4.0 |
| 16-byte sort keys, two maps built concurrently | 3.8 |
| `.nets` tokenised in parallel chunks; pin lookups in parallel | 3.3 |
| `.pl` likewise | 2.6 |
| `.nodes` likewise (components allocated in parallel) | 2.2–2.3 |

ISPD2015 got the same treatment for DEF `COMPONENTS` and `NETS` (superblue12 1.85 → 1.2 s).

**Why the parallel result is still exact.** Only order-free work runs in parallel: tokenising, and
lookups that read but never write. Everything order-sensitive runs sequentially in file order:
registering names (first wins), creating MacroClasses, linking nets to nodes, filling in a DEF
component's inherited location. A file is split only at points where an entry is known to start
(a `NetDegree` line, a `- ` line, any line in `.nodes`/`.pl`), in bounded 32 MB blocks. Without
OpenMP (pl_algo builds without it) every loop just runs serially.

**What is left** is the in-memory model itself (one `new` per node, net and map entry, plus
in-order linking); scaling past 1.6× from threads would mean changing `DataBase`'s data
structures, which the placer reads. Out of scope here.

## Scope of the reader

It covers the subset of each format our benchmarks use, and treats anything else as a logged
error with file and line — never a silent skip of data `DataBase` consumes. Known gaps: DEF
`+ PORT` pins, Bookshelf `terminal_NI` / `.shapes` / `.route` / non-empty `.wts`. (For the last
four, Limbo would also have quit, via the same silent `exit(0)`.) gzip-compressed inputs, which
Limbo's Bookshelf reader accepted, are not supported; none of our inputs are compressed.

## Decisions (Mark, 2026-10-06)

1. **Limbo stays as the test reference** until we are confident it is not needed
   (`--with-limbo`, `make test-parser`).
2. **beat_packer reads with `DesignReader` — done, see below.**

## Addendum (2026-10-06): beat_packer on the host reader — a second, independent check

`DesignReader` now feeds an abstract `DesignSink` (`ParseRecords.h`) rather than `DataBase`
directly; `DataBase` is one sink (unchanged behaviour: `make test-parser` 86/86, `test-regress`
bit-identical, `make test` passes), and `vck5000/bring_up/beat_packer/native_netlist.hpp` is
another, which builds beat_packer's `Netlist` with the legacy reader's exact conventions (file-order
node ids, height-mode macro rule, COVER = fixed, IO pins on first use, LEF offsets in microns).
`beat_packer`, `chunk_profile`, `partition_study` and `large_net_stats` now read through it.

`make check-reader` builds the `Netlist` both ways and compares every field the encoder reads:
**44/44 identical**, offset tables bit for bit. The legacy stream readers were written separately
from `DesignReader` and from Limbo, so this is an independent second check on the host reader. Read
time over the 44: **69.8 s → 18.6 s (3.7×)**; newblue7 9.95 → 1.99 s, bigblue4 8.32 → 1.73 s. The
legacy readers stay in `beat_packer.hpp`, because the tier-1 harnesses and HLS co-sim testbenches
build that header alone and cannot link the host sources.

## Reproduce

```
cd vck5000 && make test-regress-slow       # sw_only bit-identical (3/3)
cd vck5000 && make test-parser             # 86 inputs, needs bootstrap --with-limbo
cd vck5000/test/parser && REPEATS=3 bash run_bench.sh ./build/parse_bench
```
