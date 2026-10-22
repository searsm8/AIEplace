# test/parser — the native design reader vs Limbo (TODO #43)

`host/src/common/src/DesignReader.cpp` replaced the Limbo LEF/DEF/Bookshelf parsers. The
contract is **byte-identical parsed state**: for every input, a canonical dump of every field the
parse sets in `DataBase` (macros and pin offsets, components, IO pads, nets and their pins, die,
rows, units, areas; floats as `%a`, so equal means bit-equal) must match what the same `DataBase`
held when Limbo did the reading.

```
cd vck5000 && make test-parser    # ~1 min, exit 0 = every dump matches Limbo's
```

Run it after any change to `DesignReader.cpp` or to `DataBase`'s parse callbacks.
`make test-regress` exercises only 3 designs end to end; this covers 86 inputs field by field.

### Limbo is gone; its output is frozen as hashes

Limbo was removed on 2026-10-22. Before that, one last run (all 86 inputs native == Limbo,
byte-identical) recorded the sha256 of each Limbo dump in `limbo_dump_hashes.txt`. The gate now
compares the native dump's hash against that file, so it needs neither Limbo nor its old-ABI
build. Two consequences:

- **A mismatch names the input, not the line.** To see the diff, check out `f932c98` (the last
  commit with the `--limbo` bridge and the Limbo submodule), build Limbo with
  `bash vck5000/tools/bootstrap_third_party.sh --with-limbo`, and run the old gate there.
- **The hashes are of the inputs as they were on 2026-10-22.** If a benchmark file itself changes
  (e.g. `ispd2015_fix` regenerated), its hash goes stale and fails here even with a correct
  reader. Only re-record from Limbo, via that old commit; never from the native reader, or the
  gate is checking the reader against itself.

## What it compares

| inputs | count | what they add |
|---|---|---|
| `tools/benchmarks.py` | 44 | every design we run: ISPD2005 + MMS (Bookshelf), ISPD2015 (LEF/DEF) |
| `build/extra/legal_*` | 20 | the contest's legalized `after_legalized.ntup.fix.def`: every cell `PLACED`, so the PLACED→FIXED class rule is exercised |
| `build/extra/fix_*` | 20 | XPlace's regenerated `ispd2015_fix` DEFs and LEFs — a different writer's output |
| `edge_cases/{lefdef,bookshelf}` | 2 | hand-written: the syntax and Limbo quirks no benchmark uses (a DEF component with no placement inheriting the previous one's location, a MACRO without SIZE inheriting the previous one's, COVER pins, several LAYERs, `+ SYNTHESIZED`, routing, quoted `;`, 9-word `.nets` pins, `-0`, `FIXED_NI`, lower-case keywords, a node placed twice). Small, but still cut into many parallel chunks |

`make_extra_dirs.sh` builds the last two as symlink farms, since `DataBase` only reads a DEF
named `floorplan.def`.

## Files

| | |
|---|---|
| `parse_bench.cpp` | parse one design dir, print the time, optionally `--dump` the canonical state. Built exactly like the host |
| `compare_parsers.sh` | the gate: native dump per input, sha256 vs `limbo_dump_hashes.txt`, exit non-zero on any difference (`JOBS=4` parallel by default) |
| `limbo_dump_hashes.txt` | sha256 of Limbo's dump for each of the 86 inputs, keyed by the same tag the script derives from the directory |
| `run_bench.sh` | best-of-`REPEATS` parse time for all 44 designs |
| `best_of.sh` | the same for a few named designs, for tuning |
| `sampler.hpp`, `sampler_report.py` | an in-process sampling profiler (`SAMPLE_PROFILE=<file> build/parse_bench …`, then `python3 sampler_report.py build/parse_bench <file>`; `ONLY=main` keeps the main thread). This box has no `perf`, and `ptrace_scope=1` rules out gdb sampling |

## Known Limbo behaviours the bridge deliberately did NOT reproduce

- **Limbo `exit(0)`s on a LEF `NONDEFAULTRULE`**: its default `lef_nondefault_cbk` prints a
  reminder to stdout — which the old host had redirected to `/dev/null` — and calls `exit(0)`.
  The old host therefore quit silently, status 0, nothing placed, on all 20 `ispd2015_fix`
  designs. The bridge overrides that callback (as a no-op, which is what `DataBase` kept from
  it) so those DEFs can be compared at all. The native reader skips the block.
- Bookshelf `.wts` net weights, `terminal_NI`, `.shapes` and `.route` hit the same Limbo
  `exit(0)` default; the native reader reports them as unsupported errors instead. None of our
  inputs contain them.

The gate has been shown to fail on a broken reader: a mutant giving `UNPLACED` DEF components
(0,0) instead of (−1,−1) changed 32,281 lines on mgc_fft_1; a mutant nudging `.nets` offsets by
1e-6 changed 41,071 on mms/adaptec1.
