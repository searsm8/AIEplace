# RTL co-simulation of bin_density_CU_top: the 4x4-banked deposit and its forwarding registers, on a
# stream built to re-hit bins at distances 1..5 (cosim_tb.cpp). Run from pl/synth/bin_density_CU/cosim/:
#   FORWARD_DEPTH=6 LIBRARY_PATH=/usr/lib/x86_64-linux-gnu vitis_hls -f cosim.tcl   (source settings64.sh first)
# FORWARD_DEPTH=0 is the negative control and must FAIL in RTL (C simulation passes at any depth).
# PL_DENSITY_GRID_MAX shrinks the grid to 64 so the RTL memories simulate fast; the pipeline is identical. Meow.
set depth [expr {[info exists ::env(FORWARD_DEPTH)] ? $::env(FORWARD_DEPTH) : 6}]
set project [expr {[info exists ::env(COSIM_PROJECT)] ? $::env(COSIM_PROJECT) : "cosim_prj_d$depth"}]
set inc "-I../../../src -DPL_DENSITY_GRID_MAX=64 -DBIN_FORWARD_DEPTH=$depth"
open_project $project
set_top bin_density_CU_top
add_files cosim_top.cpp -cflags "$inc -std=c++14"
add_files -tb cosim_tb.cpp -cflags "$inc -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
cosim_design
exit
