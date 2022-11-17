#
#Copyright 2020 Xilinx, Inc.
#Licensed under the Apache License, Version 2.0 (the "License");
#you may not use this file except in compliance with the License.
#You may obtain a copy of the License at
#    http://www.apache.org/licenses/LICENSE-2.0
#Unless required by applicable law or agreed to in writing, software
#distributed under the License is distributed on an "AS IS" BASIS,
#WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#See the License for the specific language governing permissions and
#limitations under the License.
#
TARGET   = hw_emu#hw
MODE	 = linux
PLATFORM = ${PLATFORM_REPO_PATHS}/xilinx_vck190_base_202210_1/xilinx_vck190_base_202210_1.xpfm
XSA	 = vck190_aie_base_graph_${TARGET}.xsa
XPFM	 = vck190_xpfm
HOST_EXE = host.exe
DATA_FILE = data/data.h
SDKTARGETSYSROOT ?= ${SYSROOT}

GRAPH    = aie/graph.cpp
LIBADF  = libadf.a
AIE_CMPL_CMD = aiecompiler -platform=${PLATFORM} -workdir=./Work ${GRAPH} 

AIE_SRC_DIR = aie
AIE_INCLUDES := -include=$(AIE_SRC_DIR)
AIE_INCLUDES += -include=$(AIE_SRC_DIR)/kernels
AIE_INCLUDES += -include=$(DSPLIB_ROOT)/L1/include/aie
AIE_INCLUDES += -include=$(DSPLIB_ROOT)/L1/src/aie
AIE_INCLUDES += -include=$(DSPLIB_ROOT)/L1/tests/aie/inc
AIE_INCLUDES += -include=$(DSPLIB_ROOT)/L1/tests/aie/src
AIE_INCLUDES += -include=$(DSPLIB_ROOT)/L2/include/aie
AIE_INCLUDES += -include=$(DSPLIB_ROOT)/L2/tests/aie/common/inc
AIE_INCLUDES += -include=/proj/xbuilds/SWIP/2022.1_0420_0327/installs/lin64/Vitis/2022.1/aietools/include/

AIE_SIM_CMD = aiesimulator --pkg-dir=./Work --dump-vcd foo
EMU_CMD = ./launch_hw_emu.sh

##########################################################################################################################################################
### DO NOT MODIFY BELOW THIS LINE UNLESS NECESSARY
################################################################################################################################################


VCC      = v++
VPP_SPEC =system.cfg
VPP_FLAGS=--save-temps --verbose --config ${VPP_SPEC}  
LDCLFLAGS=

.PHONY: clean host

###
# Guarding Checks. Do not modify.
###
check_defined = \
	$(strip $(foreach 1,$1, \
		$(call __check_defined,$1,$(strip $(value 2)))))

__check_defined = \
	$(if $(value $1),, \
		$(error Undefined $1$(if $2, ($2))))

guard-PLATFORM_REPO_PATHS:
	$(call check_defined, PLATFORM_REPO_PATHS, Set your where you downloaded xilinx_vck190_es1_base_202210_1)

guard-ROOTFS:
	$(call check_defined, ROOTFS, Set to: xilinx-versal-common-v2022.1/rootfs.ext4)

guard-IMAGE:
	$(call check_defined, IMAGE, Set to: xilinx-versal-common-v2022.1/Image)

guard-CXX:
	$(call check_defined, CXX, Run: xilinx-versal-common-v2022.1/environment-setup-cortexa72-cortexa53-xilinx-linux)

guard-SDKTARGETSYSROOT:
	$(call check_defined, SDKTARGETSYSROOT, Run: xilinx-versal-common-v2022.1/environment-setup-cortexa72-cortexa53-xilinx-linux)

###

all: ${XSA} ${HOST_EXE} package
run: all emu
sd_card: all

aie: guard-PLATFORM_REPO_PATHS ${LIBADF}
${LIBADF}: aie/*
	${AIE_CMPL_CMD} ${AIE_INCLUDES}

sim: aiesim
aiesim: ${LIBADF}
	${AIE_SIM_CMD}

xsa: guard-PLATFORM_REPO_PATHS ${XSA}


${XSA}: ${LIBADF} ${VPP_SPEC} 
	${VCC} -g -l --platform ${PLATFORM} ${LIBADF} -t ${TARGET} ${VPP_FLAGS} -o $@

host: guard-CXX guard-SDKTARGETSYSROOT ${DATA_FILE} ${HOST_EXE}
${HOST_EXE}: ${GRAPH} ./Work/ps/c_rts/aie_control_xrt.cpp
	$(MAKE) -C sw/

package: guard-ROOTFS guard-IMAGE guard-PLATFORM_REPO_PATHS package_${TARGET}
package_${TARGET}: ${LIBADF} ${XSA} ${HOST_EXE} 
	${VCC} -p -t ${TARGET} -f ${PLATFORM} \
		--package.rootfs ${ROOTFS} \
		--package.kernel_image ${IMAGE} \
		--package.boot_mode=sd \
		--package.image_format=ext4 \
		--package.defer_aie_run \
		--package.sd_file embedded_exec.sh \
		--package.sd_file ${HOST_EXE} \
		${XSA} ${LIBADF}

emu:
	$(EMU_CMD)


analyze:
	vitis_analyzer ./Work/graph.aiecompile_summary

vcd:
	vcdanalyze -vcd foo.vcd 

clean:
	rm -rf _x v++_* ${XOS} ${OS} ${LIBADF} *.o.* *.o *.xpe *.xo.* \
	       vck190_aie_base*.xclbin* *.xsa *.log *.jou xnwOut Work Map_Report.csv \
	       ilpProblem* sol.db drivers .Xil
	$(MAKE) -C sw clean
