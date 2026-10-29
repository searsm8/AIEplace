# C-synthesis check for iteration_update_CU (tier 2): every loop must report Final II = 1.
# Run from pl/synth/iteration_update_CU/:  vitis_hls -f synth_check.tcl   (source Vitis settings64.sh first)
# Clock: pl/Makefile's HLS_FREQ_HZ (400 MHz, 2.5 ns), the schedule the kernel build uses. Meow.
open_project synth_check_prj
set_top iteration_update_CU_top
add_files iteration_update_CU_top.cpp -cflags "-I../../src -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 2.5 -name default
csynth_design
exit
