# HANDOFF #48: bin index by multiply, not divide (golden first, then the device)

*2026-10-27. Branch `pl_algo` at `cb6a58e`. Mark's decision (2026-10-27): do it. Evidence:
[[PLAN_48_bin_density_lanes_20261020.md]] ("What one producer costs"), [[EXPLAINER_48_bin_density_CU_overview_20261022.md]].*

## Why

Every footprint needs 4 bin indices, `(int)(edge / bin_size)`. On the device that is 4 float dividers per
producer. **Vivado synthesis** (build server, 2022.2, one producer, 3.33 ns met):

| bin index | LUT | FF | DSP |
|---|---|---|---|
| divide (today's golden) | 21,682 | 17,231 | 90 |
| multiply by `1/bin` | 13,840 | 14,525 | 94 |

The dividers cost +7.8 K LUT per producer, ~63 K LUT over the planned 8 producers (7% of the chip).
(HLS's C-synthesis estimate reports the divider cores as 0 LUT/FF/DSP: it cannot be used for this.)

A multiply rounds differently from a divide, so the device can only be bit-exact if **the golden changes
first**. `pl/synth/bin_density_producer/producer_check.cpp` already found 0 differing bins over 200 K
cells (a third placed on bin edges) between the two, so the drift should be tiny — measure it, don't
assume it.

⚠️ **XPlace divides** (`~/phd/Xplace/cpp_to_py/density_map_cuda/density_map_cuda_kernel.cu:27-30`,
`pre_normalize`: each edge `/ unit_len`, then `floor`). So this is a **deliberate divergence from XPlace**:
register it in `CLAUDE.md` → "Deliberate divergences from XPlace", as #46 was. **sw_only is frozen and
does not change**; this is sw_flat (the device's golden) and the PL only.

## The change

`index = (int)(edge * inv_bin)`, `inv_bin = 1.0f / bin_size` computed once (host side, float), wherever
the golden computes a bin index today with `(int)(edge / bin_size)`.

## Steps

1. **sw_flat, behind a switch.** `[sw_flat] bin_index = "divide" | "multiply"`, default `"divide"` so
   `make -C host/sw_flat check-placer` stays bit-identical to sw_only. Sites
   (`host/sw_flat/placer/density.cpp`): `for_each_overlap` (deposit, fixed base, force gather — one
   helper serves all three) and `compute_overflow` (the separate metric). Grep for any other `/ bin_w`
   or `/ placer.bin_w` in `host/sw_flat` (models, density_inputs.hpp).
   → verify: check-placer PASS with the switch off.
2. **Measure the drift.** `host/sw_flat/test/sum_order_drift.py`: add a variant `pl+Bprime+fixed+mul`
   (= `pl+Bprime+fixed` + `bin_index = "multiply"`) and run the 5 designs (mgc_fft_a, mgc_pci_bridge32_b,
   adaptec1, mms/adaptec1, bigblue4) with `--variants pl+Bprime+fixed pl+Bprime+fixed+mul`. Also count,
   on a few designs, how many (cell, iteration) indices actually differ between the two (expect ~0).
   → report against the noise band, as the 10-19/10-20 drift tables do.
3. **Device side, bit-exact to the new golden.**
   - `pl/src/modules/bin_density_CU.hpp` `footprint_window`: take `inv_bin_w`/`inv_bin_h` (new CU
     arguments, from `host/sw_flat/density_inputs.hpp`), index by multiply.
   - `pl/synth/bin_density_producer/producer.hpp`: `PRODUCER_DIVIDE` default → 0.
   - `make -C host/sw_flat check-bin-density` (44/44, with `bin_index = "multiply"` forced like
     `deposit = "fixed"` is) and `make -C pl test` (tier-1's double golden: switch its index to the
     multiply too, or keep it independent and note why).
4. **Re-prove forwarding.** Removing the dividers shortens the deposit pipeline, so the URAM
   read→write distance changes. Re-run `pl/synth/bin_density_CU/cosim/run_depths.sh` (depth 0 and one
   short of the new distance must FAIL in RTL, the default must PASS) and update `BIN_FORWARD_DEPTH`'s
   comment with the new stages. C simulation cannot check this (team learning).
5. **Golden config + docs.** Step 6's golden becomes `pl+Bprime+fixed+mul`; update `sum_order_drift.py`'s
   docstring, the #48 entry in `.claude/0_WORKFLOW/tasks.md`, the CLAUDE.md divergence entry, and turn
   this handoff into `REPORT_48_multiply_bin_index_<date>.md`.

## Context a fresh session needs

- WSL wrap for every Bash command (`.claude/0_WORKFLOW/rules.md`); Vitis: `source
  /tools/Xilinx/Vitis/2022.2/settings64.sh`; co-sim also needs `LIBRARY_PATH=/usr/lib/x86_64-linux-gnu`.
- **This box has no Vivado synthesis license** for the VC1902 (csynth only). Real LUT/FF: the build server
  (`ssh build`, portal opened by Mark; Vitis 2022.2 at the same path; copy sources to a scratch folder,
  as `~/scratch_48_producer` was — the server checkout is old).
- Never name an HLS parameter `bins` (SystemVerilog keyword); write bank indices in a form HLS can prove
  distinct (team learning, `.trail/learnings/`).
- Markdown and multi-line text through the Edit/Write tools, never a `wsl ... heredoc` with backticks
  (they execute on the Windows side).
- Commit file by file, never `git add -A` (it sweeps up ~1,900 synth project files).
