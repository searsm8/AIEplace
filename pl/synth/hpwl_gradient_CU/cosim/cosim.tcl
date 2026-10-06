# RTL co-simulation of hpwl_gradient_CU_top: the chunked passes, the mailbox (padding segments
# skipped whole) and the external-gradient read-add-write, as synthesized RTL. Run from
# pl/synth/hpwl_gradient_CU/cosim/:
#   COSIM_ARGV="160 4" LIBRARY_PATH=/usr/lib/x86_64-linux-gnu vitis_hls -f cosim.tcl   (source settings64.sh first)
# argv = CAPACITY HAZARD (cosim_tb.cpp). 160 is the smallest capacity whose chunks have padding
# segments and still fit the port depths; HAZARD < 4 is the negative control and must FAIL.
# PL_SLOT_CAPACITY is shrunk to 8K slots so the RTL memories simulate fast; the pipeline is identical.
set argv_tb [expr {[info exists ::env(COSIM_ARGV)] ? $::env(COSIM_ARGV) : "160 4"}]
set project [expr {[info exists ::env(COSIM_PROJECT)] ? $::env(COSIM_PROJECT) : "cosim_prj"}]
set inc "-I../../../src -I../../../test -I../../../../host/sw_flat/packer -DPL_SLOT_CAPACITY=8192"
open_project $project
set_top hpwl_gradient_CU_top
add_files cosim_top.cpp -cflags "$inc -std=c++14"
add_files -tb cosim_tb.cpp -cflags "$inc -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
cosim_design -argv $argv_tb
exit
