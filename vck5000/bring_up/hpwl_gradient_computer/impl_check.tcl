# Out-of-context Vivado place-and-route of the synthesized kernel: real timing and URAM/BRAM/DSP,
# which C-synthesis only estimates. Run AFTER synth_check.tcl, from bring_up/hpwl_gradient_computer/:
#   vitis_hls -f impl_check.tcl      (source Vitis settings64.sh first; takes an hour or more)
# Result: synth_check_prj/sol1/impl/report/verilog/*export.rpt
open_project synth_check_prj
open_solution sol1
# phys_opt replicates high-fanout registers next to their loads, e.g. the macro-pin adders' writes
# that reach every URAM bank. Meow.
config_export -vivado_phys_opt all
export_design -flow impl -rtl verilog -format xo
exit
