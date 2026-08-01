
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
#include <functional>
#include <sstream>

#include <limbo/parsers/lef/adapt/LefDriver.h>             // LEF parser
#include <limbo/parsers/def/adapt/DefDriver.h>             // DEF parser
#include <limbo/parsers/verilog/bison/VerilogDriver.h>     // verilog parser
#include <limbo/parsers/bookshelf/bison/BookshelfDriver.h> // bookshelf parser
#include <limbo/parsers/gdsii/stream/GdsWriter.h>          // GDSII writer

AIEPLACE_NAMESPACE_BEGIN

class DataBase :
    public DefParser::DefDataBase,
    public LefParser::LefDataBase,
    public BookshelfParser::BookshelfDataBase
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
    vector<Net *> mv_focus_nets; // list of nets to be highlighted with visualizer
    vector<Node *> mv_focus_nodes; // list of nodes to be highlighted with visualizer

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
    Position m_die_shift; // (0,0) unless a bookshelf die_shift was applied
    string m_design_name;
    // DEF convention default (matches this repo's ispd2015 .def samples); set_def_unit()
    // overrides it for real DEF input. Bookshelf input (ISPD2005/MMS) never calls
    // set_def_unit, so without this default the DEF written on output read garbage.
    int m_units_per_micron = 1000;
    int m_total_net_degree;
    float m_maximum_utilization = 0.0f; // 0 = not specified by benchmark
    MacroClass* m_current_lef_macro = nullptr; // tracks current macro during LEF parsing for pin callbacks
    float m_total_component_area = 0.0f; // all components (movable + fixed), cached after construction
    float m_total_fixed_area = 0.0f;     // fixed components only
    float m_total_movable_area = 0.0f;   // movable components only (= total - fixed)

    // DataBase()'s steps, broken out for readability
    void readDesignFiles();
    void computeNetDegreeTotal();
    void computeAreaBreakdown();

    // Runs parse_fn with stdout redirected to /dev/null, then restores the original stdout fd.
    static bool runParserSilenced(std::function<bool()> parse_fn);

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
    float getMaximumUtilization() { return m_maximum_utilization; }

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

    void addFocusNet(Net* net_p) { mv_focus_nets.push_back(net_p); }
    void addFocusNode(Node* node_p) { mv_focus_nodes.push_back(node_p); }
    const vector<Net *> &getFocusNets() { return mv_focus_nets; }
    const vector<Node *> &getFocusNodes() { return mv_focus_nodes; }

    float computeTotalWirelength(string, int max_net_degree = INT_MAX);
    float computeTotalComponentArea();
    float getTotalFixedArea() { return m_total_fixed_area; }
    float getTotalMovableArea() { return m_total_movable_area; }
    float getTotalOverflow();


    /// parser callback functions for reading input
    ///==== LEF Callbacks ====
    virtual void lef_version_cbk(std::string const &v);
    virtual void lef_version_cbk(double v);
    virtual void lef_casesensitive_cbk(int v);
    virtual void lef_dividerchar_cbk(std::string const &);
    virtual void lef_units_cbk(LefParser::lefiUnits const &v);
    virtual void lef_manufacturing_cbk(double);
    virtual void lef_useminspacing_cbk(LefParser::lefiUseMinSpacing const &);
    virtual void lef_clearancemeasure_cbk(std::string const &);
    virtual void lef_busbitchars_cbk(std::string const &);
    virtual void lef_layer_cbk(LefParser::lefiLayer const &);
    virtual void lef_via_cbk(LefParser::lefiVia const &);
    virtual void lef_viarule_cbk(LefParser::lefiViaRule const &);
    virtual void lef_spacing_cbk(LefParser::lefiSpacing const &);
    virtual void lef_site_cbk(LefParser::lefiSite const &s);
    virtual void lef_macrobegin_cbk(std::string const &n);
    virtual void lef_macro_cbk(LefParser::lefiMacro const &m);
    virtual void lef_pin_cbk(LefParser::lefiPin const &p);
    virtual void lef_obstruction_cbk(LefParser::lefiObstruction const &o);
    virtual void lef_prop_cbk(LefParser::lefiProp const &);
    virtual void lef_maxstackvia_cbk(LefParser::lefiMaxStackVia const &);

    ///==== DEF Callbacks ====
    virtual void set_def_busbitchars(std::string const &);
    virtual void set_def_dividerchar(std::string const &);
    virtual void set_def_version(std::string const &v);
    virtual void set_def_unit(int u);
    virtual void set_def_design(std::string const &d);
    virtual void set_def_diearea(int xl, int yl, int xh, int yh);
    virtual void add_def_row(DefParser::Row const &r);
    virtual void resize_def_component(int s);
    virtual void add_def_component(DefParser::Component const &c);
    virtual void resize_def_pin(int s);
    virtual void add_def_pin(DefParser::Pin const &p);
    virtual void resize_def_net(int s);
    virtual void add_def_net(DefParser::Net const &n);
    virtual void resize_def_blockage(int);
    virtual void add_def_placement_blockage(std::vector<std::vector<int>> const &);
    virtual void resize_def_region(int);
    virtual void add_def_region(DefParser::Region const &r);
    virtual void resize_def_group(int);
    virtual void add_def_group(DefParser::Group const &g);
    virtual void end_def_design();

    // BOOKSHELF callbacks
    /// @brief set number of terminals 
    virtual void resize_bookshelf_node_terminals(int, int);
    /// @brief set number of nets 
    virtual void resize_bookshelf_net(int);
    /// @brief set number of pins 
    virtual void resize_bookshelf_pin(int);
    /// @brief set number of rows 
    virtual void resize_bookshelf_row(int);
    /// @brief set number of shapes 
    //virtual void resize_bookshelf_shapes(int);
    /// @brief set number of NI terminals with layers 
    //virtual void resize_bookshelf_niterminal_layers(int);
    /// @brief set number of blockage nodes with layers 
    //virtual void resize_bookshelf_blockage_layers(int);
    /// @brief add terminal 
    virtual void add_bookshelf_terminal(string&, int, int);
    /// @brief add terminal_NI
    //virtual void add_bookshelf_terminal_NI(string&, int, int);
    /// @brief add node 
    virtual void add_bookshelf_node(string&, int, int, bool);
    /// @brief add net 
    virtual void add_bookshelf_net(BookshelfParser::Net const&);
    /// @brief add row 
    virtual void add_bookshelf_row(BookshelfParser::Row const&);
    /// @brief set node position 
    virtual void set_bookshelf_node_position(string const&, double, double, string const&, string const&, bool);
    /// @brief set net weight 
    //virtual void set_bookshelf_net_weight(string const& name, double w);
    /// @brief set node shapes 
    //virtual void set_bookshelf_shape(NodeShape const&); 
    /// @brief set routing information 
    //virtual void set_bookshelf_route_info(RouteInfo const&);
    /// @brief set NI terminal with layers 
    //virtual void add_bookshelf_niterminal_layer(string const&, string const&);
    /// @brief set blockages with layers 
    //virtual void add_bookshelf_blockage_layers(string const&, vector<string> const&);
    /// @brief set design name 
    virtual void set_bookshelf_design(string&);
    /// @brief a callback when a bookshelf file reaches to the end 
    virtual void bookshelf_end();

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
    bool writeDEF(const std::string& output_path) const;
    void writeHeader(std::ofstream& out) const;
    void writeDieArea(std::ofstream& out) const;
    void writeComponents(std::ofstream& out) const;
    void writePins(std::ofstream& out) const;
    void writeNets(std::ofstream& out) const;
    void writeFooter(std::ofstream& out) const;
};

AIEPLACE_NAMESPACE_END

