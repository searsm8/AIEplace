
/**
 * @file DataBase.h
 * @brief Owns the parsed design — macros, components, IO pads, nets — read from LEF/DEF or
 *        Bookshelf, plus filler generation.
 */
#pragma once
#include "Common.h"
#include "MacroClass.h"
#include "Component.h"
#include "IOPad.h"
#include "Node.h"
#include "Net.h"
#include "Bin.h"
#include "Logger.h"
#include "ParseRecords.h"
#include "NameIndex.h"
#include <sstream>

AIEPLACE_NAMESPACE_BEGIN

class DataBase
{
private:
    // Member Data, prefixed with "m_"
    fs::path m_input_dir; // Path to find directory containing design data.
                          // Expects to find a .lef and .def file

    map<string, MacroClass *> mm_macros;
    map<string, Component *> mm_components;
    map<string, IOPad *> mm_iopads;
    map<string, Net *> mm_nets;
    vector<Net *> mv_nets; // list of all nets
    vector<Component *> mv_fillers; // standard cell fillers
    map<int, std::vector<Net *>> mmv_nets_by_degree;

    // Index-addressable views of the maps above, built once by buildNodeIndex(). A std::map
    // cannot drive an `omp parallel for` (no random access), and walking its red-black tree
    // ~10x per iteration pointer-chases 200k-1M entries for nothing. Each vector holds its
    // map's iteration order, so a loop over it visits nodes in exactly the order the serial
    // map loops did -- that identical order is what keeps the reductions bit-reproducible.
    vector<Component *> mv_movable_components; // non-FIXED components, in mm_components order
    vector<Component *> mv_fixed_components;   // FIXED components,     in mm_components order
    vector<Node *> mv_iopad_nodes;             // in mm_iopads order
    vector<Net *> mv_nets_by_name;             // mm_nets order (mv_nets is insertion order)

    // "Everything that moves": mv_movable_components followed by mv_fillers. That is the order
    // every per-node loop in the placer already visited them in (movable components, then
    // fillers), and several of those loops carry a running float sum ACROSS the boundary --
    // splitting them into two loops would re-associate that sum and change the low bits. One
    // vector keeps the order, and halves the number of parallel regions per iteration.
    vector<Node *> mv_movable_nodes;
    int m_filler_start_index = 0;  // index in mv_movable_nodes where the fillers begin

    // Parse-time name -> component index, so a net pin costs one hash probe instead of two
    // string-compare walks of mm_components (most of the parse time on the large designs). Holds
    // exactly mm_components' entries (first insert wins in both). Emptied when readInput()
    // returns. Meow.
    NameIndex<Component> m_component_index;
    void addComponent(Component* comp_p);

    // mm_components / mm_nets entries made during the parse, queued in arrival order and put in
    // by flushParseInserts() in ONE sorted pass: millions of random-order tree descents were the
    // largest single cost left in the parse. The maps end up exactly as repeated emplace would
    // leave them (first value under a name wins). Nothing reads either map mid-parse. Meow.
    std::vector<Component*> mv_pending_components;
    std::vector<Net*> mv_pending_nets;
    void flushParseInserts();

    OrderedReduce m_ordered_reduce; // scratch for computeTotalWirelength (see Common.h)

    Box m_die_area;
    int m_max_x, m_max_y; // used when reading Bookshelf format to find die_area
    // Bookshelf die is derived from the .scl core-row bounding box (matches XPlace),
    // not from terminal coordinates. Node coords are then shifted so the die LL is the
    // origin (XPlace die_shift); m_die_shift is added back on DEF output.
    long m_row_xmin = 0, m_row_ymin = 0, m_row_xmax = 0, m_row_ymax = 0;
    int  m_row_count = 0;
    // Standard-cell row height in DBU (XPlace site_height), 0 when the input supplied none.
    // Bookshelf reads it from the .scl CoreRow; LEF/DEF from the CORE SITE, since a DEF ROW
    // records only an origin. Sizes fillers -- see addFillers.
    float m_row_height = 0.0f;
    // Placement-site width in DBU (XPlace site_width, database.py:147). Same two sources as
    // m_row_height. This is the design's natural length unit: XPlace divides every coordinate by
    // it (prescale_by_site_width, database.py:854), so any hyperparameter expressed as an absolute
    // displacement is in site widths there and must be scaled by this here -- see
    // Placer::estimateInitialStep(). Bookshelf .scl carries Sitewidth = 1, which is why ISPD2005/MMS
    // coordinates are already effectively site units and LEF/DEF ones are not.
    float m_site_width = 0.0f;
    Position m_die_shift; // (0,0) unless a bookshelf die_shift was applied
    string m_design_name;
    // DEF convention default (matches this repo's ispd2015 .def samples); set_def_unit()
    // overrides it for real DEF input. Bookshelf input (ISPD2005/MMS) never calls
    // set_def_unit, so without this default the DEF written on output read garbage.
    int m_units_per_micron = 1000;
    int m_total_net_degree;
    float m_maximum_utilization = 0.0f; // 0 = not specified by benchmark
    MacroClass* m_current_lef_macro = nullptr; // tracks current macro during LEF parsing for pin callbacks
    // Fence regions are PARSED AND DISCARDED -- we place the design unconstrained, exactly as
    // XPlace does. Counted only so readDEF() can warn that it happened (TODO #26).
    int m_num_def_regions = 0;
    int m_num_def_groups = 0;
    float m_total_component_area = 0.0f; // all components (movable + fixed), cached after construction
    float m_total_fixed_area = 0.0f;     // fixed components only
    float m_total_movable_area = 0.0f;   // movable components only (= total - fixed)

    // DataBase()'s steps, broken out for readability
    void readDesignFiles();
    void computeNetDegreeTotal();
    void computeAreaBreakdown();

protected:
    // Lets a subclass construct without reading, then call readInput() from its own constructor,
    // once its overrides of the parse*File hooks below are live. Only the #43 parser-equivalence
    // harness does this (it substitutes Limbo). Meow.
    struct DeferRead {};
    DataBase(fs::path input_dir, DeferRead) : m_input_dir(input_dir) {}
    void readInput();

    // Parse one file into this DataBase via the callbacks below (DesignReader.cpp). Meow.
    virtual bool parseLefFile(const fs::path& lef_file);
    virtual bool parseDefFile(const fs::path& def_file);
    virtual bool parseBookshelfAux(const fs::path& aux_file);

public:
    /// Default Constructor
    DataBase() {}
    DataBase(fs::path input_dir);

    /// Destructor
    virtual ~DataBase() {}

    // Getter functions
    // return const references to avoid copying large objects
    const map<string, MacroClass *> &getMacros() { return mm_macros; }
    const map<string, Component *> &getComponents() { return mm_components; }
    const vector<Component *> &getFillers() { return mv_fillers; }
    const map<string, IOPad *> &getIOPads() { return mm_iopads; }
    const map<string, Net *> &getNets() { return mm_nets; }
    const vector<Net *> &getNetsVector() { return mv_nets; }

    // Flat views for the threaded iteration loops -- see buildNodeIndex().
    const vector<Node *> &getMovableNodes() { return mv_movable_nodes; }
    int getFillerStartIndex() { return m_filler_start_index; } // fillers are [this, size())
    const vector<Component *> &getMovableComponents() { return mv_movable_components; }
    const vector<Component *> &getFixedComponents() { return mv_fixed_components; }
    const vector<Node *> &getIOPadNodes() { return mv_iopad_nodes; }
    const vector<Net *> &getNetsByName() { return mv_nets_by_name; }

    const map<int, std::vector<Net *>> &getNetsByDegree() { return mmv_nets_by_degree; }
    int getNetCountOfDegree(int degree) { return mmv_nets_by_degree[degree].size(); }
    int getTotalNetDegree() { return m_total_net_degree; }
    Box &getDieArea() { return m_die_area; }
    string getBenchmarkName() { return m_input_dir.filename().string(); }
    // The suite directory the design lives in ("ispd2005", "ispd2015", "mms"), and the
    // "<suite>/<design>" pair. **A bare design name is NOT a unique key**: adaptec1-4 and
    // bigblue1-4 exist in BOTH ispd2005 (fixed macros) and mms (movable macros), and are
    // different designs with different references -- ispd2005/adaptec1 GP-stops at 7.060e7,
    // mms/adaptec1 at 6.453e7, a 15% gap. Anything keyed on the bare name silently returns
    // whichever suite it happened to see. Matches tools/benchmarks.py's canonical path format.
    string getSuiteName() { return m_input_dir.parent_path().filename().string(); }
    string getBenchmarkPath() { return getSuiteName() + "/" + getBenchmarkName(); }
    float getMaximumUtilization() { return m_maximum_utilization; }
    float getRowHeight() { return m_row_height; } // 0 when the input supplied none
    float getSiteWidth() { return m_site_width; } // 0 when the input supplied none
    Position getDieShift() { return m_die_shift; } // add back to convert internal -> benchmark frame
    const string& getDesignName() const { return m_design_name; }
    int getUnitsPerMicron() const { return m_units_per_micron; }
    int getNumDefRegions() const { return m_num_def_regions; }
    int getNumDefGroups() const { return m_num_def_groups; }

    // Parse functions
    std::vector<fs::path> findExtensions(fs::path, string);
    bool readLEF();
    bool readDEF();
    void readPlacementConstraints();
    // bool readVerilog();
    bool readBookshelf();

    /// @brief Create filler cells for the design's whitespace.
    /// @return the EFFECTIVE target density -- raised when the design is denser than the
    ///         request (see the definition). Callers must adopt the returned value.
    float addFillers(float target_utilization);

    /// @brief Phase 2: freeze every movable macro at its current position, then rebuild the
    ///        area split and the flat node index. @return how many were frozen.
    int freezeMovableMacros();

    /// @brief Phase 2: discard the phase-1 fillers and size a new set in the phase-2 frame
    ///        (macros are fixed area by then). Run AFTER freezeMovableMacros().
    /// @return the effective target density for phase 2 — callers must adopt it.
    float rebuildFillers(float target_utilization);

    /// @brief (Re)build the flat node/net index. Called after parsing and filler creation, and
    ///        again at the phase-2 transition — the ONLY point where a PlacementStatus changes
    ///        or the node set is resized (freezeMovableMacros / rebuildFillers). Outside that
    ///        transition the index is stable for the life of the run.
    void buildNodeIndex();

    void iterationReset();
    void sortPositionsByX();
    void sortPositionsByY();

    float computeTotalWirelength(string, int max_net_degree = INT_MAX, bool at_probe = false);
    float computeTotalComponentArea();
    float getTotalFixedArea() { return m_total_fixed_area; }
    float getTotalMovableArea() { return m_total_movable_area; }
    float getTotalOverflow();


    /// parser callback functions for reading input, fired in file order by DesignReader.cpp
    ///==== LEF Callbacks ====
    void lef_site_cbk(const LefSite& s);
    void lef_macrobegin_cbk(const string& n);
    void lef_macro_cbk(const LefMacro& m);
    void lef_pin_cbk(const LefPin& p);

    ///==== DEF Callbacks ====
    void set_def_unit(int u);
    void set_def_design(const string& d);
    void set_def_diearea(int xl, int yl, int xh, int yh);
    void add_def_components(const std::vector<DefComponent>& components);
    void add_def_pin(const DefPin& p);
    void add_def_nets(const std::vector<DefNet>& nets);
    void resize_def_region(int);
    void resize_def_group(int);

    // BOOKSHELF callbacks
    /// @brief add .nodes entries (cells and terminals)
    void add_bookshelf_nodes(const std::vector<BookshelfNode>&);
    /// @brief add net
    void add_bookshelf_nets(const std::vector<BookshelfNet>&);
    /// @brief add row
    void add_bookshelf_row(const BookshelfRow&);
    /// @brief set node position
    void set_bookshelf_node_positions(const std::vector<BookshelfPlacement>&);
    /// @brief set design name
    void set_bookshelf_design(const string&);
    /// @brief a callback when a bookshelf file reaches to the end
    void bookshelf_end();

    // Print functions
    // const functions guarantee that this object won't be modified by the function
    void printNodes() const;
    void printIOPads() const;
    void printComponents() const;
    void printNets();
    void printNetsByDegree() const;
    void printInfo();
    void printOverlaps();

    // DEF writer functions
    bool writeDEF(const fs::path& output_path) const;
    void writeHeader(std::ofstream& out) const;
    void writeDieArea(std::ofstream& out) const;
    void writeComponents(std::ofstream& out) const;
    void writePins(std::ofstream& out) const;
    void writeNets(std::ofstream& out) const;
    void writeFooter(std::ofstream& out) const;
};

AIEPLACE_NAMESPACE_END

