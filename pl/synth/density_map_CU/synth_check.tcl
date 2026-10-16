# C-synthesis check for density_map_CU (tier 2). The prototype must synthesize; only the overflow
# reduction (reduce) is expected at II=1 once rho_URAM is banked 16 wide -- read its II in the report.
# Run from pl/synth/density_map_CU/:  vitis_hls -f synth_check.tcl   (source Vitis settings64.sh first)
open_project synth_check_prj
set_top density_map_CU_top
add_files density_map_CU_top.cpp -cflags "-I../../src -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
exit
