
#include "DataBase.h"
#include "DesignReader.h"
#include "Logger.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

AIEPLACE_NAMESPACE_BEGIN

using namespace tabulate; // table types, scoped to this .cpp (not leaked via Logger.h)

DataBase::DataBase(fs::path input_dir)
    : m_input_dir(input_dir) {
    readInput();
}

void DataBase::readInput() {
    TIME_BLOCK("DataBase read input");
    Logger::log_detail("Reading design from directory: " + m_input_dir.string());
    m_max_x = 0;
    m_max_y = 0;

    readDesignFiles();
    flushParseInserts();
    m_component_index.clear();
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
        m_site_width *= scale;   // ditto
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
            // A utilization outside (0, 1] is a malformed file, not a design choice. Refuse to run:
            // mgc_matrix_mult_a's file ended `maximum_utilization=60% ` -- one trailing space, which
            // defeats the '%' test above, so std::stof("60% ") stopped at the '%' and returned 60.0.
            // The design placed at 100x its target density for weeks (29,779,040 fillers against
            // ~144,900 expected, a filler area 20x the die) and scored 3.27x XPlace. TODO #27.
            //
            // This exits rather than warns ON PURPOSE. The old code already printed the evidence --
            // "maximum_utilization=6000%" -- on every run, and a log line nobody reads is not a
            // guard. The benchmark dir is gitignored, so a fresh clone or re-download reintroduces
            // the bad file; this is what makes that recoverable instead of silently wrong.
            if (m_maximum_utilization <= 0.0f || m_maximum_utilization > 1.0f) {
                Logger::log_error("Malformed placement constraint in " + constraints_path.string()
                    + ": maximum_utilization parsed as " + std::to_string(m_maximum_utilization)
                    + ", expected a fraction in (0, 1]. Offending line: '" + line + "'");
                exit(1);
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

void DataBase::addComponent(Component* comp_p)
{
    // The index holds exactly what mm_components will (the first component under each name),
    // so it alone decides whether this one is queued. Meow.
    if (m_component_index.insert(comp_p))
        mv_pending_components.push_back(comp_p);
}

namespace {
// "macro_<width>_<height>", the name a Bookshelf node's MacroClass goes by -- built with
// to_chars, since std::to_string's vsnprintf was 3.5% of a large Bookshelf parse. Meow.
string bookshelfMacroName(int width, int height)
{
    char buf[40] = "macro_";
    char* end = std::to_chars(buf + 6, buf + sizeof buf, width).ptr;
    *end++ = '_';
    end = std::to_chars(end, buf + sizeof buf, height).ptr;
    return string(buf, end);
}

// target.emplace(name_of(v), v) for every queued v, in order -- but sorted first so each insert
// lands at the end of the tree (O(1) with the hint); emplace_hint, like emplace, keeps the value
// already under an equal name, and the arrival number keeps equal names in arrival order, so the
// first one wins exactly as before. The sort moves 24-byte keys, not strings: the first 16 name
// bytes, big-endian and zero-padded, order exactly as std::string's byte compare does (DEF names
// like "h3a/o99999" tie on 8), and the full names are only compared when those tie. Meow.
template <typename V, typename NameOf>
void insertSorted(map<string, V>& target, std::vector<V>& pending, NameOf name_of)
{
    struct Keyed { uint64_t high, low; uint32_t arrival; };
    auto prefix = [](std::string_view name, size_t from) {
        uint64_t bytes = 0;
        for (size_t b = from; b < from + 8; b++)
            bytes = (bytes << 8) | (b < name.size() ? (unsigned char)name[b] : 0);
        return bytes;
    };
    std::vector<Keyed> keyed(pending.size());
    for (size_t i = 0; i < pending.size(); i++) {
        std::string_view name = name_of(pending[i]);
        keyed[i] = {prefix(name, 0), prefix(name, 8), (uint32_t)i};
    }
    std::sort(keyed.begin(), keyed.end(), [&](const Keyed& a, const Keyed& b) {
        if (a.high != b.high) return a.high < b.high;
        if (a.low != b.low) return a.low < b.low;
        int order = name_of(pending[a.arrival]).compare(name_of(pending[b.arrival]));
        return order != 0 ? order < 0 : a.arrival < b.arrival;
    });
    for (const Keyed& k : keyed)
        target.emplace_hint(target.end(), string(name_of(pending[k.arrival])), pending[k.arrival]);
    pending = {};
}

// Node::addNet for the parse. A cell's net list would grow 1 -> 2 -> 4 by reallocation (8% of a
// large parse); a cell sits on ~4 nets, so start it at 4. Only the capacity differs. Meow.
void addParsedNet(Node* node_p, Net* net_p)
{
    std::vector<Net*>& nets = node_p->getNets();
    if (nets.capacity() == 0) nets.reserve(4);
    node_p->addNet(net_p);
}

} // namespace

void DataBase::flushParseInserts()
{
    // Two independent maps, so two threads: neither result depends on the other. Meow.
    #pragma omp parallel sections
    {
        #pragma omp section
        insertSorted(mm_components, mv_pending_components, [](Component* c) { return std::string_view(c->getName()); });
        #pragma omp section
        insertSorted(mm_nets, mv_pending_nets, [](Net* n) { return std::string_view(n->m_name); });
    }
}

namespace {
// index.find for `count` names at once, with each lookup's cache misses issued before any is
// waited on (NameIndex::prefetch): a net's pins are resolved in about the time of one. Meow.
template <typename NameOf>
void findAll(const NameIndex<Component>& index, size_t count, NameOf name_of, Component** found)
{
    static thread_local std::vector<uint64_t> hashes;
    hashes.resize(count);
    for (size_t i = 0; i < count; i++) { hashes[i] = NameIndex<Component>::hashOf(name_of(i)); index.prefetch(hashes[i]); }
    for (size_t i = 0; i < count; i++) index.prefetchObject(hashes[i]);
    for (size_t i = 0; i < count; i++) found[i] = index.find(name_of(i), hashes[i]);
}
} // namespace

bool DataBase::parseLefFile(const fs::path& lef_file) { return readLefFile(lef_file, *this); }
bool DataBase::parseDefFile(const fs::path& def_file) { return readDefFile(def_file, *this); }
bool DataBase::parseBookshelfAux(const fs::path& aux_file) { return readBookshelfAux(aux_file, *this); }

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
        success = parseLefFile(file);

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
    if(def_file.empty())
    {
        // Without this the parser is handed an empty path and reports it as one (TODO #17).
        std::string found;
        for(const fs::path & f : def_files)
            found += (found.empty() ? "" : ", ") + f.filename().string();
        Logger::log_error("No 'floorplan.def' in " + m_input_dir.string() +
                          "; that is the only .def name readDEF() accepts. Found: " + found);
        return false;
    }

    Logger::log_detail("Begin parsing .DEF design...");
    bool success = parseDefFile(def_file);

    if (success) {
        if (m_num_def_regions > 0 || m_num_def_groups > 0) {
            Logger::log_warning("DEF declares " + std::to_string(m_num_def_regions) + " REGIONS and "
                + std::to_string(m_num_def_groups) + " GROUPS (fence regions). These are IGNORED: "
                "the design is placed unconstrained, so the result is NOT a legal ISPD2015 solution "
                "and is comparable only against another tool that also ignores them (XPlace does). "
                "Measure the violation with tools/fence_check.py. TODO #26.");
        }
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
    bool success = parseBookshelfAux(aux_files[0]);

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

/**
 * @brief Phase 2: freeze every movable macro where global placement left it.
 *
 * Mirrors XPlace's optimizer reset (run_placement_nesterov.py:171-200), which moves the macros
 * out of the movable set so the second pass spreads standard cells against them as obstacles.
 *
 * Two pieces of derived state depend on PlacementStatus and MUST be rebuilt here — this is the
 * one place in the codebase that violates buildNodeIndex()'s "no status ever changes" invariant:
 *   - the cached area split (the macros' area moves movable -> fixed), and
 *   - the flat node index every threaded loop iterates.
 *
 * The macros keep their is_movable_macro TAG (it still describes what they are, and is what the
 * DEF writer and the visualizer use to identify them); every consumer tests FIXED first, so the
 * tag is inert once frozen. A frozen macro deposits full density and is then capped per bin at
 * target_density by the existing fixed-density clamp — which is exactly what macro_deposits_target_density
 * was doing for it by hand while it was movable.
 *
 * @return how many macros were frozen.
 */
int DataBase::freezeMovableMacros()
{
    int frozen = 0;
    for (const auto& item : mm_components) {
        Component* comp_p = item.second;
        if (comp_p->getStatus() == FIXED || !comp_p->isMovableMacro()) continue;
        comp_p->setPlacementStatus(PlacementStatus::FIXED);
        // Collapse all four state fields onto the committed position. This is NOT bookkeeping:
        // once frozen these macros leave getMovableComponents() and are never stepped or
        // re-initialised again, so whatever is in probe_pos at this instant is what they keep
        // forever — and computeNodeFootprint deposits at the PROBE position, so a stale probe_pos
        // lands the macro's density somewhere it no longer is.
        //
        // The caller normally arrives here straight from restoreBestPlacement(), which since
        // 2026-08-26 writes node_pos AND probe_pos to the same snapshot, making this a no-op. It
        // is NOT redundant: beginPhase2() skips that restore entirely when phase 1
        // recorded no best solution, and then node_pos is u_k while probe_pos is v_k. Keep it.
        comp_p->initializeState(comp_p->next.node_pos);
        frozen++;
    }
    computeAreaBreakdown();   // macro area is fixed area now
    buildNodeIndex();         // mv_movable_* / mv_fixed_* / mv_movable_nodes all shift
    return frozen;
}

/**
 * @brief Phase 2: discard the phase-1 fillers and size a fresh set in the phase-2 frame.
 *
 * The filler population is not a property of the design, it is a property of the phase: once the
 * macros are fixed their area leaves the standard-cell budget entirely (it is fixed area, not
 * movable-macro area), so the whitespace the fillers must represent is different. XPlace does the
 * same, recomputing __total_mov_area_without_filler__ across the restart.
 *
 * Must run AFTER freezeMovableMacros() so addFillers sees the phase-2 statuses.
 *
 * @return the effective target density for phase 2 (addFillers may raise it, as in phase 1).
 */
float DataBase::rebuildFillers(float target_utilization)
{
    for (Component* filler_p : mv_fillers) delete filler_p;
    mv_fillers.clear();
    // addFillers installs a fresh "filler" MacroClass; drop the phase-1 one first, since
    // mm_macros::emplace would keep the stale entry and leak the new one.
    auto stale = mm_macros.find("filler_macroclass");
    if (stale != mm_macros.end()) { delete stale->second; mm_macros.erase(stale); }

    float effective_density = addFillers(target_utilization);
    buildNodeIndex();
    return effective_density;
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

float DataBase::computeTotalWirelength(string method, int max_net_degree, bool at_probe)
{
    // max_net_degree matches XPlace's ignore_net_degree (net_mask): nets with more pins are
    // excluded from the HPWL metric so the reported number, the density-weight schedule's
    // delta_hpwl, and convergence all measure the SAME masked wirelength XPlace does.
    // mv_nets_by_name is mm_nets' own order, so summing in index order reproduces the original
    // map walk exactly. Masked-out nets contribute +0.0f, which is an exact no-op on the sum.
    return m_ordered_reduce.sum((int)mv_nets_by_name.size(), [&](int i) {
        Net* net_p = mv_nets_by_name[i];
        return (net_p->getDegree() <= max_net_degree) ? net_p->computeWirelength(method, at_probe) : 0.0f;
    });
}

float DataBase::computeTotalComponentArea()
{
    return m_total_component_area;
}


    //  ======== LEF Callbacks ========
        /// @brief record the standard-cell row height and site width. A DEF ROW carries only an
        /// origin, so for LEF/DEF input both have to come from the CORE SITE it instantiates.
        /// Recorded in microns; readDesignFiles scales them to DBU with the macro sizes.
        void DataBase::lef_site_cbk(const LefSite& s) {
            if (!s.has_size) return;
            bool is_core = s.has_class && s.site_class == "CORE";
            if (is_core || m_row_height == 0.0f) m_row_height = s.size_y;
            if (is_core || m_site_width == 0.0f) m_site_width = s.size_x;
        }
        void DataBase::lef_macrobegin_cbk(const string& n) {
            // Create macro early so lef_pin_cbk (which fires before lef_macro_cbk) can add pin offsets
            MacroClass* new_macro = new MacroClass(n);
            mm_macros.emplace(std::make_pair(n, new_macro));
            m_current_lef_macro = new_macro;
        }
        void DataBase::lef_macro_cbk(const LefMacro& m) {
            // Finalize macro size (pins have already been added by lef_pin_cbk)
            m_current_lef_macro->setSize(m.size_x, m.size_y);
            // Record LEF CLASS so add_def_component can apply XPlace's PLACED->fixed rule.
            if (m.has_class) m_current_lef_macro->setClass(m.macro_class);

            m_current_lef_macro = nullptr;
        }

        // Called for each PIN within the current MACRO block.
        // Pin offset = center of the first RECT in the first port. Meow.
        void DataBase::lef_pin_cbk(const LefPin& p) {
            if (!m_current_lef_macro) return;

            // Skip power/ground pins — they don't appear in signal nets
            if (p.use == "POWER" || p.use == "GROUND") return;

            if (!p.has_rect) return;
            float cx = (float)(p.rect_xl + p.rect_xh) / 2.0f;
            float cy = (float)(p.rect_yl + p.rect_yh) / 2.0f;
            m_current_lef_macro->addPinOffset(p.name, Position(cx, cy));
        }

        ///==== DEF Callbacks ===
        void DataBase::set_def_unit(int u) { m_units_per_micron = u; }
        void DataBase::set_def_design(const string& d) { m_design_name = d; }

        void DataBase::set_def_diearea(int xl, int yl, int xh, int yh)
        {
            m_die_area = Box(Position((position_type)xl, (position_type)yl),
                             Position((position_type)xh, (position_type)yh));
        }

        // Create the components (Nodes), in file order. Building each one only reads mm_macros, so
        // that runs in parallel; what has to happen in order -- registering each name, and the
        // null entry mm_macros[] leaves for a macro the LEF never defined -- runs after. Meow.
        void DataBase::add_def_components(const std::vector<DefComponent>& components)
        {
          std::vector<Component*> created(components.size());
          #pragma omp parallel for schedule(dynamic, 1024) if(components.size() >= 4096)
          for (long i = 0; i < (long)components.size(); i++) {
            const DefComponent& c = components[i];
            Component* new_comp_p = new Component(string(c.name));
            auto found = mm_macros.find(string(c.macro_name));
            MacroClass* macro = found == mm_macros.end() ? nullptr : found->second;
            new_comp_p->setMacroClass(macro);
            // XPlace-faithful status (file_lefdef_db.cpp:1565-1595): a PLACED cell is movable only if
            // its LEF CLASS is CORE or BLOCK; PLACED non-CORE/BLOCK cells (VIA/feedthrough/fill) are
            // pre-placed and treated as FIXED. UNPLACED/FIXED pass through unchanged.
            string status(c.status);
            if (status == "PLACED" && macro) {
                const string& cls = macro->getClass();
                if (cls != "CORE" && cls != "BLOCK") status = "FIXED";
            }
            new_comp_p->setPlacementStatus(status);
            new_comp_p->setNodePos(Position((float)c.x, (float)c.y));
            // TODO: assert component is created correctly
            created[i] = new_comp_p;
          }
          for (size_t i = 0; i < components.size(); i++) {
            if (!created[i]->getMacro()) mm_macros.emplace(string(components[i].macro_name), nullptr);
            addComponent(created[i]);
          }
        }

        void DataBase::add_def_pin(const DefPin& p) {
            IOPad* new_iopad_p = new IOPad(p.name);
            new_iopad_p->setBoundingBox(p.bbox[0], p.bbox[1], p.bbox[2], p.bbox[3]);
            new_iopad_p->setPlacementStatus(p.status);
            new_iopad_p->setNodePos(Position((float)p.x, (float)p.y));
            new_iopad_p->setDirection(p.direction); // primary input or output

            mm_iopads.emplace(std::make_pair(new_iopad_p->getName(), new_iopad_p));
        }

        // Create the nets, in file order. Each component pin's node and pin offset only read the
        // index and the macros, so they are worked out first, in parallel; the nets are then
        // built one at a time exactly as one call per net would. Meow.
        void DataBase::add_def_nets(const std::vector<DefNet>& nets)
        {
          std::vector<size_t> first_pin(nets.size() + 1, 0);
          for (size_t i = 0; i < nets.size(); i++) first_pin[i + 1] = first_pin[i] + nets[i].num_pins;
          std::vector<Component*> net_components(first_pin.back());
          std::vector<Position> pin_offsets(first_pin.back(), Position(0, 0));
          #pragma omp parallel for schedule(dynamic, 256) if(nets.size() >= 4096)
          for (long i = 0; i < (long)nets.size(); i++) {
            const DefNet& def_net = nets[i];
            Component** components = &net_components[first_pin[i]];
            findAll(m_component_index, def_net.num_pins, [&](size_t k) { return def_net.pins[k].first; }, components);
            for (size_t pin_i = 0; pin_i < def_net.num_pins; pin_i++) {
                if (def_net.pins[pin_i].first == "PIN" || !components[pin_i]) continue;
                // Look up pin offset from the component's macro (LEF microns → DEF dbu)
                Position& pin_offset = pin_offsets[first_pin[i] + pin_i];
                MacroClass* macro = components[pin_i]->getMacro();
                string pin_name(def_net.pins[pin_i].second);
                if (macro && macro->hasPinOffset(pin_name)) {
                    pin_offset = macro->getPinOffset(pin_name);
                    float scale = (float)m_units_per_micron;
                    pin_offset.x *= scale;
                    pin_offset.y *= scale;
                }
            }
          }

          for (size_t net_i = 0; net_i < nets.size(); net_i++) {
            const DefNet& def_net = nets[net_i];
            Net* new_net_p = new Net(string(def_net.name));
            new_net_p->mv_nodes.reserve(def_net.num_pins);
            new_net_p->mv_pins.reserve(def_net.num_pins);
            for (size_t pin_i = 0; pin_i < def_net.num_pins; pin_i++)
            {
                const DefNetPin& net_pin = def_net.pins[pin_i];
                if (net_pin.first == "PIN")
                {
                    IOPad* iopad_p = mm_iopads[string(net_pin.second)];
                    assert(iopad_p != NULL && "PIN name points to nullptr while reading .DEF\n");
                    new_net_p->addNode(iopad_p);
                    iopad_p->addNet(new_net_p);
                }
                else // it is a component
                {
                    Component* comp_p = net_components[first_pin[net_i] + pin_i];
                    assert(comp_p != NULL && "COMPONENT name points to nullptr while reading .DEF\n");
                    new_net_p->addNode(comp_p, pin_offsets[first_pin[net_i] + pin_i], string(net_pin.second));
                    addParsedNet(comp_p, new_net_p);
                }
            }
            mv_pending_nets.push_back(new_net_p);   // -> mm_nets, see flushParseInserts. Meow.
            mv_nets.push_back(new_net_p);

            int degree = new_net_p->getDegree();
            if (mmv_nets_by_degree.count(degree) == 0) {
                mmv_nets_by_degree.emplace(std::make_pair(degree, std::vector<Net*>()));
            }
            mmv_nets_by_degree[degree].push_back(new_net_p); //emplace_back(new_net) might work more effienctly
          }
        }

        // Fence regions (DEF REGIONS + GROUPS) are DISCARDED, not implemented. The 9 ISPD2015
        // designs that carry them are placed unconstrained, and on those our placement puts
        // 59-94% of the constrained cells outside their fence (vck5000/tools/fence_check.py).
        // XPlace does the same -- it refuses the constraint outright and its released
        // `ispd2015_fix` data strips it -- so a comparison against XPlace stays fair, but the
        // result is NOT a legal ISPD2015 solution. Counted here so the run log says which
        // designs it happened on (TODO #26).
        // The warning itself is emitted by readDEF(), once the whole file is read. Meow.
        void DataBase::resize_def_region(int n) { m_num_def_regions = n; }
        void DataBase::resize_def_group(int n) { m_num_def_groups = n; }


    // *******************************************************************************
        // BOOKSHELF callbacks
        /// @brief add .nodes entries: cells, and terminals (fixed macros or IO pads) as FIXED Components. Meow.
        // Each node's MacroClass is resolved first, in file order, so MacroClasses are made exactly
        // as before; the components are then built in parallel and registered in order. Meow.
        void DataBase::add_bookshelf_nodes(const std::vector<BookshelfNode>& nodes) {
            std::vector<MacroClass*> macros(nodes.size());
            std::unordered_map<uint64_t, MacroClass*> by_size;   // (width, height) -> its MacroClass. Meow.
            for (size_t i = 0; i < nodes.size(); i++) {
                uint64_t key = ((uint64_t)(uint32_t)nodes[i].width << 32) | (uint32_t)nodes[i].height;
                auto [it, first_seen] = by_size.emplace(key, nullptr);
                if (first_seen) {
                    // for Bookshelf format, no macro classes are defined by the design
                    // So we create macros named "macro_width_height"
                    string macro_name = bookshelfMacroName(nodes[i].width, nodes[i].height);
                    MacroClass* macro_p = mm_macros[macro_name];
                    if(macro_p == NULL) {
                        macro_p = new MacroClass(macro_name, nodes[i].width, nodes[i].height);
                        mm_macros[macro_name] = macro_p;
                    }
                    it->second = macro_p;
                }
                macros[i] = it->second;
            }
            std::vector<Component*> created(nodes.size());
            #pragma omp parallel for schedule(dynamic, 1024) if(nodes.size() >= 4096)
            for (long i = 0; i < (long)nodes.size(); i++) {
                Component* comp_p = new Component(string(nodes[i].name));
                comp_p->setMacroClass(macros[i]);
                // a terminal is a fixed macro or IO pad, kept as a FIXED Component. Meow.
                comp_p->setPlacementStatus(nodes[i].terminal ? PlacementStatus::FIXED : PlacementStatus::UNPLACED);
                comp_p->setNodePos(Position(0, 0)); // default position (0, 0)
                created[i] = comp_p;
            }
            for (Component* comp_p : created) addComponent(comp_p);
        }
        /// @brief add net 
        // Every pin's node is looked up first, in parallel -- the lookups only read the index --
        // and the nets are then built one at a time in file order, exactly as one call per net
        // would. Meow.
        void DataBase::add_bookshelf_nets(const std::vector<BookshelfNet>& nets) {
          std::vector<size_t> first_pin(nets.size() + 1, 0);
          for (size_t i = 0; i < nets.size(); i++) first_pin[i + 1] = first_pin[i] + nets[i].num_pins;
          std::vector<Component*> net_components(first_pin.back());
          #pragma omp parallel for schedule(dynamic, 256) if(nets.size() >= 4096)   // a lone net: no thread wake-up. Meow.
          for (long i = 0; i < (long)nets.size(); i++)
              findAll(m_component_index, nets[i].num_pins, [&](size_t k) { return nets[i].pins[k].node_name; },
                      &net_components[first_pin[i]]);

          for (size_t net_i = 0; net_i < nets.size(); net_i++) {
            const BookshelfNet& bookshelf_net = nets[net_i];
            Component* const* components = &net_components[first_pin[net_i]];
            Net* new_net_p = new Net(string(bookshelf_net.name));
            new_net_p->mv_nodes.reserve(bookshelf_net.num_pins);
            new_net_p->mv_pins.reserve(bookshelf_net.num_pins);
            for (size_t pin_i = 0; pin_i < bookshelf_net.num_pins; pin_i++)
            {
                const BookshelfNetPin& net_pin = bookshelf_net.pins[pin_i];
                Component* comp_p = components[pin_i];
                // Bookshelf never has IO pads (terminals are components), so the map is empty; the
                // test keeps the old precedence without building a string per pin to probe it. Meow.
                if(!mm_iopads.empty() && mm_iopads.count(string(net_pin.node_name)) > 0) {
                    IOPad* iopad_p = mm_iopads[string(net_pin.node_name)];
                    new_net_p->addNode(iopad_p);
                    iopad_p->addNet(new_net_p);
                } else if(comp_p){ // it's a component
                    // Bookshelf pin offsets are relative to the cell CENTER; sw_only node_pos is the
                    // lower-left corner, so shift by half-size to make the stored offset LL-relative
                    // (the NetPin.offset convention shared with the LEF/DEF path). Offsets and sizes
                    // are already in the same units here (no micron→dbu scaling for bookshelf).
                    Position pin_offset(0, 0);
                    pin_offset.x = comp_p->getXsize() / 2.0f + (float)net_pin.offset_x;
                    pin_offset.y = comp_p->getYsize() / 2.0f + (float)net_pin.offset_y;
                    new_net_p->addNode(comp_p, pin_offset, string(net_pin.pin_name));
                    addParsedNet(comp_p, new_net_p);
                } else {
                    Logger::log_error("Node was not found while parsing bookshelf nets.");
                    exit(7);
                }
            }

            mv_pending_nets.push_back(new_net_p);   // -> mm_nets, see flushParseInserts. Meow.
            mv_nets.push_back(new_net_p);
            
            // Add net to degree map for easy access
            int degree = new_net_p->getDegree();
            if (mmv_nets_by_degree.count(degree) == 0) {
                mmv_nets_by_degree.emplace(std::make_pair(degree, std::vector<Net*>()));
            }
            mmv_nets_by_degree[degree].push_back(new_net_p);
          }
         }

        /// @brief add row — accumulate the .scl core-row bounding box (the die comes from
        /// the rows, not the terminal coordinates). A row spans [SubrowOrigin, +NumSites*
        /// SiteSpacing] in x and [Coordinate, +Height] in y.
        void DataBase::add_bookshelf_row(const BookshelfRow& row) {
            long spacing = row.site_spacing > 0 ? row.site_spacing : row.site_width;
            long x0 = row.origin_x;
            long x1 = x0 + (long)row.site_num * spacing;
            long y0 = row.origin_y;
            long y1 = y0 + row.height;
            if (m_row_count == 0) {
                m_row_xmin = x0; m_row_xmax = x1;
                m_row_ymin = y0; m_row_ymax = y1;
                m_row_height = (float)row.height;  // uniform across CoreRows; sizes fillers
                m_site_width = (float)row.site_width;  // .scl Sitewidth; 1 on every ISPD2005/MMS design
            } else {
                m_row_xmin = std::min(m_row_xmin, x0);
                m_row_xmax = std::max(m_row_xmax, x1);
                m_row_ymin = std::min(m_row_ymin, y0);
                m_row_ymax = std::max(m_row_ymax, y1);
            }
            m_row_count++;
        }
        /// @brief set node position — all bookshelf nodes (terminals + cells) are now Components
        /// Lookups first, in parallel; then applied in file order, so a node placed twice keeps
        /// its last placement exactly as before. Meow.
        void DataBase::set_bookshelf_node_positions(const std::vector<BookshelfPlacement>& placements) {
          constexpr long LOOKUP_BATCH = 64;
          std::vector<Component*> components(placements.size());
          #pragma omp parallel for schedule(dynamic, 64) if(placements.size() >= 4096)
          for (long first = 0; first < (long)placements.size(); first += LOOKUP_BATCH)
              findAll(m_component_index, std::min(LOOKUP_BATCH, (long)placements.size() - first),
                      [&](size_t k) { return placements[first + k].name; }, &components[first]);

          for (size_t i = 0; i < placements.size(); i++) {
            const BookshelfPlacement& placement = placements[i];
            double x = placement.x, y = placement.y;
            Component* comp_p = components[i];
            assert(comp_p != NULL && "invalid component name!");
            comp_p->setNodePos(Position(x, y));
            comp_p->setOrientation(string(placement.orient));
            if(placement.status == "FIXED") {
                comp_p->setPlacementStatus(PlacementStatus::FIXED);
                // Bookshelf format doesn't explicitly give die area,
                // so we infer it from the outermost fixed terminal coordinates
                if(x > m_max_x) m_max_x = x;
                if(y > m_max_y) m_max_y = y;
            }
          }
        }
        /// @brief set design name
        void DataBase::set_bookshelf_design(const string& s) {
            m_design_name = s;
            Logger::log_detail("Bookshelf design: " + s);
        }

        /// @brief a callback when a bookshelf file reaches to the end 
        void DataBase::bookshelf_end() {
            flushParseInserts();   // the die shift below walks mm_components. Meow.
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

bool DataBase::writeDEF(const fs::path& output_path) const
{
    fs::path output_filename = output_path / (m_design_name + ".def");
    std::ofstream out(output_filename);
    if (!out.is_open()) {
        Logger::log_error("DEF write: invalid output filename: " + output_filename.string());
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