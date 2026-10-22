# C-synthesis of the whole bank-FIFO deposit (banked_top.cpp): 8 producers, 512 FIFOs, 64 URAM banks
# with forwarding, at the full 1024 grid, to price it (#48 round 3).
# Run from pl/synth/bin_density_banked/:  vitis_hls -f synth_check.tcl   (source settings64.sh first). Meow.
open_project synth_check_prj
set_top banked_top
add_files banked_top.cpp -cflags "-I../../src -std=c++14"
open_solution sol1 -flow_target vitis
set_part {xcvc1902-vsvd1760-2MP-e-S}
create_clock -period 3.33 -name default
csynth_design
exit
