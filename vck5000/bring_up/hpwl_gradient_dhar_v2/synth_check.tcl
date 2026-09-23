# C-synthesis smoke test for hpwl_gradient_dhar_v2 (bbox/HPWL-only milestone, net_loop II check).
# Run from bring_up/hpwl_gradient_dhar_v2/: vitis_hls -f synth_check.tcl   (source Vitis settings64.sh first).
open_project synth_check_prj
set_top hpwl_gradient_dhar_v2_top
add_files src/hpwl_gradient_dhar_v2_top.cpp -cflags "-Isrc -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
exit
