### vck5000/hw

This directory contains source code for the PL computation kernels, which act as data movers and support the operation of the AIE kernels.

# Suggested workflow:
* Build PL kernels: `make device TARGET=hw` (default is hw_emu)
* If PL kernels and interfacing have not changed, use `make repackage` to avoid rebuilding PL kernels.