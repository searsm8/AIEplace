#ifndef SW_FLAT_FLAT_DATABASE_HPP
#define SW_FLAT_FLAT_DATABASE_HPP

// flat_database.hpp -- the one parse (#45 step 3): FlatDatabase is what the host reader
// (host/src/common/src/DesignReader.cpp) writes into -- a DesignSink, the reader's callback interface
// -- and it fills a FlatDesign straight from the files, with no DataBase objects. (Was FlatSink.)
//
// Every rule is DataBase's (DataBase.cpp, callbacks section), copied down to the float operation
// order, because DataBase is what sw_only -- the golden -- places from. test/flat_database_check.cpp
// holds the two bit-equal on every design. Where DataBase would crash on bad input (a net naming
// an unknown node, a component of an undefined LEF macro), FlatDatabase records an error instead.
// Meow.

#include "flat_design.hpp"
#include "DesignReader.h"

#include <map>
#include <string_view>
#include <unordered_map>

namespace sw_flat {

// Name -> node id, flat open addressing (host/src/common/include/NameIndex.h's design, keyed on
// FlatDesign's own name array instead of T::getName()). A parse looks a node up once per net pin,
// ~10 M times on the largest designs, and a node-based map makes each lookup several dependent
// cache misses. hash / prefetch / find are split so a caller holding a whole net's names can start
// every miss before waiting on any. find() is safe from many threads while nothing inserts. Meow.
class NodeIndex
{
public:
    explicit NodeIndex(const std::vector<std::string>& names) : names(names) {}

    static uint64_t hash_of(std::string_view name) { return std::hash<std::string_view>()(name); }
    void prefetch(uint64_t hash) const { if (!slots.empty()) __builtin_prefetch(&slots[hash & mask]); }
    void prefetch_name(uint64_t hash) const
    {
        if (!slots.empty() && slots[hash & mask].id >= 0) __builtin_prefetch(&names[slots[hash & mask].id]);
    }
    int find(std::string_view name, uint64_t hash) const;   // -1 if absent
    // Keeps the FIRST id under a name, like std::map::emplace: returns the id already there, or
    // records new_id (whose name must already be names[new_id]) and returns it. Meow.
    int insert(uint64_t hash, int new_id);

private:
    struct Slot { uint64_t hash; int id; };
    const std::vector<std::string>& names;
    std::vector<Slot> slots;
    size_t size = 0, mask = 0;
    void grow();
};

class FlatDatabase : public AIEplace::DesignSink
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
        // microns, from the lower-left; std::less<> so a pin is found by string_view, no copy
        std::map<std::string, std::pair<float, float>, std::less<>> pin_offset;
    };
    std::vector<LefMacroFacts> lef_macros;
    std::map<std::string, int, std::less<>> lef_macro_id;
    LefMacroFacts* current_macro = nullptr;
    LefMacroFacts discarded_macro;            // a repeated MACRO's body: DataBase keeps the first
    std::vector<int> node_macro;              // per node, the LEF macro id; -1 = not a component

    // Components and IO pads are separate namespaces, as in DataBase; Bookshelf nodes are components.
    NodeIndex component_id{design.node_name}, iopad_id{design.node_name};
    long row_xmin = 0, row_ymin = 0, row_xmax = 0, row_ymax = 0;
    int  row_count = 0;
    int  terminal_max_x = 0, terminal_max_y = 0;   // DataBase's m_max_x / m_max_y: ints, on purpose

    // Appends the node unless `index` already holds its name (then returns -1 and adds nothing).
    int  add_node(NodeIndex& index, uint64_t hash, std::string_view name, NodeKind kind, float x, float y,
                  float width, float height, int macro);
    // Sizes the arrays for a batch of nets (first_pin: the batch's own CSR offsets, from 0) and
    // writes its CSR rows; returns the first new net and the first new pin. Names, pins and
    // offsets are the caller's to fill, in parallel. Meow.
    std::pair<int, int> append_nets(const std::vector<size_t>& first_pin);
    void fail(const std::string& what) { if (error.empty()) error = what; }
};

// DataBase::readInput's file discovery and order, then placement.constraints: every .lef in the
// directory, then floorplan.def; Bookshelf (the first .aux) only if either is missing or fails.
// Exits on a malformed maximum_utilization, as DataBase does (TODO #27). On any read or parse error,
// returns false with the reason in `why`. Meow.
bool read_flat_design(const fs::path& dir, FlatDatabase& flat_db, std::string& why);

} // namespace sw_flat

#endif // SW_FLAT_FLAT_DATABASE_HPP
