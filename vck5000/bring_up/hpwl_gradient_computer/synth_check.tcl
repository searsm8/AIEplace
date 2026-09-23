# C-synthesis smoke test for hpwl_gradient_computer (beat_loop II=1: gather, trees, combiner, merge, scatter-add).
# Run from bring_up/hpwl_gradient_computer/: vitis_hls -f synth_check.tcl   (source Vitis settings64.sh first).
open_project synth_check_prj
set_top hpwl_gradient_computer_top
add_files src/hpwl_gradient_computer_top.cpp -cflags "-Isrc -I../hpwl_computer_v2/src -I../hpwl_computer/src -I../beat_packer -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
exit
