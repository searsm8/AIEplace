# HANDOFF #45: a hardware test flow for `hpwl_gradient_CU` (v++ → sw_emu → hw_emu → card)

*2026-10-19. Branch `pl_algo`, HEAD `5340ac4`. Written at the end of a long session; the next session
starts here. Not started: this file is the brief.*

## The question Mark asked
"Are we ready to build and test the full hw implementation of `hpwl_gradient_CU`?"
**Answer: the module is ready; the flow around it is not.** Nothing in `pl/` can produce an xclbin or
drive one yet. Mark's pending choice (left in the last wrap-up, unanswered): build the flow at a
250 MHz kernel clock, verify in sw_emu (+ hw_emu if PL-only works), stop before the build server —
**or** close timing at 300 MHz on the build server first. Ask him before starting.

## What is done and verified (don't redo)
| item | state | evidence |
|---|---|---|
| tier 1 | `make -C pl test` PASS; real-mode sweep 44/44 | tasks.md #45 step 2 |
| C-synth | every loop II=1, Fmax est. **287.2 MHz** (target 300) at C8 `a98b3fe` | tasks.md #47 C8 entry |
| RTL co-sim | capacity 1536, 16 chunks: hazard 4 PASS, hazard 1 FAILS (negative control) | same |
| post-route | only on predecessors: `hpwl_gradient_computer_v2` **3.955 ns ≈ 253 MHz**, WNS −0.625 | [[_NEW_REPORT_41_next_steps_20261002.md]] §2 |
| software golden | sw_flat `[sw_flat] wa_sums="tree", grad_merge="stream", hpwl="fixed", wa_exp="lut"` equals the CU (g++) **bit for bit** on 6 designs incl. chunked + macros | `make -C host/sw_flat check-record-stream`, [[_NEW_REPORT_45_pl_arithmetic_20261016.md]] |

The kernel top already exists and is Vitis-shaped: `pl/synth/hpwl_gradient_CU/hpwl_gradient_CU_top.cpp`
(`extern "C"`, m_axi bundles gmem0..gmem9, `s_axilite` `hpwl_total_fixed`, `-flow_target vitis`, part
`xcvc1902-vsvd1760-2MP-e-S`). Use it as the `v++ -c` input; don't write a second top.

## What is missing (the work)
1. **`pl/` build flow.** `v++ -c` (top → .xo) → `v++ -l` (.xsa; connectivity cfg mapping the bundles
   to DDR, kernel clock) → `v++ -p` (.xclbin). Versal is this 3-step flow (CLAUDE.md "Build /
   emulation"). PL-only, so no `libadf.a`. Template: the legacy
   `vck5000/bring_up/hpwl_gradient_dhar/Makefile` (it ran on the real card, #40, and failed on timing),
   plus memory `hwemu_pl_only_flow` (PL-only hw_emu works with `-DPL_ONLY`; strip the Windows PATH
   before `v++ -p`). Add targets to `pl/Makefile`; keep `make -C pl test` cheap.
2. **XRT host** in `host/sw_flat/` (all software lives there, D4). Easiest base:
   `host/sw_flat/test/record_stream_check.cpp`. It already builds `packer::chunked_device_arrays`
   per axis from a real placement and runs the CU in g++. Swap the g++ call for `xrt::bo`s +
   `xrt::kernel` (one run per axis), and keep the same verdicts: every OWNED slot's gradient
   bit-equal, both `hpwl_total_fixed` equal, the CU-in-g++ / sw_flat result as golden. Keep "a test
   asserts" (exit non-zero). Grad image pre-filled NaN.
3. **sw_emu, then hw_emu, then hardware.** Only sw_emu is viable for AIE designs, but this is PL-only,
   so hw_emu should work (memory `hwemu_pl_only_flow`). Real HW: the build server (`ssh build`; Vitis/XRT
   + the VCK5000; repo at `~/AIEplace`). It needs **Mark to open the tunnel** (`wsl ssh -fN build`):
   ask once, launch long jobs detached, never poll (rules.md).
4. **Still-open hardware questions** (#45 step 8 carried items): the NMU bandwidth check (§1 of
   [[_NEW_REPORT_41_next_steps_20261002.md]]: can one m_axi port feed 64 B/cycle?), and the per-axis
   URAM strategy (one axis is 256 of 463 URAM, so the axes run one call each, which the CU already does).

## Traps
- **Every shell command is wrapped**: `wsl -e bash -c "cd /home/msears/phd/AIEplace && <cmd>"`. Inside
  `bash -c "..."`, `$(...)` and `$VAR` expand early: a python heredoc editing a Makefile was silently
  mangled this way. Use the Edit tool for Makefiles/prose. The Edit tool sometimes fails with
  `EPERM ... rename`; re-issue it (it succeeds on retry, check `git diff` first).
- **Spawned sessions fail here**: their git runs on Windows against the WSL repo ("could not open this
  repository's Git state"). Do the work in this session.
- **Another session works on #47 in this checkout** (`pl/src/modules/hpwl_gradient/slot_arrays.hpp`,
  `host/sw_flat/model/*`, tasks.md #47 hunks, rules.md, several #41/#48 reports are modified and not
  mine). Stage hunks explicitly. `git diff -U0 file > p; filter hunks; git apply --cached --unidiff-zero`
  works.
- Bash/Monitor background jobs re-invoke you on finish; don't poll.
- `make test-regress` baselines were regenerated 2026-10-16 (#49). PASS is the contract again.

## Also done this session (context only)
- #45 step 5b closed (`8323939`): the record-stream switches above, and drift within noise.
- #49 closed (`4942144`): sw_only's HPWL metric now sums in double (it read 0.39% low on bigblue4).
  Post-DP unchanged; regress baselines regenerated.
- dse.py label fix (`5340ac4`): labels are `<suite>_<name>@grid`, and duplicates refuse to launch.
  #49's archived after-run CSV is flagged wrong for its bigblue rows (README there).

## Done looks like
`make -C pl <hw target>` builds an xclbin for `hpwl_gradient_CU`; a host run in sw_emu (then hw_emu)
on mgc_fft_a, mms/adaptec1 and bigblue4 (3 chunks) exits 0 with bit-exact gradients and HPWL vs the
golden; a REPORT records clock, resources and the emulation results. The card run waits for Mark.
