#!/bin/bash

# temp path to pick up updated cmake > 3.15 to compile cmake
#export PATH=/group/xrlabs/tools/x86_64_RHEL7/bin:${PATH}
#export LD_LIBRARY_PATH=/group/xrlabs/tools/x86_64_RHEL7/lib:${LD_LIBRARY_PATH}

# export LOCAL_TOOLS=/group/xrlabs/tools/x86_64_RHEL7_gcc10_clang10
export LOCAL_TOOLS=/group/xrlabs/tools/x86_64_RHEL7_gcc8.3_clang10
#------------------------------
# local
#------------------------------
export ver=`lsb_release -d`
export ARCH=`uname -m`
if [ `echo "${ver}" | grep -c 'release 7'` -eq  1 ] 
then
     export PATH=${LOCAL_TOOLS}/bin:${PATH}
     export LD_LIBRARY_PATH=${LOCAL_TOOLS}/lib:${LD_LIBRARY_PATH}
fi
 
export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${LOCAL_TOOLS}/lib64

export DEVKITS_ROOT=/tools/batonroot/rodin/devkits/lnx64

# python 3.8.3
export PATH=${DEVKITS_ROOT}/python-3.8.3/python-3.8.3/bin/:${PATH}
export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${DEVKITS_ROOT}/python-3.8.3/python-3.8.3/lib

# cmake 3.3.2
#export PATH=${DEVKITS_ROOT}/cmake-3.3.2/bin/:${PATH}


#------------------------------
# gcc
#------------------------------
GCC_VERSION=8.3.0
echo "Setup gcc $GCC_VERSION environment ..."
export GCC_BASE=${DEVKITS_ROOT}/gcc-${GCC_VERSION}
#export GCC_BASE=${LOCAL_TOOLS}
export PATH=${GCC_BASE}/bin:${PATH}
export LD_LIBRARY_PATH=${GCC_BASE}/lib64:${LD_LIBRARY_PATH}
export CXX=${GCC_BASE}/bin/g++
export CC=${GCC_BASE}/bin/gcc


#------------------------------
# Vitis/Vivado
#------------------------------
#export MYXILINX_VER=2020.1
#export MYXILINX_VER=2021.2
export MYXILINX_VER=2022.1
export MYXILINX_BASE=/proj/xbuilds/${MYXILINX_VER}_daily_latest
export XILINX_LOC=$MYXILINX_BASE/installs/lin64/Vitis/$MYXILINX_VER
export CARDANO_ROOT=$XILINX_LOC/cardano
export AIETOOLS_ROOT=$XILINX_LOC/aietools
#export LM_LICENSE_FILE=2100@aiengine
export LM_LICENSE_FILE=2100@aiengine-eng
#export PATH=${AIETOOLS_ROOT}/bin:$CARDANO_ROOT/bin:$XILINX_LOC/bin:$PATH
#export PATH=$CARDANO_ROOT/bin:$XILINX_LOC/bin:$PATH
export PATH=${AIETOOLS_ROOT}/bin:$XILINX_LOC/bin:$PATH
export PLATFORM_REPO_PATHS=$MYXILINX_BASE/internal_platforms


#export MLIR_AIE_INSTALL=/group/xrlabs/jackl/rdi_diskspace/nobkup/github/Xilinx/mlir-aie/install
#export LLVM_INSTALL=/group/xrlabs/jackl/rdi_diskspace/nobkup/ghe/XRLabs/llvm/install
#export PATH=$MLIR_AIE_INSTALL/bin/:$LLVM_INSTALL/bin:$PATH
#export LD_LIBRARY_PATH=$MLIR_AIE_INSTALL/lib:$LLVM_INSTALL/lib:$LD_LIBRARY_PATH
##export PYTHONPATH=$MLIR_AIE_INSTALL/python:$MLIR_AIE_INSTALL/python_packages:$PYTHONPATH

export LC_ALL=C