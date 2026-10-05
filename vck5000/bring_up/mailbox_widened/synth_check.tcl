# C-synthesis check for the #42 widened mailbox loops (II=1 per loop, crossbar cost).
# Run from bring_up/mailbox_widened/: vitis_hls -f synth_check.tcl   (source Vitis settings64.sh first).
open_project synth_check_prj
set_top mailbox_widened_top
add_files src/mailbox_widened_top.cpp -cflags "-Isrc -I../hpwl_gradient_computer/src -I../hpwl_computer_v2/src -I../hpwl_computer/src -I../beat_packer -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
exit
