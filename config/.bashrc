PS1='\w\$ '

#source /scratch/msears/acdc/utils/setup_env.sh
#source /scratch/msears/acdc/utils/setup_python_packages.sh /scratch/msears/acdc

source /home/msears/jacks_setup.sh
source /home/msears/tools/SDK/environment-setup-cortexa72-cortexa53-xilinx-linux

export ACDC=/scratch/msears/acdc/build/install
export PATH=/sbin:${ACDC}/bin:${ACDC}/peano/bin:${PATH}
export PATH=/group/xrlabs2/acdc-buildbot/installs/mlir-aie_daily_latest/bin:${PATH}
export PYTHONPATH=${ACDC}/python:${PYTHONPATH}
export PYTHONPATH=/home/msears/.local/lib/python3.8/site-packages:${PYTHONPATH}
export LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/:${ACDC}/lib:${LD_LIBRARY_PATH}
export TORCH_MLIR=/scratch/msears/acdc/build/torch-mlir
export PYTHONPATH=${PYTHONPATH}:${TORCH_MLIR}/python_packages/torch_mlir

