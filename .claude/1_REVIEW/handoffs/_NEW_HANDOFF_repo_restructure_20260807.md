# HANDOFF — Repo restructure: one host at top level, `vck5000/` for PL+AIE only

*Written 2026-08-07. Companion to TODO #21. Nothing was changed in the repo — this document
and the TODO entry are the entire output of the session that produced them.*

---

## 0. The target shape (as stated)

```
AIEplace/
  host/src/            ONE host. Runs the algorithm software-only, PL-only, or PL+AIE.
  vck5000/pl/          PL kernels for the VCK5000.
  vck5000/aie/         AIE kernels for the VCK5000.
```

Today:

```
AIEplace/
  vck5000/host/src/{common,sw_only,pl_algo}   3 dirs, 2 buildable variants, selected by HOST=
  vck5000/pl/src/{markv1,pl_algo}             selected by PL=
  vck5000/aie/src/{markv1,pl_algo}            selected by AIE=
  vck5000/{Makefile,common.mk}                PROJECT_ROOT := vck5000/
```

---

## 1. Verdict

**Feasible, and worth doing — but it is two changes, not one, and only the first is cheap.**

| | what | cost | risk to Geert |
|---|---|---|---|
| **A. The move** | `vck5000/host/` → `host/`, rewire `PROJECT_ROOT` | ~1 day, mechanical | **low, if ordered correctly** |
| **B. The merge** | three host variants → one host with a backend switch | **days**, and it is TODO #20's hard part | **high — it collides with a rewrite Geert already started** |

The thing to be clear about: **B is not extra work bolted onto #20 — B *is* #20.** "One host that
can run sw / PL / PL+AIE" is the same sentence as "stop maintaining a second copy of the ePlace
schedule in `host/src/pl_algo/src/Placement.hpp`." Doing it under the restructure banner is a
better shape than doing it as a pl_algo-internal fix. But it inherits every one of #20's
preconditions, including the one that says **do not touch the pl_algo algorithm until the schedule
trace and tier-1 coverage are restored**, because right now nothing can tell you whether the
unified host still computes what the old one did.

---

## 2. The Geert question, answered with numbers

### 2.1 Today, the merge is trivial

`git merge-tree --write-tree --name-only HEAD origin/geert` (read-only probe, run 2026-08-07):

```
CONFLICT (content): Merge conflict in .gitignore
Auto-merging vck5000/aie/Makefile          <- clean
```

**One conflict, in `.gitignore`, and it is two independent appends to the tail of the file.**
That is a 30-second resolution. `vck5000/aie/Makefile` — the only other file both sides touched —
auto-merges: Geert changed `DENSITY_INSTANCES` → `DENSITY_CHANNELS` and the `aiesim` prerequisite
path; Mark changed the `PLATFORM` resolution to use `PLATFORM_REPO_PATHS`. Different hunks.

### 2.2 Why it is trivial: the two work-sets barely intersect

Fork point is `a006500` (Merge PR #14, 2026-03-20).

- **Geert since the fork: 12 commits, 42 files.** All but three are *new files* under
  `vck5000/host/src/v2/**` (25 files) and `vck5000/aie/src/v2/**` + `aie/test_data/density/*` (7).
  The three shared ones are `.gitignore`, `Dockerfile` (new), `vck5000/aie/Makefile`.
- **Mark since the fork: 4493 files** (mostly the Limbo submodule extraction and the
  markv1→sw_only rename).
- **Intersection: exactly 2 files** — `.gitignore` and `vck5000/aie/Makefile`.

```bash
cd /home/msears/phd/AIEplace && git diff --name-only a006500 origin/geert | sort > /tmp/g.txt && git diff --name-only a006500 HEAD | sort > /tmp/m.txt && comm -12 /tmp/g.txt /tmp/m.txt
```

### 2.3 …and the real risk is not a merge conflict at all

**`vck5000/host/src/v2/` is Geert's own from-scratch rewrite of the entire host.** From his
`README.md` and `makeflags.mk` on that branch, v2 has its own `DataBase.cpp`, `Parsers.cpp`
(**Limbo removed**, hand-written `cells.lef`/`floorplan.def` parsers), `Library.cpp`
(`ComponentTypeLibrary`, FPGA-targeted), `Grid.cpp`, `Placer.cpp`, `PlacementEngine.cpp`,
`FPGADriver.cpp`, `Logger.cpp`, a **JSON** config (`run_config.json`) and a vendored `json.h`.

So the repo currently contains **two independent, unaware consolidations of the same component**:

- `vck5000/host/src/common/` — Mark's, landed 2026-08-04 (TODO #9). Limbo-based, ASIC/Bookshelf,
  TOML config. Shared by `sw_only` and `pl_algo`.
- `vck5000/host/src/v2/` — Geert's, last touched **2026-06-19**. No Limbo, FPGA library model,
  JSON config. His own README says v2's `performGradientStep()` and `computeMomentumStep()` are
  **stubs** — algorithmically it is a scaffold, well behind `sw_only`.

Git will merge these two happily forever, because they never touch the same file. **That is the
danger, not a safeguard.** This is the same failure mode as auto-memory
`cross-variant-coupling-is-invisible`: the coupling is semantic, and the build cannot see it.
"One host" cannot mean four hosts. Someone has to decide whether v2 is the future data model
(FPGA placement is presumably where Geert is going), whether `common/` is, or whether they are
deliberately two targets. **That decision is Mark's and Geert's, not a merge resolution's**, and
it should be made *before* step B, not discovered during it.

### 2.4 Geert's recent work is in the AIE, not the host

```
81efdb5 2026-07-10  feat: add fft only pipelines to AIE (64-bit PLIO + 4 tiles)
d7e696c 2026-07-03  WIP(density): add ksi ifft pipelines
32411a4 2026-07-03  WIP(density): add fft and ifft graphs
eb08f2d 2026-07-02  WIP(aie): basic density fft pipeline
5e18239 2026-06-19  Refactor(v2): attach new DataBase to AIEplace placer   <- last host commit
```

His last four commits are all `vck5000/aie/src/v2/`. **The proposal leaves `vck5000/aie/` exactly
where it is**, so his active work-front is untouched by the restructure. What the restructure moves
is the part of his branch that has been dormant for seven weeks. That is about as good as the
timing gets.

Also worth knowing: `origin/geert` is **15 commits behind `origin/main`** and 12 ahead. He is
already carrying a merge debt independent of anything here.

---

## 3. What the move actually breaks

### 3.1 `PROJECT_ROOT` is `vck5000/`, and everything hangs off it

`vck5000/common.mk` is the hub:

```make
HOST_DIR        = $(PROJECT_ROOT)/host/src/$(HOST)
HOST_COMMON_DIR = $(PROJECT_ROOT)/host/src/common
BUILD_DIR       = $(PROJECT_ROOT)/build
AIE_DIR         = $(PROJECT_ROOT)/aie/src/$(AIE)
PL_DIR          = $(PROJECT_ROOT)/pl/src/$(PL)
```

and the per-variant `makeflags.mk` files reach *upward* out of it:

```make
THIRD_PARTY = $(PROJECT_ROOT)/../third_party     # sw_only, pl_algo, and Geert's v2
```

Moving `host/` to the repo root means `PROJECT_ROOT` can no longer serve both. The clean fix is to
introduce **`REPO_ROOT`** (the git root) and keep `PROJECT_ROOT` meaning "the vck5000 platform
dir", then:

```make
HOST_ROOT   = $(REPO_ROOT)/host
THIRD_PARTY = $(REPO_ROOT)/third_party           # no more ../ climbing
```

This is a small edit but it is in the one file **every** Makefile includes, so it wants to be its
own commit.

### 3.2 Blast radius: 92 references across ~20 directories

```bash
cd /home/msears/phd/AIEplace && git grep -In -e 'vck5000/host' -e 'host/src' -e 'HOST_DIR' -e 'HOST=' -- . ':(exclude)third_party' | wc -l
```

Concentrations: `vck5000/tools/` (8 files — the DSE/benchmark/regression drivers),
`vck5000/test/regress/run_regress.sh` (hardcodes
`$PROJECT_ROOT/build/hw/host/sw_only/aieplace_sw_only.exe` and `cd`s to `vck5000/` because *every
path in a frozen regression config is relative to `vck5000/`*), `vck5000/pl/src/pl_algo/`,
`CLAUDE.md`, `.gitignore`.

**`test/regress/` is the one to be careful with.** The three frozen configs
(`configs/*.toml`, `configs/slow/*.toml`) contain benchmark paths relative to `vck5000/`. If
`host/benchmarks/` moves with `host/`, those configs change — and a frozen config that changes is
exactly what the regression suite exists to notice. Options: (a) leave `benchmarks/` under
`vck5000/` (it is data, not host code), (b) move it and regenerate the three baselines with an
explicit `--reason`. **(a) is strongly preferred** — regenerating a baseline to accommodate a
directory move destroys the tripwire's whole value for that change. See
`vck5000/test/regress/README.md`.

### 3.3 Pre-existing breakage the move will expose

`vck5000/aie/src/markv1/makeflags.mk:11` reads:

```make
AIE_FLAGS += -include="$(PROJECT_ROOT)/host/src/include"
```

**That directory does not exist** and has not for some time (`ls vck5000/host/src/include` →
`No such file or directory`). It is silently a no-op `-include` today. Fix it in passing; don't
"port" it.

Also: `vck5000/.git/` exists as a stray directory containing only an empty `info/`. It is not a
repository (the real one is at the top level) and it is not tracked. Harmless, but it will confuse
anyone who runs `git` commands from inside `vck5000/`. Delete it as part of the sweep.

### 3.4 The two hosts have genuinely incompatible build flags

Not a blocker, but the unified host must reconcile these rather than pick one:

| | `sw_only` | `pl_algo` |
|---|---|---|
| optimization | `-O2` (deliberate — `-O0` perturbs the golden's low bits) | `-O0` |
| OpenMP | `-fopenmp`, threaded over nodes/nets/rows (TODO #12) | none |
| C++ ABI | `-D_GLIBCXX_USE_CXX11_ABI=0` throughout (Limbo) | `=0`, **except `Driver.o`** which is forced to `=1` for XRT |
| XRT | never | `ifdef BUILD_XRT` |
| config | TOML | TOML |
| visualizer | offline tool only (TODO #16) | none |

The per-TU ABI override in `pl_algo/makeflags.mk` is the interesting one — it already demonstrates
the pattern a unified host needs (old-ABI parser side, new-ABI driver side, `PackedDesign` as the
ABI-neutral boundary). Keep that, don't re-derive it.

---

## 4. Recommended ordering (this is the part that protects the merge)

**The single highest-value decision is to merge Geert *before* restructuring, not after.**

Rationale: today's merge is one `.gitignore` conflict. After the move, Geert's 25 `host/src/v2/**`
files are *adds on his side into a directory Mark deleted on his side*. Git 2.50's directory-rename
detection (default `merge.directoryRenames=conflict`) will flag it rather than silently misplace
them — but it becomes a manual relocation of 25 files instead of a no-op. Merging first makes the
restructure a rename applied to a tree that **already contains v2**, so v2 moves with everything
else and Geert's next merge sees a normal rename.

```
1. Merge origin/geert into the integration branch NOW.
   Resolve .gitignore (both appends, keep both). Everything else is automatic.
   -> Tell Geert first. This is his branch; do not merge it without a word.

2. PURE-RENAME COMMIT. `git mv` only. Zero content edits. Build is broken at this commit
   and that is fine and intended.
      vck5000/host/  ->  host/
   Content edits in the same commit as a move defeat git's rename detection and turn every
   future cross-branch merge into a manual diff. This commit must be `git show --stat`-clean:
   100% renames, nothing else.

3. BUILD-REWIRE COMMIT. REPO_ROOT/PROJECT_ROOT split in common.mk; host/Makefile;
   the three makeflags.mk; vck5000/Makefile's `$(MAKE) -C host`.
   -> verify: `make host HOST=sw_only`, `make host HOST=pl_algo`, `make host HOST=v2`
      all build. KEEP the HOST= selector working through the whole transition.

4. SWEEP COMMIT. The 92 path references: tools/, test/regress/, docs, CLAUDE.md, .gitignore.
   -> verify: `make test` (seconds) and `make test-regress` (~12 s) both green,
      WITHOUT regenerating any baseline. If a baseline needs regenerating, stop —
      something moved that should not have.

   ===== STOP. Steps 1-4 are shippable on their own and are the whole of change A. =====
   ===== Everything below is change B and is gated on TODO #20's step 1-3.        =====

5. Decide what v2 is. Mark + Geert. Not a merge resolution.
6. Restore dumpScheduleTrace + tier-1 coverage  (TODO #20 steps 1-3). Non-negotiable
   prerequisite: without it there is no way to show the unified host still computes
   what sw_only computes.
7. Collapse Placement.hpp into the sw_only schedule behind a backend interface.
```

### Why steps 2 and 3 must be separate commits

Stated again because it is the single mechanical thing that decides whether Geert's next merge is
five minutes or a day: **git detects renames by file similarity, per file.** A commit that moves
`Foo.cpp` and also edits it may fall below the similarity threshold, at which point git sees a
delete plus an add, and any change Geert made to `Foo.cpp` is a delete/modify conflict he must
resolve by hand. A commit that *only* moves files is 100 % similarity on every one and always
detected.

---

## 5. What "one host, three modes" has to mean concretely

The proposal says the one host "will be able to run the algorithm as software only, pl only or
pl+aie". Three readings, and they are not the same amount of work:

1. **Compile-time**: `HOST_BACKEND=sw|pl|pl_aie`, one binary per backend. Cheapest. Preserves the
   current ABI/OpenMP/optimization divergence trivially (the flags just key off the backend).
2. **Link-time**: one binary, backend chosen by an abstract interface + which `.o` you link.
   Middle. Requires the XRT ABI boundary to be real, which `PackedDesign` already is.
3. **Run-time**: one binary, `--backend=` at the command line. Most useful for A/B-ing the golden
   against the device on the same input in the same process; also the most work, and it forces
   XRT into the sw-only build.

**Recommendation: (2).** It gets the actual goal — one algorithm, one schedule, one convergence
test, three executors — without dragging XRT into the CPU golden's link line, and the existing
old-ABI/new-ABI split already sits exactly on the right seam. Say which one is meant in the TODO
before anyone starts, because the answer changes `common.mk`.

The natural backend seam, from the current code:

| stage | sw | pl | pl+aie |
|---|---|---|---|
| HPWL partials | `Partials.cpp` | `hpwl_gradient.hpp` | AIE graph |
| density binning | `Density.cpp` | `density_bin.hpp` | PL |
| DCT/IDCT/IDXST | `DCT.cpp` | `dct_1d`+`fft_pl` | PL pre/post + **AIE FFT** |
| field solve | `Density.cpp` | `field_solve_pl.hpp` | PL |
| γ/λ schedule, convergence | `Schedule.cpp` | `param_scheduler.hpp` | PL |
| step / momentum | `Step.cpp` | `iteration_update.hpp` | PL |

Note the last two rows: `Schedule.cpp` and `param_scheduler.hpp` are the pair that TODO #19b showed
had *silently different maths under the same concept*. Unifying the host does not by itself fix
that — it makes it visible, which is the point, but only if step 6 lands first.

---

## 6. Questions for Mark

1. **Has Geert been told?** Merging his branch and then moving the directory his host lives in is
   not something to spring on him. The ordering in §4 is designed so he only ever rebases across a
   rename, but he should know it is coming.
2. **What is `host/src/v2/`?** Kept as a third variant, folded into `common/`, or retired? Change A
   does not need the answer; change B cannot start without it.
3. **Does `benchmarks/` move with `host/`?** Recommend **no** — it is data, and moving it forces a
   regression re-baseline that would mask a real regression. But it is odd to leave design data
   under `vck5000/` when the host is at the top.
4. **Which of the three "modes" in §5?** Recommend link-time (2).
5. **Does `vck5000/test/` stay, or split?** `test/regress/` tests the *host* (would follow it to
   `host/test/`); `test/*.cpp` tests the *PL* (stays). Splitting them is more correct and breaks
   `make test` / `make test-regress` invocations that are written down in CLAUDE.md, the skills,
   and half the reports in `1_REVIEW/`. Recommend: **leave both under `vck5000/test/` for change
   A**, revisit later. The value of not breaking the two commands everyone types is higher than
   the tidiness.
6. **Where does `PROJECT_ROOT`-named-`vck5000` end?** If a second platform ever appears, the
   `REPO_ROOT`/`PROJECT_ROOT` split in §3.1 is already the right shape. Worth naming it that way
   now even though there is only one platform.

---

## 7. One-line summary

**Change A (the move) is safe and cheap, and safest if Geert's branch is merged first — today it
costs one `.gitignore` conflict.** **Change B (one host) is TODO #20 wearing a different hat, it
is blocked on #20's verification prerequisites, and it collides with a dormant parallel host
rewrite (`host/src/v2/`) whose fate is an unmade decision, not a merge problem.**
