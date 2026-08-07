# AIEplace — project notes for Claude

## TL;DR — where the project stands
*Last updated 2026-08-06.* **Whenever you write a handoff, checkpoint, or end-of-session
summary, update these four lines as part of writing it.** A stale TL;DR is worse than no
TL;DR, because it gets believed. If what you're summarising doesn't change any of these
lines, say so and leave them alone.

- **Working on:** two threads. (1) **sw_only algorithm quality vs XPlace** — the active one, see
  TODO #19; (2) `pl_algo` (branch `pl_algo`) — moving the entire placement iteration onto the PL.
- **Done:** *sw_only:* two-phase mixed-size flow with LP macro legalization, plus TODO #19's two
  XPlace faithfulness fixes (every XPlace overflow metric excludes fillers; the γ/λ throttle gates
  on the preconditioner ratio κ, not a gradient ratio). Confirmed on all 16 MMS designs:
  **post-DP HPWL +0.74% vs XPlace over all 16** (was +1.15%), **15/16 runs converge** (was 6/16),
  post-DP density parity unchanged. adaptec3 joined the table once TODO #3's LG/DP-harness bug was
  fixed (we had been patching the wrong `.pl` template), and is now the suite's best at −2.69%.
  Regression baselines regenerated and green.
  *pl_algo:* all datapath modules written; HLS C-synthesis clean; each verified against the
  sw_only CPU golden one at a time; `bb_reduce` + `param_scheduler` built and verified — but read
  the next bullet before trusting "verified": those goldens are late-July.
- **In progress:** *sw_only:* TODO #19 is landed and measured; what is open is small — decide
  whether to retire the `schedule_gate_metric` toggle, and look at **newblue4**, the one design
  the fix did not convert to `converged`. ✅ **#19 NARROWS the pl_algo drift on the schedule axis**
  (an earlier note here said "widens" — wrong): `pl_algo`'s `sched_dff` closed form
  `c·λ/(1+c·λ)` is *algebraically* XPlace's κ, so pl_algo has been right all along and sw_only was
  the one that was wrong. The two now agree. `sched_verify`'s own dff_coef-constancy check has
  been reporting the disagreement as a **633,000× spread** for weeks — printed, never asserted.
  *pl_algo:* **TODO #20 opened 2026-08-06 — do NOT compose Stage 5 first.** pl_algo's algorithm is
  frozen at the **2026-07-14** sw_only, and the mechanism that would have caught that
  (`dumpScheduleTrace()`) was deleted from sw_only as dead code on 07-28, so `make test`'s green
  `sched_verify` checks against a 07-18 golden and always will. Restore the trace and the tier-1
  coverage (3 of 17 modules today) *before* the resident loop. `top.cpp` is still a mode-switch
  bring-up scaffold, the host still owns the γ/λ schedule one round-trip per iteration, and
  `make host HOST=pl_algo` needs one `make clean HOST=pl_algo` (stale `.d`, not a source break).
  Assessment: `1_REVIEW/reports/_NEW_REPORT_pl_algo_stage5_assessment_20260806.md`.
- **To verify anything:** `cd vck5000 && make test` (pl_algo, seconds, no Vitis) and
  `make test-regress` (sw_only vs committed baselines, ~12 s). See
  *Verification Loop* below before writing or checking any module.

## ⚠️ The Bash tool runs on Windows, not WSL — wrap every command in `wsl`
The `Bash` tool executes in **Git Bash / MINGW64 on Windows**, even though this project
lives in WSL. Symptoms when you forget: `uname` reports `MINGW64_NT…Msys`, WSL paths like
`/home/msears/phd/AIEplace` return `No such file or directory`, and the Linux toolchain
(Vitis, XRT, `make`, `v++`) is missing. The working dir shows up as the UNC path
`//wsl.localhost/Ubuntu/…`.

**Fix — run everything inside WSL by wrapping the command:**
```bash
wsl -e bash -c "cd /home/msears/phd/AIEplace && <your command>"
```
Use real WSL/Linux paths (`/home/msears/…`) *inside* the wrapper, not the Windows UNC path.
Anything touching the repo build, the toolchain, or Linux tools must go through `wsl -e bash -c`.

## 🔧 A fresh clone must bootstrap third_party first
`bash vck5000/tools/bootstrap_third_party.sh` — fetches the **Limbo submodule**
(`third_party/Limbo`, pinned to upstream tag 3.5.2) and builds it out of tree into
`third_party/limbo_{build,install}` (both gitignored). Without it `third_party/Limbo/` is empty
and the host build fails on missing `limbo/parsers/...` headers. **No `.a` is tracked in this
repo** — if you find yourself wanting to commit one, that is the bug. Details + the
`-DBoost_NO_BOOST_CMAKE=ON` gotcha: `vck5000/host/README.md`.

## 📋 Always read vck5000/0_TODO/TODO.md for context
Before starting work, read `vck5000/0_TODO/TODO.md` to understand current priorities, blockers,
and in-progress tasks. This file is the source of truth for project state and helps avoid
re-doing work or working on stale branches.

## 📄 Naming what you hand Mark — the filename carries the metadata
Work here is asynchronous and multi-session. A directory listing must tell you a document's
**read state** and **age** before you open it, because the moment you read the filename that
context is already loaded — and because another session will read the same name without any of
your context. Both halves matter: the writing rule and the reading rule.

```
[_NEW_]<TYPE>_<brief_description>[_<YYYYMMDD>].<ext>
```

**Writing**
- `<TYPE>` — one of `REPORT`, `HANDOFF`, `PLAN`, `EXPLAINER`. Deliberately small; if none fits,
  it is a `REPORT`. Type leads so a listing groups by kind, then description, then date.
- `<brief_description>` — snake_case, 2-4 words (`footprint_ab`, `viz_offline_tool`).
- `_<YYYYMMDD>` — **only** for files in `1_REVIEW/` and `2_ARTIFACTS/`. Nothing else gets a date;
  a run directory is already timestamped, and dating a file twice is worse than not at all.
- `_NEW_` — **only** on a document written for Mark to read that he has not read yet. Not on
  intermediate output, not on anything a tool consumes, not "because it is recent."
  **Only Mark clears the prefix. Never drop it yourself**, and never add it to something that was
  not written for him — if everything is `_NEW_`, nothing is. The leading underscore is load-bearing:
  it sorts the unread set to the top of a listing in File Explorer, VS Code and PowerShell.

**Does not apply to** code, machine-read data (`*.tsv`/`*.csv` an `analyze_*.py` reads), files
inside a timestamped run directory, fixed-name contracts (`manifest.json`, `results.csv`,
`iterations.dat`, `config_used.toml`, `nodes_gen<N>.bin`), or scratch. Renaming a contract breaks
its reader; that is the whole reason this rule is scoped to documents.

**Reading — this is the half that prevents misconstrual**
- `_NEW_` = **unreviewed**. It is a proposal, not a decision. Do not cite it as agreed, established,
  or settled, and do not build on its conclusions without saying they are unconfirmed.
- No prefix = Mark has read it. **That means read, NOT correct.** A reviewed report can still be
  wrong and can go stale without being amended — `1_REVIEW/reports/REPORT_phase2_mms_suite_20260802.md`
  §5 asserted ~15 XPlace GPU re-runs were needed, was retracted 2026-08-04, and kept costing
  sessions time until a pointer was appended to it on 2026-08-06. Its correction still carries
  `_NEW_` (unread), so for four days the wrong file read as authoritative and the right one read
  as a draft. **Prefix state is about attention, never about truth.**
- The date is **when it was written, not when it was true.** Check `0_TODO/TODO.md` before treating
  a dated claim as current.

Unread files sort to the top ahead of the type grouping. That is intended — the `_NEW_` set is
small by design, so it should be the first thing a listing shows. The 19 pre-existing `NEW_` files
were renamed to `_NEW_` on 2026-08-06 with their cross-references updated; the older un-prefixed
files were left alone, since retro-fitting `<TYPE>_` onto files Mark has already read buys nothing
and breaks links.

## What this is
AIEplace ports the ePlace analytical placement algorithm onto the AMD Versal VCK5000
(Programmable Logic + AI Engines) for acceleration. Design variants live under
`vck5000/{aie,pl,host}/src/<variant>/`, selected by the make vars `AIE=`/`PL=`/`HOST=`
(host defaults to `sw_only`; `AIE`/`PL` default to `markv1`).

- **`sw_only`** (`HOST=sw_only`) — the working, tuned software-only golden reference: the full
  placement iteration on the CPU. Golden reference used to verify the new design, but still
  under construction to match XPlace. (Renamed from `markv1`; the partial-offload hardware
  kernels it was co-developed with remain under `AIE=markv1`/`PL=markv1`.)
- **`pl_algo`** — the new PL-centric design (git branch `pl_algo`): the entire placement
  iteration runs on the PL; the AIE does only the FFT and the HPWL gradient graph.

**`vck5000/host/src/common/` is NOT a variant** — it is the parser + data model (DataBase, Grid,
Node/Component/IOPad, Net, Bin, Logger, Common) that both host variants build into themselves,
plus the prebuilt Limbo parser libs in `common/lib/`. Landed 2026-08-04 (TODO #9) to end the
silent fork between the two hosts. Fix a parser or geometry bug **there**, once. Nothing in
`common/` may include `AIEplace.h`, `Visualizer.h`, or anything from `pl/`; see its README.

## Algorithm goal: mimic XPlace as closely as possible
sw_only's placement algorithm should track the XPlace reference (`~/phd/Xplace/src/`) as
faithfully as possible. Prefer matching XPlace's formulation over ad-hoc heuristics or
"crutches" that XPlace does not use — e.g. XPlace bounds the Barzilai-Borwein step with its
backtracking line search alone and applies **no magnitude clamp**, so sw_only does the same
(the fixed `[0.0001, 4000]` step clamp was removed). When sw_only diverges from XPlace, that
divergence should be deliberate and documented, not an accidental workaround.

**Before inventing a heuristic, go read how XPlace does it** — don't reason it out from first
principles and don't guess from memory:
```bash
grep -rn "<the quantity>" ~/phd/Xplace/src/
```
If XPlace has a formulation, match it. If it genuinely has none, say so explicitly and flag
that the choice is ours — that is exactly the kind of decision that must be written down
(TODO.md or memory), or a later session will re-derive it from scratch.

## pl_algo current state
**`vck5000/pl/src/pl_algo/DATAFLOW.md` is the single authoritative source** — read it before
touching pl_algo, and update it (not this section) when the state changes. `README.md` next to it
is the orientation/entry point. Only the stable summary lives here:

- **HLS C-synthesis passes** (`cd vck5000/pl && make PL=pl_algo TARGET=hw` → 0 errors, `top.xo`
  built). The datapath modules are written and sw_emu-verified against the sw_only CPU golden,
  one at a time.
- `top.cpp` is still a **bring-up scaffold**: one kernel, one xclbin, a `mode` arg selecting which
  module runs (`host_interface.hpp` `top_mode`). Stage 5 replaces the mode switch with the unified
  per-iteration datapath.
- The device-resident control modules (`bb_reduce`, `param_scheduler`) are **built and verified but
  not yet wired into `top.cpp`** — that is the correct in-progress state, not an oversight. Until
  the resident loop lands, the host (`host/src/pl_algo/src/{Driver.cpp,Placement.hpp}`) owns the
  γ/λ schedule and convergence test, one round-trip per iteration.

## Key design decisions
- Hardware grid is **1024×1024** (sw_only used 64). Matrices (bin density, Ex, Ey) are
  **DDR-resident**, streamed in row tiles; on-chip RAM holds only working tiles.
- DCT/IDCT/IDXST **pre/post-processing runs in PL; the AIE does only the FFT** (one
  forward-FFT config; the transform_mode FSM lives entirely in the PL). IDXST is
  implemented in v1 (Stage 4): the golden `compute_eField_DCT` uses IDXST on *both*
  Ex (x-axis) and Ey (y-axis), and IDXST is nearly free — same FFT + twiddle ROM as
  IDCT, plus an input reversal and an odd-output sign-flip.
- v1 keeps the γ/λ schedule + convergence test **on the host** (it's the most-tuned part);
  expose them as register-mapped values so the policy can migrate onto the PL later.
- **No backtracking in v1** — Barzilai-Borwein/Lipschitz step length only.

## Build / emulation (Versal VCK5000, Vitis 2022.2)
- Source first: `/tools/Xilinx/Vitis/2022.2/settings64.sh` and `/opt/xilinx/xrt/setup.sh`.
  `PLATFORM_REPO_PATHS` and the license MAC-pin are already in `~/.bashrc` / `/etc/wsl.conf`
  (details in auto-memory `hardware_bringup`).
- **Only `sw_emu` is viable for AIE designs** — `hw_emu` lacks `xclGraphOpen` graph control on the
  VCK5000 QDMA platform. Real-hardware (`TARGET=hw`) runs on colleague **Geert's** card.
- Versal is a **3-step flow**: `v++ -c` (.xo) → `v++ -l` (.xsa) → `v++ -p` (.xclbin; include the
  AIE `libadf.a` as a package input).
- An emulation host run needs `$XILINX_VITIS/lib/lnx64.o` and `$XILINX_XRT/lib` on
  `LD_LIBRARY_PATH`.

## Verification Loop
**Every PL module is verified offline against a golden before it goes near the device.**
A module that hasn't cleared steps 2 and 3 isn't done, and optimizing it wastes the effort.

1. **Write the harness.** `test/<module>_test.cpp` — pure g++, no XRT, no HLS,
   runs in seconds. Add it to `HARNESSES` in `test/Makefile`.
2. **Compare against a golden, and state the tolerance in the file.** Goldens come in three
   kinds, all in use today:
   - a **sw_only CPU function** (`host/src/sw_only/src/placer/`) — `computeHpwlPartials_CPU`
     (`Partials.cpp`), `compute_eField_DCT` (`Density.cpp`), `computeOverlaps` (`Density.cpp`)
   - a **naive double-precision reference** written inline in the test (see `field_solve_test.cpp`)
   - a **recorded sw_only trace** replayed row-for-row (see `sched_verify.cpp`)

   Scalar/control paths must match **bit-exact**; float datapath ~1e-6 rel_rms.
3. **Confirm it synthesizes** — `test/synth_check.tcl`. This is a *separate* check from
   numerical correctness; passing one says nothing about the other.
4. **Only then** wire it into `top.cpp` and sw_emu-verify the trajectory vs the golden.

### A test asserts; it does not print
The harness must compute the verdict itself and **exit 0 (pass) / non-zero (fail)**. Printing
`rel_rms=9.8e-07` next to the words "PASS if ~1e-6" is not a test — it's a report that requires
a human to read it, and it will pass forever once nobody does. Three of the five harnesses were
exactly this until 2026-08-05. Keep printing the numbers (drift is informative), but always
*also* compare in code. When adding a threshold, take it from the observed value with real
margin — `field_solve_test` sits at 0.98× a 1e-6 bound, so its bound is 2e-6; a genuine
regression here is orders of magnitude, not a few percent.

**…and that applies to EVERY number it prints as evidence, not just the headline one.** A harness
that asserts its main comparison and prints a secondary one is still half-disarmed, and the
half that is printed is where the bug hides. `sched_verify` derives `dff_coef` from the golden and
its own comment says *"its constancy across the run is itself a check on the closed form"* — then
prints `min=2.34 max=1.48e6`, a **633,000× spread**, takes the median, and exits 0. That check was
correct, deliberate, and reporting a real defect (TODO #19b) for weeks. If a line is worth
printing because it would tell you something is wrong, it is worth an `if` and a non-zero exit.
If it genuinely is not a verdict, say so in the output (`[info]`) so nobody mistakes it for one.

### Three tiers, by cost
| tier | what | cost | needs | how |
|---|---|---|---|---|
| **1 — offline** | `test/*.cpp` vs golden | **seconds** | just `g++` | `cd vck5000 && make test` |
| **2 — synthesis** | `test/synth_check.tcl` | minutes | Vitis | `vitis_hls -f synth_check.tcl` |
| **3 — emulation** | the `run-*` bring-up modes | slow | Vitis + built xclbin | `make run-<mode>` (see `vck5000/Makefile`) |

**Run tier 1 after every edit under `pl/src/pl_algo/src/modules/`** — it costs nothing and it is
the only thing standing between a normalization typo and finding out three weeks later in sw_emu.
Tier 3 is for integration points. A slow test you skip protects nothing.

These three tiers are the **pl_algo** loop. sw_only has its own target at tier-2 cost —
`make test-regress`, see below.

Test *inputs* live in `test/fixtures/` and are committed. They deliberately do **not** live in
`vck5000/results/`, which is gitignored — an automated test cannot depend on a file that isn't
in the repo. See `test/fixtures/README.md` before swapping a fixture; `sched_verify`'s
convergence config must match its trace's `config_used.json`.

### sw_only's tripwire is a separate target
The five tier-1 harnesses cover `pl_algo` only. sw_only is covered by **`make test-regress`**
(`vck5000/test/regress/`, built 2026-08-05, TODO #17): it runs the real placer on a **frozen**
config and asserts the per-iteration trajectory and a hash of every final cell position are
**bit-identical** to a committed baseline. `deterministic = true` plus a pinned `random_seed`
make that exact — there is no tolerance and nothing to flake.

```bash
cd vck5000 && make test-regress          # 2 ISPD-2015 designs, ~12 s
cd vck5000 && make test-regress-slow     # + mms/adaptec1: phase 2 + LP legalization, ~3 min
```

**Run it before and after any change to `host/src/sw_only/`.** It is not part of `make test` on
purpose — that suite is only useful because it costs seconds.

Two things it does *not* do. It checks **stability, not quality**: a reproducibly wrong sw_only
passes, so an intended algorithm change still needs a manual A/B against XPlace. And it is
deliberately insensitive to `run_config.toml` — the frozen configs do not track it, so tuning a
hyperparameter there is invisible here. Before regenerating a baseline read
`vck5000/test/regress/README.md`; `--reason` is mandatory precisely so that "the test failed so I
refreshed the baseline" leaves a trace.

### Other references
- Toy bring-up templates (outside this repo, both build + emulate cleanly):
  `~/phd/toy_design` (pure-PL vadd) and `~/phd/toy_aie` (minimal AIE + PL). See auto-memory
  `toy_reference_designs`.

## Running it
- `make run` — builds if needed, then runs `HOST=sw_only` with `host/src/sw_only/run_config.toml`.
- `make test` — the tier-1 suite above.
- `make help` — current variable settings and every build target.

## Coding style for this repo
General style rules are in `~/.claude/CLAUDE.md`; this is the hardware-specific addition.

**HLS code reads differently from pure software.** Annotate the datapath: pragmas,
memory-resource intent (`_URAM`/`_BRAM`/`_DDR` suffixes), and a short note on why a loop is
pipelined/unrolled the way it is. The hardware structure is not obvious from the C, so make it
explicit. Host and model code is plain C++ and wants none of that — there, favor clarity and
brevity and let idiomatic control flow carry the meaning.

### Naming a quantity we borrow from XPlace
**Name it for what it IS; map it to XPlace's name in a comment at the declaration.** XPlace's own
names are frequently poor (`weighted_weight` describes nothing), so copying them verbatim is not
faithfulness, it is just inheriting a bad name. Rename freely — then anchor it:

```cpp
float precond_kappa;  // matches XPlace's weighted_weight (param_scheduler.py:386)
```

One line, at the declaration, naming the upstream symbol **and where it lives**. That is what
makes `grep -rn "weighted_weight" ~/phd/Xplace/src/` round-trip back to our code.

**sw_only and pl_algo must use the SAME name for the same quantity.** This is not tidiness — it is
the only thing that makes a divergence between them visible. TODO #19b cost real time precisely
because both variants had a `density_force_fraction` and they were *different functions*:
sw_only's was a gradient-norm ratio, pl_algo's closed form `c·λ/(1+c·λ)` was algebraically
XPlace's κ. Same name, same units, same range, silently different maths — and no diff, grep, or
test could see it. When you rename on one side, rename on the other in the same change.

**A comment that names an upstream function is a claim. Check the code below it computes that
function.** The κ bug sat under a doc-block that said it was computing `weighted_weight` while the
next line assigned something else. Prefer to make the claim checkable (see *A test asserts*) over
asserting it in prose.

## Keeping `0_TODO/TODO.md` true
TODO.md is the **source of truth for project state** — CLAUDE.md's TL;DR points at it and every
session reads it first. It is also 2000+ lines of corrections layered on corrections, and on
2026-08-06 four checkboxes were still open on work that had already landed. A source of truth
that is 4 items stale is worse than one nobody trusts, because it gets believed. The procedure:

1. **Verify before you inherit.** An unchecked box is a claim about the code, not a fact. Before
   working an item, confirm it against the code — `Setup.cpp` had carried the "unify the two macro
   definitions" fix, with the measurement in its comment, for four days while P2 read as open.
2. **The headline states current truth; superseded text goes into `<details>`.** Do not append a
   correction under a wrong statement and leave both at the same level — that is how
   `#4`/`#8`/`#11` became unreadable. Rewrite the top line, fold the original underneath.
   *Never delete the original* — the retraction trail is the most valuable content in the file
   (see `#11b`'s "RULED OUT" verdict, kept verbatim because it was cited for four days).
3. **State what would falsify it.** "Re-measure before closing" is only actionable with the
   command and the artifact path. `/tmp` does not survive; put durable data in `2_ARTIFACTS/`
   and name it in the entry.
4. **Close the loop the same day the measurement lands.** The re-baseline that closed adaptec5
   finished on 08-04 and the entry still read "pending" on 08-06 — the run was done, the
   conclusion was sitting in `progress.txt`, and two sessions re-derived the open question.
5. **When you close an item, check what it was blocking.** #19b closed #7's first bullet outright;
   nobody would have found that by reading #7.
6. **A number in TODO.md carries its basis or it is not a number.** Which phase, masked or exact,
   which sw_only row, td and grid — see the `xplace-compare` skill. Numbers without that have
   needed retracting three times.
