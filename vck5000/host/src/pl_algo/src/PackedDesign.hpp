#ifndef PL_ALGO_PACKED_DESIGN_HPP
#define PL_ALGO_PACKED_DESIGN_HPP

// PackedDesign.hpp -- host staging of the v0 host->PL buffers (see
// host_interface.hpp). Deliberately parser-free (no DataBase / Limbo): it holds
// only POD records in std::vectors, so it can be shared by the parser-side
// packer (old GLIBCXX ABI, for Limbo) and the XRT driver (new ABI, for libxrt)
// without dragging the heavy parser headers or an ABI conflict across that line.
// std::vector layout is ABI-stable across _GLIBCXX_USE_CXX11_ABI, so passing a
// PackedDesign between the two is safe; only std::string would not be.

#include "host_interface.hpp"
#include <vector>
#include <cstdint>

namespace plalgo {

struct PackedDesign {
    DesignHeader           header;
    std::vector<coord_t>   node_pos;  // [num_nodes]  movable [0,M), fixed [M,N)
    std::vector<int32_t>   net_ptr;   // [num_nets+1] CSR prefix offsets
    std::vector<PinRecord> pins;      // [num_pins]   flattened, net-major
};

} // namespace plalgo

#endif // PL_ALGO_PACKED_DESIGN_HPP
