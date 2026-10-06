#ifndef PL_ALGO_PACKED_DESIGN_HPP
#define PL_ALGO_PACKED_DESIGN_HPP

// PackedDesign.hpp -- host staging of the v0 host->PL buffers (see
// host_interface.hpp). Deliberately parser-free (no DataBase): it holds only POD
// records in std::vectors, so the XRT driver can take it without dragging the
// parser headers along. (It once also kept an ABI line: the parser side was
// built with the old GLIBCXX ABI for Limbo and the driver with the new one for
// libxrt. Limbo is gone (TODO #43), so the whole host now uses the new ABI.)

#include "host_interface.hpp"
#include <vector>
#include <cstdint>

namespace plalgo {

struct PackedDesign {
    DesignHeader         header;
    std::vector<coord_t> node_pos;  // [num_nodes]  movable [0,M), fixed [M,N)
    std::vector<NodeBox> node_box;  // [num_nodes]  same order; {x,y,w,h} for density binning
    std::vector<int32_t> net_ptr;   // [num_nets+1] CSR prefix offsets
    std::vector<NodePin> pins;      // [num_pins]   flattened, NET-major (CSR order)
    std::vector<NodePin> npins;     // [num_npins]  movable pins, NODE-major (sorted)
    // Static pin offsets, parallel to the two arrays above (P2). NodePin now carries the
    // ABSOLUTE position, refreshed each iteration by refresh_pin_pos on the device; these hold
    // the constant part and are uploaded once. See host_interface.hpp NodePin/PinOffset.
    std::vector<PinOffset> pin_off;   // [num_pins]
    std::vector<PinOffset> npin_off;  // [num_npins]
    // Static scatter permutation for the HPWL phase-2.5 restructure: pin_to_npin[p] is the
    // node-major slot (index into npins) of net-major pin p, or -1 if p has no gradient slot
    // (masked net, or a fixed node). The inverse of the sort that builds npins. Uploaded once.
    std::vector<int32_t> pin_to_npin; // [num_pins]
};

} // namespace plalgo

#endif // PL_ALGO_PACKED_DESIGN_HPP
