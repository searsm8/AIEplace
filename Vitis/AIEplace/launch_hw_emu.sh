#!/bin/bash -f
EMUDIR="$PWD"
    if [ -d "$EMUDIR" ]
      then
      OUTDIR=$EMUDIR
      cd $OUTDIR
      echo "Current working dir $OUTDIR"
      if [ -e "qemu_args.txt" ]
        then
        echo "Required emulation files like qemu_args exists"
      else
        echo "Required emulation files like qemu_args.txt doesn't exist. Please run from output dir"
        exit 1
      fi
    else
        if [ -e "qemu_args.txt" ]
          then
          echo "Required emulation files like qemu_args exists"
          cd .
          echo "Current working dir $PWD"
        else
          echo "Required emulation files like qemu_args.txt doesn't exist. Please run from output dir"
          exit 2
        fi
    fi
    if [ -z "$LD_LIBRARY_PATH" ]; then
      LD_LIBRARY_PATH="$XILINX_VITIS/tps/lnx64/python-3.8.3/lib:$XILINX_VITIS/lib/lnx64.o/:$XILINX_VIVADO/data/emulation/ip_utils/xtlm_ipc/xtlm_ipc_v1_0/cpp/lib/:$XILINX_VIVADO/lib/lnx64.o/Default/"
    else
      LD_LIBRARY_PATH="$XILINX_VITIS/tps/lnx64/python-3.8.3/lib:$XILINX_VITIS/lib/lnx64.o/:$XILINX_VIVADO/data/emulation/ip_utils/xtlm_ipc/xtlm_ipc_v1_0/cpp/lib/:$XILINX_VIVADO/lib/lnx64.o/Default/:$LD_LIBRARY_PATH"
    fi
    export LD_LIBRARY_PATH
$XILINX_VITIS/tps/lnx64/python-3.8.3/bin/python3 $XILINX_VITIS/bin/launch_emulator.py  -device-family versal \
-aie-sim-config  $PWD/cfg/aie.sim.config.txt \
-noc-memory-config $PWD/sim/behav_waveform/xsim/noc_memory_config.txt \
-qemu-dtb $PWD/sim/behav_waveform/xsim/board-versal-ps-cosim-vitis-vck190.dtb \
-pmc-dtb $PWD/sim/behav_waveform/xsim/board-versal-pmc-virt.dtb \
-target hw_emu \
-qemu-args-file $PWD/qemu_args.txt \
-pmc-args-file $PWD/pmc_args.txt \
-pl-sim-dir $PWD/sim/behav_waveform/xsim \
-sd-card-image $PWD/sd_card.img \
-enable-prep-target \
-xtlm-log-state WAVEFORM_AND_LOG \
-platform-name xilinx_vck190_base_202210_1 \
$*
