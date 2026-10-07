#ifndef SW_FLAT_FLAT_DESIGN_HPP
#define SW_FLAT_FLAT_DESIGN_HPP

// flat_design.hpp -- one parsed design as flat arrays: what FlatSink (flat_sink.hpp) fills from a
// single read of the LEF/DEF or Bookshelf files, and what sw_flat and the packer both start from.
//
// It holds FACTS FROM THE FILES only, in the units and conventions of sw_only's DataBase (the
// golden), so the two can be compared field by field (test/flat_sink_check.cpp does, bit for bit).
// Anything derived -- movable-macro tags, fillers, the net-degree mask, slot order -- is computed
// later, as sw_only's Setup does (flat_netlist.hpp).
//
// Conventions (each one DataBase's, host/src/common/src/DataBase.cpp):
//   - Units are DBU. DEF: LEF microns x UNITS DISTANCE MICRONS (readDesignFiles, add_def_nets).
//     Bookshelf: as written. units_per_micron keeps DataBase's default of 1000 when no DEF set it.
//   - A cell's or fixed node's position is its LOWER-LEFT corner. Bookshelf positions are shifted so
//     the die's lower-left is (0, 0) (bookshelf_end's die shift); DEF positions are as written.
//   - An IO pad's position is its DEF PIN placement point and its size is its PIN bbox, which is
//     relative to that point (so the point need not be the bbox's corner). Its pins have offset 0.
//   - A pin offset is from the node's LOWER-LEFT corner (add_bookshelf_nets adds half the size to
//     the file's centre-relative offset).
//   - FIXED means exactly DataBase's FIXED: .nodes terminal, .pl /FIXED, DEF FIXED, and DEF PLACED
//     whose LEF class is neither CORE nor BLOCK (add_def_components). DEF COVER and a missing
//     status are NOT fixed -- DataBase maps them to UNKNOWN, and every consumer tests == FIXED.
//   - Nodes are in callback order: DEF COMPONENTS and DEF PINS (IO pads) as the file orders the two
//     sections, or .nodes order. A repeated name keeps its first definition, as DataBase does. Nets
//     are in file order and ALL are kept, whatever their degree; masking is the placer's choice.
// Meow.

#include <cstdint>
#include <string>
#include <vector>

namespace sw_flat {

enum class NodeKind : uint8_t { CELL, FIXED, IO_PAD };   // CELL = movable

struct FlatDesign {
    std::string name;
    int   units_per_micron = 1000;
    float die_xl = 0, die_yl = 0, die_xh = 0, die_yh = 0;
    float die_shift_x = 0, die_shift_y = 0;   // Bookshelf: subtracted from every position; DEF: 0
    float row_height = 0, site_width = 0;     // 0 when the input gave none
    float maximum_utilization = 0;            // placement.constraints (ISPD2015); 0 = not given

    // Per node, callback order. Indices are FlatDesign's node ids (not slots: that is the packer's).
    std::vector<std::string> node_name;
    std::vector<NodeKind>    node_kind;
    std::vector<float>       node_x, node_y;            // DBU, after the die shift
    std::vector<float>       node_width, node_height;   // DBU

    // Nets, CSR: net k's pins are [net_first_pin[k], net_first_pin[k + 1]).
    std::vector<std::string> net_name;
    std::vector<int32_t>     net_first_pin = {0};       // num_nets + 1
    std::vector<int32_t>     pin_node;
    std::vector<float>       pin_offset_x, pin_offset_y;   // from the node's lower-left, DBU

    int num_nodes() const { return (int)node_name.size(); }
    int num_nets() const { return (int)net_name.size(); }
    int net_degree(int net) const { return net_first_pin[net + 1] - net_first_pin[net]; }
};

} // namespace sw_flat

#endif // SW_FLAT_FLAT_DESIGN_HPP
