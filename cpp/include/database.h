
#ifndef AIEPLACE_DATABASE_H
#define AIEPLACE_DATABASE_H
#include "aieplace.h"

#include <limbo/parsers/lef/adapt/LefDriver.h> // LEF parser 
#include <limbo/parsers/def/adapt/DefDriver.h> // DEF parser 
#include <limbo/parsers/verilog/bison/VerilogDriver.h> // verilog parser
#include <limbo/parsers/bookshelf/bison/BookshelfDriver.h> // bookshelf parser 
#include <limbo/parsers/gdsii/stream/GdsWriter.h> // GDSII writer 
#include <limbo/string/String.h>

#endif

AIEPLACE_NAMESPACE_BEGIN 

class DataBase: public DefParser::DefDataBase, public LefParser::LefDataBase
{
    public:

        /// default constructor
        DataBase(); 

        /// destructor
        virtual ~DataBase() {}

        // parse functions
        bool readLEF();
        bool readDEF();
        //void prereadDEF(std::string const& filename);
        //bool readVerilog();
        //bool readBookshelf();

        /// parser callback functions 
        ///==== LEF Callbacks ====
        virtual void lef_version_cbk(std::string const& v);
        virtual void lef_version_cbk(double v); 
        virtual void lef_casesensitive_cbk(int v); 
        virtual void lef_dividerchar_cbk(std::string const& ); 
        virtual void lef_units_cbk(LefParser::lefiUnits const& v);
        virtual void lef_manufacturing_cbk(double );
        virtual void lef_useminspacing_cbk(LefParser::lefiUseMinSpacing const&);
        virtual void lef_clearancemeasure_cbk(std::string const&);
        virtual void lef_busbitchars_cbk(std::string const& );
        virtual void lef_layer_cbk(LefParser::lefiLayer const& );
        virtual void lef_via_cbk(LefParser::lefiVia const& );
        virtual void lef_viarule_cbk(LefParser::lefiViaRule const& );
        virtual void lef_spacing_cbk(LefParser::lefiSpacing const& );
        virtual void lef_site_cbk(LefParser::lefiSite const& s);
        virtual void lef_macrobegin_cbk(std::string const& n); 
        virtual void lef_macro_cbk(LefParser::lefiMacro const& m);
        virtual void lef_pin_cbk(LefParser::lefiPin const& p);
        virtual void lef_obstruction_cbk(LefParser::lefiObstruction const& o);
        virtual void lef_prop_cbk(LefParser::lefiProp const&);
        virtual void lef_maxstackvia_cbk(LefParser::lefiMaxStackVia const&);

        ///==== DEF Callbacks ====
        virtual void set_def_busbitchars(std::string const&);
        virtual void set_def_dividerchar(std::string const&);
        virtual void set_def_version(std::string const& v);
        virtual void set_def_unit(int u);
        virtual void set_def_design(std::string const& d);
        virtual void set_def_diearea(int xl, int yl, int xh, int yh);
        virtual void add_def_row(DefParser::Row const& r);
        virtual void resize_def_component(int s);
        virtual void add_def_component(DefParser::Component const& c);
        virtual void resize_def_pin(int s);
        virtual void add_def_pin(DefParser::Pin const& p);
        virtual void resize_def_net(int s);
        virtual void add_def_net(DefParser::Net const& n);
        virtual void resize_def_blockage(int);
        virtual void add_def_placement_blockage(std::vector<std::vector<int> > const&);
        virtual void resize_def_region(int);
        virtual void add_def_region(DefParser::Region const& r);
        virtual void resize_def_group(int);
        virtual void add_def_group(DefParser::Group const& g);
        virtual void end_def_design(); 

};


AIEPLACE_NAMESPACE_END