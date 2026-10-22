---
title: Renaming review reports (_NEW_ un-prefixing)
author: Searsm8
date: 2026-10-22
branch: pl_algo
touches:
  - pl/synth/bin_density_producer/impl_prj_div0/hls.app
  - pl/synth/bin_density_producer/impl_prj_div0/kernel.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.automg_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.autopilot_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_impl.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_syn.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/__ctype_info__.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/a.g
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/a.g.0.bc
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/a.g.1.bc
---

## Decided
- Renaming a report in .claude/1_REVIEW/reports means rewriting every reference to it too: links live in workflow md, READMEs, AND code comments (hpwl_CU.hpp, DataBase.cpp, sw_flat model, bring_up modules). Use git mv, then sed on the exact basenames from git grep.
- history.md filename references were updated too (a link fix only, not a rewrite of history).

## Tried and ruled out
- git add -A for the commit: it staged ~1900 untracked synth project files under pl/synth/bin_density_producer/. Stage with git add -u plus the specific paths instead.

## Watch out
- The text in AGENTS.md / tasks.md:67 that describes the _NEW_ convention is not a file reference, so leave it alone.
