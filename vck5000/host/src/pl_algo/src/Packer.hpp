#ifndef PL_ALGO_PACKER_HPP
#define PL_ALGO_PACKER_HPP

// Packer.hpp -- host-side staging of the v0 host->PL transfer buffers.
// Walks the parsed DataBase and produces the flat, index-based buffers defined
// in host_interface.hpp. This is the "rework" half of the pl_algo host: the
// parser/data-model is reused unchanged; everything below turns it into the PL
// contract.

#include "DataBase.h"
#include "host_interface.hpp"
#include <vector>
#include <cstdint>

namespace plalgo {

// Host staging of the five v0 buffers (see host_interface.hpp).
struct PackedDesign {
    DesignHeader           header;
    std::vector<coord_t>   node_pos;  // [num_nodes]  movable [0,M), fixed [M,N)
    std::vector<int32_t>   net_ptr;   // [num_nets+1] CSR prefix offsets
    std::vector<PinRecord> pins;      // [num_pins]   flattened, net-major
};

// Build the v0 buffers from a parsed DataBase.
//   - movable components  -> indices [0, M)
//   - FIXED components + IOPads -> indices [M, N)
//   - fillers are excluded (v0 is HPWL-only)
// Nets reference nodes by these indices; pin offsets come from NetPin.offset.
PackedDesign packDesign(AIEplace::DataBase& db);

// CPU reference: total HPWL computed directly from a PackedDesign. Mirrors what
// the PL kernel will do over the same buffers, so it verifies the packing in
// isolation (independent of, and cross-checked against, the DataBase golden).
float hpwlFromPacked(const PackedDesign& pk);

} // namespace plalgo

#endif // PL_ALGO_PACKER_HPP
