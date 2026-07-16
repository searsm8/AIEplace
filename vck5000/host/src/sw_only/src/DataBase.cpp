
#include "DataBase.h"
#include "Logger.h"
#include <algorithm>

AIEPLACE_NAMESPACE_BEGIN

DataBase::DataBase(fs::path input_dir, bool enable_pin_offsets)
    : m_input_dir(input_dir), m_enable_pin_offsets(enable_pin_offsets) {
    TIME_BLOCK("DataBase read input");
    Logger::log_info("Reading design from directory: " + m_input_dir.string());
    m_max_x = 0;
    m_max_y = 0;

    // try to read LEF/DEF
    bool LEF_success = readLEF();
    bool DEF_success = readDEF();

    // LEF macro sizes are in microns; DEF coordinates are in database units (dbu).
    // Scale macro sizes to match DEF coordinate system.
    if (LEF_success && DEF_success && m_units_per_micron > 0) {
        float scale = (float)m_units_per_micron;
        for (auto& item : mm_macros) {
            MacroClass* macro = item.second;
            macro->setSize(macro->getXsize() * scale, macro->getYsize() * scale);
        }
        Logger::log_info("Scaled " + std::to_string(mm_macros.size()) +
                        " LEF macro sizes by " + std::to_string(m_units_per_micron) +
                        " (microns -> dbu)");
    }

    // else look for bookshelf
    if (!LEF_success || !DEF_success)
    {
        bool bookshelf_success = readBookshelf();
        if(!bookshelf_success ) {
            Logger::log_error("Design could not be read. Exiting...");
            exit(1);
        }
    }

    // Read placement constraints (maximum_utilization) if present
    readPlacementConstraints();

    // Add IO pins as focus nets for visualizer
    //int focus_nets_added = 0;
    //const int FOCUS_IO_LIMIT = 2;
    //for (auto item : mm_pins) {
    //    if(focus_nets_added >= FOCUS_IO_LIMIT)
    //        break;
    //    Pin* pin = item.second;
    //    for(auto net : pin->getNets()) {
    //        addFocusNet(net);
    //        Logger::log_info("Adding IO pin net to visualizer focus nets: " + net->to_string());
    //        if(++focus_nets_added >= FOCUS_IO_LIMIT)
    //            break;
    //    }
    //}

    initializePacketContents();

    m_total_net_degree = 0;
    for (auto* net : mv_nets) {
        m_total_net_degree += net->getDegree();
    }

    // Cache area breakdown (constant for the lifetime of the design). FIXED components are
    // clipped to the die — XPlace counts only the fixed area that lands inside the die
    // (fixed_node_area = init_density_map inside the core), so terminals overhanging the die
    // don't inflate the placeable-area denominator that sets the filler count. Movable area
    // is the raw sum (movable cells sit inside the die).
    float die_xl = m_die_area.getPosBottomLeft().x, die_yl = m_die_area.getPosBottomLeft().y;
    float die_xu = m_die_area.getPosTopRight().x,   die_yu = m_die_area.getPosTopRight().y;
    double movable_sum = 0;
    double fixed_sum = 0;
    int fixed_count = 0;
    for (auto item : mm_components) {
        Component* comp = item.second;
        if (comp->getStatus() == FIXED) {
            float ox = std::max(0.0f, std::min(comp->getX() + comp->getXsize(), die_xu) - std::max(comp->getX(), die_xl));
            float oy = std::max(0.0f, std::min(comp->getY() + comp->getYsize(), die_yu) - std::max(comp->getY(), die_yl));
            fixed_sum += (double)ox * oy;
            fixed_count++;
        } else {
            movable_sum += comp->getArea();
        }
    }
    m_total_fixed_area = (float)fixed_sum;
    m_total_movable_area = (float)movable_sum;
    m_total_component_area = m_total_fixed_area + m_total_movable_area;

    Logger::log_info("Fixed components: " + std::to_string(fixed_count)
        + " (area: " + std::to_string((long long)m_total_fixed_area)
        + ", " + std::to_string((int)(100.0f * m_total_fixed_area / m_die_area.getArea())) + "% of die)");
    Logger::log_info("Movable components: " + std::to_string((int)mm_components.size() - fixed_count)
        + " (area: " + std::to_string((long long)m_total_movable_area) + ")");
}

/**
 * Read placement.constraints file if present (ISPD2015 format).
 * Parses "maximum_utilization=XX%" and stores as a float in [0, 1].
 */
void DataBase::readPlacementConstraints()
{
    fs::path constraints_path = m_input_dir / "placement.constraints";
    if (!fs::exists(constraints_path)) return;

    std::ifstream file(constraints_path);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        // Look for "maximum_utilization=XX%"
        auto pos = line.find("maximum_utilization=");
        if (pos != std::string::npos) {
            std::string value_str = line.substr(pos + strlen("maximum_utilization="));
            // Strip trailing '%' if present
            if (!value_str.empty() && value_str.back() == '%') {
                value_str.pop_back();
                m_maximum_utilization = std::stof(value_str) / 100.0f;
            } else {
                m_maximum_utilization = std::stof(value_str);
            }
            Logger::log_info("Read placement constraint: maximum_utilization=" +
                            std::to_string((int)(m_maximum_utilization * 100)) + "%");
            break;
        }
    }
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
            Logger::log_detail(extension_match + " file found: \"" + entry.path().string() + "\"");
        }
    }

    return matches;
}

bool DataBase::readLEF() 
{
    std::vector<fs::path> lef_files = findExtensions(m_input_dir, ".lef");
    if (lef_files.size() == 0) 
    {
        Logger::log_warning("No .lef files found.");
        return false;
    }

    bool success = true;
    for(fs::path file : lef_files)
    {
        //disable LEF parser output by redirecting C stream stdout
        FILE* oldStdout = freopen("/dev/null", "w", stdout); // "/dev/null" discards all data written to it
        success = LefParser::read(*this, file.string());
        // restore stdout
        freopen("/dev/tty", "w", stdout); // "/dev/tty" is the terminal

        if (success) {
            Logger::log_info(".lef file parsing successful: " + file.string());
        } else {
            Logger::log_error(".lef file parsing FAILED: " + file.string());
        }
    }
    return success;
}

bool DataBase::readDEF() 
{
    std::vector<fs::path> def_files = findExtensions(m_input_dir, ".def");
    if (def_files.size() == 0) 
    {
        Logger::log_warning("No .def files found.");
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



    Logger::log_info("Begin parsing .DEF design...");
    //disable DEF parser output by redirecting C stream stdout
    FILE* oldStdout = freopen("/dev/null", "w", stdout); // "/dev/null" discards all data written to it
    bool success = DefParser::read(*this, def_file);
    // restore stdout
    freopen("/dev/tty", "w", stdout); // "/dev/tty" is the terminal

    if (success) {
        Logger::log_info(".def file parsing successful: " + def_file.string());
        return true;
    } else {
        Logger::log_error(".def file parsing FAILED: " + def_file.string());
        return false;
    }
}


bool DataBase::readBookshelf()
{
    std::vector<fs::path> aux_files = findExtensions(m_input_dir, ".aux");
    if (aux_files.size() == 0) 
    {
        Logger::log_error("No .aux file found.");
        return false;
    }

    if (aux_files.size() > 1) 
    {
        Logger::log_warning("Multiple .aux files found! Using first one: " + aux_files[0].string());
    }

    Logger::log_info("Begin parsing bookshelf design...");
    //disable parser output by redirecting C stream stdout
    //FILE* oldStdout = freopen("/dev/null", "w", stdout); // "/dev/null" discards all data written to it
    bool success = BookshelfParser::read(*this, aux_files[0]);
    // restore stdout
    //freopen("/dev/tty", "w", stdout); // "/dev/tty" is the terminal

    if (success) {
        Logger::log_info("Bookshelf parsing successful!");
        return true;
    } else {
        Logger::log_error("Bookshelf parsing FAILED!");
        return false;
    }
}


/* @brief: Add fillers to design 
* Computes the total area of all components in the design and adds enough filler cells
* to bring the filled area to target utilization.
*/
bool DataBase::addFillers(float target_utilization)
{
    // Filler cells occupy whitespace so real cells don't have to over-spread to hit the
    // density target — without them overflow can't fall to the stop threshold. Size a filler
    // like a typical movable standard cell (XPlace compute_filler_without_fence): each
    // dimension is the trimmed mean of the movable cells' sizes, dropping the smallest and
    // largest 5% so macros and min-width cells don't skew it. (The previous code sized the
    // filler from the smallest *macro*, which yields ~0 fillers on standard-cell designs.)
    std::vector<float> widths, heights;
    for (auto item : mm_components) {
        Component* comp = item.second;
        if (comp->getStatus() == FIXED) continue;
        widths.push_back(comp->getXsize());
        heights.push_back(comp->getYsize());
    }
    if (widths.empty()) {
        Logger::log_warning("No movable cells found — no fillers added.");
        return false;
    }

    std::sort(widths.begin(), widths.end());
    std::sort(heights.begin(), heights.end());
    auto trimmed_mean = [](const std::vector<float>& sorted) {
        int n = (int)sorted.size();
        int lo = (int)(n * 0.05f), hi = (int)(n * 0.95f);
        if (hi <= lo) { lo = 0; hi = n; }  // too few cells to trim: use the whole range
        double sum = 0.0;
        for (int i = lo; i < hi; i++) sum += sorted[i];
        return (float)(sum / (hi - lo));
    };
    float filler_xsize = trimmed_mean(widths);
    float filler_ysize = trimmed_mean(heights);  // std cells are ~1 row tall, so this ≈ row height

    MacroClass* filler_macro = new MacroClass("filler", filler_xsize, filler_ysize);
    mm_macros.emplace(std::make_pair("filler_macroclass", filler_macro));

    Logger::log_info("Adding filler cells to database...");
    Logger::log_info("Filler cell size: (" + PREC(filler_xsize) + ", " + PREC(filler_ysize) + ")");

    // Fill the placeable whitespace up to the target utilization (XPlace: target_density *
    // stdcell_placeable_area - mov_stdcell_area). With no movable macros this reduces to
    // target_util * (die - fixed) - movable_area.
    float available_area = getDieArea().getArea() - m_total_fixed_area;
    float unfilled_area = available_area * target_utilization - m_total_movable_area;
    Logger::log_info("Available area (die - fixed): " + PREC(available_area));
    Logger::log_info("Total area to fill: " + PREC(unfilled_area));

    int fillers_needed = std::max(0, (int)(unfilled_area / filler_macro->getArea()));

    for (int i = 0; i < fillers_needed; i++)
    {
        Component* filler = new Component("filler_" + stringify(i));
        filler->setMacroClass(filler_macro);
        filler->setPlacementStatus(PlacementStatus::UNPLACED);
        filler->setNodePos(Position(0.0f, 0.0f)); // position will be updated during placement
        mv_fillers.push_back(filler);
    }
    Logger::log_info("Total fillers added: " + stringify(fillers_needed));
    return true;
}

/* @brief: Reset all nodes and nets in preparation for the next iteration.
*/
void DataBase::iterationReset()
{
    for (auto item : mm_components)
        item.second->iterationReset();
    for (auto filler : mv_fillers)
        filler->iterationReset();
    for (auto item : mm_iopads)
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

float DataBase::computeTotalWirelength(string method, int max_net_degree)
{
    // max_net_degree matches XPlace's ignore_net_degree (net_mask): nets with more pins are
    // excluded from the HPWL metric so the reported number, the density-weight schedule's
    // delta_hpwl, and convergence all measure the SAME masked wirelength XPlace does.
    float total = 0;
    for (auto item : mm_nets)
        if (item.second->getDegree() <= max_net_degree)
            total += item.second->computeWirelength(method);
    return total;
}

float DataBase::computeTotalComponentArea()
{
    return m_total_component_area;
}

/* @brief: Organize data into “groups” to create “packets”.
Define a “group” as X and Y data of 4 nets of the same size, which gives 8 operands
A packet is a set of groups. The number of groups_per_packet  =  840 / net_size 
840 is LCM of sizes 2-8, which means all nets <= 8 will fit neatly into 840

Loop over net_size 2 thru 8.
Generate packets of current net_size until all nets of this size are assigned.

*/
void DataBase::initializePacketContents()
{
    Logger::log("packets", "BEGIN initializePacketContents()");
    int graph_index = 0;
    m_packet_count = 0;
    for(int net_size = MIN_AIE_NET_SIZE; net_size <= MAX_AIE_NET_SIZE; net_size++) {
    //for(int net_size = TEST_NET_SIZE; net_size <= TEST_NET_SIZE; net_size++) {
        Logger::log("packets",  "net_size = " + stringify(net_size));
        int groups_per_packet = LCM_BUFFSIZE/net_size; // for netsize 2 thru 8, this will be an exact integer
        Logger::log("packets",  "groups_per_packet = " + stringify(groups_per_packet));
        Logger::log("packets",  "nets_per_packet = " + stringify(groups_per_packet*NETS_PER_GROUP));
        int num_nets = mmv_nets_by_degree[net_size].size();
        Logger::log("packets",  "num_nets = " + stringify(num_nets));

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

            PacketIndex &pir = p->contents[0];
            //cout << "graph_index: " << graph_index  << "\t" << p->contents[0].to_string();
            table.add_row({ stringify(graph_index), stringify(pir.net_size), 
                            stringify(pir.group_start), stringify(pir.group_count) });

            //move start/stop for next packet
            packet_start += groups_per_packet;

            if(graph_index == 0) m_packet_count++;
            graph_index = (graph_index+1) % PARTIALS_GRAPH_COUNT;
        }
        table.format().font_align(FontAlign::center);
        Logger::log("packets", table);
    }
    Logger::log("packets", "END initializePacketContents()");
}

/*
 * @brief: Prepare data in correct format to be sent to PL and then AIE input streams
 * A net group is four (NETS_PER_GROUP) of size (net_size).
 * Group will have a size of (2 x NETS_PER_GROUP x net_size)
 * The 2x is because X and Y coord are grouped together
**/
void DataBase::prepareNetGroup(float * input_data, int net_size, int offset)
{
    TIME_FUNCTION();
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
            input_data[base_addr + 2*net_idx + j*8] = nodes[j]->getProbeX();

            // check for correct ordering
            if(j > 0) if(input_data[base_addr + 2*net_idx + 0*8] < input_data[base_addr + 2*net_idx + j*8])  {
                Logger::log_debug("MAX COORD MUST BE FIRST! Net: " + net->getName()
                    + "\n1st input: " + std::to_string(input_data[base_addr + 2*net_idx + 0*8])
                    + "\n" + std::to_string(j+1) + "nd input: " + std::to_string(input_data[base_addr + 2*net_idx + j*8]));
                exit(2);
            }
            if(j > 1) if(input_data[base_addr + 2*net_idx + 1*8] > input_data[base_addr + 2*net_idx + j*8]) {
                Logger::log_debug("MIN COORD MUST BE 2ND! Net: " + net->getName()
                    + "\n2nd input: " + std::to_string(input_data[base_addr + 2*net_idx + 1*8])
                    + "\n" + std::to_string(j+1) + "nd input: " + std::to_string(input_data[base_addr + 2*net_idx + j*8]));
                exit(2);
            }

            //if(nodes[j]->getX() != nodes[j]->getX()) // check for nan
            //{
            //    Logger::log_warning(string("NaN detected:\n" + net->to_string() + "\n"));
            //    nan = true;
            //}
        }

        // prep y data
        net->sortPositionsMaxMinY();
        for(int j = 0; j < net_size; j++) {
            input_data[base_addr + 2*net_idx + j*8 + 1] = nodes[j]->getProbeY();
            //if(nodes[j]->getY() != nodes[j]->getY()) // check for nan
            //{
            //    Logger::log_warning(string("NaN detected:\n" + net->to_string() + "\n"));
            //    nan = true;
            //}
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

// Debugging vars
constexpr float MAX_PARTIAL = 2.0;
static int nan_count = 0;
constexpr int MAX_WARNINGS = 100;
float max_size_good_x {0};
float max_size_good_y {0};
float min_size_nan_x {__FLT_MAX__};
float min_size_nan_y {__FLT_MAX__};
int DataBase::storeNetGroup(float * output_data, int net_size, int offset)
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
        Net* net = mmv_nets_by_degree[net_size][offset + net_idx ];

        std::vector<Node*> nodes = net->getNodes(); // should this be a reference, to avoid being a copy?

        // nodes are already sorted by Y coords, so store them first
        for(int n = 0; n < net_size; n++) {
            Logger::log_debug("Net " + net->getName() + "[" + std::to_string(n) + "] is node " + nodes[n]->getName());
            //float partial = max(-MAX_PARTIAL, min(MAX_PARTIAL, output_data[base_addr + 2*net_idx + n*8 + 1]));
            float partial = output_data[base_addr + 2*net_idx + n*8 + 1];
            nodes[n]->next.probe_grad.y += partial;
            Logger::log_debug("Partial received from AIE for Node [" + std::to_string(n) + "] : " + nodes[n]->getName() + " on net " + net->getName() + " :::: probe_grad.y += " + std::to_string(partial) + " :::: Total Partial = " + std::to_string(nodes[n]->next.probe_grad.y));

            //debugging
            float y_size = net->getBoundingBox().getYsize();
            if(partial != partial) // check for nan
            {
                if(++nan_count < MAX_WARNINGS) {
                    Logger::log_warning(string("NaN detected on Y partial for node: " + nodes[n]->getName() + " \nInputs:" + net->to_string() + "\n"));
                    Logger::log_warning(string("partial: " + std::to_string(partial) + "\n"));
                    Logger::log_warning(string("Net Name: " + net->getName() + "\n"));
                    Logger::log_warning(string("BAD RESULT: net Y size = " + std::to_string(y_size)));
                    if(y_size < min_size_nan_y)
                        min_size_nan_y = y_size;
                }
            }
            else {
                if(nan_count < MAX_WARNINGS) 
                    //Logger::log_warning(string("GOOD RESULT: net Y size = " + std::to_string(y_size)));
                    if(y_size > max_size_good_y)
                        max_size_good_y = y_size;
            }
        }

        net->sortPositionsMaxMinX(); // X or Y
        nodes = net->getNodes();

        for(int n = 0; n < net_size; n++) {
            Logger::log_debug("Net " + net->getName() + "[" + std::to_string(n) + "] is node " + nodes[n]->getName());
            //float partial = max(-MAX_PARTIAL, min(MAX_PARTIAL, output_data[base_addr + 2*net_idx + n*8]));
            float partial = output_data[base_addr + 2*net_idx + n*8 + 0];
            nodes[n]->next.probe_grad.x += partial;
            Logger::log_debug("Partial received from AIE for Node [" + std::to_string(n) + "] : " + nodes[n]->getName() + " on net " + net->getName() + " :::: probe_grad.x += " + std::to_string(partial) + " :::: Total Partial = " + std::to_string(nodes[n]->next.probe_grad.x));

            //debugging
            float x_size = net->getBoundingBox().getXsize();
            if(partial != partial) // check for nan
            {
                if(++nan_count < MAX_WARNINGS) {
                    Logger::log_warning(string("NaN detected on X partial for node: " + nodes[n]->getName() + " \nInputs:" + net->to_string() + "\n"));
                    Logger::log_warning(string("partial: " + std::to_string(partial) + "\n"));
                    Logger::log_warning(string("BAD RESULT: net X size = " + std::to_string(x_size)));
                    if(x_size < min_size_nan_x)
                        min_size_nan_x = x_size;
                }
            }
            else {
                if(nan_count < MAX_WARNINGS) 
                    //Logger::log_warning(string("GOOD RESULT: net X size = " + std::to_string(x_size)));
                    if(x_size > max_size_good_x)
                        max_size_good_x = x_size;
            }
        }
    }

    //Logger::log_debug(("max_size_good_x: " + std::to_string(max_size_good_x)));
    //Logger::log_debug(("max_size_good_y: " + std::to_string(max_size_good_y)));
    //Logger::log_debug(("min_size_nan_x: " +  std::to_string(min_size_nan_x)));
    //Logger::log_debug(("min_size_nan_y: " +  std::to_string(min_size_nan_y)));

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

    return nan_count;
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
        void DataBase::lef_macrobegin_cbk(std::string const& n) {
            // Create macro early so lef_pin_cbk (which fires before lef_macro_cbk) can add pin offsets
            MacroClass* new_macro = new MacroClass(n);
            mm_macros.emplace(std::make_pair(n, new_macro));
            m_current_lef_macro = new_macro;
        }
        void DataBase::lef_macro_cbk(LefParser::lefiMacro const& m) {
            // Finalize macro size (pins have already been added by lef_pin_cbk)
            m_current_lef_macro->setSize(m.sizeX(), m.sizeY());
            // Record LEF CLASS so add_def_component can apply XPlace's PLACED->fixed rule.
            if (m.hasClass()) m_current_lef_macro->setClass(m.macroClass());

            m_current_lef_macro = nullptr;
        }

        // Called for each PIN within the current MACRO block.
        // Extracts pin offset as center of first RECT in first port.
        void DataBase::lef_pin_cbk(LefParser::lefiPin const& p) {
            if (!m_current_lef_macro) return;

            // Skip power/ground pins — they don't appear in signal nets
            if (p.hasUse()) {
                string use = p.use();
                if (use == "POWER" || use == "GROUND") return;
            }

            // Find the center of the first RECT in the first port
            if (p.numPorts() < 1) return;
            LefParser::lefiGeometries* geom = p.port(0);
            for (int gi = 0; gi < geom->numItems(); gi++) {
                if ((int)geom->itemType(gi) == (int)LefParser::lefiGeomRectE) {
                    LefParser::lefiGeomRect* rect = geom->getRect(gi);
                    float cx = (float)(rect->xl + rect->xh) / 2.0f;
                    float cy = (float)(rect->yl + rect->yh) / 2.0f;
                    m_current_lef_macro->addPinOffset(p.name(), Position(cx, cy));
                    break; // use first RECT only
                }
            }
        }
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
            m_die_area = Box(Position((position_type)xl, (position_type)yl),
                             Position((position_type)xh, (position_type)yh));
        }

        void DataBase::add_def_row(DefParser::Row const& r) {}
        void DataBase::resize_def_component(int s) {}

        void DataBase::add_def_component(DefParser::Component const& c) 
        // Create a new component (Node) and add it to the database
        {
            Component* new_comp = new Component(c.comp_name);
            MacroClass* macro = mm_macros[c.macro_name];
            new_comp->setMacroClass(macro);
            // XPlace-faithful status (file_lefdef_db.cpp:1565-1595): a PLACED cell is movable only if
            // its LEF CLASS is CORE or BLOCK; PLACED non-CORE/BLOCK cells (VIA/feedthrough/fill) are
            // pre-placed and treated as FIXED. UNPLACED/FIXED pass through unchanged.
            string status = c.status;
            if (status == "PLACED" && macro) {
                const string& cls = macro->getClass();
                if (cls != "CORE" && cls != "BLOCK") status = "FIXED";
            }
            new_comp->setPlacementStatus(status);
            new_comp->setNodePos(Position((float)c.origin[0], (float)c.origin[1]));
            // TODO: assert component is created correctly
            mm_components.emplace(std::make_pair(new_comp->getName(), new_comp));
        }

        void DataBase::resize_def_pin(int s) {}

        void DataBase::add_def_pin(DefParser::Pin const& p) {
            IOPad* new_iopad = new IOPad(p.pin_name);
            std::vector<int> bb = p.vBbox.front();
            new_iopad->setBoundingBox(bb[0], bb[1], bb[2], bb[3]);
            new_iopad->setPlacementStatus(p.status);
            new_iopad->setNodePos(Position((float)p.origin[0], (float)p.origin[1]));
            new_iopad->setDirection(p.direct); // primary input or output

            mm_iopads.emplace(std::make_pair(new_iopad->getName(), new_iopad));
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
                    IOPad* iopad_p = mm_iopads[net_pin.second];
                    assert(iopad_p != NULL && "PIN name points to nullptr while reading .DEF\n");
                    new_net->addNode(iopad_p);
                    iopad_p->addNet(new_net);
                }
                else // it is a component
                {
                    Component* comp_p = mm_components[net_pin.first];
                    assert(comp_p != NULL && "COMPONENT name points to nullptr while reading .DEF\n");
                    // Look up pin offset from the component's macro (LEF microns → DEF dbu)
                    Position pin_offset(0, 0);
                    MacroClass* macro = comp_p->getMacro();
                    if (m_enable_pin_offsets && macro && macro->hasPinOffset(net_pin.second)) {
                        pin_offset = macro->getPinOffset(net_pin.second);
                        float scale = (float)m_units_per_micron;
                        pin_offset.x *= scale;
                        pin_offset.y *= scale;
                    }
                    new_net->addNode(comp_p, pin_offset, net_pin.second);
                    comp_p->addNet(new_net);
                }
            }
            mm_nets.emplace(std::make_pair(new_net->getName(), new_net));
            mv_nets.push_back(new_net);

            int degree = new_net->getDegree();
            if (mmv_nets_by_degree.count(degree) == 0) {
                mmv_nets_by_degree.emplace(std::make_pair(degree, std::vector<Net*>()));
            }
            mmv_nets_by_degree[degree].push_back(new_net); //emplace_back(new_net) might work more effienctly
        }

        void DataBase::resize_def_blockage(int) {}
        void DataBase::add_def_placement_blockage(std::vector<std::vector<int> > const&) {}
        void DataBase::resize_def_region(int) {}
        void DataBase::add_def_region(DefParser::Region const& r) {}
        void DataBase::resize_def_group(int) {}
        void DataBase::add_def_group(DefParser::Group const& g) {}
        void DataBase::end_def_design() {}
        

    // *******************************************************************************
        // BOOKSHELF callbacks
        /// @brief set number of terminals 
        void DataBase::resize_bookshelf_node_terminals(int NumNodes, int NumTerminals) {
        }
        /// @brief set number of nets 
        void DataBase::resize_bookshelf_net(int NumNets) {
        }
        /// @brief set number of pins 
        void DataBase::resize_bookshelf_pin(int NumPins) {
        }
        /// @brief set number of rows 
        void DataBase::resize_bookshelf_row(int NumRows) {
        }
        /// @brief set number of shapes 
        //void DataBase::resize_bookshelf_shapes(int) {}
        /// @brief set number of NI terminals with layers 
        //void DataBase::resize_bookshelf_niterminal_layers(int) {}
        /// @brief set number of blockage nodes with layers 
        //void DataBase::resize_bookshelf_blockage_layers(int) {}

        /// @brief add terminal (fixed macro or IO pad) as a Component with FIXED status
        void DataBase::add_bookshelf_terminal(string& name, int width, int height) {
            Component* comp = new Component(name);
            string macro_name = "macro_" + std::to_string(width) + "_" + std::to_string(height);
            MacroClass* macro_p = mm_macros[macro_name];
            if(macro_p == NULL) {
                macro_p = new MacroClass(macro_name, width, height);
                mm_macros[macro_name] = macro_p;
            }
            comp->setMacroClass(macro_p);
            comp->setPlacementStatus(PlacementStatus::FIXED);
            comp->setNodePos(Position(0, 0));
            mm_components.emplace(std::make_pair(name, comp));
        }

        /// @brief add terminal_NI
        //void DataBase::add_bookshelf_terminal_NI(string&, int, int) {}
        /// @brief add node 
        void DataBase::add_bookshelf_node(string& name, int width, int height, bool notsurewhatthisboolisfor) {
            Component* new_comp = new Component(name);
            // for Bookshelf format, no macro classes are defined by the design
            // So we create macros named "macro_width_height"
            string macro_name = "macro_" + std::to_string(width) + "_" + std::to_string(height);
            MacroClass* macro_p = mm_macros[macro_name];
            if(macro_p == NULL) {
                macro_p = new MacroClass(macro_name, width, height);
                //mm_macros.emplace(std::make_pair(macro_name, macro_p));
                mm_macros[macro_name] = macro_p;
            }
            new_comp->setMacroClass(macro_p);
            new_comp->setPlacementStatus(PlacementStatus::UNPLACED);
            new_comp->setNodePos(Position(0, 0)); // default position (0, 0)
            mm_components.emplace(std::make_pair(new_comp->getName(), new_comp));
        }
        /// @brief add net 
        void DataBase::add_bookshelf_net(BookshelfParser::Net const& bookshelf_net) { 
            Net* new_net = new Net(bookshelf_net.net_name);
                //cout << "Add net: " << bookshelf_net.net_name << endl;
            for (BookshelfParser::NetPin net_pin : bookshelf_net.vNetPin)
            {
                //cout << "\tNetPin: " << net_pin.node_name << endl;
                if(mm_iopads.count(net_pin.node_name) > 0) {
                    IOPad* iopad_p = mm_iopads[net_pin.node_name];
                    new_net->addNode(iopad_p);
                    iopad_p->addNet(new_net);
                } else if(mm_components.count(net_pin.node_name)){ // it's a component
                    Component* comp_p = mm_components[net_pin.node_name];
                    // Bookshelf pin offsets are relative to the cell CENTER; sw_only node_pos is the
                    // lower-left corner, so shift by half-size to make the stored offset LL-relative
                    // (the NetPin.offset convention shared with the LEF/DEF path). Offsets and sizes
                    // are already in the same units here (no micron→dbu scaling for bookshelf).
                    Position pin_offset(0, 0);
                    if (m_enable_pin_offsets) {
                        pin_offset.x = comp_p->getXsize() / 2.0f + (float)net_pin.offset[0];
                        pin_offset.y = comp_p->getYsize() / 2.0f + (float)net_pin.offset[1];
                    }
                    new_net->addNode(comp_p, pin_offset, net_pin.pin_name);
                    comp_p->addNet(new_net);
                } else {
                    Logger::log_error("Node was not found while parsing bookshelf nets.");
                    exit(7);
                }
            }

            mm_nets.emplace(std::make_pair(new_net->getName(), new_net));
            mv_nets.push_back(new_net);
            
            // Add net to degree map for easy access
            int degree = new_net->getDegree();
            if (mmv_nets_by_degree.count(degree) == 0) {
                mmv_nets_by_degree.emplace(std::make_pair(degree, std::vector<Net*>()));
            }
            mmv_nets_by_degree[degree].push_back(new_net);

         }

        /// @brief add row — accumulate the .scl core-row bounding box (the die comes from
        /// the rows, not the terminal coordinates). A row spans [SubrowOrigin, +NumSites*
        /// SiteSpacing] in x and [Coordinate, +Height] in y.
        void DataBase::add_bookshelf_row(BookshelfParser::Row const& row) {
            long spacing = row.site_spacing > 0 ? row.site_spacing : row.site_width;
            long x0 = row.origin[0];
            long x1 = x0 + (long)row.site_num * spacing;
            long y0 = row.origin[1];
            long y1 = y0 + row.height;
            if (m_row_count == 0) {
                m_row_xmin = x0; m_row_xmax = x1;
                m_row_ymin = y0; m_row_ymax = y1;
            } else {
                m_row_xmin = std::min(m_row_xmin, x0);
                m_row_xmax = std::max(m_row_xmax, x1);
                m_row_ymin = std::min(m_row_ymin, y0);
                m_row_ymax = std::max(m_row_ymax, y1);
            }
            m_row_count++;
        }
        /// @brief set node position — all bookshelf nodes (terminals + cells) are now Components
        void DataBase::set_bookshelf_node_position(string const& name, double x, double y, string const& orientation, string const& placement_status, bool notsurewhatfor) {
            Component* comp = mm_components[name];
            assert(comp != NULL && "invalid component name!");
            comp->setNodePos(Position(x, y));
            comp->setOrientation(orientation);
            if(placement_status == "FIXED") {
                comp->setPlacementStatus(PlacementStatus::FIXED);
                // Bookshelf format doesn't explicitly give die area,
                // so we infer it from the outermost fixed terminal coordinates
                if(x > m_max_x) m_max_x = x;
                if(y > m_max_y) m_max_y = y;
            }
        }
        /// @brief set net weight 
        //void DataBase::set_bookshelf_net_weight(string const& name, double w) {}
        /// @brief set node shapes 
        //void DataBase::set_bookshelf_shape(NodeShape const&) {}
        /// @brief set routing information 
        //void DataBase::set_bookshelf_route_info(RouteInfo const&) {}
        /// @brief set NI terminal with layers 
        //void DataBase::add_bookshelf_niterminal_layer(string const&, string const&) {}
        /// @brief set blockages with layers 
        //void DataBase::add_bookshelf_blockage_layers(string const&, vector<string> const&) {}

        /// @brief set design name 
        void DataBase::set_bookshelf_design(string& s) { 
            m_design_name = s;
            Logger::log_info("Bookshelf design: " + s);
        }

        /// @brief a callback when a bookshelf file reaches to the end 
        void DataBase::bookshelf_end() {
            if (m_row_count > 0) {
                // Die = core-row bounding box (matches XPlace). Shift all node coords so the
                // die lower-left becomes the origin — the grid/solver assume die LL = (0,0),
                // and XPlace applies the identical die_shift. Added back on DEF output.
                m_die_shift = Position((float)m_row_xmin, (float)m_row_ymin);
                for (auto& item : mm_components)
                    item.second->translate(-m_die_shift.x, -m_die_shift.y);
                m_die_area = Box(Position(0, 0),
                                 Position((float)(m_row_xmax - m_row_xmin),
                                          (float)(m_row_ymax - m_row_ymin)));
            } else {
                // No core rows parsed (defensive): fall back to terminal-inferred extent.
                m_die_area = Box(Position(0, 0),
                                 Position((float)m_max_x, (float)m_max_y));
            }
            Logger::log_info("End of Bookshelf design reading.");
        }
        
// Print info functions
void DataBase::printNodes() const
{
    printComponents();
    printIOPads();
}

void DataBase::printIOPads() const
{
    for(auto item : mm_iopads)
    {
        IOPad* iopad_p = item.second;
        cout << iopad_p->getName() << iopad_p->next.node_pos.to_string() << endl;
        cout << "\tArea: " << iopad_p->getArea() << "\tStatus: " << iopad_p->getStatus() << endl;
    }
}

void DataBase::printComponents() const
{
    for(auto item : mm_components)
    {
        Component* comp_p = item.second;
        cout << comp_p->getName() << comp_p->next.node_pos.to_string() << endl;
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
            cout << node->next.node_pos.x << '\t';
        cout << endl;

        sortPositionsByY();
        cout << "Y descending: ";
        for(auto node : net_p->getNodes())
            cout << node->next.node_pos.y << '\t';
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
    data.add_row(RowStream{} << "IO Pads" << mm_iopads.size());
    data.add_row(RowStream{} << "Components" << mm_components.size());
    data.add_row(RowStream{} << "Nets" << mm_nets.size());
    data.add_row(RowStream{} << "Die Area" << m_die_area.getArea());
    data.add_row(RowStream{} << "Component area: " << computeTotalComponentArea());

    top.add_row({data});
    top.format().font_align(FontAlign::center);
    Logger::log_data(top);
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
        header.add_row(RowStream{} << "Position" << node_p->next.node_pos.to_string());
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
        
        Logger::log_info(top);
    }
}

bool DataBase::writeDEF(const std::string& output_path) const
{
    string output_filename = output_path + "/" + m_design_name + ".def";
    std::ofstream out(output_filename);
    if (!out.is_open()) {
        Logger::log_error("DEF write: invalid output filename: " + output_filename);
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
    // Un-shift back to the original benchmark frame (see bookshelf_end die_shift).
    out << "DIEAREA ( "
        << m_die_area.getPosBottomLeft().x + m_die_shift.x << " "
        << m_die_area.getPosBottomLeft().y + m_die_shift.y << " ) ( "
        << m_die_area.getPosTopRight().x + m_die_shift.x << " "
        << m_die_area.getPosTopRight().y + m_die_shift.y << " ) ;\n\n";
}

void DataBase::writeComponents(std::ofstream& out) const {
    out << "COMPONENTS " << mm_components.size() << " ;\n";
    for (const auto& item : mm_components) {
        auto comp = item.second;
        out << "    - " << comp->getName() << " " << comp->getMacro()->getName() << "\n"
            << "      + " << "PLACED"/*comp->getStatus()*/ << " ( "
            << comp->getX() + m_die_shift.x << " " << comp->getY() + m_die_shift.y << " ) "
            << comp->getOrientation() << " ;\n";
    }
    out << "END COMPONENTS\n\n";
}

void DataBase::writePins(std::ofstream& out) const {
    out << "PINS " << mm_iopads.size() << " ;\n";
    for (const auto& item : mm_iopads) {
        IOPad* iopad = item.second;
        out << "    - " << iopad->getName() << " + NET " << iopad->getName()
            << "\n      + DIRECTION " << iopad->getDirection()
            << "\n";
        if (iopad->isPlaced()) {
            out << "      + PLACED "
                << " ( " << iopad->getX() << " " << iopad->getY() << " ) "
                << iopad->getOrientation() << "\n";
        }
        out << "      + LAYER " << iopad->getLayer()
            << iopad->getBoundingBox().getDEFstring()
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
        for (const auto& pin : net->mv_pins) {
            if (dynamic_cast<IOPad*>(pin.node)) {
                out << " ( PIN " << pin.node->getName() << " )";
            } else {
                out << " ( " << pin.node->getName() << " " << pin.pin_name << " )";
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