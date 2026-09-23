# RTL co-simulation of hpwl_gradient_computer_top: the hazard contract check tier 1 cannot do.
# Run from bring_up/hpwl_gradient_computer/cosim/: vitis_hls -f cosim.tcl (source settings64.sh first).
# PL_SLOT_CAPACITY is shrunk to 8K slots so the RTL memories simulate fast; the pipeline is identical.
set inc "-I../src -I../../hpwl_computer_v2/src -I../../hpwl_computer/src -I../../beat_packer -I../../../test -I../../../pl/src/pl_algo/src -DPL_SLOT_CAPACITY=8192"
open_project cosim_prj
set_top hpwl_gradient_computer_top
add_files cosim_top.cpp -cflags "$inc -std=c++14"
add_files -tb cosim_tb.cpp -cflags "$inc -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
cosim_design -argv "4"
cosim_design -argv "1"
exit
