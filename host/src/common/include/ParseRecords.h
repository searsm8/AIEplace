/**
 * @file ParseRecords.h
 * @brief The plain records a design reader hands to DataBase: one per LEF SITE / MACRO / PIN,
 *        DEF COMPONENT / PIN / NET, and Bookshelf net / row. DesignReader.cpp fills them; nothing
 *        here knows which parser produced them (the #43 equivalence harness fills the same
 *        records from Limbo, which is how the two are compared field for field). Meow.
 */
#pragma once
#include "Common.h"
#include <string_view>
#include <utility>

AIEPLACE_NAMESPACE_BEGIN

struct LefSite {
    bool has_size = false;
    double size_x = 0, size_y = 0;   // microns
    bool has_class = false;
    string site_class;               // "CORE", "PAD", ...
};

struct LefMacro {
    double size_x = 0, size_y = 0;   // microns; 0 when the MACRO has no SIZE
    bool has_class = false;
    string macro_class;              // the CLASS words joined by one space, e.g. "PAD INPUT"
};

/// A MACRO PIN, reduced to what DataBase uses: its USE and the first RECT of its first PORT.
struct LefPin {
    string name;
    string use;                      // empty when the PIN has no USE
    bool has_rect = false;
    double rect_xl = 0, rect_yl = 0, rect_xh = 0, rect_yh = 0;   // microns, as written
};

// DefComponent and DefNet are views into the reader's buffer, valid only during the callback,
// like the Bookshelf .nets records. Meow.
struct DefComponent {
    std::string_view name, macro_name;
    std::string_view status;         // "PLACED" / "FIXED" / "COVER" / "UNPLACED", or "" if none given
    int x = -1, y = -1;              // DBU
};

struct DefPin {
    string name, direction;
    string status;                   // "PLACED" / "FIXED" / "UNPLACED", or ""
    int x = -1, y = -1;              // DBU; -1 when the PIN has no placement
    bool has_layer = false;
    int bbox[4] = {0, 0, 0, 0};      // first LAYER rectangle, relative to the pin origin
};

using DefNetPin = std::pair<std::string_view, std::string_view>;   // (component, pin), or ("PIN", io pin)

struct DefNet {
    std::string_view name;
    const DefNetPin* pins = nullptr;
    size_t num_pins = 0;
};

// One .nodes line: a cell, or a FIXED terminal. A view, like the .nets records below. Meow.
struct BookshelfNode {
    std::string_view name;
    int width = 0, height = 0;
    bool terminal = false;
};

// The two .nets records are VIEWS into the reader's buffer, valid only during the callback: a
// large design has ~10 M pins, and copying each name out was a real cost. Meow.
struct BookshelfNetPin {
    std::string_view node_name, pin_name;   // pin_name is empty unless the .nets line names one
    double offset_x = 0, offset_y = 0;      // from the node CENTER
};

struct BookshelfNet {
    std::string_view name;
    const BookshelfNetPin* pins = nullptr;
    size_t num_pins = 0;
};

// One .pl line. Views, like the .nets records; orient and status are canonical upper case. Meow.
struct BookshelfPlacement {
    std::string_view name;
    double x = 0, y = 0;
    std::string_view orient;         // "N", "S", "W", "E", "FN", "FS", "FW", "FE"
    std::string_view status;         // "FIXED", "FIXED_NI", "PLACED", "UNPLACED", or empty
};

struct BookshelfRow {
    int origin_x = -1, origin_y = -1;    // SubrowOrigin, Coordinate
    int height = 0, site_num = 0, site_width = 0, site_spacing = 0;
};

/// What a reader feeds, in file order. DataBase is the host's sink; another consumer (beat_packer's
/// netlist builder) overrides only what it needs -- every callback defaults to ignoring its input.
/// The batched callbacks get records in file order, once per block of the file. Meow.
class DesignSink
{
public:
    virtual ~DesignSink() = default;
    virtual void lef_site_cbk(const LefSite&) {}
    virtual void lef_macrobegin_cbk(const string&) {}
    virtual void lef_macro_cbk(const LefMacro&) {}
    virtual void lef_pin_cbk(const LefPin&) {}
    virtual void set_def_unit(int) {}
    virtual void set_def_design(const string&) {}
    virtual void set_def_diearea(int, int, int, int) {}
    virtual void add_def_components(const std::vector<DefComponent>&) {}
    virtual void add_def_pin(const DefPin&) {}
    virtual void add_def_nets(const std::vector<DefNet>&) {}
    virtual void resize_def_region(int) {}
    virtual void resize_def_group(int) {}
    virtual void add_bookshelf_nodes(const std::vector<BookshelfNode>&) {}
    virtual void add_bookshelf_nets(const std::vector<BookshelfNet>&) {}
    virtual void add_bookshelf_row(const BookshelfRow&) {}
    virtual void set_bookshelf_node_positions(const std::vector<BookshelfPlacement>&) {}
    virtual void set_bookshelf_design(const string&) {}
    virtual void bookshelf_end() {}
};

AIEPLACE_NAMESPACE_END
