# sw_only cleanup — 2026-07-30

Acting on the fresh-eyes codebase review from the same session. **Scope: `sw_only` only**, per your
call. pl_algo items and the sw_only/pl_algo fork merge were parked as TODOs instead (see bottom).

**Bottom line:** every change is behavior-preserving. The 60-iteration golden trajectory on
`mgc_fft_1@512` (seed 42) is **bit-identical** (`md5 8019a989…`) after each individual change and
after all of them combined, verified against a baseline captured before any edit.

---

## Verification method

Fixed-seed config (`random_seed = 42`, `bins_per_row = 512`, iterations pinned to 60) on
`ispd2015/mgc_fft_1`. Baseline `iterations.dat` captured *before* the first edit; re-checked after
every step. Also run: a full clean rebuild from scratch, `ispd2005/adaptec1` (211k cells, exercises
the fixed-macro footprint branch), and the `partials_compute_method = "simple"` backend that the
default config never touches. No warnings, no errors.

| checkpoint | md5 of `iterations.dat` |
|---|---|
| baseline (pre-change) | `8019a989d7d74731c0e672a4acb23549` |
| after every step, and final | `8019a989d7d74731c0e672a4acb23549` |

---

## What changed

### 1. Hot-loop copies — `const auto&` (38 sites)
`getComponents()` / `getIOPads()` / `getNets()` return `const map<string, T*>&`, so
`for (auto item : …)` was **copying a `std::string` per element per iteration** — in
`combineGradients`, `stepAllNodes`, `computeLipschitzEstimate`, `computeOverlaps`,
`updatePrecondWeights`, `computeHpwlPartials`, and the snapshot/restore loops. One site
(`DataBase.cpp:748`, `mmv_nets_by_degree`) was copying a whole `std::vector<Net*>` per element.

The compiler caught the single loop that genuinely mutated (`Logger::printFunctionStats`); it only
reads, so its binding became `const FunctionStatBlock&` rather than reverting to by-value.

Measured A/B on `mgc_fft_1@512`, 3 runs each: **5.22 / 5.25 / 5.27 s → 5.04 / 5.08 / 5.10 s
(~3.3%)**. That understates it — this benchmark is DCT-bound (3.45 s of 5.3 s is
`computeElectricFields_DCT`). The saving scales with movable-cell count, so expect more on
adaptec/bigblue-class designs.

### 2. Dead threading scaffolding removed
Nothing in sw_only was threaded. Removed:
- `MAX_THREADS` member + its `require<int>` config read + the `max_threads` config key
  (comment already admitted "multithreading not yet implemented" — a knob that lied).
- `Node::m_mutex` and `Node::lock()` / `unlock()` — never called.
- `get_index(std::thread::id)` — defined in `Common.cpp`, never called.
- `<thread>` / `<mutex>` from `Common.h` (they were being pulled into every TU).

`Logger`'s mutex is **real** (singleton guard + `updateFunctionStats`) and was left alone; it has
its own `<mutex>` include.

### 3. `makeflags.mk` — removed a build path that could not work
`BUILD_XRT=1 HOST=sw_only` added `GraphDriver.cpp` to `HOST_SRCS`. **That file does not exist in
sw_only** — the build would simply fail. `USE_AIE_ACCELERATION` / `USE_XILINX_XRT` guard nothing in
this variant (zero references). Replaced with a comment stating sw_only is CPU-only by design and
pointing at pl_algo + TODO #9.

Also removed, both verified dead:
- `-I$(PROJECT_ROOT)/pl/src/pl_algo/src`, whose comment described a `use_pl_scheduler` drop-in check
  that doesn't exist — every `param_scheduler` mention in sw_only refers to *XPlace's*
  `param_scheduler.py`, not the PL header. **sw_only no longer includes anything from pl_algo**,
  which also removes a coupling that worked against keeping the two independent.
- `-I$(XILINX_XRT)/include/ -I$(XILINX_VIVADO)/include/` and the commented-out TBB block (TBB backed
  the `orig` partials method, which no longer exists).

Verified with a clean from-scratch rebuild.

### 4. `Partials.cpp` — divide-before-guard (real latent bug)
The four `1.0f / (B.* * B.*)` reciprocals were computed **before** the `if (B.plus.x == 0 || …)`
diagnostic, so the error report only fired after inf had already been produced — and the `assert`s
behind it compile out under `NDEBUG`, leaving a silent NaN to propagate into the gradient. Guard now
runs first.

> ⚠️ **One deliberate behavior change, flag for your review.** On the zero-B path the code previously
> logged and then hit `assert` (active in this build → hard abort). It now sets `m_nan_detected` and
> returns, matching what the NaN check 20 lines below already does — same class of event, and that
> path was deliberately changed from `exit(1)` so "a DSE sweep no longer loses the run". The asserts
> became unreachable and were removed. Easy to revert to an abort if you'd rather it be loud.

Also fixed 3 signed/unsigned comparisons (`size_t i < int net_size`).

**Follow-up (per your review):** both diagnostic bodies were then lifted out into file-local helpers,
`logZeroBTermDiagnostic()` and `logNaNPartialDiagnostic()`, declared just above the function.
`computeHpwlPartials_CPU` went 109 → 93 lines, and more to the point the gradient math is no longer
broken up by 18 lines of string concatenation — the two cold divergence paths are now one call each.
Both helpers take non-const refs because `Term::to_string()`, `Net::to_string()` and `getName()` are
not const in this codebase (noted in a comment above them; making them const is a wider change that
touches `Common.h`/`Net.h`/`Node.h`, so it was left alone).

### 5. `DCT.cpp` — rectangular-transpose bug + matrix copies
`transpose()` allocated `num_rows × num_cols` and then indexed `input[col][row]`. That is only the
transpose for a **square** matrix; for an R×C input it reads out of bounds. Correct now (C×R output,
`output[c][r] = input[r][c]`), with an empty-input guard. Latent only — the bin grid is always
square, which is why the trajectory is unchanged.

Acted on the existing `DCT.h` TODO ("should be operating on the parameter passed by reference, not
copying large data structures"): `transpose`, `DCT_naive`, `IDCT_naive`, `IDXST_naive`, `DCT_fft`,
`IDCT_fft`, `IDXST_fft` now take `const&`. At a 1024² grid the by-value matrix parameter was ~4 MB
per call, and `compute_eField_DCT` alone calls `transpose` 6× per iteration.

### 6. Density footprint — one definition instead of three
The √2 clamp + area-conserving weight + in-die shift existed twice in sw_only
(`Grid::computeBinOverlaps` and `computeOverflow`'s `deposit` lambda), each carrying a "must stay in
sync with the other" comment — and they had already drifted cosmetically (`1.41421356f` vs
`(float)M_SQRT2`). Extracted `computeNodeFootprint()` (`Grid.h` / `Grid.cpp`), mirroring the PL's
`node_footprint.hpp`, and both call sites now use it. Net −34 lines.

Proved behavior-preserving rather than assumed:
- The two √2 constants are **the same float** (both `0x3FB504F3`) — checked directly, not by eye.
- The only other divergence is a zero-area node's weight under `clamp=false` (was `0.0`, now `1.0`),
  which is unreachable: a zero-width/height footprint gives `ox`/`oy ≤ 0` and is skipped before
  weight is applied.

### 7. Doc truth pass
- **`host/src/sw_only/README.md` rewritten.** The old one described a codebase that doesn't exist:
  a `GraphDriver.h` (not in this variant), an `orig = TBB-parallel` partials method, "FFT-based
  stubs are empty" (the FFT works and is verified), `performGradientStep()` / `computeMomentumStep()`
  marked "(stub)" (neither function exists), and "Random initial placement" (it's a Gaussian cluster
  at die center). Now matches the code, with a reading path and the real per-iteration flow.
- `Setup.cpp` — "(comment-stripped) JSON config file" → TOML.
- `AIEplace.h` — dropped the `// or DebugFramework` include comment (removed in `46ca2eb`) and
  rewrote the stale `nlohmann` rationale in terms of toml++.
- `run_config.toml` — `partials_compute_method` listed `aie, cpu, simple, hybrid, threaded`; only
  `cpu` and `simple` exist, the rest `exit(1)`. Same for `density_compute_method`.

### 8. Repo hygiene
Untracked `vck5000/host/src/objs/Visualizer.d` (a stray build artifact; real output goes to
`build/`) and added the dir to `.gitignore`.

> Note: `host/src/sw_only/lib/*.a` **are tracked on purpose and were left alone** — `makeflags.mk`
> links against them (`-L$(HOST_DIR)/lib -llefparseradapt …`) and the vck5000 README explains Limbo
> is painful to build. Removing them would break the build. Flagged in the review as a candidate
> hygiene issue; it isn't one.

---

## TODOs added (not executed)

**`0_TODO/TODO.md` #9 (User Friendliness)** — merge the sw_only / pl_algo host forks into one host
that runs on CPU or offloads to the VCK5000 when available. Written up with the current per-file
divergence table (15 shared files; `DataBase.cpp` alone is 886 diff-lines) and a suggested shape
(extract `host/src/common/`, reconcile `DataBase.cpp` first). Explicitly noted as deferred by you
until pl_algo bring-up completes.

**`0_TODO/TODO.md` #5 (Logger cleanup)** — added a finding turned up while answering "what does the
mutex in Logger actually do?". Short answer: nearly nothing. `Logger::getLogger()` is never called
from anywhere, so `iLogger` is permanently `nullptr` and no Logger object is ever constructed — the
`log_*` helpers work only because `log()` is a *static* member, so `iLogger->log(...)` resolves
statically and never dereferences. That makes the lazy-init `lock_guard` a guard on a function that
never runs. The one live lock (`updateFunctionStats`) is inconsistent: `getFunctionTime()` and
`printFunctionStats()` touch the same `function_stats_map` unlocked, so it would not actually be
thread-safe under concurrency. Not fixed — Logger is a live area under #5 and the right choice
(drop the singleton vs. complete the locking) depends on whether the merged host (#9) is threaded.

**`0_TODO/TODO.md` #10 (new section) — pl_algo cleanup & clarity.** Deferred. Covers:
- Stale docs: `pl/src/pl_algo/README.md` Status ("module internals are stubs" — they aren't) and
  root `CLAUDE.md` ("Next step = Gate 1" — Gate 1 passed); `Driver.cpp`'s obsolete kernel signature;
  `CHECKPOINT.md` misplaced in the pl_algo dir while describing reverted sw_only numerics (the BB
  clamp and `dct_normalize_inverse` no longer exist in any source file).
- Dead/unwired: delete `density_manager.hpp` (a TODO-stub the README lists as a real module);
  document that `bb_reduce` + `param_scheduler` are built-and-verified but not yet in `top.cpp`.
- Structural: `top.cpp` port aliasing (11 modes reinterpreting 12 `m_axi` ports, documented only in
  prose — drift is a wrong answer, not a compile error); `Driver.cpp`'s 18× XRT boilerplate;
  ~14 copy-pasted `run-*` Makefile targets that could use the existing `STAGE4_RUN` define.
- Hygiene: `pl/src/pl_algo/_x/` and the `model/` compiled ELFs are tracked in git; `common.mk`
  defaults `AIE`/`PL` to the dead `markv1` variant.

---

## XPlace faithfulness check on `computeNodeFootprint` (added after review)

Verified line-by-line against XPlace **source**, not against our comments. The refactor itself is
sound; what it inherited is mostly faithful, with two real gaps → written up as **TODO #11**.

**Confirmed faithful:**

| behaviour | XPlace | sw_only |
|---|---|---|
| √2 inflation, per-axis | `mov_node_size.clamp(min=unit_len*√2)`, `unit_len=[bin_w,bin_h]` (`database.py:915`) | `max(w, bin_w*√2)`, `max(h, bin_h*√2)` ✓ |
| area-conserving weight | `expand_ratio = mov_node_area / clamp_mov_node_area` (`database.py:918`) | `weight = (w*h)/(cw*ch)` ✓ |
| centering | `node_pos` **is the centre** (`GPDatabase::getNodeCPosTensor` = `Lx + W/2`); kernel forms `node_pos ± size/2` | `xl = probeX + w/2 − cw/2` (LL convention → same centre) ✓ |
| macros exempt from clamp | via `clamp(min=…)` | via `max(w, …)` ✓ |
| fixed nodes: exact size, no expand | `forward_naive(..., node_weight=1)`, no `expand_ratio` (`initializer.py:29`) | `clamp=false` for FIXED ✓ |
| fixed nodes: **clipped, not shifted** | `x_l = max(…, mgn)`, `x_h = min(…, num_bin−mgn)` (naive kernel:36-37) | FIXED skips the shift branch ✓ |
| fixed density capped | `init_density_map.clamp_(0,1).mul_(target_density)` (`initializer.py:82`) | `Grid::clampFixedDensity(target_density)` ✓ |

So the FIXED-vs-movable asymmetry is real and correct — XPlace genuinely truncates fixed footprints
geometrically rather than shifting them.

**Gap (a) — the movable in-die shift does not match.** XPlace clamps the **position** to
`[die_ll + size/2, die_ur − size/2]` using the **expanded** size, as a projection re-applied on every
gradient evaluation (`calculator.py:27`) — the cell itself moves. sw_only instead clamps position
with the **raw** size in `enforceDieBoundaries()` and then applies a second, non-persisted shift of
the expanded footprint at deposit time. Both keep the deposit in-die, but ours leaves the cell put
and displaces the mass off-centre from it. Self-consistent (the force gather reads the same shifted
bins), not a bug — but not XPlace's formulation, and undocumented until now.

**Gap (b) — movable macros.** XPlace overwrites `expand_ratio` with `target_density` for movable
macros when `target_density < 1.0` (`database.py:921`). sw_only has no equivalent. Inert on ISPD2005
(target_density 1.0) but live on exactly the ISPD2015/MMS movable-macro designs — the same
population as the open MMS under-spreading issue (#4). Flagged as worth a cheap experiment.

Neither is behavior-preserving to change, so I did **not** touch them — both are parked in TODO #11
with the evidence and a suggested decision.

---

## Suggested follow-up

Worth a wider regression before this lands on anything you care about — I verified on `mgc_fft_1`
(bit-identical) plus smoke runs of adaptec1 and the `simple` backend, but not the full suite. Given
every change is provably behavior-preserving I'd expect a clean sweep; the one thing genuinely worth
your eye is the `Partials.cpp` abort→graceful-stop change in §4.
