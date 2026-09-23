# C-synthesis smoke test for hpwl_computer_v3 (chunked: export / import passes + the v2 beat loop).
# Run from bring_up/hpwl_computer_v3/: vitis_hls -f synth_check.tcl   (source Vitis settings64.sh first).
open_project synth_check_prj
set_top hpwl_computer_v3_top
add_files src/hpwl_computer_v3_top.cpp -cflags "-Isrc -I../hpwl_computer_v2/src -I../hpwl_computer/src -I../beat_packer -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
exit
