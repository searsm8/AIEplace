
#include "DataBase.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unistd.h>

AIEPLACE_NAMESPACE_BEGIN

using namespace tabulate; // table types, scoped to this .cpp (not leaked via Logger.h)

DataBase::DataBase(fs::path input_dir)
    : m_input_dir(input_dir) {
    TIME_BLOCK("DataBase read input");
    Logger::log_detail("Reading design from directory: " + m_input_dir.string());
    m_max_x = 0;
    m_max_y = 0;

    readDesignFiles();
    readPlacementConstraints();
    computeNetDegreeTotal();
    computeAreaBreakdown();
}

// Reads LEF/DEF (scaling LEF macro sizes to DEF's dbu units), falling back to Bookshelf if
// either LEF or DEF is missing. Exits if neither format could be read.
void DataBase::readDesignFiles()
{
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
        m_row_height *= scale;   // lef_site_cbk recorded it in microns too
        Logger::log_detail("Scaled " + std::to_string(mm_macros.size()) +
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


void DataBase::computeNetDegreeTotal()
{
    m_total_net_degree = 0;
    for (auto* net_p : mv_nets) {
        m_total_net_degree += net_p->getDegree();
    }
}

// Cache area breakdown (constant for the lifetime of the design). FIXED components are
// clipped to the die — XPlace counts only the fixed area that lands inside the die
// (fixed_node_area = init_density_map inside the core), so terminals overhanging the die
// don't inflate the placeable-area denominator that sets the filler count. Movable area
// is the raw sum (movable cells sit inside the die).
void DataBase::computeAreaBreakdown()
{
    float die_xl = m_die_area.getPosBottomLeft().x, die_yl = m_die_area.getPosBottomLeft().y;
    float die_xu = m_die_area.getPosTopRight().x,   die_yu = m_die_area.getPosTopRight().y;
    double movable_sum = 0;
    double fixed_sum = 0;
    int fixed_count = 0;
    for (const auto& item : mm_components) {
        Component* comp_p = item.second;
        if (comp_p->getStatus() == FIXED) {
            float ox = std::max(0.0f, std::min(comp_p->getX() + comp_p->getXsize(), die_xu) - std::max(comp_p->getX(), die_xl));
            float oy = std::max(0.0f, std::min(comp_p->getY() + comp_p->getYsize(), die_yu) - std::max(comp_p->getY(), die_yl));
            fixed_sum += (double)ox * oy;
            fixed_count++;
        } else {
            movable_sum += comp_p->getArea();
        }
    }
    m_total_fixed_area = (float)fixed_sum;
    m_total_movable_area = (float)movable_sum;
    m_total_component_area = m_total_fixed_area + m_total_movable_area;

    Logger::log_detail("Fixed components: " + std::to_string(fixed_count)
        + " (area: " + std::to_string((long long)m_total_fixed_area)
        + ", " + std::to_string((int)(100.0f * m_total_fixed_area / m_die_area.getArea())) + "% of die)");
    Logger::log_detail("Movable components: " + std::to_string((int)mm_components.size() - fixed_count)
        + " (area: " + std::to_string((long long)m_total_movable_area) + ")");
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

// Runs parse_fn with stdout redirected to /dev/null, then restores the ORIGINAL stdout.
// (The old code restored to a hardcoded "/dev/tty", which blocks under a headless
//  launch — e.g. dse.py sweeps — when the controlling terminal buffer fills.)
bool DataBase::runParserSilenced(std::function<bool()> parse_fn)
{
    fflush(stdout);
    int saved_stdout = dup(STDOUT_FILENO);
    if (!freopen("/dev/null", "w", stdout)) { /* non-fatal: stdout stays as-is, parser noise is not */ }
    bool success = parse_fn();
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    clearerr(stdout);
    return success;
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
        success = runParserSilenced([&]() { return LefParser::read(*this, file.string()); });

        if (success) {
            Logger::log_detail(".lef file parsing successful: " + file.string());
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

    fs::path def_file;
    for(int i = 0; i < def_files.size(); i++)
    {
        if(def_files[i].filename() == "floorplan.def")
            def_file = def_files[i];
    }

    Logger::log_detail("Begin parsing .DEF design...");
    bool success = runParserSilenced([&]() { return DefParser::read(*this, def_file); });

    if (success) {
        // Marks the transition from "reading input files" to "reporting on the parsed design" —
        // everything logged from here on describes the design that was just read.
        Table section;
        section.add_row({"Reading Input"});
        section.format().font_align(FontAlign::center).font_style({FontStyle::bold});
        Table content;
        content.add_row(RowStream{} << "DEF file" << def_file.string());
        section.add_row({content});
        Logger::log_detail(section);
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

    Logger::log_detail("Begin parsing bookshelf design...");
    bool success = runParserSilenced([&]() { return BookshelfParser::read(*this, aux_files[0]); });

    if (success) {
        Logger::log_detail("Bookshelf parsing successful!");
        return true;
    } else {
        Logger::log_error("Bookshelf parsing FAILED!");
        return false;
    }
}


/**
 * @brief Create filler cells to occupy the design's whitespace, and report the density target
 *        that is actually achievable.
 *
 * Mirrors XPlace compute_filler_without_fence (database.py:662). Fillers stand in for whitespace
 * so the real cells reach the density target without over-spreading; with none, overflow never
 * falls to the stop threshold.
 *
 * Everything is computed in the STANDARD-CELL frame: movable macros leave the size sample, the
 * placeable area, and the area to fill. A macro is neither shaped like a filler nor able to make
 * room for one, so counting it does not describe the space fillers actually compete for.
 * Requires Placer::tagMovableMacros() to have run.
 *
 * @param  target_utilization (expected 0 to 1) density the benchmark/config asks for.
 * @return the EFFECTIVE target density — raised to the standard-cell utilization when the design
 *         is denser than the request. The raise yields no fillers (at that density the whitespace
 *         is zero by definition); what it does is stop the placer chasing a density the design
 *         cannot reach. The caller must adopt the returned value.
 */
float DataBase::addFillers(float target_utilization)
{
    // Movable standard cells only. A macro is 100-1000x a standard cell in both dimensions and
    // would drag the filler size with it (XPlace masks is_mov_macro out of the same sample).
    std::vector<float> stdcell_widths, stdcell_heights;
    float movable_macro_area = 0.0f;
    for (const auto& item : mm_components) {
        Component* comp_p = item.second;
        if (comp_p->getStatus() == FIXED) continue;
        if (comp_p->isMovableMacro()) { movable_macro_area += comp_p->getArea(); continue; }
        stdcell_widths.push_back(comp_p->getXsize());
        stdcell_heights.push_back(comp_p->getYsize());
    }
    if (stdcell_widths.empty()) {
        Logger::log_warning("No movable standard cells found — no fillers added.");
        return target_utilization;
    }

    // Drop the smallest and largest 5% so min-width cells don't set the size.
    auto trimmed_mean = [](std::vector<float>& sizes) {
        std::sort(sizes.begin(), sizes.end());
        int n = (int)sizes.size();
        int lo = (int)(n * 0.05f), hi = (int)(n * 0.95f);
        if (hi <= lo) { lo = 0; hi = n; }  // too few cells to trim: use the whole range
        double sum = 0.0;
        for (int i = lo; i < hi; i++) sum += sizes[i];
        return (float)(sum / (hi - lo));
    };

    float filler_xsize = trimmed_mean(stdcell_widths);
    // Height is the ROW height, not a statistic: a filler occupies whitespace inside the
    // standard-cell rows, so it is exactly one row tall (XPlace uses site_height directly). On a
    // design whose movable cells are all single-row the two agree exactly; they diverge on
    // multi-row cells, which is why the mean is only a fallback for input that gave us no rows.
    float filler_ysize = m_row_height;
    if (filler_ysize <= 0.0f) {
        filler_ysize = trimmed_mean(stdcell_heights);
        Logger::log_warning("No row height in the input; sizing fillers from the mean cell height.");
    }

    // Whitespace budget. Movable macros consume placeable area without being standard-cell area,
    // so they leave both terms. Keeping them in (as this did before) under-fills by
    // movable_macro_area * (1 - target_density): zero at density 1.0, but on a macro-heavy design
    // below it that term IS the filler population.
    float stdcell_placeable_area = getDieArea().getArea() - m_total_fixed_area - movable_macro_area;
    float stdcell_area           = m_total_movable_area - movable_macro_area;

    float stdcell_utilization = stdcell_area / std::max(1.0f, stdcell_placeable_area);
    if (stdcell_utilization > target_utilization) {
        Logger::log_warning("Standard-cell utilization " + PREC(stdcell_utilization) +
            " exceeds target density " + PREC(target_utilization) +
            "; raising the target to match (XPlace database.py:679).");
        target_utilization = stdcell_utilization;
    }

    float whitespace_area = std::max(0.0f,
        target_utilization * stdcell_placeable_area - stdcell_area);
    int fillers_needed = (int)std::lround(whitespace_area / (filler_xsize * filler_ysize));
    if (fillers_needed == 0)
        Logger::log_warning("No fillers added: no whitespace at target density " +
            PREC(target_utilization) + ". Overflow will not reach the stop threshold.");

    MacroClass* filler_macro = new MacroClass("filler", filler_xsize, filler_ysize);
    mm_macros.emplace(std::make_pair("filler_macroclass", filler_macro));

    for (int i = 0; i < fillers_needed; i++)
    {
        Component* filler_p = new Component("filler_" + stringify(i));
        filler_p->setMacroClass(filler_macro);
        filler_p->setPlacementStatus(PlacementStatus::UNPLACED);
        filler_p->setNodePos(Position(0.0f, 0.0f)); // position will be updated during placement
        mv_fillers.push_back(filler_p);
    }

    Logger::log_info("Fillers: " + stringify(fillers_needed) + " at (" + PREC(filler_xsize) +
        ", " + PREC(filler_ysize) + "), effective target density " + PREC(target_utilization));
    return target_utilization;
}

/// @brief Build the flat, index-addressable views of the node/net maps — see DataBase.h.
void DataBase::buildNodeIndex()
{
    mv_movable_components.clear();
    mv_fixed_components.clear();
    mv_iopad_nodes.clear();
    mv_nets_by_name.clear();
    mv_movable_nodes.clear();

    for (const auto& item : mm_components) {
        if (item.second->getStatus() == FIXED) mv_fixed_components.push_back(item.second);
        else                                   mv_movable_components.push_back(item.second);
    }
    for (const auto& item : mm_iopads) mv_iopad_nodes.push_back(item.second);
    for (const auto& item : mm_nets)   mv_nets_by_name.push_back(item.second);

    for (Component* comp_p : mv_movable_components) mv_movable_nodes.push_back(comp_p);
    m_filler_start_index = (int)mv_movable_nodes.size();
    for (Component* filler_p : mv_fillers) mv_movable_nodes.push_back(filler_p);
}

/** @brief: Reset all nodes and nets in preparation for the next iteration.
*/
void DataBase::iterationReset()
{
    // Per-node clears with no shared state, so threading them reorders nothing.
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)mv_movable_nodes.size(); i++) mv_movable_nodes[i]->iterationReset();
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)mv_fixed_components.size(); i++) mv_fixed_components[i]->iterationReset();
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)mv_iopad_nodes.size(); i++) mv_iopad_nodes[i]->iterationReset();
}


// For all Nets in the database, sort Positions (X descending)
void DataBase::sortPositionsByX()
{
    for (const auto& item : mm_nets)
        item.second->sortPositionsByX();
}

// For all Nets in the database, sort Positions (Y descending)
void DataBase::sortPositionsByY()
{
    for (const auto& item : mm_nets)
        item.second->sortPositionsByY();
}

float DataBase::computeTotalWirelength(string method, int max_net_degree)
{
    // max_net_degree matches XPlace's ignore_net_degree (net_mask): nets with more pins are
    // excluded from the HPWL metric so the reported number, the density-weight schedule's
    // delta_hpwl, and convergence all measure the SAME masked wirelength XPlace does.
    // mv_nets_by_name is mm_nets' own order, so summing in index order reproduces the original
    // map walk exactly. Masked-out nets contribute +0.0f, which is an exact no-op on the sum.
    return m_ordered_reduce.sum((int)mv_nets_by_name.size(), [&](int i) {
        Net* net_p = mv_nets_by_name[i];
        return (net_p->getDegree() <= max_net_degree) ? net_p->computeWirelength(method) : 0.0f;
    });
}

float DataBase::computeTotalComponentArea()
{
    return m_total_component_area;
}


    //  ======== LEF Callbacks ======== 
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
        /// @brief record the standard-cell row height. A DEF ROW carries only an origin, so for
        /// LEF/DEF input the height has to come from the CORE SITE it instantiates. Recorded in
        /// microns; readDesignFiles scales it to DBU with the macro sizes.
        void DataBase::lef_site_cbk(LefParser::lefiSite const& s) {
            if (!s.hasSize()) return;
            bool is_core = s.hasClass() && std::string(s.siteClass()) == "CORE";
            if (is_core || m_row_height == 0.0f) m_row_height = s.sizeY();
        }
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
            Component* new_comp_p = new Component(c.comp_name);
            MacroClass* macro = mm_macros[c.macro_name];
            new_comp_p->setMacroClass(macro);
            // XPlace-faithful status (file_lefdef_db.cpp:1565-1595): a PLACED cell is movable only if
            // its LEF CLASS is CORE or BLOCK; PLACED non-CORE/BLOCK cells (VIA/feedthrough/fill) are
            // pre-placed and treated as FIXED. UNPLACED/FIXED pass through unchanged.
            string status = c.status;
            if (status == "PLACED" && macro) {
                const string& cls = macro->getClass();
                if (cls != "CORE" && cls != "BLOCK") status = "FIXED";
            }
            new_comp_p->setPlacementStatus(status);
            new_comp_p->setNodePos(Position((float)c.origin[0], (float)c.origin[1]));
            // TODO: assert component is created correctly
            mm_components.emplace(std::make_pair(new_comp_p->getName(), new_comp_p));
        }

        void DataBase::resize_def_pin(int s) {}

        void DataBase::add_def_pin(DefParser::Pin const& p) {
            IOPad* new_iopad_p = new IOPad(p.pin_name);
            std::vector<int> bb = p.vBbox.front();
            new_iopad_p->setBoundingBox(bb[0], bb[1], bb[2], bb[3]);
            new_iopad_p->setPlacementStatus(p.status);
            new_iopad_p->setNodePos(Position((float)p.origin[0], (float)p.origin[1]));
            new_iopad_p->setDirection(p.direct); // primary input or output

            mm_iopads.emplace(std::make_pair(new_iopad_p->getName(), new_iopad_p));
        }

        void DataBase::resize_def_net(int s) {}

        void DataBase::add_def_net(DefParser::Net const& def_net) 
        // Create a new net, add it to the database
        {
            Net* new_net_p = new Net(def_net.net_name);
            for (auto net_pin : def_net.vNetPin)
            {
                if (net_pin.first == "PIN")
                {
                    IOPad* iopad_p = mm_iopads[net_pin.second];
                    assert(iopad_p != NULL && "PIN name points to nullptr while reading .DEF\n");
                    new_net_p->addNode(iopad_p);
                    iopad_p->addNet(new_net_p);
                }
                else // it is a component
                {
                    Component* comp_p = mm_components[net_pin.first];
                    assert(comp_p != NULL && "COMPONENT name points to nullptr while reading .DEF\n");
                    // Look up pin offset from the component's macro (LEF microns → DEF dbu)
                    Position pin_offset(0, 0);
                    MacroClass* macro = comp_p->getMacro();
                    if (macro && macro->hasPinOffset(net_pin.second)) {
                        pin_offset = macro->getPinOffset(net_pin.second);
                        float scale = (float)m_units_per_micron;
                        pin_offset.x *= scale;
                        pin_offset.y *= scale;
                    }
                    new_net_p->addNode(comp_p, pin_offset, net_pin.second);
                    comp_p->addNet(new_net_p);
                }
            }
            mm_nets.emplace(std::make_pair(new_net_p->getName(), new_net_p));
            mv_nets.push_back(new_net_p);

            int degree = new_net_p->getDegree();
            if (mmv_nets_by_degree.count(degree) == 0) {
                mmv_nets_by_degree.emplace(std::make_pair(degree, std::vector<Net*>()));
            }
            mmv_nets_by_degree[degree].push_back(new_net_p); //emplace_back(new_net) might work more effienctly
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
            Component* comp_p = new Component(name);
            string macro_name = "macro_" + std::to_string(width) + "_" + std::to_string(height);
            MacroClass* macro_p = mm_macros[macro_name];
            if(macro_p == NULL) {
                macro_p = new MacroClass(macro_name, width, height);
                mm_macros[macro_name] = macro_p;
            }
            comp_p->setMacroClass(macro_p);
            comp_p->setPlacementStatus(PlacementStatus::FIXED);
            comp_p->setNodePos(Position(0, 0));
            mm_components.emplace(std::make_pair(name, comp_p));
        }

        /// @brief add terminal_NI
        //void DataBase::add_bookshelf_terminal_NI(string&, int, int) {}
        /// @brief add node 
        void DataBase::add_bookshelf_node(string& name, int width, int height, bool notsurewhatthisboolisfor) {
            Component* new_comp_p = new Component(name);
            // for Bookshelf format, no macro classes are defined by the design
            // So we create macros named "macro_width_height"
            string macro_name = "macro_" + std::to_string(width) + "_" + std::to_string(height);
            MacroClass* macro_p = mm_macros[macro_name];
            if(macro_p == NULL) {
                macro_p = new MacroClass(macro_name, width, height);
                //mm_macros.emplace(std::make_pair(macro_name, macro_p));
                mm_macros[macro_name] = macro_p;
            }
            new_comp_p->setMacroClass(macro_p);
            new_comp_p->setPlacementStatus(PlacementStatus::UNPLACED);
            new_comp_p->setNodePos(Position(0, 0)); // default position (0, 0)
            mm_components.emplace(std::make_pair(new_comp_p->getName(), new_comp_p));
        }
        /// @brief add net 
        void DataBase::add_bookshelf_net(BookshelfParser::Net const& bookshelf_net) { 
            Net* new_net_p = new Net(bookshelf_net.net_name);
                //cout << "Add net: " << bookshelf_net.net_name << endl;
            for (BookshelfParser::NetPin net_pin : bookshelf_net.vNetPin)
            {
                //cout << "\tNetPin: " << net_pin.node_name << endl;
                if(mm_iopads.count(net_pin.node_name) > 0) {
                    IOPad* iopad_p = mm_iopads[net_pin.node_name];
                    new_net_p->addNode(iopad_p);
                    iopad_p->addNet(new_net_p);
                } else if(mm_components.count(net_pin.node_name)){ // it's a component
                    Component* comp_p = mm_components[net_pin.node_name];
                    // Bookshelf pin offsets are relative to the cell CENTER; sw_only node_pos is the
                    // lower-left corner, so shift by half-size to make the stored offset LL-relative
                    // (the NetPin.offset convention shared with the LEF/DEF path). Offsets and sizes
                    // are already in the same units here (no micron→dbu scaling for bookshelf).
                    Position pin_offset(0, 0);
                    pin_offset.x = comp_p->getXsize() / 2.0f + (float)net_pin.offset[0];
                    pin_offset.y = comp_p->getYsize() / 2.0f + (float)net_pin.offset[1];
                    new_net_p->addNode(comp_p, pin_offset, net_pin.pin_name);
                    comp_p->addNet(new_net_p);
                } else {
                    Logger::log_error("Node was not found while parsing bookshelf nets.");
                    exit(7);
                }
            }

            mm_nets.emplace(std::make_pair(new_net_p->getName(), new_net_p));
            mv_nets.push_back(new_net_p);
            
            // Add net to degree map for easy access
            int degree = new_net_p->getDegree();
            if (mmv_nets_by_degree.count(degree) == 0) {
                mmv_nets_by_degree.emplace(std::make_pair(degree, std::vector<Net*>()));
            }
            mmv_nets_by_degree[degree].push_back(new_net_p);

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
                m_row_height = (float)row.height;  // uniform across CoreRows; sizes fillers
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
            Component* comp_p = mm_components[name];
            assert(comp_p != NULL && "invalid component name!");
            comp_p->setNodePos(Position(x, y));
            comp_p->setOrientation(orientation);
            if(placement_status == "FIXED") {
                comp_p->setPlacementStatus(PlacementStatus::FIXED);
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
            Logger::log_detail("Bookshelf design: " + s);
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
            Logger::log_detail("End of Bookshelf design reading.");
        }
        
// Print info functions
void DataBase::printNodes() const
{
    printComponents();
    printIOPads();
}

void DataBase::printIOPads() const
{
    for(const auto& item : mm_iopads)
    {
        IOPad* iopad_p = item.second;
        Logger::log_info(iopad_p->getName() + iopad_p->next.node_pos.to_string());
        Logger::log_info("\tArea: " + std::to_string(iopad_p->getArea()) + "\tStatus: " + std::to_string(iopad_p->getStatus()));
    }
}

void DataBase::printComponents() const
{
    for(const auto& item : mm_components)
    {
        Component* comp_p = item.second;
        Logger::log_info(comp_p->getName() + comp_p->next.node_pos.to_string());
        Logger::log_info("\t" + comp_p->getMacro()->getName() + "\tArea: " + std::to_string(comp_p->getArea()) + "\tStatus: " + std::to_string(comp_p->getStatus()));
    }
}

void DataBase::printNets()
{
    int count = 0;
    for(const auto& item : mm_nets)
    {
        Net* net_p = item.second;
        Logger::log_info("NET: " + net_p->to_string());

        sortPositionsByX();
        string x_line = "X descending: ";
        for(auto node_p : net_p->getNodes())
            x_line += std::to_string(node_p->next.node_pos.x) + '\t';
        Logger::log_info(x_line);

        sortPositionsByY();
        string y_line = "Y descending: ";
        for(auto node_p : net_p->getNodes())
            y_line += std::to_string(node_p->next.node_pos.y) + '\t';
        Logger::log_info(y_line);

        if (++count > 100) return;
    }
}

void DataBase::printNetsByDegree() const
{
    Logger::log_info("&&& Nets by degree:");

    for (const auto& item : mmv_nets_by_degree)
    {
        Logger::log_info(std::to_string(item.second.size()) + " nets of degree " + std::to_string(item.first) + ".");
    }
}

void DataBase::printInfo()
{
    Table top;
    top.add_row({"Benchmark info"});

    // The fixed/movable/filler breakdown used to be four separate console lines during the load;
    // it belongs with the rest of the design's shape, so it lives here and the loose lines are DETAIL.
    int fixed_count = 0;
    for (const auto& item : mm_components)
        if (item.second->getStatus() == FIXED) fixed_count++;

    Table data;
    data.add_row(RowStream{} << "Benchmark" << getBenchmarkName());
    data.add_row(RowStream{} << "Macros" << mm_macros.size());
    data.add_row(RowStream{} << "IO Pads" << mm_iopads.size());
    data.add_row(RowStream{} << "Components" << mm_components.size());
    data.add_row(RowStream{} << "  fixed" << std::to_string(fixed_count)
        + " (" + std::to_string((int)(100.0f * m_total_fixed_area / m_die_area.getArea())) + "% of die)");
    data.add_row(RowStream{} << "  movable" << (int)mm_components.size() - fixed_count);
    data.add_row(RowStream{} << "Fillers" << mv_fillers.size());
    data.add_row(RowStream{} << "Nets" << mm_nets.size());
    data.add_row(RowStream{} << "Die Area" << m_die_area.getArea());
    data.add_row(RowStream{} << "Component area: " << computeTotalComponentArea());

    top.add_row({data});
    top.format().font_align(FontAlign::center);
    Logger::log_info(top);
}


void DataBase::printOverlaps()
{
    // for each node in db
    int count = 0;
    for (const auto& item : getComponents())
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
            overlaps.add_row(RowStream{} << b.bin_p->bb.getPos().to_string() << b.overlap);
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
        auto comp_p = item.second;
        out << "    - " << comp_p->getName() << " " << comp_p->getMacro()->getName() << "\n"
            << "      + " << "PLACED"/*comp->getStatus()*/ << " ( "
            << comp_p->getX() + m_die_shift.x << " " << comp_p->getY() + m_die_shift.y << " ) "
            << comp_p->getOrientation() << " ;\n";
    }
    out << "END COMPONENTS\n\n";
}

void DataBase::writePins(std::ofstream& out) const {
    out << "PINS " << mm_iopads.size() << " ;\n";
    for (const auto& item : mm_iopads) {
        IOPad* iopad_p = item.second;
        out << "    - " << iopad_p->getName() << " + NET " << iopad_p->getName()
            << "\n      + DIRECTION " << iopad_p->getDirection()
            << "\n";
        if (iopad_p->isPlaced()) {
            out << "      + PLACED "
                << " ( " << iopad_p->getX() << " " << iopad_p->getY() << " ) "
                << iopad_p->getOrientation() << "\n";
        }
        out << "      + LAYER " << iopad_p->getLayer()
            << iopad_p->getBoundingBox().getDEFstring()
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
        Net* net_p = item.second;
        out << "    - " << net_p->getName();
        int count = 0;
        for (const auto& pin : net_p->mv_pins) {
            if (dynamic_cast<IOPad*>(pin.node_p)) {
                out << " ( PIN " << pin.node_p->getName() << " )";
            } else {
                out << " ( " << pin.node_p->getName() << " " << pin.pin_name << " )";
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