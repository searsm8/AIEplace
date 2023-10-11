# AIEplace C++ host code

Host code to run AIEplace algorithm. The role of the host is to read in the design and send coordinate data to the AI Engines.

The host code for running AIEplace can be found in `./src` and compiled with `make`.

Limbo libraries are used to read LEF/DEF benchmarks, and must be built:

    cd Limbo
    mkdir build
    cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=absolute/path/to/your/installation
    make
    make install 