# RTL co-simulation of the 64-bank deposit (cosim_top.cpp = ../banked_top.cpp with bench-sized m_axi depths)
# on a stream built to re-hit bank words across producers at distances 1..8 (cosim_tb.cpp). Run from
# pl/synth/bin_density_banked/cosim/:
#   FORWARD_DEPTH=6 LIBRARY_PATH=/usr/lib/x86_64-linux-gnu vitis_hls -f cosim.tcl   (source settings64.sh first)
# FORWARD_DEPTH=1 is the RTL negative control; 0 fails already in C (deferred write-back), so co-sim stops there.
# PL_DENSITY_GRID_MAX shrinks the grid to 64 (64-word banks) so RTL sim is fast; the drain pipeline is the same. Meow.
set depth [expr {[info exists ::env(FORWARD_DEPTH)] ? $::env(FORWARD_DEPTH) : 6}]
set project [expr {[info exists ::env(COSIM_PROJECT)] ? $::env(COSIM_PROJECT) : "cosim_prj_d$depth"}]
set inc "-I../../../src -DPL_DENSITY_GRID_MAX=64 -DBANKED_FORWARD_DEPTH=$depth"
open_project $project
set_top banked_cosim_top
add_files cosim_top.cpp -cflags "$inc -std=c++14"
add_files -tb cosim_tb.cpp -cflags "$inc -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
cosim_design
exit
