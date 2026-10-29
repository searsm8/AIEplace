# Vivado synthesis (out of context) of the whole bank-FIFO deposit, for the real timing verdict on
# drain_banks: csynth's per-operation delays are estimates. Run on the build server (no Vivado synth
# license for the VC1902 here), from pl/synth/bin_density_banked/:
#   vitis_hls -f impl_check.tcl   (source settings64.sh first)
# Result: impl_prj/sol1/impl/report/verilog/export_syn.rpt, timing in impl_prj/sol1/impl/verilog/report/. Meow.
open_project impl_prj
set_top banked_top
add_files banked_top.cpp -cflags "-I../../src -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
export_design -flow syn -rtl verilog -format ip_catalog
exit
