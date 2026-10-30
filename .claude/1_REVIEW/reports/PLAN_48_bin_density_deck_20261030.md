# PLAN #48: slide deck brief for `bin_density_CU`

*2026-10-30. Branch `pl_algo`. For Mark's PowerPoint on the 8-producer, 64-bank density deposit. Slide by slide, with
enough here to build the .pptx with the pptx skill; the .pptx itself is not built.*

**Figures:** `.claude/2_ARTIFACTS/bin_density_diagrams_48/` (untracked), each as `.png` (200 dpi, 2400 × 1280) and
`.svg`, about 16:8.5, so one figure fills a slide under its title. Regenerate with `python3 make_diagrams.py`.
That script asserts every layout claim on the full 1024 grid before it draws (bin to (bank, address) is one to one;
16 K words per bank; bank and address are the low and high bits; any window up to 8 × 8 hits distinct banks, 9 × 9 does not).

**Sources:** `pl/src/modules/bin_density_CU.hpp`, [[REPORT_48_fold_banked_into_cu_20261029.md]],
[[REPORT_48_bank_address_multiplies_20261028.md]], [[REPORT_48_drain_timing_20261028.md]],
[[REPORT_48_banked_cosim_20261029.md]], [[EXPLAINER_48_bin_density_CU_overview_20261022.md]].

## Where this differs from the request

Checked against the code; the deck uses the second column.

| the request said | the code and reports say |
|---|---|
| a footprint window is at most 8 × 8 bins | **4 × 4** (`WINDOW_BINS = 4`), so at most 16 updates per cell. 8 × 8 is the largest window the layout *could* serve (`static_assert(WINDOW <= BANK_SIDE)`). |
| csynth 2.571 ns stands as the timing | **Vivado synth of the fold: 4.062 ns, NOT met at 3.33** (WNS −0.732, 504 endpoints, one path family). The result is committed (088f62e). A `style=frp` fix is in the working tree, its run not reported here. Slide 15 has to say so. |
| 517 K LUT (57 %) | csynth's number. **Vivado says 294 K (32 %).** The team's note is to quote Vivado. Both are on the chart. |

Two limits on what the figures claim:
- The layout removes conflicts **inside one cell**, not between cells. Two producers can want one bank in the same
  cycle: that is what the FIFOs absorb (slide 9). Say this out loud on slide 7, or someone in the room will.
- Throughput figures (5.8 updates per cell, the 5.2-8.3× speedup) come from the pre-fold model. They are not measured
  on this design; the fold report chose to measure on the device instead.

## Slides

### 1. Title
- **Title:** Building the density map on chip: `bin_density_CU`
- **Key message:** A density map is many small adds to memory, and the design is about doing them in parallel.
- **Diagram:** none.
- **Bullets:** TODO #48; AIEplace on the Versal VCK5000; presenter and date.
- **Notes:** This part of the placer takes real time on a GPU too: XPlace spends about half its time on scatter plus
  gather. Today is only the scatter half, on the FPGA.

### 2. What it computes
- **Title:** One cell, a few small adds
- **Key message:** Each cell adds its overlap area to the bins it touches, for 8 cells per cycle.
- **Diagram:** [[fig01_what_it_computes.png]]
- **Bullets:** 1024 × 1024 bins; at most 16 bins per normal cell, about 5.8 on average; 8 cells per cycle is about 46 updates per cycle.
- **Notes:** Each add is a read, an add and a write to one memory word. One memory does one of those per cycle.
  So 46 per cycle means we need many memories, and the question for the next five slides is how to divide the
  bins between them. The 5.8 is from the earlier model on five designs.

### 3. The patch
- **Title:** Give neighbours different memories
- **Key message:** In any 8 × 8 patch, every bin has its own memory.
- **Diagram:** [[fig02_patch_8x8.png]]
- **Bullets:** 64 memories, called banks; colour and number name the bank; bank = 8·(col % 8) + row % 8.
- **Notes:** Colour family is the column in the patch, light to dark is the row. Read one or two: column 3, row 2 is bank 26.
  Nothing clever yet: just deal the 64 bins out to 64 memories. Introduce the word "bank" here, after the idea.

### 4. The tiling
- **Title:** The same patch, everywhere
- **Key message:** The pattern repeats across the grid, so any spot looks the same.
- **Diagram:** [[fig03_patch_tiled.png]]
- **Bullets:** 32 × 32 corner shown; 128 patches per side.
- **Notes:** This is the bottom-left corner only. Note what this implies before the next slide: a bank does not own a
  region. It turns up once in every patch.

### 5. One bank
- **Title:** One bank is spread over the whole die
- **Key message:** A bank holds every 8th column of every 8th row: 128 × 128 bins, an even sprinkle.
- **Diagram:** [[fig04_one_bank_lattice.png]]
- **Bullets:** Bank 0 in red; 16,384 bins per bank; every one of the 64 banks has the same shape, shifted.
- **Notes:** This is the key picture. Pause on it. The opposite of the mental model most people bring, which is that a
  memory holds one area of the chip.

### 6. Finding a bin
- **Title:** From a bin to a word
- **Key message:** Close up the gaps in the sprinkle and a bank is a plain 128 × 128 array.
- **Diagrams:** [[fig05_bank_and_address.png]], then [[fig06_bits_rewired.png]] (build as two slides if preferred).
- **Bullets:** Bin (19, 47) is bank 31, address 261; address = (col / 8)·128 + row / 8; 128 is always GRID_MAX / 8; the low 3 bits pick the bank, the high 7 bits make the address.
- **Notes:** Worked example first, then the bit picture. The second figure is the punch line: no arithmetic at all, only
  wires. The pitch is fixed at GRID_MAX / 8 even on a smaller grid. With the design's own grid size it was a real
  multiply: one DSP per bank per producer, 512 DSPs. Fixing it took the deposit from 1,168 to 656 DSP. A smaller
  grid leaves some words unused, which were allocated anyway. Each bank is 2 URAMs, so 128 in all.

### 7. Why interleave
- **Title:** Why not give each bank one block of the die?
- **Key message:** Interleaved, a cell's 16 updates leave in 1 cycle; in blocks, they queue for 16.
- **Diagram:** [[fig07_why_interleave.png]]
- **Bullets:** Same cell on both sides; interleaved: 16 banks; blocks: 1 bank for 95 % of positions; 128 × 128 blocks would hold the same amount of memory.
- **Notes:** Same memory, same cell, only the mapping differs. The 95 % is arithmetic: (125/128)², the share of 4 × 4
  windows that sit inside one block; 4.6 % touch 2 and 0.05 % touch 4. Be straight about the limit: this fixes
  conflicts inside one cell. Two different cells can still want the same bank in the same cycle, and that is the job
  of the FIFOs on slide 9.

### 8. Any position
- **Title:** It works wherever the cell lands
- **Key message:** The 4 × 4 window never hits a bank twice, even across a patch edge or corner.
- **Diagram:** [[fig08_any_position.png]]
- **Bullets:** Four neighbouring columns have four different remainders; same for rows; 16 of 64 banks at most; the layout would hold up to 8 × 8.
- **Notes:** This is why the design needs no collision logic inside a producer. Ask the audience to find a repeated
  colour in a window. There isn't one. The generator proves it for all positions.

### 9. The whole fast path
- **Title:** The fast path, end to end
- **Key message:** 8 producers feed 64 banks through 512 private FIFOs.
- **Diagram:** [[fig10_dataflow.png]]
- **Bullets:** 8 cells per cycle in; up to 64 updates per cycle stored; each FIFO has one writer and one reader, depth 8; a full FIFO pauses one producer only.
- **Notes:** Read a row: everything producer 2 sends. Read a column: everything bank 31 receives. Their crossing, in red,
  is one FIFO. Private FIFOs mean no shared write port and nothing for producers to fight over. The bank takes
  from one of its 8 FIFOs per cycle. When two producers want one bank, one waits a little in its FIFO. 8 × 5.8 is
  about 46 updates against 64 places, so on average there is room.

### 10. Feeding the producers
- **Title:** Dealing the cells out
- **Key message:** Each 16-slot memory beat is dealt 2 slots to each producer.
- **Diagram:** [[fig11_deal_slots.png]]
- **Bullets:** Lanes 2p and 2p+1 go to producer p; x, y and footprint index on 3 memory ports; each producer has its own 128-entry footprint table.
- **Notes:** Dealing is by lane, not by location, so it is cheap. The catch: how evenly real cells spread over producers
  depends on the slot order. Accepted, to be measured on the device.

### 11. Inside a producer
- **Title:** A producer is an assembly line
- **Key message:** A new cell enters every cycle, and up to 16 amounts leave for 16 banks.
- **Diagram:** [[fig12_producer.png]]
- **Bullets:** 33 stages, 1 cell per cycle; 32 float multiplies; 82 DSP and 42.8 K LUT each (csynth); bin index by multiply, not divide (saved 7.8 K LUT per producer).
- **Notes:** Steps 5 and 6 are the cost. A producer does the same float arithmetic as the software golden, in the same
  order, which is why it can match it bit for bit.

### 12. Inside a bank
- **Title:** One bank, one update per cycle
- **Key message:** Each bank picks one update and does a read-add-write, keeping its last sums at hand.
- **Diagram:** [[fig13_one_bank.png]]
- **Bullets:** Fixed priority over 8 FIFOs; URAM read takes several cycles; the sum is written back the next cycle; registers hold the last 6 sums.
- **Notes:** The next slide says why the registers are needed. Writing back a cycle later keeps the URAM's data input
  coming straight from a register, which is what made this meet timing in `banked_top`.

### 13. Forwarding
- **Title:** The same bin twice in a row
- **Key message:** A bin hit again before its write lands must take the value from a register; 4 are needed, 6 are kept.
- **Diagram:** [[fig14_forwarding.png]]
- **Bullets:** Reads up to 4 cycles later see an old value; RTL co-sim: 1-3 registers FAIL, 4-6 PASS; the code keeps 6.
- **Notes:** C simulation cannot see this: it runs one iteration at a time, so depths 1-3 pass there. Only RTL shows it.
  The schedule predicts 4 and RTL agrees. The bench was built to collide across producers on purpose, at distances
  1 to 8. Re-run the sweep whenever `drain_banks` changes.

### 14. Sequence and exactness
- **Title:** One order, and it adds up exactly
- **Key message:** Overflow is summed after the cells and before the fillers, and integer adds make arrival order irrelevant.
- **Diagrams:** [[fig15_phases.png]], then [[fig16_fixed_point.png]] (build as two slides if preferred).
- **Bullets:** Load base, cells, slow cells, overflow sum, fillers, slow fillers, output; macros and very large cells take the slow path one bin at a time; 36-bit integers, fraction bits set per design; bit-equal to sw_flat on 44/44, shuffled too.
- **Notes:** XPlace's overflow counts cells and fixed blocks but not fillers, so the sum has to fall between the two. The
  dataflow region returns only when every bank has seen every producer's `last`, and that return is the barrier.
  Same layout helps here too: see the backup slide. On exactness: in float, the order of adds changes the answer.
  Eight producers in parallel have no fixed order, so the bins are integers. The overlap is still computed in float
  exactly as the golden does, and rounded once.

### 15. Cost and status
- **Title:** What it costs, and where it stands
- **Key message:** It fits, the producers are the bill, and timing is not closed yet.
- **Diagram:** [[fig17_resources.png]]
- **Bullets:** csynth: 689 DSP, 517 K LUT (57 %), 128 URAM, 2.571 ns; Vivado synthesis: 687 DSP, 294 K LUT (32 %), 128 URAM, **4.062 ns, target 3.33 not met**; one failing path: the stall logic through the shared URAM port to the URAM enables.
- **Notes:** Quote Vivado's LUT; csynth is 1.75× high here. Timing: all 504 failing endpoints are one family. The deposit
  alone met 2.928 ns, so what the fold added is the mux that shares each URAM between the five phases. The fix being tried
  is `style=frp` on the drain loop. **Before presenting, update this slide with that run's result**, from
  [[REPORT_48_fold_banked_into_cu_20261029.md]] or its successor.

### 16. Summary
- **Title:** Summary
- **Key message:** The bank layout is what turns one memory into 64 parallel ones without collisions.
- **Diagram:** none, or reuse [[fig04_one_bank_lattice.png]] small.
- **Bullets:** Spread each bank across the die, so a cell never hits a bank twice; 8 producers, 512 FIFOs, 64 banks; bit-equal to the golden on 44/44, RTL proven; next: close timing, then macro tiles for the slow path, then the force gather.
- **Notes:** Close on the lattice picture if there is time for one image.

### Backup. The layout pays twice
- **Title:** The sweeps get faster too
- **Key message:** One address across all banks is one 8 × 8 patch, so whole-grid reads are fast as well.
- **Diagram:** [[fig09_sweeps_get_fast_too.png]]
- **Bullets:** Overflow sum: 64 bins per cycle, 16.4 K cycles; load and output: one beat per cycle, 65.5 K cycles, 2 words from each of 8 banks.
- **Notes:** Use it if asked what else the layout buys. URAM is true dual port, so a bank gives two words per cycle.

## Build notes for the pptx skill

- One figure per slide, full width under the title. The figures carry their own text, so keep slide bullets to the speaker's
  prompts or move them to notes. Mark prefers plain idea first: keep the order within slides 3-8.
- Figures 05 and 06 fit one slide only at small size; 15 and 16 likewise. Prefer two slides each, which gives 18 slides.
- Use the PNGs for the build, SVGs for later editing (text is converted to paths in the SVG).
- Meow.
