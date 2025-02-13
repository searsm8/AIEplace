
#include "DataBase.h"
#include "Logger.h"

AIEPLACE_NAMESPACE_BEGIN

DataBase::DataBase(fs::path input_dir) : m_input_dir(input_dir) {
    cout << "INPUT DIR: " << m_input_dir << endl;
    bool LEF_success = readLEF();
    bool DEF_success = readDEF();
    if (!LEF_success || !DEF_success)
    {
        std::cerr << "Design could not be read. Exiting..." << endl;
        exit(1);
    }
    initializePacketContents();
}

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
            log_info(extension_match + " file found: \"" + entry.path().string() + "\"");
        }
    }

    return matches;
}

bool DataBase::readLEF() 
{
    std::vector<fs::path> lef_files = findExtensions(m_input_dir, ".lef");
    if (lef_files.size() == 0) 
    {
        log_error("No .lef files found.");
        return false;
    }

    bool success = true;
    for(fs::path file : lef_files)
    {
        bool flag = LefParser::read(*this, file.string());
        if (flag) {
            log_info(".lef file parsing successful: " + file.string());
        } else {
            log_error(".lef file parsing FAILED: " + file.string());
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
        log_error("No .def files found.");
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
        log_info(".def file parsing successful: " + def_file.string());
        return true;
    } else {
        log_error(".def file parsing FAILED: " + def_file.string());
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

double DataBase::computeTotalWirelength(string method)
{
    double total = 0;
    for (auto item : mm_nets)
        total += item.second->computeWirelength(method);
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
    return total_area;
}

/* @brief: Set up the Packet Contents structure
*/
void DataBase::initializePacketContents()
{
    log("packets", "BEGIN initializePacketContents()");
    int graph_index = 0;
    m_packet_count = 0;
    for(int net_size = MIN_AIE_NET_SIZE; net_size <= MAX_AIE_NET_SIZE; net_size++) {
    //for(int net_size = TEST_NET_SIZE; net_size <= TEST_NET_SIZE; net_size++) {
        log("packets", "net_size = " + stringify(net_size));
        int groups_per_packet = LCM_BUFFSIZE/net_size; // for netsize 2 thru 8, this will be an exact integer
        log("packets", "groups_per_packet = " + stringify(groups_per_packet));
        log("packets", "nets_per_packet = " + stringify(groups_per_packet*NETS_PER_GROUP));
        int num_nets = mmv_nets_by_degree[net_size].size();
        log("packets", "num_nets = " + stringify(num_nets));

        int packet_start = 0;
        //int nets_per_graph = num_nets / PARTIALS_GRAPH_COUNT;
        //int net_groups_per_graph = nets_per_graph / NETS_PER_GROUP; // We send 4 nets of data in each net group 

        // Fill packets with data ranges
        // TODO: This is oversimplified and leads to a lot of zeroes being sent!
        Table table;
        table.add_row({ "graph_index", "net_size", "group_start", "group_count" });
        while(packet_start*NETS_PER_GROUP < num_nets) {
            Packet* p = new Packet();
            p->graph_index = graph_index;
            // For now only a single PacketIndex in each packet for simplicity
            // This could lead to many zeroes (empty packets) being sent and could be improved.
            p->contents.push_back(PacketIndex(net_size, packet_start, groups_per_packet));
            mv_packet[graph_index].push_back(p);

            PacketIndex* pi = &p->contents[0];
            //cout << "graph_index: " << graph_index  << "\t" << p->contents[0].to_string();
            table.add_row({ stringify(graph_index), stringify(pi->net_size), 
                            stringify(pi->group_start), stringify(pi->group_count) });

            //move start/stop for next packet
            packet_start += groups_per_packet;

            if(graph_index == 0) m_packet_count++;
            graph_index = (graph_index+1) % PARTIALS_GRAPH_COUNT;
        }
        table.format().font_align(FontAlign::center);
        log("packets", table);
    }
    log("packets", "END initializePacketContents()");
}

/*
 * @brief: Prepare data in correct format to be sent to PL and then AIE input streams
 * A net group is four (NETS_PER_GROUP) of size (net_size).
 * Group will have a size of (2 x NETS_PER_GROUP x net_size)
 * The 2x is because X and Y coord are grouped together
**/
void DataBase::prepareNetGroup(float * input_data, int net_size, int offset)
{
    // Base address starts at VEC_SIZE to leave room for control data
    int base_addr = VEC_SIZE + (2*offset*net_size)%(LCM_BUFFSIZE*VEC_SIZE); // TODO: This needs an explanation
    bool nan = false;

    for(int net_idx = 0; net_idx < NETS_PER_GROUP; net_idx++) { // prepare XY data for 4 nets at a time
        // check if we have reached the end of nets with this degree
        if(net_idx + offset >= mmv_nets_by_degree[net_size].size()) {
            // prep data with trailing zeroes
            //cout << "Zeroes at index: " << net_idx + offset << endl; 
            for(int j = 0; j < net_size; j++) {
                input_data[base_addr + 2*net_idx + j*8] = 0;
                input_data[base_addr + 2*net_idx + j*8 + 1] = 0;
            }
            continue;
        }

        // otherwise, prep X coordinate data
        auto net = mmv_nets_by_degree[net_size][offset + net_idx ];

        auto &nodes = net->getNodes(); // reference to avoid copy
        net->sortPositionsMaxMinX(); // This sort might be redundant? 
        for(int j = 0; j < net_size; j++) {
            input_data[base_addr + 2*net_idx + j*8] = nodes[j]->getX();
            if(nodes[j]->getX() != nodes[j]->getX()) // check for nan
            {
                cout << "nan detected:\n" << net->to_string() << endl;
                nan = true;
            }
        }

        // prep y data
        net->sortPositionsMaxMinY();
        for(int j = 0; j < net_size; j++) {
            input_data[base_addr + 2*net_idx + j*8 + 1] = nodes[j]->getY();
            if(nodes[j]->getY() != nodes[j]->getY()) // check for nan
            {
                cout << "nan detected:\n" << net->to_string() << endl;
                nan = true;
            }
        }
    }

    // DEBUG: print the prepared data!
    //if(nan)
    //if(net_size == 5)
    //{
    //    cout << endl << "Prepared XY data @offset = " << offset << std::setprecision(3);
    //    for(int i = base_addr; i < base_addr + VEC_SIZE*net_size; i++) {
    //        if((i-base_addr)%8 == 0) cout << endl;
    //        cout << input_data[i] << " ";
    //    }
    //    cout << endl;
    //}

}

void DataBase::storeNetGroup(float * output_data, int net_size, int offset)
{
    //int offset = mm_output_index[net_size]; // offset tracks which nets partials should be added to
    //int offset = 0;
    int base_addr = (2*offset*net_size)%(LCM_BUFFSIZE*VEC_SIZE); // TODO: This needs an explanation

    for(int net_idx = 0; net_idx < NETS_PER_GROUP; net_idx ++) { // prepare XY data for 4 nets at a time
        // check if we have reached the end of nets with this degree
        if(net_idx + offset >= mmv_nets_by_degree[net_size].size()) {
            // No store needed
            //cout << "No data stored @offset=" << net_idx + offset << endl; 
            continue;
        }

        // otherwise, store partials results in node.terms.partials
        auto net = mmv_nets_by_degree[net_size][offset + net_idx ];
        auto &nodes = net->getNodes(); // needs to be a reference, to avoid being a copy!

        // nodes are already sorted by Y coords, so store them first
        for(int n = 0; n < net_size; n++) {
            nodes[n]->partials_aie.y += output_data[base_addr + 2*net_idx + n*8 + 1];
        }

        net->sortPositionsMaxMinX(); // X or Y
        //nodes = net->getNodes();
        for(int n = 0; n < net_size; n++) {
            nodes[n]->partials_aie.x += output_data[base_addr + 2*net_idx + n*8];
        }
    }

    // DEBUG: print the stored data!
    //if(net_size == 5)
    //{
    //    cout << "\nStored XY data @offset = " << offset;
    //    for(int i = 0; i < 2*NETS_PER_GROUP*net_size; i++) {
    //        if(i%8 == 0) cout << endl;
    //        cout << output_data[base_addr + i] << " ";
    //    }
    //    cout << endl;
    //}

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
        void DataBase::set_def_unit(int u) { m_units_per_micron = u; }
        void DataBase::set_def_design(std::string const& d) { m_design_name = d; }

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
            new_pin->setDirection(p.direct); // primary input or output
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
                    new_net->addNetPin(comp_p, net_pin.second);
                    comp_p->addNet(new_net);
                }
            }
            mm_nets.emplace(std::make_pair(new_net->getName(), new_net));

            int degree = new_net->getDegree();
            if (mmv_nets_by_degree.count(degree) == 0) {
                mmv_nets_by_degree.emplace(std::make_pair(degree, std::vector<Net*>()));
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
void DataBase::printNodes() const
{
    printComponents();
    printPins();
}

void DataBase::printPins() const
{
    for(auto item : mm_pins)
    {
        Pin* pin_p = item.second;
        cout << pin_p->getName() << pin_p->getPosition().to_string() << endl;
        cout << "\tArea: " << pin_p->getArea() << "\tStatus: " << pin_p->getStatus() << endl;
    }
}

void DataBase::printComponents() const
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

void DataBase::printNetsByDegree() const
{
    cout << "&&& Nets by degree:" << endl;

    for (auto item : mmv_nets_by_degree)
    {
        cout << item.second.size() << " nets of degree " << item.first << ".\n";
    }
}

void DataBase::printInfo()
{
    Table top;
    top.add_row({"DataBase info"});

    Table data;
    data.add_row(RowStream{} << "Macros" << mm_macros.size());
    data.add_row(RowStream{} << "Pins" << mm_pins.size());
    data.add_row(RowStream{} << "Components" << mm_components.size());
    data.add_row(RowStream{} << "Nets" << mm_nets.size());
    data.add_row(RowStream{} << "Die Area" << m_die_area.getArea());
    data.add_row(RowStream{} << "Component area: " << computeTotalComponentArea());

    top.add_row({data});
    top.format().font_align(FontAlign::center);
    log("DATA", top);
}


void DataBase::printOverlaps()
{
    // for each node in db
    int count = 0;
    for (auto item : getComponents())
    {
        if(count++ > 100) return;
        // print overlaps
        string name = item.first;
        Node* node_p = item.second;
        Table header;
        header.add_row(RowStream{} << std::setprecision(2) << "Bin Overlaps for " << name);
        header.add_row(RowStream{} << "Position" << node_p->getPosition().to_string());
        header.add_row(RowStream{} << "Area" << node_p->getArea());
        header.column(0).format().font_align(FontAlign::right);

        Table overlaps;
        overlaps.add_row(RowStream{} << "bin" << "overlap");
        for (BinOverlap b : node_p->getBinOverlaps())
            overlaps.add_row(RowStream{} << b.bin->bb.getPos().to_string() << b.overlap);
        overlaps.format().font_align(FontAlign::center);

        Table top;
        top.add_row({header});
        top.add_row({overlaps});
        top.format().font_align(FontAlign::center);
        
        log_info(top);
    }
}

bool DataBase::writeDEF(const std::string& output_path) const
{
    string output_filename = output_path + "/" + m_design_name + ".def";
    std::ofstream out(output_filename);
    if (!out.is_open()) {
        log_error("DEF write: invalid output filename.");
        return false;
    }
    out.imbue(std::locale::classic()); // set to standard output
    writeHeader(out);
    writeDieArea(out);
    writeComponents(out);
    writePins(out);
    writeNets(out);
    writeFooter(out);

    return true;
}


void DataBase::writeHeader(std::ofstream& out) const {
    // Metadata
    out << "# Design after global placement\n"
        << "# Produced by AIEplace " << AIEPLACE_VERSION << endl;
    // Begin DEF format
    out << "VERSION 5.8 ;\n"
        << "DIVIDERCHAR \"/\" ;\n"
        << "BUSBITCHARS \"[]\" ;\n"
        << "DESIGN " << m_design_name << " ;\n"
        << "UNITS DISTANCE MICRONS " << m_units_per_micron << " ;\n\n";
}

void DataBase::writeDieArea(std::ofstream& out) const {
    out << "DIEAREA ( "
        << m_die_area.getPosBottomLeft().getX() << " " << m_die_area.getPosBottomLeft().getY() << " ) ( "
        << m_die_area.getPosTopRight().getX() << " " << m_die_area.getPosTopRight().getY() << " ) ;\n\n";
}

void DataBase::writeComponents(std::ofstream& out) const {
    out << "COMPONENTS " << mm_components.size() << " ;\n";
    for (const auto& item : mm_components) {
        auto comp = item.second;
        out << "    - " << comp->getName() << " " << comp->getMacro()->getName() << "\n"
            << "      + " << "PLACED"/*comp->getStatus()*/ << " ( " << comp->getX() << " " << comp->getY() << " ) "
            << comp->getOrientation() << " ;\n";
    }
    out << "END COMPONENTS\n\n";
}

void DataBase::writePins(std::ofstream& out) const {
    out << "PINS " << mm_pins.size() << " ;\n";
    for (const auto& item : mm_pins) {
        Pin* pin = item.second;
        out << "    - " << pin->getName() << " + NET " << pin->getName()
            << "\n      + DIRECTION " << pin->getDirection()
            //<< " + USE " << pin.use 
            << "\n";
        if (pin->isPlaced()) {
            out << "      + PLACED "
                << " ( " << pin->getX() << " " << pin->getY() << " ) "
                << pin->getOrientation() << "\n";
        }
        out << "      + LAYER " << pin->getLayer()
            << pin->getBoundingBox().getDEFstring()
            << " ;\n";
    }
    out << "END PINS\n\n";
}

void DataBase::writeNets(std::ofstream& out) const {
    // NOT HANDLING SPECIAL NETS
    // Write special nets if any exist
    //auto special_nets = std::count_if(nets.begin(), nets.end(),
    //                                [](const Net& net) { return net.isSpecial; });
    //if (special_nets > 0) {
    //    out << "SPECIALNETS " << special_nets << " ;\n";
    //    for (const auto& net : nets) {
    //        if (net.isSpecial) {
    //            out << "    - " << net.name << "\n";
    //            for (const auto& conn : net.connections) {
    //                out << "      ( " << conn.first << " " << conn.second << " )\n";
    //            }
    //            out << "      ;\n";
    //        }
    //    }
    //    out << "END SPECIALNETS\n\n";
    //}
    
    // Write regular nets
    out << "NETS " << mm_nets.size() << " ;\n";
    for (const auto& item : mm_nets) {
        Net* net = item.second;
        out << "    - " << net->getName();
        int count = 0;
        for (const auto& node : net->mv_nodes) {
            try {
                Pin& pin = dynamic_cast<Pin&>(*node);
                // node is a primary IO pin
                out << " ( PIN " << pin.getName() << " )";

            } catch(const std::bad_cast&) {
                // else node is a component
                out << " ( " << node->getName() << " " << net->mm_net_pins[node] << " )";
            }
            if(++count == 4) { // print 4 nodes, then newline
                out << endl;
                count = 0;
            }
        }
        out << " ;\n";
    }
    out << "END NETS\n\n";
}

void DataBase::writeFooter(std::ofstream& out) const {
    out << "END DESIGN\n";
}



AIEPLACE_NAMESPACE_END