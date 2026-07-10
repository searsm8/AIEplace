# Legalize + detail-place an AIEplace (sw_only) GP result with OpenROAD's opendp, and report
# the legal HPWL for a legal-vs-legal comparison against XPlace's published number.
#
# Inputs via env vars (all paths relative to vck5000/ or absolute):
#   ORD_TECH_LEF  ORD_CELLS_LEF  ORD_IN_DEF  ORD_OUT_DEF
# The input DEF must already carry ROWS + our GP coords (see tools/merge_gp_into_floorplan.py).
# OpenROAD's detailed_placement prints 'original HPWL' (our GP) and 'legalized HPWL' (LG+DP) in
# microns; multiply by the DEF UNITS (units/micron) for DBU.  Run:
#   openroad -no_init -exit tools/opendp_legalize.tcl
read_lef $env(ORD_TECH_LEF)
read_lef $env(ORD_CELLS_LEF)
read_def $env(ORD_IN_DEF)
detailed_placement
if {[catch {check_placement -verbose} msg]} { puts "LEGAL_CHECK: FAILED $msg" } else { puts "LEGAL_CHECK: OK" }
write_def $env(ORD_OUT_DEF)
