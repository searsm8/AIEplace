# C-synthesis check for hpwl_gradient_CU (tier 2): every loop must report Final II = 1.
# Run from pl/synth/hpwl_gradient_CU/:  vitis_hls -f synth_check.tcl   (source Vitis settings64.sh first)
open_project synth_check_prj
set_top hpwl_gradient_CU_top
add_files hpwl_gradient_CU_top.cpp -cflags "-I../../src -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
exit
