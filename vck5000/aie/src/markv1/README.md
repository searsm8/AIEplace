### vck5000/aie

This directory contains source code for the AIE computation kernels, which efficiently compute HPWL partials and electromagnetic forces based on density. These kernels are the primary acceleration of the AIEplace application, supported by the PL kernels (vck5000/hw) and controlled by the host code (vck5000/sw).

# Suggested workflow:

* Build full AIE graph with `make aie`
* Run AIE simulation using `make aiesim`
* Display Vitis analyzer using `make analyze` (GUI required)
* Move on to `vck5000/hw`

# Other options:
* Build graph with only partials computation using `make partials`
* Build graph with only density computation using `make density`
* Remove all generated files with `make clean`