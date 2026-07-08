# C-synthesis smoke test for the resident-loop control core (bb_reduce + param_scheduler).
# Run from model/: vitis_hls -f synth_check.tcl   (source Vitis settings64.sh first).
open_project synth_check_prj
set_top synth_check
add_files synth_check.cpp -cflags "-I../src -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
exit
