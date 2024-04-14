# AIEplace
Implementation of the [RePlace](https://github.com/The-OpenROAD-Project/RePlAce)<sup>[2]</sup> algorithm on the AMD Versal architecture, utilizing AIE, PL, and PS regions of the chip.

### Directory Structure

* The AIEplace application targeting the VCK5000 acceleration card can be found in the [vck5000](./vck5000) directory, and can be built and run using the Makefiles found there.
* A simplified version of the ePlace algorithm is implemented in the [python](./python).


### Functional examples on tiny benchmarks:

![Python implementation of AIEplace 8x48](docs/images/AIEplace_8x48.gif)
![Python implementation of AIEplace 8x8](docs/images/AIEplace_8x8.gif)
![Python implementation of AIEplace 16x16](docs/images/AIEplace_16x16.gif)
![Python implementation of AIEplace 24x24](docs/images/AIEplace_24x24.gif)

# References

[1] Lu, Jingwei, et al. "ePlace: Electrostatics-based placement using fast fourier transform and Nesterov's method." ACM Transactions on Design Automation of Electronic Systems (TODAES) 20.2 (2015): 1-34.

[2] Cheng, Chung-Kuan, et al. "Replace: Advancing solution quality and routability validation in global placement." IEEE Transactions on Computer-Aided Design of Integrated Circuits and Systems 38.9 (2018): 1717-1730.

[3] Lin, Yibo, et al. "Dreamplace: Deep learning toolkit-enabled gpu acceleration for modern vlsi placement." IEEE Transactions on Computer-Aided Design of Integrated Circuits and Systems 40.4 (2020): 748-761.
