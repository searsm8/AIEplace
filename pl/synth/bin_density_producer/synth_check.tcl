# C-synthesis of ONE bin_density producer (producer_top.cpp), to price P copies of it (#48 round 3).
# Run from pl/synth/bin_density_producer/:  PRODUCER_DIVIDE=0|1 vitis_hls -f synth_check.tcl
# (source settings64.sh first). 0 = bin index by multiply, 1 = by divide. Meow.
set divide [expr {[info exists ::env(PRODUCER_DIVIDE)] ? $::env(PRODUCER_DIVIDE) : 0}]
open_project producer_prj_div$divide
set_top producer_top
add_files producer_top.cpp -cflags "-I../../src -std=c++14 -DPRODUCER_DIVIDE=$divide"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
exit
