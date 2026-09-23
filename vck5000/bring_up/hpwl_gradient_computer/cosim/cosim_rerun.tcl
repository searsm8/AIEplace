# Re-run only co-simulations on the already-synthesized cosim_prj (see cosim.tcl), at the packer
# hazards given in $::env(COSIM_HAZARDS) (default "4 1"). Needs LIBRARY_PATH=/usr/lib/x86_64-linux-gnu
# in the environment: Vivado's bundled linker does not search Ubuntu's multiarch directory.
open_project cosim_prj
open_solution sol1
set hazards "4 1"
if {[info exists ::env(COSIM_HAZARDS)]} { set hazards $::env(COSIM_HAZARDS) }
foreach h $hazards { cosim_design -argv "$h" }
exit
