#include "flat_database.hpp"

#include <climits>
#include <cmath>
#include <cstring>
#include <fstream>

namespace sw_flat {

using namespace AIEplace;

// ---- NodeIndex. Meow.

int NodeIndex::find(std::string_view name, uint64_t hash) const
{
    if (slots.empty()) return -1;
    for (size_t i = hash & mask; ; i = (i + 1) & mask) {
        const Slot& slot = slots[i];
        if (slot.id < 0) return -1;
        if (slot.hash == hash && names[slot.id] == name) return slot.id;
    }
}

int NodeIndex::insert(uint64_t hash, int new_id)
{
    if (2 * (size + 1) > slots.size()) grow();
    const std::string& name = names[new_id];
    for (size_t i = hash & mask; ; i = (i + 1) & mask) {
        Slot& slot = slots[i];
        if (slot.id < 0) { slot = {hash, new_id}; size++; return new_id; }
        if (slot.hash == hash && names[slot.id] == name) return slot.id;
    }
}

void NodeIndex::grow()
{
    std::vector<Slot> old = std::move(slots);
    slots.assign(old.empty() ? 1024 : 2 * old.size(), Slot{0, -1});
    mask = slots.size() - 1;
    for (const Slot& slot : old) {
        if (slot.id < 0) continue;
        size_t i = slot.hash & mask;
        while (slots[i].id >= 0) i = (i + 1) & mask;
        slots[i] = slot;
    }
}

namespace {

// index.find for every name of one net, each slot's cache miss started before any is waited on
// (DataBase's findAll). Runs inside the parallel net loops, hence the per-thread scratch. Meow.
template <typename IndexOf, typename NameOf>
void find_all(size_t count, IndexOf index_of, NameOf name_of, int32_t* found)
{
    static thread_local std::vector<uint64_t> hashes;
    hashes.resize(count);
    for (size_t k = 0; k < count; k++) { hashes[k] = NodeIndex::hash_of(name_of(k)); index_of(k).prefetch(hashes[k]); }
    for (size_t k = 0; k < count; k++) index_of(k).prefetch_name(hashes[k]);
    for (size_t k = 0; k < count; k++) found[k] = index_of(k).find(name_of(k), hashes[k]);
}

constexpr long PARALLEL_MIN = 4096;   // a smaller batch is not worth waking the threads for

} // namespace

int FlatDatabase::add_node(NodeIndex& index, uint64_t hash, std::string_view name, NodeKind kind, float x, float y,
                       float width, float height, int macro)
{
    design.node_name.emplace_back(name);
    int id = design.num_nodes() - 1;
    if (index.insert(hash, id) != id) { design.node_name.pop_back(); return -1; }
    design.node_kind.push_back(kind);
    design.node_x.push_back(x);
    design.node_y.push_back(y);
    design.node_width.push_back(width);
    design.node_height.push_back(height);
    node_macro.push_back(macro);
    return id;
}

std::pair<int, int> FlatDatabase::append_nets(const std::vector<size_t>& first_pin)
{
    const int first_net = design.num_nets();
    const int pin_base = (int)design.pin_node.size();
    const size_t count = first_pin.size() - 1;
    design.net_name.resize(first_net + count);
    design.net_first_pin.resize(first_net + count + 1);
    for (size_t i = 0; i < count; i++) design.net_first_pin[first_net + i + 1] = pin_base + (int32_t)first_pin[i + 1];
    const size_t num_pins = pin_base + first_pin[count];
    design.pin_node.resize(num_pins);
    design.pin_offset_x.resize(num_pins);
    design.pin_offset_y.resize(num_pins);
    return {first_net, pin_base};
}

template <typename Net>
static std::vector<size_t> batch_first_pin(const std::vector<Net>& nets)
{
    std::vector<size_t> first_pin(nets.size() + 1, 0);
    for (size_t i = 0; i < nets.size(); i++) first_pin[i + 1] = first_pin[i] + nets[i].num_pins;
    return first_pin;
}

// ---- LEF. Sizes and pin offsets stay in microns until finish_lefdef. Meow.

void FlatDatabase::lef_site_cbk(const LefSite& s)
{
    if (!s.has_size) return;
    bool is_core = s.has_class && s.site_class == "CORE";
    if (is_core || design.row_height == 0.0f) design.row_height = s.size_y;
    if (is_core || design.site_width == 0.0f) design.site_width = s.size_x;
}

void FlatDatabase::lef_macrobegin_cbk(const std::string& name)
{
    if (lef_macro_id.count(name)) {
        discarded_macro = LefMacroFacts();
        current_macro = &discarded_macro;
        return;
    }
    lef_macro_id.emplace(name, (int)lef_macros.size());
    lef_macros.emplace_back();
    current_macro = &lef_macros.back();
}

void FlatDatabase::lef_macro_cbk(const LefMacro& m)
{
    if (!current_macro) return;
    current_macro->width = (float)m.size_x;
    current_macro->height = (float)m.size_y;
    if (m.has_class) current_macro->lef_class = m.macro_class;
    current_macro = nullptr;
}

void FlatDatabase::lef_pin_cbk(const LefPin& p)
{
    if (!current_macro) return;
    if (p.use == "POWER" || p.use == "GROUND") return;
    if (!p.has_rect) return;
    // A repeated PIN name overwrites, as MacroClass::addPinOffset does. Meow.
    current_macro->pin_offset[p.name] = {(float)(p.rect_xl + p.rect_xh) / 2.0f, (float)(p.rect_yl + p.rect_yh) / 2.0f};
}

// ---- DEF. Meow.

void FlatDatabase::set_def_diearea(int xl, int yl, int xh, int yh)
{
    design.die_xl = (float)xl; design.die_yl = (float)yl;
    design.die_xh = (float)xh; design.die_yh = (float)yh;
}

// Hashes and LEF macro lookups only read, so they run in parallel; registering each name keeps the
// first under it, so that runs in file order. Meow.
void FlatDatabase::add_def_components(const std::vector<DefComponent>& components)
{
    const long count = (long)components.size();
    std::vector<uint64_t> hashes(count);
    std::vector<int> macros(count);
    #pragma omp parallel for schedule(static) if(count >= PARALLEL_MIN)
    for (long i = 0; i < count; i++) {
        hashes[i] = NodeIndex::hash_of(components[i].name);
        auto macro = lef_macro_id.find(components[i].macro_name);
        macros[i] = macro == lef_macro_id.end() ? -1 : macro->second;
    }
    for (long i = 0; i < count; i++) {
        const DefComponent& c = components[i];
        if (macros[i] < 0) {
            if (component_id.find(c.name, hashes[i]) >= 0) continue;   // a repeat: DataBase drops it unread
            return fail("component " + std::string(c.name) + " of undefined LEF macro " + std::string(c.macro_name));
        }
        const std::string& lef_class = lef_macros[macros[i]].lef_class;
        bool placed_fixed = c.status == "PLACED" && lef_class != "CORE" && lef_class != "BLOCK";
        NodeKind kind = c.status == "FIXED" || placed_fixed ? NodeKind::FIXED : NodeKind::CELL;
        int id = add_node(component_id, hashes[i], c.name, kind, (float)c.x, (float)c.y, 0, 0, macros[i]);
        if (id < 0) continue;
        if (placed_fixed) placed_fixed_nodes.push_back(id);
        if (c.status == "COVER") cover_nodes.push_back(id);
    }
}

void FlatDatabase::add_def_pin(const DefPin& p)
{
    float width = std::fabs((float)p.bbox[0] - (float)p.bbox[2]);
    float height = std::fabs((float)p.bbox[1] - (float)p.bbox[3]);
    add_node(iopad_id, NodeIndex::hash_of(p.name), p.name, NodeKind::IO_PAD, (float)p.x, (float)p.y, width, height, -1);
}

// Each pin's node and offset only read the indexes and the macros, so whole nets resolve in
// parallel into arrays sized up front. Meow.
void FlatDatabase::add_def_nets(const std::vector<DefNet>& nets)
{
    const std::vector<size_t> first_pin = batch_first_pin(nets);
    const auto [first_net, pin_base] = append_nets(first_pin);
    const float scale = (float)design.units_per_micron;
    long first_bad_net = LONG_MAX;
    #pragma omp parallel for schedule(dynamic, 256) reduction(min : first_bad_net) if((long)nets.size() >= PARALLEL_MIN)
    for (long i = 0; i < (long)nets.size(); i++) {
        const DefNet& net = nets[i];
        const size_t pin0 = pin_base + first_pin[i];
        design.net_name[first_net + i].assign(net.name);
        auto is_pad = [&](size_t k) { return net.pins[k].first == "PIN"; };
        find_all(net.num_pins, [&](size_t k) -> const NodeIndex& { return is_pad(k) ? iopad_id : component_id; },
                 [&](size_t k) { return is_pad(k) ? net.pins[k].second : net.pins[k].first; }, &design.pin_node[pin0]);
        for (size_t k = 0; k < net.num_pins; k++) {
            const int node = design.pin_node[pin0 + k];
            float offset_x = 0, offset_y = 0;
            if (node < 0) { first_bad_net = std::min(first_bad_net, i); continue; }
            if (!is_pad(k)) {
                const auto& offsets = lef_macros[node_macro[node]].pin_offset;
                auto offset = offsets.find(net.pins[k].second);
                if (offset != offsets.end()) {
                    offset_x = offset->second.first * scale;
                    offset_y = offset->second.second * scale;
                }
            }
            design.pin_offset_x[pin0 + k] = offset_x;
            design.pin_offset_y[pin0 + k] = offset_y;
        }
    }
    if (first_bad_net == LONG_MAX) return;
    const DefNet& net = nets[first_bad_net];
    for (size_t k = 0; k < net.num_pins; k++)
        if (design.pin_node[pin_base + first_pin[first_bad_net] + k] < 0)
            return fail("net " + std::string(net.name) + " names unknown " + (net.pins[k].first == "PIN"
                        ? "PIN " + std::string(net.pins[k].second) : "component " + std::string(net.pins[k].first)));
}

void FlatDatabase::finish_lefdef(bool scale_to_dbu)
{
    if (scale_to_dbu) {
        const float scale = (float)design.units_per_micron;
        for (LefMacroFacts& macro : lef_macros) { macro.width *= scale; macro.height *= scale; }
        design.row_height *= scale;
        design.site_width *= scale;
    }
    for (int node = 0; node < design.num_nodes(); node++) {
        if (node_macro[node] < 0) continue;
        design.node_width[node] = lef_macros[node_macro[node]].width;
        design.node_height[node] = lef_macros[node_macro[node]].height;
    }
}

// ---- Bookshelf. Meow.

void FlatDatabase::add_bookshelf_nodes(const std::vector<BookshelfNode>& nodes)
{
    const long count = (long)nodes.size();
    std::vector<uint64_t> hashes(count);
    #pragma omp parallel for schedule(static) if(count >= PARALLEL_MIN)
    for (long i = 0; i < count; i++) hashes[i] = NodeIndex::hash_of(nodes[i].name);
    for (long i = 0; i < count; i++) {
        const BookshelfNode& n = nodes[i];
        add_node(component_id, hashes[i], n.name, n.terminal ? NodeKind::FIXED : NodeKind::CELL, 0, 0,
                 (float)n.width, (float)n.height, -1);
    }
}

void FlatDatabase::add_bookshelf_nets(const std::vector<BookshelfNet>& nets)
{
    const std::vector<size_t> first_pin = batch_first_pin(nets);
    const auto [first_net, pin_base] = append_nets(first_pin);
    long first_bad_net = LONG_MAX;
    #pragma omp parallel for schedule(dynamic, 256) reduction(min : first_bad_net) if((long)nets.size() >= PARALLEL_MIN)
    for (long i = 0; i < (long)nets.size(); i++) {
        const BookshelfNet& net = nets[i];
        const size_t pin0 = pin_base + first_pin[i];
        design.net_name[first_net + i].assign(net.name);
        find_all(net.num_pins, [&](size_t) -> const NodeIndex& { return component_id; },
                 [&](size_t k) { return net.pins[k].node_name; }, &design.pin_node[pin0]);
        for (size_t k = 0; k < net.num_pins; k++) {
            const int node = design.pin_node[pin0 + k];
            if (node < 0) { first_bad_net = std::min(first_bad_net, i); continue; }
            design.pin_offset_x[pin0 + k] = design.node_width[node] / 2.0f + (float)net.pins[k].offset_x;
            design.pin_offset_y[pin0 + k] = design.node_height[node] / 2.0f + (float)net.pins[k].offset_y;
        }
    }
    if (first_bad_net == LONG_MAX) return;
    const BookshelfNet& net = nets[first_bad_net];
    for (size_t k = 0; k < net.num_pins; k++)
        if (design.pin_node[pin_base + first_pin[first_bad_net] + k] < 0)
            return fail("net " + std::string(net.name) + " names unknown node " + std::string(net.pins[k].node_name));
}

void FlatDatabase::add_bookshelf_row(const BookshelfRow& row)
{
    long spacing = row.site_spacing > 0 ? row.site_spacing : row.site_width;
    long x0 = row.origin_x, x1 = x0 + (long)row.site_num * spacing;
    long y0 = row.origin_y, y1 = y0 + row.height;
    if (row_count == 0) {
        row_xmin = x0; row_xmax = x1; row_ymin = y0; row_ymax = y1;
        design.row_height = (float)row.height;
        design.site_width = (float)row.site_width;
    } else {
        row_xmin = std::min(row_xmin, x0); row_xmax = std::max(row_xmax, x1);
        row_ymin = std::min(row_ymin, y0); row_ymax = std::max(row_ymax, y1);
    }
    row_count++;
}

// Lookups in parallel; applied in file order, so a node placed twice keeps its last placement. Meow.
void FlatDatabase::set_bookshelf_node_positions(const std::vector<BookshelfPlacement>& placements)
{
    constexpr long LOOKUP_BATCH = 64;
    const long count = (long)placements.size();
    std::vector<int32_t> nodes(count);
    #pragma omp parallel for schedule(dynamic, 64) if(count >= PARALLEL_MIN)
    for (long first = 0; first < count; first += LOOKUP_BATCH)
        find_all(std::min(LOOKUP_BATCH, count - first), [&](size_t) -> const NodeIndex& { return component_id; },
                 [&](size_t k) { return placements[first + k].name; }, &nodes[first]);
    for (long i = 0; i < count; i++) {
        const BookshelfPlacement& p = placements[i];
        const int node = nodes[i];
        if (node < 0) return fail(".pl places unknown node " + std::string(p.name));
        design.node_x[node] = (float)p.x;
        design.node_y[node] = (float)p.y;
        if (p.status == "FIXED") {
            if (design.node_kind[node] != NodeKind::FIXED) pl_fixed_nodes.push_back(node);
            design.node_kind[node] = NodeKind::FIXED;
            if (p.x > terminal_max_x) terminal_max_x = (int)p.x;
            if (p.y > terminal_max_y) terminal_max_y = (int)p.y;
        }
    }
}

void FlatDatabase::bookshelf_end()
{
    if (row_count > 0) {
        design.die_shift_x = (float)row_xmin;
        design.die_shift_y = (float)row_ymin;
        for (int node = 0; node < design.num_nodes(); node++) {
            if (design.node_kind[node] == NodeKind::IO_PAD) continue;
            design.node_x[node] += -design.die_shift_x;   // Position::translate(-shift): DataBase's exact op
            design.node_y[node] += -design.die_shift_y;
        }
        design.die_xl = 0; design.die_yl = 0;
        design.die_xh = (float)(row_xmax - row_xmin);
        design.die_yh = (float)(row_ymax - row_ymin);
    } else {
        design.die_xl = 0; design.die_yl = 0;
        design.die_xh = (float)terminal_max_x;
        design.die_yh = (float)terminal_max_y;
    }
}

// ---- Files. Meow.

namespace {

std::vector<fs::path> files_with_extension(const fs::path& dir, const std::string& extension)
{
    std::vector<fs::path> matches;
    for (const auto& entry : fs::directory_iterator(dir))
        if (entry.path().extension().string() == extension) matches.push_back(entry.path());
    return matches;
}

// DataBase::readPlacementConstraints, rule for rule, including its refusal to run on a utilization
// outside (0, 1] (TODO #27: a trailing space once made "60% " parse as 60). Meow.
float read_maximum_utilization(const fs::path& dir)
{
    std::ifstream file(dir / "placement.constraints");
    if (!file.is_open()) return 0.0f;
    std::string line;
    while (std::getline(file, line)) {
        auto pos = line.find("maximum_utilization=");
        if (pos == std::string::npos) continue;
        std::string value = line.substr(pos + std::strlen("maximum_utilization="));
        float utilization;
        if (!value.empty() && value.back() == '%') { value.pop_back(); utilization = std::stof(value) / 100.0f; }
        else utilization = std::stof(value);
        if (utilization <= 0.0f || utilization > 1.0f) {
            fprintf(stderr, "Malformed placement constraint in %s: maximum_utilization parsed as %g, expected (0, 1]. "
                    "Offending line: '%s'\n", (dir / "placement.constraints").c_str(), utilization, line.c_str());
            exit(1);
        }
        return utilization;
    }
    return 0.0f;
}

} // namespace

bool read_flat_design(const fs::path& dir, FlatDatabase& flat_db, std::string& why)
{
    bool lef_ok = false;
    for (const fs::path& lef : files_with_extension(dir, ".lef")) lef_ok = readLefFile(lef, flat_db);

    bool def_ok = false;
    for (const fs::path& def : files_with_extension(dir, ".def"))
        if (def.filename() == "floorplan.def") { def_ok = readDefFile(def, flat_db); break; }
    flat_db.finish_lefdef(lef_ok && def_ok && flat_db.design.units_per_micron > 0);

    if (!lef_ok || !def_ok) {
        std::vector<fs::path> aux = files_with_extension(dir, ".aux");
        if (aux.empty()) { why = "no LEF/DEF pair and no .aux in " + dir.string(); return false; }
        if (!readBookshelfAux(aux[0], flat_db)) { why = "Bookshelf read failed: " + aux[0].string(); return false; }
    }
    if (!flat_db.error.empty()) { why = flat_db.error; return false; }
    flat_db.design.maximum_utilization = read_maximum_utilization(dir);
    return true;
}

} // namespace sw_flat
