
#include "database.h"

AIEPLACE_NAMESPACE_BEGIN

DataBase::DataBase()
{
    cout << "database::constructor()" << endl;
}

bool DataBase::readLEF() 
{
    cout << "database::readLEF()" << endl;
    string filename = "/home/msears/AIEplace/benchmarks/ispd2019/ispd19_test1/ispd19_test1.input.lef";
    bool flag = LefParser::read(*this, filename);
    if (flag) {
        cout << "LEF file parsing successful: " << filename.c_str() << endl;
        return true;
    } else {
        cout << "LEF file parsing FAILED: " << filename.c_str() << endl;
        return false;
    }
}

bool DataBase::readDEF() 
{
    cout << "database::readLEF()" << endl;
    string filename = "/home/msears/AIEplace/benchmarks/ispd2019/ispd19_test1/ispd19_test1.input.def";
    bool flag = DefParser::read(*this, filename);
    if (flag) {
        cout << "DEF file parsing successful: " << filename.c_str() << endl;
        return true;
    } else {
        cout << "DEF file parsing FAILED: " << filename.c_str() << endl;
        return false;
    }
}


        /// parser callback functions 
        ///==== LEF Callbacks ====
        void DataBase::lef_version_cbk(std::string const& v) {}
        void DataBase::lef_version_cbk(double v) {}
        void DataBase::lef_casesensitive_cbk(int v) {}
        void DataBase::lef_dividerchar_cbk(std::string const& ) {}
        void DataBase::lef_units_cbk(LefParser::lefiUnits const& v) {}
        void DataBase::lef_manufacturing_cbk(double ) {}
        void DataBase::lef_useminspacing_cbk(LefParser::lefiUseMinSpacing const&) {}
        void DataBase::lef_clearancemeasure_cbk(std::string const&) {}
        void DataBase::lef_busbitchars_cbk(std::string const& ) {}
        void DataBase::lef_layer_cbk(LefParser::lefiLayer const& ) {}
        void DataBase::lef_via_cbk(LefParser::lefiVia const& ) {}
        void DataBase::lef_viarule_cbk(LefParser::lefiViaRule const& ) {}
        void DataBase::lef_spacing_cbk(LefParser::lefiSpacing const& ) {}
        void DataBase::lef_site_cbk(LefParser::lefiSite const& s) {}
        void DataBase::lef_macrobegin_cbk(std::string const& n) {}
        void DataBase::lef_macro_cbk(LefParser::lefiMacro const& m) {}
        void DataBase::lef_pin_cbk(LefParser::lefiPin const& p) {}
        void DataBase::lef_obstruction_cbk(LefParser::lefiObstruction const& o) {}
        void DataBase::lef_prop_cbk(LefParser::lefiProp const&) {}
        void DataBase::lef_maxstackvia_cbk(LefParser::lefiMaxStackVia const&) {}

        ///==== DEF Callbacks === {}
        void DataBase::set_def_busbitchars(std::string const&) {}
        void DataBase::set_def_dividerchar(std::string const&) {}
        void DataBase::set_def_version(std::string const& v) {}
        void DataBase::set_def_unit(int u) {}
        void DataBase::set_def_design(std::string const& d) {}
        void DataBase::set_def_diearea(int xl, int yl, int xh, int yh) {}
        void DataBase::add_def_row(DefParser::Row const& r) {}
        void DataBase::resize_def_component(int s) {}

        void DataBase::add_def_component(DefParser::Component const& c) 
        {
            cout << "add_def_component: " << c.comp_name << " " << c.macro_name<< endl;
            cout << "\tLocated at (" << c.origin[0] << ", " << c.origin[1] << ")" << endl;
        }

        void DataBase::resize_def_pin(int s) {}
        void DataBase::add_def_pin(DefParser::Pin const& p) {}
        void DataBase::resize_def_net(int s) {}

        void DataBase::add_def_net(DefParser::Net const& n) 
        {
            //cout << "add_def_net of size " << n.vNetPin.size() << endl;
        }

        void DataBase::resize_def_blockage(int) {}
        void DataBase::add_def_placement_blockage(std::vector<std::vector<int> > const&) {}
        void DataBase::resize_def_region(int) {}
        void DataBase::add_def_region(DefParser::Region const& r) {}
        void DataBase::resize_def_group(int) {}
        void DataBase::add_def_group(DefParser::Group const& g) {}
        void DataBase::end_def_design() {}
        


AIEPLACE_NAMESPACE_END