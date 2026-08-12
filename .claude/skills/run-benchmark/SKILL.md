---
name: run-benchmark
description: >
  Run the AIEplace sw_only placer on a benchmark design and report the result. Use this whenever
  the user asks to run, re-run, or "do a fresh run of" a design (adaptec1, bigblue4, newblue5,
  mgc_fft_a, superblue...), wants a run with a tweaked hyperparameter or config value, wants a run
  that dumps positions so a GIF can be made afterwards, or asks what a design currently converges
  to. Covers deriving a config without touching the tracked default, the per-design target-density
  and grid values that a bare config silently gets wrong, checking the shared box for concurrent
  work first, and reading the result out of the run directory.
  Do NOT use this for: multi-run parameter sweeps or sensitivity studies (that is tools/dse.py and
  tools/morris.py); the sw_only regression tripwire (`make test-regress`); pl_algo hardware
  modules, which are verified offline by `make test` and never run this way; or rendering the
  frames a run produced (that is the `viz-gif` skill, after this one).
---

# Running a benchmark on sw_only

One placement run: build if needed, derive a config, run, report. Minutes, not hours — an ISPD2005
design converges in 600-800 iterations.

## 1. Check the box before you start

The machine and the working tree are shared with other sessions — multi-hour sweeps, and other
agents that edit and commit.

```bash
ps aux | grep -iE 'aieplace|dse.py|morris' | grep -v grep
```

If something is running, say so before competing for the CPU: a placement run alongside a sweep
gives both bad timings, and sw_only takes all cores by default (cap yours with `OMP_NUM_THREADS`
if you proceed anyway). This check also catches the subtler case — another session editing
`default_config.toml` underneath you.

## 2. Derive a config; never edit the tracked one

`host/src/sw_only/default_config.toml` is the shared default. Copy it, edit the copy, pass the copy
— the exe takes a config path as its only argument. Editing it in place fights whoever else is
working, and leaves the repo dirty for a one-off run.

```bash
sed -e 's|^benchmark = .*|benchmark = "host/benchmarks/<suite>/<design>"|' \
    host/src/sw_only/default_config.toml > <scratch>/<design>.toml
```

Then run it from the repo root — paths inside the config are relative to the working directory:

```bash
./build/hw/host/sw_only/aieplace_sw_only.exe <scratch>/<design>.toml
```

`make host HOST=sw_only` first if the binary might be stale; it no-ops when it isn't. (`make run`
also works but only ever uses the tracked default, so it is the wrong tool the moment you need to
change anything.)

### The three settings a bare config gets wrong

**Target density and grid.** MMS and ISPD2005 are Bookshelf — no `placement.constraints` file — so
`target_density` keeps whatever the config says, which is `maximum_utilization = 1.0`. Only
ISPD2015 (LEF/DEF) self-configures. XPlace runs each design at its own tuned density and grid, and
those live in `tools/benchmarks.py::_ROWS` (`(design, suite, tier, grid, target_density)`), copied
from XPlace's `setup_dataset.py`. **Look them up rather than assuming** — read the row, don't
recall it:

```bash
python3 -c "import sys; sys.path.insert(0,'tools'); import benchmarks; print(benchmarks.BENCHMARKS['<suite>/<design>'])"
```

If the design's row says something other than `1.0` / auto, set `maximum_utilization` **and**
`bins_per_row` in your derived config. This is not a detail: an MMS A/B run at td=1.0 instead of
0.5 once produced 1.5M fillers against XPlace's 310k, and the change under test was mathematically
incapable of showing an effect at td=1.0. The run looked fine and answered nothing.

**`random_seed`.** Absent from the default config, so it defaults to a time-based seed and two
"identical" runs differ. `deterministic = true` does not fix this — it fixes summation order across
threads, not the seed. Pin it for anything you might compare against later.

**`iterations_per_dump`, if a GIF is coming.** `dump_positions = true` writes
`<run_dir>/coord_dump/`; the cadence decides how many frames exist, and no amount of re-rendering
adds more. Cadence 20 on a 700-iteration run is a choppy ~35 frames; 5 gives a smooth ~140 at
~4× the disk (~400 MB for adaptec1). Off by default because the dump is large.

State any of these you set, and why, when you report the run.

## 3. Read the result

Everything lands in `results/single_runs/<design>/<timestamp>_<methods>/`:

| file | what |
|---|---|
| `run.log` | full-detail log; the console only ever showed a subset |
| `iterations.dat` | per-iteration `iter, hpwl, overflow, alpha, lambda, phase` |
| `config_used.toml` | the exact config — *this*, not the default, is what the run used |
| `coord_dump/` | node positions, only if `dump_positions = true` |

The end-of-run summary is the thing to quote, but it prints **two HPWLs and two or three
overflows** and they are not interchangeable: one HPWL is masked at `ignore_net_degree = 100` and
one is not, and the overflow rows differ in whether fillers and movable macros are counted (one
row even changes its own label depending on `convergence_include_fillers`). Read the labels on the
actual run rather than assuming which is which.

If the number is going anywhere near an XPlace comparison, read the header comments on
`_XPLACE_MMS_MIXED_GP` / `_XPLACE_MMS_FINAL` in `tools/benchmarks.py` before quoting anything —
they spell out which XPlace line pairs with which sw_only value, and which of XPlace's two
evaluators masks. Picking the wrong row is the single most repeated mistake in this project.

Also worth reporting: **Stop reason** (`converged` vs hitting `convergence_max_iterations`, which
means it did *not* converge) and the best-solution iteration.

## Gotchas

- **`make run` ignores your config.** It runs `HOST=sw_only` against the tracked default. Fine for
  a vanilla run of whatever the default points at; useless once you change anything.
- **A two-phase (mixed-size MMS) run is not done at phase 1.** Macro-heavy designs freeze macros
  and re-seed the standard cells, and phase 1's std-cell placement is *discarded*. Iteration counts
  span both phases — `convergence_max_iterations` is a whole-run backstop, not a schedule.
- **sw_only deliberately leaves one CPU free.** Running 8-of-8 threads busy-spins the master into
  an ~8× slowdown. Don't "fix" the thread count to speed a run up.
- **Timings on this box are not trustworthy** unless step 1 came back empty.
- **Don't refresh a regression baseline because a run looked different.** `make test-regress` is a
  separate tripwire with its own frozen configs; it is insensitive to `default_config.toml` on
  purpose.
