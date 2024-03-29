
Within this directory is the source files for AIEplace targeting the VCK5000 platform. They can be built using the Makefiles.

# Suggested workflow:

You can build the different parts of the application these commands:
* `make aie`: Build the entire AIE array configuration. Output: libadf.a
* `make partials`: Build only the AIE kernels for partials computation.
* `make density`: Build only the AIE kernels for density computation.
* `make pl`: Build HLS kernels for the PL. Output: xilinx objects [.xo] in pl/reference_files_hw
* `make package`: Package the previously built PL kernels and libadf.a into finished hardware configuration. Output: bin/aieplace.hw.xclbin
* `make host`: Builds the host code which runs on a host x86 machine.
* Run application on hardware: `cd bin; ./host_aieplace.exe aieplace.hw.xclbin`
