# Vivado synthesis (not just HLS's estimate) of ONE producer, to price it for real. csynth's report
# leaves the float divider cores out entirely (fdiv_*_no_dsp shows 0 LUT/FF/DSP), so the divide vs
# multiply question needs this. Run from pl/synth/bin_density_producer/:
#   PRODUCER_DIVIDE=0|1 vitis_hls -f impl_check.tcl   (source settings64.sh first)
# Result: impl_prj_div<0|1>/sol1/impl/report/verilog/export_syn.rpt (or the log's utilization table). Meow.
set divide [expr {[info exists ::env(PRODUCER_DIVIDE)] ? $::env(PRODUCER_DIVIDE) : 1}]
open_project impl_prj_div$divide
set_top producer_top
add_files producer_top.cpp -cflags "-I../../src -std=c++14 -DPRODUCER_DIVIDE=$divide"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
export_design -flow syn -rtl verilog -format ip_catalog
exit
