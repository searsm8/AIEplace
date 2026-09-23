#ifndef RECORD_DESIGN_HPP
#define RECORD_DESIGN_HPP

// record_design.hpp -- shared fixture for the pin-record harnesses (#41): a synthetic netlist that
// exercises every record feature, random positions, and an optional real-benchmark loader.
//
// The synthetic netlist deliberately contains: cells sharing a small offset library, movable macros
// and fixed nodes with many distinct pin offsets (-> MACRO_PIN / FIXED_PIN slots), nodes repeated
// on one net (-> adjacent-lane merge), every degree 2..16, and 17..40-pin nets (out of scope, must
// be ignored). Meow.

#include "beat_packer.hpp"

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace fixture {

struct SyntheticSpec {
    int cells  = 3000;
    int macros = 6;
    int fixed  = 40;
    int nets   = 2600;
};

inline packer::Netlist build_synthetic(unsigned seed, const SyntheticSpec& spec = SyntheticSpec()) {
    std::mt19937 rng(seed);
    packer::Netlist nl;
    nl.name = "synthetic";
    for (int n = 0; n < spec.cells; n++)  { nl.movable.push_back(1); nl.is_macro.push_back(0); }
    for (int n = 0; n < spec.macros; n++) { nl.movable.push_back(1); nl.is_macro.push_back(1); }
    for (int n = 0; n < spec.fixed; n++)  { nl.movable.push_back(0); nl.is_macro.push_back(0); }
    const int macro_begin = spec.cells, fixed_begin = spec.cells + spec.macros;

    packer::OffsetDict dict[2];
    std::uniform_int_distribution<int> cell_offset(-10, 10);      // x0.5: a 21-value cell library
    std::uniform_int_distribution<int> big_offset(-400, 400);     // x0.5: macro pin geometry
    // Few offsets per fixed node, so one fixed pin node is shared by several nets (and, chunked,
    // copied into several chunks) -- as an IO pad or a macro pin driving many nets is. Meow.
    std::uniform_int_distribution<int> fixed_offset(-1, 1);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    auto pick_degree = [&]() {
        const double u = unit(rng);
        if (u < 0.03) return 17 + (int)(unit(rng) * 24);           // out of scope
        if (u < 0.40) return 2;
        if (u < 0.58) return 3;
        if (u < 0.70) return 4;
        return 5 + (int)(unit(rng) * 12);                          // 5..16
    };
    for (int net_index = 0; net_index < spec.nets; net_index++) {
        const int degree = pick_degree();
        std::vector<packer::Pin> net;
        for (int p = 0; p < degree; p++) {
            int node;
            const double u = unit(rng);
            if (p > 0 && u < 0.05)  node = net.back().node;                                   // repeated node
            else if (u < 0.12)      node = macro_begin + (int)(unit(rng) * spec.macros);
            else if (u < 0.19)      node = fixed_begin + (int)(unit(rng) * spec.fixed);
            else                    node = (int)(unit(rng) * spec.cells);
            packer::Pin pin; pin.node = node;
            for (int axis = 0; axis < 2; axis++) {
                const int half_units = node >= fixed_begin ? 150 * fixed_offset(rng)
                                     : node >= macro_begin ? big_offset(rng) : cell_offset(rng);
                pin.offset_key[axis] = dict[axis].key(0.5f * half_units, nl.offset_value[axis]);
            }
            net.push_back(pin);
        }
        nl.nets.push_back(net);
    }
    return nl;
}

// Positions indexed by parsed node, per axis.
inline std::vector<float> random_positions(size_t num_nodes, unsigned seed, float extent = 20000.0f) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> coord(0.0f, extent);
    std::vector<float> pos(num_nodes);
    for (float& value : pos) value = coord(rng);
    return pos;
}

inline float pin_position(const packer::Netlist& nl, const packer::Pin& pin, int axis, const std::vector<float>& node_pos) {
    return node_pos[pin.node] + nl.offset_value[axis][pin.offset_key[axis]];
}

// `--bookshelf DIR NAME` or `--def FILE NAME` -> a real netlist; returns false if no such args.
inline bool load_from_args(int argc, char** argv, packer::Netlist& nl) {
    if (argc == 4 && std::strcmp(argv[1], "--bookshelf") == 0) { nl = packer::read_bookshelf(argv[2], argv[3]); return true; }
    if (argc == 4 && std::strcmp(argv[1], "--def") == 0)       { nl = packer::read_def(argv[2], argv[3]); return true; }
    return false;
}

} // namespace fixture

#endif // RECORD_DESIGN_HPP
