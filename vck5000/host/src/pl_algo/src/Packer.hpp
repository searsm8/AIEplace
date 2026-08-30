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

// Tag movable macros on the DataBase (XPlace is_mov_macro rule). Call BEFORE db.addFillers() and
// packDesign() so both see the macro tags. Port of sw_only Placer::tagMovableMacros (Setup.cpp:106).
void tagMovableMacros(AIEplace::DataBase& db);

// Build the host->PL buffers from a parsed DataBase. The movable prefix is NESTED by kind
// (host_interface.hpp DesignHeader): std-cells [0,first_macro), movable macros
// [first_macro,first_filler), fillers [first_filler,M); then FIXED components + IOPads [M,N).
// Reads Node::isMovableMacro() (set by tagMovableMacros) to bucket macros, and db.getFillers()
// (populated by db.addFillers()) for fillers -- so a caller that skips both gets the old
// all-std-cell, filler-free pack (first_macro == first_filler == M), byte-identical to before.
// Filler initial positions are uniform-random across the die, seeded DETERMINISTICALLY (a fixed
// stream): exact parity with sw_only's interleaved std::rand() filler order is not attempted, since
// fillers are auxiliary and the A/B compares converged HPWL, not filler trajectories.
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
