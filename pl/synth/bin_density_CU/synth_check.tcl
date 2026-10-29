# C-synthesis check for bin_density_CU (tier 2): 8 producers, 64 URAM banks. Expected II=1 in deal_beats,
# produce_cells, drain_banks' drain, load_base, reduce and drain; read them in the report. Meow.
# Run from pl/synth/bin_density_CU/:  vitis_hls -f synth_check.tcl   (source Vitis settings64.sh first)
open_project synth_check_prj
set_top bin_density_CU_top
add_files bin_density_CU_top.cpp -cflags "-I../../src -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
exit
