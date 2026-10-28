---
title: "Writing a progress report: where the status lives"
author: Searsm8
date: 2026-10-27
branch: pl_algo
touches:
  - pl/src/modules/bin_density_CU.hpp
  - pl/synth/bin_density_producer/impl_prj_div0/hls.app
  - pl/synth/bin_density_producer/impl_prj_div0/kernel.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.automg_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.autopilot_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_impl.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_syn.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/__ctype_info__.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/a.g
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/a.g.0.bc
---

## Decided
- A progress report comes from four sources: .claude/0_WORKFLOW/milestones.md (verified landmarks), summary.md (current state), the tasks.md Active Task line plus the #45/#47/#48 sections, and git log --since. That was enough; reading code wasn't needed.
- Next-work order as of 2026-10-27: 230 MHz card run -> #44 step 1 XRT round-trip timing -> #48 bin_density_CU (divide->multiply golden, then P=8 banked deposit, macro tiles) -> rest of #45 step 6 -> step 7 compose.

## Tried and ruled out
- Nothing.

## Watch out
- summary.md's 'Updated' header date can lag its content (it said 10-16 while holding 10-27 facts). Trust the dated bullets and git log over the header.
- The #45 step list (1-9) lives in PLAN_45_integration_20261006.md under '## Steps', not in tasks.md.
