#ifndef PL_ALGO_PACKER_HPP
#define PL_ALGO_PACKER_HPP

// Packer.hpp -- host-side staging of the v0 host->PL transfer buffers.
// Walks the parsed DataBase and produces the flat, index-based buffers defined
// in host_interface.hpp. This is the "rework" half of the pl_algo host: the
// parser/data-model is reused unchanged; everything below turns it into the PL
// contract.

#include "DataBase.h"
#include "PackedDesign.hpp"

namespace plalgo {

// Build the v0 buffers from a parsed DataBase.
//   - movable components  -> indices [0, M)
//   - FIXED components + IOPads -> indices [M, N)
//   - fillers are excluded (v0 is HPWL-only)
// Nets reference nodes by these indices; pin offsets come from NetPin.offset.
PackedDesign packDesign(AIEplace::DataBase& db);

// CPU reference: total HPWL computed directly from a PackedDesign. Mirrors what
// the PL kernel does over the same buffers, so it verifies the packing in
// isolation (independent of, and cross-checked against, the DataBase golden).
// Accumulates in double: at ~1e6 nets, summing per-net HPWL (~1e5 each) into a
// float accumulator is order-dependent to ~0.3%, so float is not a usable
// reference. Per-net bbox stays in float (positions are float).
double hpwlFromPacked(const PackedDesign& pk);

} // namespace plalgo

#endif // PL_ALGO_PACKER_HPP
