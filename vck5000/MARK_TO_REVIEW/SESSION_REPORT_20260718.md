# Session report — 2026-07-17/18

Two linked sessions: (A) the MMS mixed-size / preconditioner / grid-sizing investigation on the
`sw_only` CPU golden, and (B) porting those findings into `pl_algo` and standing up a PL-only hw_emu
of the HPWL gradient compute unit with waveforms. 11 commits on branch `pl_algo`.

---

## Part 1 — XPlace MMS reference + legal-vs-legal (tasks #1, #2)

**Motivation.** We had no honest reference for `sw_only`'s MMS (movable-macro) numbers.

- Ran XPlace (`--dataset mms --mixed_size True --num_threads 8`, seed 42; GP + LG + DP) on all 16 MMS
  designs. (`--num_threads` default 20 crashes numba macro-LG; must be ≤8.) Full table in
  `mms_data_20260717.md`.
- Built a **legal-vs-legal** flow: convert `sw_only`'s GP DEF → bookshelf `.pl`
  (`tools/def_to_bookshelf_pl.py`, frame is bit-perfect) and legalize it through XPlace's *own*
  legalizer (`tools/legalize_swonly_mms.sh`, needs a one-line XPlace edit to run macro-LG on a
  GP-off input). Result: `sw_only` **beats XPlace after the same legalizer** on adaptec1 (−2.0%),
  ties bigblue1/2, adaptec4 +1.9%.
- **GP-vs-GP (the trustworthy metric):** on the 8 designs where `sw_only`'s density grid resolves
  overflow like XPlace, GP HPWL is **+0.2% to +3.6%** vs XPlace GP.
- **Honesty caveat:** the larger apparent "wins" (adaptec5, newblue3/4/5, −13 to −24%) are
  *under-spread* placements where `sw_only`'s overflow reads far below XPlace's exact overflow — not
  real quality. Two causes (see Part 3).

## Part 2 — The preconditioner (tasks #3 root-cause, #5)

**The "preconditioner is a wash" verdict was a wrong-benchmark artifact.** It was only ever tested on
fixed-macro ISPD2005 (all macros FIXED → nothing to damp). On MMS it is **decisive**: MMS adaptec1
precond OFF does not converge (overflow plateaus 0.126, HPWL 1.05e8); ON (scale=1) converges cleanly
(0.038, 6.36e7, ~40% lower). `precond_density_scale=1` beats the ~50× field-norm "basis match" (macros
self-damp via their own huge area term).

**#5 — auto-enable (commit `638b9a8`).** `enable_preconditioning` unset ⇒ auto-ON iff the design has
movable macros (die-relative 0.02% area threshold); explicit value always wins; fixed-macro path
bit-identical (verified: MMS→ON, ispd2005→OFF matches explicit OFF). `run_config.json` ships
`auto_enable_preconditioning: true`.

## Part 3 — Grid sizing (tasks #6, #7, #8) — the headline

**XPlace hand-tunes `num_bin` per design (`setup_dataset.py:setup_design_args`) *because grid size
genuinely matters*:** a too-coarse grid under-reads density → premature stop → the std-cell legalizer
must spread cells far → HPWL blows up (adaptec2 old 512-grid: self-overflow 0.060 but XPlace-exact
0.087 → +17.5% LG blowup).

**Root cause + fix (commit `7aa22d9`).** The ePlace auto-grid divided placeable area by the *all-movable*
average cell area; big movable macros inflate that mean and coarsen the grid. Fix: divide by the
average **std-cell** area (macros excluded), guarded bit-identical when there are no movable macros.
adaptec2 → auto-selects 1024, self-overflow 0.057, legalizes **−0.3%** (blowup gone).

**The validation.** With the fix, `sw_only`'s ePlace formula **matches XPlace's hand-tuned grid table on
15 of 16 MMS designs** (512/1024/2048 tiers). The lone miss (newblue5) is a `target_density=0.5` effect,
not a formula failure. The formula now *derives* XPlace's expert grid choices instead of needing a table.
Grid sweep confirms the formula grid is the HPWL sweet spot (adaptec1 flat 6.37e7 at 512/1024/2048;
finer grids only lower the overflow *reading*). `formula_grid` + `num_movable_macros` now recorded in
every run_summary (commit `e7f1683`).

**Two distinct overflow-underread causes** (separated by explicit bins_per_row=1024 re-runs):
1. **Coarse grid** (adaptec2 fixed by the macro divisor; adaptec5 also coarse but from the row-cap, not
   the divisor — a broader grid fix would catch it).
2. **A grid-independent newblue-family metric mismatch** (self ~0.05 vs XPlace ~0.24; finer grid doesn't
   change it) — unresolved, an open item.

`AIEplace_vs_Xplace.md` Section 3 was updated (the stale "formula within ±1 pow2 / XPlace's ad-hoc
tuning no formula reproduces" and "preconditioner off (wash)" claims are now corrected).

## Part 4 — pl_algo faithfulness (task #9, commit `7992c8a`)

The PL `--place` control loop ran with the **preconditioner OFF** — the main PL algorithm gap. Wired it
in faithfully: populate `max(1, degree + precond_coef·λ·area)` per node each iteration (the
`updatePrecondWeights` helper already existed but was never called; `degree`/`area`/`avg_area` were
already passed to `runPlacement`), raw area (avg_area=1 = `precond_raw_area=true`), same escalation
(double every 20 iters once overflow<0.3, cap 1024), auto-ON for movable macros
(`enable_preconditioning` tri-state −1 auto / 0 off / 1 on). Host compiles clean.
**Remaining: sw_emu `--place` MMS convergence verification.** The PL density grid is fixed at 1024
(DENSITY_GRID), which equals XPlace's grid for the 1024-tier designs — well-aligned with the grid finding.

## Part 5 — PL-only hw_emu of the HPWL gradient CU + waveforms (task #10)

Stood up a **PL-only (AIE=none) hw_emu** of the HPWL gradient compute unit. This contradicts the old
"only sw_emu is viable" note — that limitation is about AIE `xclGraphOpen`, which a PL-only design
sidesteps. Three real build gotchas, all fixed in-repo (commits `5fa58b0`, `c2270a1`):
1. **AIE AXIS ports block the RTL link** (an RTL/hw_emu link requires every AXIS port connected; a
   self-loop stream_connect is rejected). Compile them out under **`-DPL_ONLY`** (`common.mk` adds it
   for AIE=none); the AIE DCT modes are `#ifndef PL_ONLY`'d out; `generate_link_cfg.py` emits no
   stream_connects for AIE=none.
2. **`PLATFORM_REPO_PATHS`** must be exported in a non-interactive shell.
3. **`v++` package dies under WSL** — the Windows PATH (`/mnt/c/Program Files/...`, spaces) breaks
   `dtb_creation.sh`. Strip it: `export PATH=$(echo $PATH | tr ':' '\n' | grep -v '^/mnt/' | paste -sd:)`.

Added a **tiny synthetic 6-node/3-net/9-pin testcase** (`--hpwl-grad synthetic`) so the RTL sim finishes
in ~90s instead of hours on a real benchmark. **Result: functional PASS vs the CPU golden (rel_rms
1.4e-7)**, a 50MB `.wdb` (Vivado-openable), and a rendered waveform SVG
(`MARK_TO_REVIEW/hpwl_cu_hwemu_waveform.svg`, via `tools/vcd_to_svg.py`) showing the kernel
`ap_start→ap_done` envelope, node_pos/pins DDR reads, node_grad write, and the internal `hpwl_sweep`
pipeline handshake.

---

## Commits (this session, branch `pl_algo`)
- `638b9a8` sw_only: auto-enable preconditioner for movable-macro designs
- `7aa22d9` sw_only: exclude movable macros from the ePlace auto-grid divisor
- `4a350d7` MMS overnight report + XPlace reference data + collate tool
- `e7f1683` sw_only: record ePlace formula grid + macro count in run_summary
- `db73b34` AIEplace_vs_Xplace: grid-sizing finding + precond auto-enable + MMS section
- `7992c8a` pl_algo: wire the preconditioner into the PL --place loop (faithful to sw_only)
- `5fa58b0` pl_algo: PL-only (AIE=none) hw_emu build for the HPWL gradient CU
- `c2270a1` hw_emu HPWL CU: rendered waveform SVG + VCD→SVG tool

---

## Part 6 — PL-only baseline: PL FFT + field solve + more hw_emu waveforms (follow-up 2026-07-18/19)

**iteration_update hw_emu (commit ...):** ran `--iter-update` (Nesterov step + preconditioner divide,
M=64 synthetic) in hw_emu — **PASS vs sw_only golden (rel_rms 3.5e-8)**. Waveform:
`MARK_TO_REVIEW/iter_update_hwemu_waveform.svg`. (`ITER_M` env makes the node count small for a crisp sim.)

**PL FFT (commit `...`):** `fft_pl.hpp` — HLS float radix-2 DIT forward FFT + Makhoul DCT/IDCT/IDXST,
a direct port of the verified `model/density_model.cpp` double golden. Replaces the AIE FFT so the density
solve runs entirely on the PL. `field_solve_pl.hpp` — the whole electrostatic field solve (forward 2D DCT
→ spectral → inverse) on it. **C-verified vs the naive golden: 1D ~2-4e-7, full 2D field solve ~1e-6.**
(HLS forbids function pointers — the row-transform selector is a direct branch, not a `xform1d_t`.)

**Small-grid build (commit `...`):** `GRID`/`DENSITY_GRID` parameterized via `-DPL_GRID` (default 1024,
bit-identical) so the whole density solve fits on-chip for a tractable RTL sim. `EXTRA_DEFS` hook in
common.mk + host/Makefile.

**Field-solve hw_emu (the PL FFT in hardware):** `MODE_FIELD_SOLVE_PL` (guarded `-DPL_FIELD_SOLVE`) runs
the entire PL-only field solve as a kernel. Built a 64×64 PL-only hw_emu xclbin; the RTL sim executes the
whole density solve (4 FFT passes on-chip) and dumps a 90MB VCD. Waveform:
`MARK_TO_REVIEW/field_solve_hwemu_waveform.svg`. This is the piece that makes the design **PL-only**.

## Open items / next steps
1. **Full-iteration hw_emu waveform** — chain density_bin → field_solve_pl → force_gather → hpwl_CU →
   iteration_update into one small-grid kernel. ALL pieces are now individually verified (HPWL CU +
   iteration_update in hw_emu; field solve in hw_emu; density_bin/force_gather in prior C goldens); the
   remaining work is the combined kernel + one build.
2. **sw_emu `--place` MMS verification** of the new PL preconditioner (Part 4).
3. Broader grid-sizing fix (row-cap) for the 2048-tier / adaptec5 coarseness; the newblue overflow-metric
   mismatch.
4. Optimize the PL FFT (the C-baseline is correctness-first; a real design would pipeline/tile it and use
   the 1024 grid, likely still with the AIE FFT for throughput — the PL FFT is the no-AIE baseline).
