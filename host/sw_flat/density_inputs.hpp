#ifndef SW_FLAT_DENSITY_INPUTS_HPP
#define SW_FLAT_DENSITY_INPUTS_HPP

// density_inputs.hpp -- bin_density_CU's host-side inputs (#48), built from a set-up sw_flat Placer, so
// the device deposits exactly what sw_flat_place does (pl/src/format/density_footprint.hpp):
//   footprints   the distinct footprints, by (width, height, movable-macro) bits; every filler shares one
//   index        per slot: its footprint, or NO_DEPOSIT (BORROWED copies, pin slots, EMPTY padding)
//   fixed_base   Placer::fixed_base_q (col-major, capped, fixed units), 16 bins per beat
// with the scalars fixed_scale and overflow_cap as the placer computed them; needs [sw_flat] deposit =
// "fixed". The positions are the placer's next.probe_pos per slot, the ones its deposit reads. Meow.

#include "format/density_footprint.hpp"
#include "placer/placer.hpp"

#include <cstring>
#include <map>
#include <tuple>
#include <vector>

namespace sw_flat {

struct DensityInputs {
    std::vector<densrec::Footprint> footprints;
    std::vector<uint32_t> index;   // per slot
    std::vector<densrec::FixedBeat> fixed_base;
    float fixed_scale = 0;
    uint32_t overflow_cap = 0;
};

inline DensityInputs make_density_inputs(const Placer& placer)
{
    auto bits = [](float v) { uint32_t b; std::memcpy(&b, &v, sizeof b); return b; };
    DensityInputs in;
    in.index.assign(placer.layout.num_slots, densrec::NO_DEPOSIT);
    std::map<std::tuple<uint32_t, uint32_t, bool>, uint32_t> table_of;
    for (long slot = 0; slot < placer.layout.num_slots; slot++) {
        const SlotRole role = placer.layout.role[slot];
        if (role != SlotRole::OWNED && role != SlotRole::FILLER) continue;
        const float w = placer.layout.width[slot], h = placer.layout.height[slot];
        const bool macro = placer.is_movable_macro[slot];
        const auto key = std::make_tuple(bits(w), bits(h), macro);
        auto it = table_of.find(key);
        if (it == table_of.end()) {
            it = table_of.emplace(key, (uint32_t)in.footprints.size()).first;
            in.footprints.push_back(densrec::make_footprint(w, h, macro, placer.bin_w, placer.bin_h,
                                                            placer.cfg.target_density, placer.cfg.enable_density_clamp));
        }
        in.index[slot] = it->second;
    }
    in.fixed_base.resize(placer.fixed_base_q.size() / pinrec::LANES);
    for (size_t bin = 0; bin < placer.fixed_base_q.size(); bin++)
        in.fixed_base[bin / pinrec::LANES].q[bin % pinrec::LANES] = (uint32_t)placer.fixed_base_q[bin];
    in.fixed_scale = placer.fixed_scale;
    in.overflow_cap = placer.fixed_overflow_cap;
    return in;
}

} // namespace sw_flat

#endif // SW_FLAT_DENSITY_INPUTS_HPP
