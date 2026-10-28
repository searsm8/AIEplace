---
title: "#47 naming: borrowed (borrower side) vs shared (owner side); B4 = A not built"
author: Searsm8
date: 2026-10-26
branch: pl_algo
touches:
  - host/sw_flat/packer/beat_packer.hpp
  - pl/src/modules/bin_density_CU.hpp
  - pl/src/modules/hpwl_gradient/borrowed_copies.hpp
  - pl/src/modules/hpwl_gradient/slot_arrays.hpp
  - pl/src/modules/hpwl_gradient_CU.hpp
  - pl/synth/bin_density_producer/impl_prj_div0/hls.app
  - pl/synth/bin_density_producer/impl_prj_div0/kernel.xml
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.automg_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/.autopilot_exit
  - pl/synth/bin_density_producer/impl_prj_div0/sol1/.autopilot/db/.message_impl.xml
---

## Decided
- Mark, 2026-10-26: a borrower chunk's copies of nodes owned elsewhere are 'borrowed' copies/rows/blocks/passes (was 'tail'). 'shared' stays the OWNER side (first_private_slot, C8 shared-first rows). Never merge the two words. Commit bfe68c7.
- 'filler tail' / 'density-only tail' is a different concept (the filler slots after the chunks) and keeps its name.
- The #47 pass removal (B4 = A: send + merge inside iteration_update) is decided but NOT built; only the M2 golden (host/sw_flat/borrowed_fusion.hpp) and the M0 model exist. See PLAN_47_borrowed_copies_status_20261026.md.
## Watch out
- model/borrowed_layout.cpp (M1) computes its own pre-C8 owner slots; its assembly-buffer numbers predate C7's per-owner blocks and C8.
- To commit around another session's uncommitted hunks in a renamed file: build the index blob yourself (git hash-object -w --stdin + git update-index --cacheinfo), and for other files filter git diff -U0 hunks into git apply --cached --unidiff-zero. Verify on git checkout-index -a --prefix=/tmp/x/.
