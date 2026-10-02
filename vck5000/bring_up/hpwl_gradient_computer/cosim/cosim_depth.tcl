# Beat-FIFO depth sweep for hpwl_gradient_computer_top (#41, packer rule L8): synthesize with the
# stage FIFOs at depth $::env(COSIM_DEPTH) and co-simulate at the packer hazards in
# $::env(COSIM_HAZARDS) (default "4"). cosim_tb.cpp's design has a large net of extent 13, so a
# depth that cannot hold it must deadlock in RTL -- C simulation never can.
# Run from bring_up/hpwl_gradient_computer/cosim/ with LIBRARY_PATH=/usr/lib/x86_64-linux-gnu (see
# cosim_rerun.tcl). One project per depth: cosim_prj_d<depth>.
set depth $::env(COSIM_DEPTH)
set hazards "4"
if {[info exists ::env(COSIM_HAZARDS)]} { set hazards $::env(COSIM_HAZARDS) }
set inc "-I../src -I../../hpwl_computer_v2/src -I../../hpwl_computer/src -I../../beat_packer -I../../../test -I../../../pl/src/pl_algo/src -DPL_SLOT_CAPACITY=8192 -DPL_BEAT_FIFO_DEPTH=$depth"
open_project cosim_prj_d$depth
set_top hpwl_gradient_computer_top
add_files cosim_top.cpp -cflags "$inc -std=c++14"
add_files -tb cosim_tb.cpp -cflags "$inc -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
foreach h $hazards { cosim_design -argv "$h" }
exit
