
Within this directory is the source files for AIEplace targeting the VCK5000 platform. They can be built using the Makefiles in each subdirectory.

# Suggested workflow:

* Build AIE kernels: `cd aie; make aie; cd -`
* Build PL kernels: `cd hw; make device; cd -`
* Build host code: `cd sw; make host; cd -`
* Run application on hardware: `cd bin; ./host_aieplace.exe aieplace.hw.xclbin`

### vck5000/sw
* Contains host software source code.
* Build with `make host`