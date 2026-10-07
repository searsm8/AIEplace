#ifndef SW_FLAT_FLAT_SINK_HPP
#define SW_FLAT_FLAT_SINK_HPP

// flat_sink.hpp -- the one parse (#45 step 3): a DesignSink that fills a FlatDesign straight from
// the host reader (host/src/common/src/DesignReader.cpp), with no DataBase objects.
//
// Every rule is DataBase's (DataBase.cpp, callbacks section), copied down to the float operation
// order, because DataBase is what sw_only -- the golden -- places from. test/flat_sink_check.cpp
// holds the two bit-equal on every design. Where DataBase would crash on bad input (a net naming
// an unknown node, a component of an undefined LEF macro), FlatSink records an error instead.
// Meow.

#include "flat_design.hpp"
#include "DesignReader.h"

#include <map>
#include <unordered_map>

namespace sw_flat {

class FlatSink : public AIEplace::DesignSink
{
public:
    FlatDesign design;
    std::string error;   // the first one; the reader's own parse errors go to the log

    // Why a node's FIXED status differs from the bare file status word -- kept so a consumer
    // that used another rule (the legacy packer reader) can account for every difference. Meow.
    std::vector<int> cover_nodes;            // DEF COVER: movable here, fixed in the legacy reader
    std::vector<int> placed_fixed_nodes;     // DEF PLACED, LEF class not CORE/BLOCK -> FIXED
    std::vector<int> pl_fixed_nodes;         // Bookshelf .pl /FIXED on a non-terminal -> FIXED

    void lef_site_cbk(const AIEplace::LefSite&) override;
    void lef_macrobegin_cbk(const std::string&) override;
    void lef_macro_cbk(const AIEplace::LefMacro&) override;
    void lef_pin_cbk(const AIEplace::LefPin&) override;
    void set_def_unit(int units) override { design.units_per_micron = units; }
    void set_def_design(const std::string& name) override { design.name = name; }
    void set_def_diearea(int xl, int yl, int xh, int yh) override;
    void add_def_components(const std::vector<AIEplace::DefComponent>&) override;
    void add_def_pin(const AIEplace::DefPin&) override;
    void add_def_nets(const std::vector<AIEplace::DefNet>&) override;
    void add_bookshelf_nodes(const std::vector<AIEplace::BookshelfNode>&) override;
    void add_bookshelf_nets(const std::vector<AIEplace::BookshelfNet>&) override;
    void add_bookshelf_row(const AIEplace::BookshelfRow&) override;
    void set_bookshelf_node_positions(const std::vector<AIEplace::BookshelfPlacement>&) override;
    void set_bookshelf_design(const std::string& name) override { design.name = name; }
    void bookshelf_end() override;

    // DataBase::readDesignFiles' micron -> DBU step, once LEF and DEF are both read: LEF sizes and
    // the CORE site are in microns until then. Fills in every component's size. Meow.
    void finish_lefdef(bool scale_to_dbu);

private:
    struct LefMacroFacts {
        float width = 0, height = 0;          // microns until finish_lefdef
        std::string lef_class;
        std::map<std::string, std::pair<float, float>> pin_offset;   // microns, from the lower-left
    };
    std::vector<LefMacroFacts> lef_macros;
    std::unordered_map<std::string, int> lef_macro_id;
    LefMacroFacts* current_macro = nullptr;
    LefMacroFacts discarded_macro;            // a repeated MACRO's body: DataBase keeps the first
    std::vector<int> node_macro;              // per node, the LEF macro id; -1 = not a component

    std::unordered_map<std::string, int> component_id, iopad_id;
    long row_xmin = 0, row_ymin = 0, row_xmax = 0, row_ymax = 0;
    int  row_count = 0;
    int  terminal_max_x = 0, terminal_max_y = 0;   // DataBase's m_max_x / m_max_y: ints, on purpose

    int  add_node(const std::string& name, NodeKind kind, float x, float y, float width, float height,
                  int macro);
    void add_net(const std::string& name);
    void fail(const std::string& what) { if (error.empty()) error = what; }
};

// DataBase::readInput's file discovery and order, then placement.constraints: every .lef in the
// directory, then floorplan.def; Bookshelf (the first .aux) only if either is missing or fails.
// Exits on a malformed maximum_utilization, as DataBase does (TODO #27). On any read or sink error,
// returns false with the reason in `why`. Meow.
bool read_flat_design(const fs::path& dir, FlatSink& sink, std::string& why);

} // namespace sw_flat

#endif // SW_FLAT_FLAT_SINK_HPP
