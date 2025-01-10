
#ifndef AIEPLACE_DATABASE_H
#define AIEPLACE_DATABASE_H
#include "Common.h"
#include "Position.h"
#include "MacroClass.h"
#include "Component.h"
#include "Pin.h"
#include "Node.h"
#include "Net.h"
#include "Bin.h"
#include "Logger.h"

#include <limbo/parsers/lef/adapt/LefDriver.h>             // LEF parser
#include <limbo/parsers/def/adapt/DefDriver.h>             // DEF parser
#include <limbo/parsers/verilog/bison/VerilogDriver.h>     // verilog parser
#include <limbo/parsers/bookshelf/bison/BookshelfDriver.h> // bookshelf parser
#include <limbo/parsers/gdsii/stream/GdsWriter.h>          // GDSII writer

AIEPLACE_NAMESPACE_BEGIN
// On AIEs, we process only nets of size 2 thru 8. This covers the great majority of all nets
// other nets above size 8 will be processed on the host.
#define MIN_AIE_NET_SIZE 2 // should be 2 by default
#define MAX_AIE_NET_SIZE 8 // should be 8 by default

struct PacketIndex
{
    int net_size;
    int group_start; // node index to begin this packet
    int group_count; // how many nodes of this net_size are to be in this packet

    PacketIndex(int size, int gs, int gc) { set(size, gs, gc); } // default constructor

    void set(int size, int gs, int gc)
    {
        net_size = size;
        group_start = gs;
        group_count = gc;
    }

    string to_string() 
    {
        string str = "PacketIndex: ";
        str += "\tnetsize = "       + stringify(net_size);
        str += "\tgroup_start = "   + stringify(group_start);
        str += "\tgroup_count = "   + stringify(group_count);
        str += "\n";
        return str;
    }
};

struct Packet
{
    int graph_index;   // The AIE partials graph which this packet should be sent to.
    int id;
    
    vector<PacketIndex> contents;

    int ctrl_data[8]; //  first 8 floats of the packet are control data which dictate
                     //   the netsize and quantity of coordinate data to expect.

    // ALTERNATE APPROACH
    float ** content = new float* [LCM_BUFFSIZE*VEC_SIZE]; // array of float pointers in the order expected by AIE kernels

    // Default constructor
    Packet() {}
};


class DataBase : public DefParser::DefDataBase, public LefParser::LefDataBase
{
private:
    // Member Data, prefixed with "m_"
    fs::path m_input_dir; // Path to find directory containing design data.
                          // Expects to find a .lef and .def file

    map<string, MacroClass *> mm_macros;
    map<string, Component *> mm_components;
    map<string, Pin *> mm_pins;
    map<string, Net *> mm_nets;
    map<int, std::vector<Net *>> mmv_nets_by_degree;
    //map<int, int> mm_input_index; // Used to track what data has been sent as input to AIEs
    //map<int, int> mm_output_index; // Used to track what data has been received as output from AIEs

    Box<position_type> m_die_area;
    int m_packet_count;

public:
    // each compute graph has a vector of PacketIndex which records what data has been sent
    vector<Packet*> mv_packet[PARTIALS_GRAPH_COUNT];


    /// Default Constructor
    DataBase() {}
    DataBase(fs::path input_dir);

    /// Destructor
    virtual ~DataBase() {}

    // Getter functions
    // return const references to avoid copying large objects
    const map<string, MacroClass *> &getMacros() { return mm_macros; }
    const map<string, Component *> &getComponents() { return mm_components; }
    const map<string, Pin *> &getPins() { return mm_pins; }
    const map<string, Net *> &getNets() { return mm_nets; }
    const map<int, std::vector<Net *>> &getNetsByDegree() { return mmv_nets_by_degree; }
    int getNetCountOfDegree(int degree) { return mmv_nets_by_degree[degree].size(); }
    Box<position_type> &getDieArea() { return m_die_area; }
    string getBenchmarkName() { return m_input_dir.filename().string(); }

    // Parse functions
    std::vector<fs::path> findExtensions(fs::path, string);
    bool readLEF();
    bool readDEF();
    // void prereadDEF(string const& filename);
    // bool readVerilog();
    // bool readBookshelf();


    void iterationReset();
    void sortPositionsByX();
    void sortPositionsByY();
    void sortPositionsMaxMinX();
    void sortPositionsMaxMinY();

    double computeTotalWirelength(string);
    double computeTotalComponentArea();
    double getTotalOverflow();

    // Packet loading/unloading
    void initializePacketContents();
    void prepareNetGroup(float * input_data, int net_size, int offset);
    void storeNetGroup(float * output_data, int net_size, int offset);
    int  getPacketCount() { return m_packet_count; }


    /// parser callback functions
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

    // Print functions
    void printNodes();
    void printPins();
    void printComponents();
    void printNets();
    void printNetsByDegree();
    void printInfo();
    void printOverlaps();
};

AIEPLACE_NAMESPACE_END

#endif