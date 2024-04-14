
#include "DataBase.h"

AIEPLACE_NAMESPACE_BEGIN

/**
 * Search the specified directory path for files with the specified extension.
 * 
 * @param dir_path: Path to the directory containing all design files
 * @param extension_match: extension which is being searched for e.g. ".lef" or ".def"
 * 
 * @return: vector of paths to files in the directory with matching extension
 */
std::vector<fs::path> DataBase::findExtensions(fs::path dir_path, string extension_match)
{
    std::vector<fs::path> matches;
    for (const auto& entry : fs::directory_iterator(dir_path)) {
        const auto file_extension = entry.path().extension().string();
        if (file_extension == extension_match)
        {
            matches.push_back(entry.path());
            cout << "### " << extension_match << " file found: \"" << entry.path().string() << "\"" <<endl;
        }
    }

    return matches;
}

bool DataBase::readLEF() 
{
    std::vector<fs::path> lef_files = findExtensions(m_input_dir, ".lef");
    if (lef_files.size() == 0) 
    {
        cout << "No LEF files found." << endl;
        return false;
    }

    bool success = true;
    for(fs::path file : lef_files)
    {
        bool flag = LefParser::read(*this, file.string());
        if (flag) {
            cout << "### LEF file parsing successful: " << file.c_str() << endl;
        } else {
            cout << "### LEF file parsing FAILED: " << file.c_str() << endl;
            success = false;
        }
    }
    return success;
}

bool DataBase::readDEF() 
{
    std::vector<fs::path> def_files = findExtensions(m_input_dir, ".def");
    if (def_files.size() == 0) 
    {
        cout << "No DEF files found." << endl;
        return false;
    }

    //if (def_files.size() > 1)
    //{
    //    cout << "ERROR: Multiple .def files found, but only one is expected:" << endl;
    //    for (fs::path file : def_files)
    //        cout << file.string() << endl;
    //    return false;
    //}
    
    fs::path def_file;
    for(int i = 0; i < def_files.size(); i++)
    {
        if(def_files[i].filename() == "floorplan.def")
            def_file = def_files[i];
    }
    bool flag = DefParser::read(*this, def_file);
    if (flag) {
        cout << "### DEF file parsing successful: " << def_file.string() << endl;
        return true;
    } else {
        cout << "### DEF file parsing FAILED: " << def_file.string() << endl;
        return false;
    }
}


/* @brief: Reset all nodes and nets in preparation for the next iteration.
*/
void DataBase::iterationReset()
{
    for (auto item : mm_components)
        item.second->iterationReset();
    for (auto item : mm_pins)
        item.second->iterationReset();

    for (auto item : mm_nets)
        item.second->iterationReset();

    // Reset mm_data_index to all zeroes
    for(auto item : mm_data_index)
        item.second = 0;
}


// For all Nets in the database, sort Positions (X descending)
void DataBase::sortPositionsByX()
{
    for (auto item : mm_nets)
        item.second->sortPositionsByX();
}

// For all Nets in the database, sort Positions (Y descending)
void DataBase::sortPositionsByY()
{
    for (auto item : mm_nets)
        item.second->sortPositionsByY();
}

// Places Nodes in order for AIE kernel execution:
// max_x, min_x, x, x, x...
void DataBase::sortPositionsMaxMinX()
{
    for (auto item : mm_nets)
        item.second->sortPositionsMaxMinX();
}

// Places Nodes in order for AIE kernel execution:
// max_y, min_y, y, y, y...
void DataBase::sortPositionsMaxMinY()
{
    for (auto item : mm_nets)
        item.second->sortPositionsMaxMinY();
}

double DataBase::computeTotalWirelength()
{
    double total = 0;
    for (auto item : mm_nets)
        total += item.second->computeWirelength();
    return total;
}

double DataBase::computeTotalComponentArea()
{
    double total_area = 0;
    for(auto item : mm_components)
    {
        Component* comp_p = item.second;
        total_area += comp_p->getArea();
    }
    cout << "Total component area: " << total_area << endl;
    return total_area;
}

/*
 * @brief Prepare data in correct format to be sent to PL and then AIE input streams
 *
**/
void DataBase::prepareXdata(float * input_data, int net_size)
{
    input_data[0] = net_size;
    input_data[1] = 8;
    input_data[2] = 0;
    input_data[3] = 0;

    int offset = mm_data_index[net_size]; // offset tracks where data should be taken from

    for(int i = 0; i < 8; i++) { // prepare data for 8 nets at a time
        auto net = mmv_nets_by_degree[net_size][i+offset];
        net->sortPositionsMaxMinX(); // X or Y
        auto nodes = net->getNodes();
        cout << "\nNode order" << endl;
        for(int j = 0; j < net_size; j++) {
            input_data[i+4+j*8] = nodes[j]->getX(); // X or Y
            cout << nodes[j]->getName() << "\t";
        }
        cout << endl;
    }

    // Move mm_data_index to the next unsent data
    mm_data_index[net_size] += 8 * net_size;
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
        void DataBase::lef_macro_cbk(LefParser::lefiMacro const& m) {
            MacroClass* new_macro = new MacroClass(m.name(), m.sizeX(), m.sizeY());
            mm_macros.emplace(std::make_pair(m.name(), new_macro));
        }
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

        void DataBase::set_def_diearea(int xl, int yl, int xh, int yh)
        {
            m_die_area = Box<position_type>(Position((position_type)xl, (position_type)yl), 
                                  Position((position_type)xh, (position_type)yh));
        }

        void DataBase::add_def_row(DefParser::Row const& r) {}
        void DataBase::resize_def_component(int s) {}

        void DataBase::add_def_component(DefParser::Component const& c) 
        // Create a new component (Node) and add it to the database
        {
            Component* new_comp = new Component(c.comp_name);
            new_comp->setMacroClass(mm_macros[c.macro_name]);
            new_comp->setPlacementStatus(c.status);
            new_comp->setPosition(Position((position_type)c.origin[0], (position_type)c.origin[1]));
            // TODO: assert component is created correctly
            mm_components.emplace(std::make_pair(new_comp->getName(), new_comp));
        }

        void DataBase::resize_def_pin(int s) {}

        void DataBase::add_def_pin(DefParser::Pin const& p) {
            Pin* new_pin = new Pin(p.pin_name);
            std::vector<int> bb = p.vBbox.front();
            new_pin->setBoundingBox(bb[0], bb[1], bb[2], bb[3]);
            new_pin->setPlacementStatus(p.status);
            new_pin->setPosition(Position((position_type)p.origin[0], (position_type)p.origin[1]));

            // TODO: assert pin is created correctly

            mm_pins.emplace(std::make_pair(new_pin->getName(), new_pin));
        }

        void DataBase::resize_def_net(int s) {}

        void DataBase::add_def_net(DefParser::Net const& def_net) 
        // Create a new net, add it to the database
        {
            Net* new_net = new Net(def_net.net_name);
            for (auto net_pin : def_net.vNetPin)
            {
                if (net_pin.first == "PIN")
                {
                    Pin* pin_p = mm_pins[net_pin.second];
                    assert(pin_p != nullptr && "PIN name points to nullptr while reading .DEF\n");
                    new_net->addNode(pin_p);
                    pin_p->addNet(new_net);
                }
                else // it is a component
                {
                    Component* comp_p = mm_components[net_pin.first];
                    assert(comp_p != nullptr && "COMPONENT name points to nullptr while reading .DEF\n");
                    new_net->addNode(comp_p);
                    comp_p->addNet(new_net);
                }
            }
            mm_nets.emplace(std::make_pair(new_net->getName(), new_net));

            int degree = new_net->getDegree();
            if (mmv_nets_by_degree.count(degree) == 0) {
                mmv_nets_by_degree.emplace(std::make_pair(degree, std::vector<Net*>()));
                mm_data_index.emplace(std::make_pair(degree, 0));
            }
            mmv_nets_by_degree[degree].push_back(new_net);
        }

        void DataBase::resize_def_blockage(int) {}
        void DataBase::add_def_placement_blockage(std::vector<std::vector<int> > const&) {}
        void DataBase::resize_def_region(int) {}
        void DataBase::add_def_region(DefParser::Region const& r) {}
        void DataBase::resize_def_group(int) {}
        void DataBase::add_def_group(DefParser::Group const& g) {}
        void DataBase::end_def_design() {}
        
// Print info functions
void DataBase::printNodes()
{
    printComponents();
    printPins();
}

void DataBase::printPins()
{
    for(auto item : mm_pins)
    {
        Pin* pin_p = item.second;
        cout << pin_p->getName() << pin_p->getPosition().to_string() << endl;
        cout << "\tArea: " << pin_p->getArea() << "\tStatus: " << pin_p->getStatus() << endl;
    }
}

void DataBase::printComponents()
{
    for(auto item : mm_components)
    {
        Component* comp_p = item.second;
        cout << comp_p->getName() << comp_p->getPosition().to_string() << endl;
        cout << "\t" << comp_p->getMacro()->getName() << "\tArea: " << comp_p->getArea() << "\tStatus: " << comp_p->getStatus() << endl;
    }
}

void DataBase::printNets()
{
    int count = 0;
    for(auto item : mm_nets)
    {
        Net* net_p = item.second;
        cout << endl << "NET: " << net_p->to_string() << endl;

        sortPositionsByX();
        cout << "X descending: ";
        for(auto node : net_p->getNodes())
            cout << node->getPosition().getX() << '\t';
        cout << endl;

        sortPositionsByY();
        cout << "Y descending: ";
        for(auto node : net_p->getNodes())
            cout << node->getPosition().getY() << '\t';
        cout << endl;
        cout << endl;

        if (++count > 100) return;
    }
}

void DataBase::printNetsByDegree()
{
    cout << "&&& Nets by degree:" << endl;

    for (auto item : mmv_nets_by_degree)
    {
        cout << item.second.size() << " nets of degree " << item.first << ".\n";
    }
}

void DataBase::printInfo()
{
    cout << "---###--- DataBase info ---###---" << endl;
    cout << std::scientific;
    cout << "Die Area: " << m_die_area.getArea() << "\t" << m_die_area.to_string() <<  endl;
    cout << std::fixed;
    cout << mm_macros.size() << " macro classes" << endl;
    cout << mm_pins.size() << " pins" << endl;
    cout << mm_components.size() << " components" << endl;
    cout << mm_nets.size() << " nets" << endl;
}


void DataBase::printOverlaps()
{
    // for each node in db
    for (auto item : getComponents())
    {
        // print overlaps
        string name = item.first;
        Node* node_p = item.second;
        if (node_p->getBinOverlaps().size() < 2) continue;
        cout << "Bin Overlaps for " << name << node_p->getPosition().to_string() << "\tarea = " << node_p->getArea() << endl;
        for (BinOverlap b : node_p->getBinOverlaps())
        {
            cout << "\t" << b.overlap << " in bin " << b.bin->bb.getPos().to_string() << endl;
        }

    }

}


AIEPLACE_NAMESPACE_END