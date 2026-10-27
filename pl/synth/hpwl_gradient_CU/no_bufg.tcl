# no_bufg.tcl -- pre-opt_design hook for the v++ link (--vivado.prop run.impl_1.STEPS.OPT_DESIGN.TCL.PRE).
# opt_design moves a high-fanout pipeline enable onto a BUFG_FABRIC when timing has room for one, and
# the platform's dynamic-region Pblock has no BUFG range, so placement fails DRC HDPR-18 (225 MHz,
# 2026-10-26). CLOCK_BUFFER_TYPE NONE keeps the kernel's signal nets on fabric routing. Meow.
set kernel_nets [get_nets -hierarchical -quiet -filter {NAME =~ top_i/ulp/hpwl_gradient_CU_top_1/* && TYPE == SIGNAL && FLAT_PIN_COUNT > 1000}]
if {[llength $kernel_nets]} { set_property CLOCK_BUFFER_TYPE NONE $kernel_nets }
puts "no_bufg.tcl: CLOCK_BUFFER_TYPE NONE on [llength $kernel_nets] kernel nets"
