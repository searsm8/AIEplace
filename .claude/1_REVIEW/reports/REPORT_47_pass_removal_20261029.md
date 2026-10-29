# REPORT #47 item 4 / D — the borrowed passes out of `hpwl_gradient_CU` (2026-10-29)

**Result:** `hpwl_gradient_CU` is now the compute pass alone. `send_borrowed_pass`, `merge_borrowed_pass`,
the `chunked` branch and the `borrowed_blocks` / `borrowed_ops` ports are gone; the exchange is
`iteration_update_CU`'s (STEP sends before the call, COMBINE merges after, `643bb74`). With the exchange
done around it by its golden (M2, `host/sw_flat/borrowed_fusion.hpp` `fused_send` / `fused_merge`), the
gradient is **bit-exact**: byte-identical to the old CU's whole grad image, and bit-exact vs sw_flat on
real chunked designs. B3's device exchange (`borrowed_copies.hpp`, `borrowed_copies_test`) and its only
other helper `load_slot_array` are deleted (Mark, 2026-10-29).

## What changed
| file | change |
|---|---|
| `pl/src/modules/hpwl_gradient_CU.hpp` | passes, `chunked`, the two ports and the include removed; `pos_DDR` is now `const` (the CU no longer writes it) |
| `pl/src/modules/hpwl_gradient/slot_arrays.hpp` | `load_slot_array` removed (only B3 and its test used it) |
| `pl/src/modules/hpwl_gradient/borrowed_copies.hpp`, `pl/test/borrowed_copies_test.cpp` | deleted; dropped from `HARNESSES` |
| `pl/test/hpwl_gradient_CU_test.cpp` | `fused_send` on the slot images before the CU, `fused_merge` on the grad image after |
| `host/sw_flat/test/record_stream_check.cpp` | same, around the g++ CU |
| `host/sw_flat/test/hpwl_gradient_xrt.cpp` | `fused_send` in `make_inputs` (both the g++ CU and the device get the sent image); `fused_merge` on the device image before [3]; kernel arg indices 7/9/11/15 (were 9/11/13/17) |
| `pl/synth/hpwl_gradient_CU/hpwl_gradient_CU_top.cpp`, `cosim/cosim_top.cpp` | two m_axi ports gone (`gmem4` is no longer used); `pos` const |
| `pl/synth/hpwl_gradient_CU/cosim/cosim_tb.cpp`, `cosim.tcl` | M2 exchange around the call; the B3 row-buffer back-to-back coverage replaced by "≥ 3 chunks and a borrowed node"; `-I host/sw_flat` |

## Evidence
1. **A/B, old CU vs new CU + M2, whole grad image + HPWL, both axes: byte-identical** (`cmp`), on the
   tier-1 synthetic design at capacity 4096 (3 chunks), 2560 (30 chunks), 1 M (1 chunk) and the co-sim
   design at 1536 (16 chunks). Driver and dumps: `.claude/2_ARTIFACTS/scratch_47/cu_ab/` (`cu_ab.cpp`,
   `old/` = the CU at `643bb74`). The bigblue4 A/B was stopped (encode at capacity 64 K too slow); item 3
   covers the real chunked designs.
2. **`make -C pl test`: PASS, 4/4 harnesses** (`hpwl_gradient_CU_test` [1]-[5] all ok; was 5 with
   `borrowed_copies_test`).
3. **`make -C host/sw_flat check-record-stream`: PASS 6/6** — mgc_fft_a, mgc_pci_bridge32_b, adaptec1,
   mms/adaptec1, **bigblue4 and mms/bigblue4 (3 chunks each)**: every owned slot bit-exact vs sw_flat,
   HPWL int64 equal.
4. **C-synth** (`synth_check.tcl`, 3.33 ns): 0 errors, **19/19 pipelined loops Final II = 1** (24 before:
   `send_ops`, `merge_ops` and their loads/drain gone), **est. 3.482 ns / Fmax 287.2 MHz**. The old CU
   (`643bb74`) synthesized the same way: 283.6 MHz. Same clock, old -> new: **BRAM 282 -> 218** (B3's two
   32-bank row buffers), **LUT 294.8 K -> 250.8 K (-44 K)**, FF 361.8 K -> 344.3 K, DSP 826 and URAM 64
   unchanged (csynth estimates; `scratch_47/cu_ab/old_synth_prj`).
5. **sw_emu, `make -C pl xclbin TARGET=sw_emu` + `make -C host/sw_flat hpwl-gradient-xrt TARGET=sw_emu`:
   PASS 4/4.** [1] whole image vs the g++ CU, 0 slots differ, on synthetic (16 chunks), mgc_fft_a,
   mms/adaptec1, **bigblue4 (3 chunks, 2.62 M slots)**; [3] owned slots after `fused_merge` vs sw_flat,
   0 differ on all three real designs.
6. **RTL co-sim** (`cosim.tcl`, capacity 1536, hazard 4, project `cosim_prj_d`): **PASS**, 16 chunks with M2 around the RTL, both axes within tier-1 tolerance, HPWL equal; 16 min. The hazard < 4 negative control was not re-run (the beat loop is unchanged).

## Not done / follow-ups
- **The 250 MHz card xclbin (build H) no longer matches the kernel interface** (two ports fewer).
  **Build I launched 2026-10-29 20:54 CET** on `build` (Mark opened the tunnel): build H's flow unchanged
  (HLS 400 MHz, link at 250 MHz with the timing check off, package), in `build:~/AIEplace_timing/expI/pl/`,
  outputs in `build/run/` (`done` records link and package exits; script `scratch_47/synth_I/run.sh`).
  Source: HEAD `38460cb` + this change's uncommitted `hpwl_gradient_CU.hpp`, `slot_arrays.hpp` and the
  synth top, exported clean (not the live tree, which another session is editing). Build H's link took
  ~4 h 15 min. **Next:** read the kernel's routed slack from the dcp (`worst.tcl`, `classify_paths.tcl`, as
  for H), then a card run on `vck` (Mark books it). Expect timing to ease: B3's `send_ops` RAM read was
  among H's near-critical endpoints.
- **Commit hazard:** the deletions of `borrowed_copies.hpp` / `borrowed_copies_test.cpp` were staged here
  and went into another session's commit `38460cb`; the CU edits did not. Until this change is committed,
  HEAD's `hpwl_gradient_CU.hpp` includes a file HEAD no longer has.
- **The packer and format still carry B3:** `pin_record.hpp` `BorrowedBlockDesc`, `BORROWED_BEAT_OP`,
  `make_borrowed_entry`; `ChunkDesc::first_borrowed_block` / `num_borrowers`; `chunked_device_arrays`'
  `borrowed_blocks` / `borrowed_ops`; `check_chunked`'s B3 replay; `model/cycle_model.hpp`'s TODAY
  layer. Nothing on the device reads them now. `ChunkDesc` is a shared device struct, so its fields go
  in a separate change.
