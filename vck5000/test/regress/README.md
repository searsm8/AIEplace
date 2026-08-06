# test/regress — the sw_only regression tripwire

```bash
cd vck5000 && make test-regress          # ~12 s
cd vck5000 && make test-regress-slow     # + the mixed-size design, ~3 min
```

Runs `sw_only` on a **frozen** config and asserts the result is **bit-identical** to a committed
baseline. Non-zero exit on any drift. This closes TODO #17: before it, sw_only — the most-tuned
code in the repo, and the golden every `pl_algo` module is verified against — had no automated
check at all, so a quality regression was invisible until someone eyeballed a sweep.

**This is not part of `make test`, on purpose.** That suite is tier 1 (seconds, pure `g++`) and
earns its keep by being cheap enough to run after every edit. This one builds and runs the real
placer, so it belongs in its own target. See `AIEplace/CLAUDE.md` § *Verification Loop*.

## What it asserts

Two things per design, both exact — there is no tolerance:

| assertion | catches |
|---|---|
| `iterations.dat` matches the baseline **row for row** | any change to the trajectory, *and tells you the iteration it starts at* |
| `sha256` of the output `.def` matches | final-position drift too small to move a printed HPWL digit |

Exact comparison is possible because `params.deterministic = true` makes sw_only reproducible.
That was re-verified when this test was built, not assumed: `mgc_fft_a` and `mgc_pci_bridge32_b`
each produced an identical trajectory *and* an identical final-position hash across repeat runs
and at `OMP_NUM_THREADS` = 1, 4 and unset.

The trajectory is the primary signal and the hash is the backstop. `iterations.dat` prints 4
significant figures, so a sub-0.1% drift can hide there; the hash cannot miss it. When the hash
fails but the trajectory passes, the script says so explicitly — that combination means the
placement moved by less than the printed precision.

### How sensitive, measured

Placement is a chaotic iterative optimization, so a perturbation far below the printed precision
amplifies over hundreds of iterations. Measured on `mgc_pci_bridge32_b` when this test was built:
changing `init_gamma` from `4` to `4.000001` — **2.5e-7 relative**, ~2 ULP of float32 — moves the
printed trajectory by iteration **65** and changes the final-position hash. Comparing the whole
trace is therefore far more sensitive than comparing a final HPWL, and it localizes the change.

The failure paths were exercised, not assumed: a perturbed input goes red with the first
diverging row quoted, a corrupted hash with an intact trajectory produces the "drift below the
printed precision" message, `--update-baselines` without `--reason` exits 2, and an unknown
design name or a missing executable fails cleanly rather than passing vacuously.

## The designs, and why these

| config | design | grid | target density | iterations | wall |
|---|---|---|---|---|---|
| `configs/mgc_pci_bridge32_b.toml` | ISPD-2015 | 128 | 0.143 | 668 | ~5 s |
| `configs/mgc_fft_a.toml` | ISPD-2015 | 256 | 0.500 | 616 | ~8 s |
| `configs/slow/mms_adaptec1.toml` | MMS | 512 | 1.0 | 1373 | ~150 s |

- **ISPD-2015 rather than ISPD-2005 or MMS for the fast tier.** Only ISPD-2015 ships a
  `placement.constraints`, so target density comes from the benchmark and the run cannot silently
  sit at `td = 1.0` — the Bookshelf trap in `mms-needs-explicit-target-density`. The slow config
  is Bookshelf and therefore states `maximum_utilization` and `bins_per_row` explicitly.
- **Two fast designs, not one.** The second is nearly free and is not redundant: they differ in
  auto-sized grid (128 vs 256) and sit at very different target densities, so they exercise the
  grid-sizing formula and the filler path differently. `mgc_pci_bridge32_b` is also the design
  `pl_algo` bring-up uses.
- **`bins_per_row` is left unset in the fast configs** so the run exercises the default
  auto-sizing path. A change to that formula moves the grid and fails the test. That is intended.
- **The slow tier exists because no ISPD-2015 design has movable macros**, so the fast tier
  cannot reach phase 2 at all. `mms/adaptec1` has 62 movable macros and runs both phases plus LP
  macro legalization — most of TODO #13. It is opt-in only because 150 s is too slow to run
  reflexively.
- **ISPD-2019 cannot be used.** `DataBase::readDEF()` only ever parses a file literally named
  `floorplan.def`, and ISPD-2019 ships `<design>.input.def`, so those benchmarks fail to load.
  (`BENCHMARKS.md` independently puts ISPD-2019 out of scope: no XPlace reference data.)

## The configs are frozen snapshots

`configs/*.toml` are **pinned copies**, not files that track
`host/src/sw_only/run_config.toml`. Editing `run_config.toml` does not affect this test, and that
is the point: this is a tripwire on the **code**, not on whatever the tuned default config
happens to be today. A test whose expected output moves whenever someone tunes a hyperparameter
is a test nobody can keep green.

The one addition every frozen config makes is **`random_seed = 42`**. `run_config.toml` omits it,
which defaults to a time-based seed — `deterministic = true` alone is *not* enough. Measured on
`mgc_pci_bridge32_b`: two unpinned runs took 668 and 662 iterations and ended in different
positions.

## Regenerating a baseline

```bash
test/regress/run_regress.sh --update-baselines --reason "why the expected result changed"
```

`--reason` is mandatory and is written into the baseline header, so the reason shows up in the
git diff next to the numbers it explains. **Read the diff before committing it.** The failure
this guards against is refreshing a baseline to make a red test go green: an unexplained baseline
commit is indistinguishable from a silently accepted regression. The body of a baseline is the
full trajectory, so the row where the diff starts tells you *when* behaviour changed, which is
usually enough to say whether the change was the one you meant to make.

Regenerate when you deliberately change the algorithm, and only then. If you changed something
you believed was behaviour-preserving and this test went red, the test is doing its job.

## Layout

```
configs/       frozen run configs -- the exact input that produced each baseline
configs/slow/  same, for designs measured in minutes (opt-in via --slow)
baselines/     expected trajectory + final-position hash, one file per design
work/          scratch output of the run under test (gitignored)
```

## Known gaps

- The fast tier does not reach **phase 2** or macro legalization; only the slow tier does.
- Nothing here checks placement *quality* against XPlace — it checks that behaviour has not
  changed. A run that is reproducibly wrong passes.
