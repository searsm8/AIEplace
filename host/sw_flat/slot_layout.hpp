#ifndef SW_FLAT_SLOT_LAYOUT_HPP
#define SW_FLAT_SLOT_LAYOUT_HPP

// slot_layout.hpp -- the whole design in device slot order (#45 step 4): the one index every
// per-node array the PL touches is laid out in.
//
//   global slot = chunk_first_slot[k] + chunk k's local slot     for k = 0 .. num_chunks-1
//               | first_filler_slot + filler index               the density-only tail
//
// The chunk part IS the gradient's layout (encode_chunked, unchanged): owned cells and macros, the
// external tail rows (copies of nodes owned by another chunk), macro-pin slots, fixed-pin slots, and
// row-padding EMPTY slots. Every movable node owns exactly one slot there, netless ones included --
// assign_banks gives a node on no net the least-loaded bank -- so the density-only tail holds only
// the fillers. They have no pins, so they never enter a record beat, a bank colouring or a chunk's
// capacity, and the gradient module never sees them.
//
// The density path streams the whole range and deposits only the slots with a non-zero size:
// OWNED and FILLER. An EXTERNAL slot is a copy of a node deposited by its owner, a pin slot is a
// point. test/slot_layout_check.cpp holds these invariants on all 44 designs. Meow.

#include "fillers.hpp"
#include "flat_design.hpp"
#include "packer/beat_packer.hpp"

#include <cstdint>
#include <vector>

namespace sw_flat {

enum class SlotRole : uint8_t {
    EMPTY,       // row padding; no node
    OWNED,       // a movable node's own slot: position, gradient, density
    EXTERNAL,    // a copy of a movable node another chunk owns (packer rule C7)
    MACRO_PIN,   // one pin of a movable macro, refreshed on chip from the macro
    FIXED_PIN,   // one pin of a fixed node or IO pad, constant position
    FILLER,      // the density-only tail
};

struct SlotLayout {
    int num_chunks = 0;
    std::vector<long> chunk_first_slot;   // num_chunks + 1; chunk k is [chunk_first_slot[k], chunk_first_slot[k + 1])
    long first_filler_slot = 0;           // = chunk_first_slot[num_chunks]
    long num_slots = 0;                   // a multiple of BANKS; slots after the last filler are EMPTY

    // Per global slot.
    std::vector<SlotRole> role;
    std::vector<int32_t>  node;           // FlatDesign node (a pin slot: the node it is a pin of); FILLER: filler index; EMPTY: -1
    std::vector<float>    width, height;  // DBU; non-zero only on OWNED and FILLER, the slots that deposit density

    std::vector<long> home_slot;          // per FlatDesign node: its OWNED slot, -1 if fixed or an IO pad
};

// The netlist behind `chunked` must be to_netlist(design, ...): its node ids are FlatDesign's. Meow.
inline SlotLayout build_slot_layout(const FlatDesign& design, const packer::Chunked& chunked, const Fillers& fillers)
{
    SlotLayout layout;
    layout.num_chunks = chunked.num_chunks;
    layout.chunk_first_slot.assign(1, 0);
    for (const packer::Chunk& chunk : chunked.chunks)
        layout.chunk_first_slot.push_back(layout.chunk_first_slot.back() + chunk.enc.num_slots);
    layout.first_filler_slot = layout.chunk_first_slot.back();
    const long filler_end = layout.first_filler_slot + fillers.count;
    layout.num_slots = (filler_end + pinrec::BANKS - 1) / pinrec::BANKS * pinrec::BANKS;

    layout.role.assign(layout.num_slots, SlotRole::EMPTY);
    layout.node.assign(layout.num_slots, -1);
    layout.width.assign(layout.num_slots, 0.0f);
    layout.height.assign(layout.num_slots, 0.0f);
    layout.home_slot.assign(design.num_nodes(), -1);

    for (int k = 0; k < chunked.num_chunks; k++) {
        const packer::Chunk& chunk = chunked.chunks[k];
        for (size_t local = 0; local < chunk.work_node.size(); local++) {
            if (chunk.enc.node_slot[local] < 0) continue;
            const long slot = layout.chunk_first_slot[k] + chunk.enc.node_slot[local];
            const int node = chunked.global.base_node[chunk.work_node[local]];
            layout.node[slot] = node;
            switch (chunk.enc.kind[local]) {
            case packer::MACRO_PIN: layout.role[slot] = SlotRole::MACRO_PIN; break;
            case packer::FIXED_PIN: layout.role[slot] = SlotRole::FIXED_PIN; break;
            default:
                if (chunk.external[local]) { layout.role[slot] = SlotRole::EXTERNAL; break; }
                layout.role[slot] = SlotRole::OWNED;
                layout.width[slot] = design.node_width[node];
                layout.height[slot] = design.node_height[node];
                layout.home_slot[node] = slot;
            }
        }
    }
    for (long slot = layout.first_filler_slot; slot < filler_end; slot++) {
        layout.role[slot] = SlotRole::FILLER;
        layout.node[slot] = (int32_t)(slot - layout.first_filler_slot);
        layout.width[slot] = fillers.width;
        layout.height[slot] = fillers.height;
    }
    return layout;
}

} // namespace sw_flat

#endif // SW_FLAT_SLOT_LAYOUT_HPP
