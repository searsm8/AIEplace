# Vivado synthesis (out of context) of the folded bin_density_CU: real LUT/FF/DSP/URAM and the post-synth
# period, which csynth only estimates. Run on the build server (no Vivado synth license for the VC1902 here),
# from pl/synth/bin_density_CU/:
#   vitis_hls -f impl_check.tcl   (source Vitis 2022.2 settings64.sh first)
# Result: impl_prj/sol1/impl/report/verilog/export_syn.rpt, timing in impl_prj/sol1/impl/verilog/report/. Meow.
open_project impl_prj
set_top bin_density_CU_top
add_files bin_density_CU_top.cpp -cflags "-I../../src -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
export_design -flow syn -rtl verilog -format ip_catalog
exit
