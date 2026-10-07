#include "flat_sink.hpp"

#include <cmath>
#include <cstring>
#include <fstream>

namespace sw_flat {

using namespace AIEplace;

int FlatSink::add_node(const std::string& name, NodeKind kind, float x, float y, float width, float height,
                       int macro)
{
    design.node_name.push_back(name);
    design.node_kind.push_back(kind);
    design.node_x.push_back(x);
    design.node_y.push_back(y);
    design.node_width.push_back(width);
    design.node_height.push_back(height);
    node_macro.push_back(macro);
    return design.num_nodes() - 1;
}

void FlatSink::add_net(const std::string& name)
{
    design.net_name.push_back(name);
    design.net_first_pin.push_back(design.net_first_pin.back());
}

// ---- LEF. Sizes and pin offsets stay in microns until finish_lefdef. Meow.

void FlatSink::lef_site_cbk(const LefSite& s)
{
    if (!s.has_size) return;
    bool is_core = s.has_class && s.site_class == "CORE";
    if (is_core || design.row_height == 0.0f) design.row_height = s.size_y;
    if (is_core || design.site_width == 0.0f) design.site_width = s.size_x;
}

void FlatSink::lef_macrobegin_cbk(const std::string& name)
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

void FlatSink::lef_macro_cbk(const LefMacro& m)
{
    if (!current_macro) return;
    current_macro->width = (float)m.size_x;
    current_macro->height = (float)m.size_y;
    if (m.has_class) current_macro->lef_class = m.macro_class;
    current_macro = nullptr;
}

void FlatSink::lef_pin_cbk(const LefPin& p)
{
    if (!current_macro) return;
    if (p.use == "POWER" || p.use == "GROUND") return;
    if (!p.has_rect) return;
    // A repeated PIN name overwrites, as MacroClass::addPinOffset does. Meow.
    current_macro->pin_offset[p.name] = {(float)(p.rect_xl + p.rect_xh) / 2.0f, (float)(p.rect_yl + p.rect_yh) / 2.0f};
}

// ---- DEF. Meow.

void FlatSink::set_def_diearea(int xl, int yl, int xh, int yh)
{
    design.die_xl = (float)xl; design.die_yl = (float)yl;
    design.die_xh = (float)xh; design.die_yh = (float)yh;
}

void FlatSink::add_def_components(const std::vector<DefComponent>& components)
{
    for (const DefComponent& c : components) {
        std::string name(c.name);
        if (component_id.count(name)) continue;
        auto macro = lef_macro_id.find(std::string(c.macro_name));
        if (macro == lef_macro_id.end()) return fail("component " + name + " of undefined LEF macro " + std::string(c.macro_name));
        const std::string& lef_class = lef_macros[macro->second].lef_class;
        bool placed_fixed = c.status == "PLACED" && lef_class != "CORE" && lef_class != "BLOCK";
        NodeKind kind = c.status == "FIXED" || placed_fixed ? NodeKind::FIXED : NodeKind::CELL;
        int id = add_node(name, kind, (float)c.x, (float)c.y, 0, 0, macro->second);
        component_id.emplace(name, id);
        if (placed_fixed) placed_fixed_nodes.push_back(id);
        if (c.status == "COVER") cover_nodes.push_back(id);
    }
}

void FlatSink::add_def_pin(const DefPin& p)
{
    if (iopad_id.count(p.name)) return;
    float width = std::fabs((float)p.bbox[0] - (float)p.bbox[2]);
    float height = std::fabs((float)p.bbox[1] - (float)p.bbox[3]);
    iopad_id.emplace(p.name, add_node(p.name, NodeKind::IO_PAD, (float)p.x, (float)p.y, width, height, -1));
}

void FlatSink::add_def_nets(const std::vector<DefNet>& nets)
{
    const float scale = (float)design.units_per_micron;
    for (const DefNet& net : nets) {
        add_net(std::string(net.name));
        for (size_t k = 0; k < net.num_pins; k++) {
            const DefNetPin& pin = net.pins[k];
            int node;
            float offset_x = 0, offset_y = 0;
            if (pin.first == "PIN") {
                auto it = iopad_id.find(std::string(pin.second));
                if (it == iopad_id.end()) return fail("net " + std::string(net.name) + " names unknown PIN " + std::string(pin.second));
                node = it->second;
            } else {
                auto it = component_id.find(std::string(pin.first));
                if (it == component_id.end()) return fail("net " + std::string(net.name) + " names unknown component " + std::string(pin.first));
                node = it->second;
                const auto& offsets = lef_macros[node_macro[node]].pin_offset;
                auto offset = offsets.find(std::string(pin.second));
                if (offset != offsets.end()) {
                    offset_x = offset->second.first * scale;
                    offset_y = offset->second.second * scale;
                }
            }
            design.pin_node.push_back(node);
            design.pin_offset_x.push_back(offset_x);
            design.pin_offset_y.push_back(offset_y);
            design.net_first_pin.back()++;
        }
    }
}

void FlatSink::finish_lefdef(bool scale_to_dbu)
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

void FlatSink::add_bookshelf_nodes(const std::vector<BookshelfNode>& nodes)
{
    for (const BookshelfNode& n : nodes) {
        std::string name(n.name);
        if (component_id.count(name)) continue;
        component_id.emplace(name, add_node(name, n.terminal ? NodeKind::FIXED : NodeKind::CELL, 0, 0,
                                            (float)n.width, (float)n.height, -1));
    }
}

void FlatSink::add_bookshelf_nets(const std::vector<BookshelfNet>& nets)
{
    for (const BookshelfNet& net : nets) {
        add_net(std::string(net.name));
        for (size_t k = 0; k < net.num_pins; k++) {
            const BookshelfNetPin& pin = net.pins[k];
            auto it = component_id.find(std::string(pin.node_name));
            if (it == component_id.end()) return fail("net " + std::string(net.name) + " names unknown node " + std::string(pin.node_name));
            int node = it->second;
            design.pin_node.push_back(node);
            design.pin_offset_x.push_back(design.node_width[node] / 2.0f + (float)pin.offset_x);
            design.pin_offset_y.push_back(design.node_height[node] / 2.0f + (float)pin.offset_y);
            design.net_first_pin.back()++;
        }
    }
}

void FlatSink::add_bookshelf_row(const BookshelfRow& row)
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

void FlatSink::set_bookshelf_node_positions(const std::vector<BookshelfPlacement>& placements)
{
    for (const BookshelfPlacement& p : placements) {
        auto it = component_id.find(std::string(p.name));
        if (it == component_id.end()) return fail(".pl places unknown node " + std::string(p.name));
        int node = it->second;
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

void FlatSink::bookshelf_end()
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

bool read_flat_design(const fs::path& dir, FlatSink& sink, std::string& why)
{
    bool lef_ok = false;
    for (const fs::path& lef : files_with_extension(dir, ".lef")) lef_ok = readLefFile(lef, sink);

    bool def_ok = false;
    for (const fs::path& def : files_with_extension(dir, ".def"))
        if (def.filename() == "floorplan.def") { def_ok = readDefFile(def, sink); break; }
    sink.finish_lefdef(lef_ok && def_ok && sink.design.units_per_micron > 0);

    if (!lef_ok || !def_ok) {
        std::vector<fs::path> aux = files_with_extension(dir, ".aux");
        if (aux.empty()) { why = "no LEF/DEF pair and no .aux in " + dir.string(); return false; }
        if (!readBookshelfAux(aux[0], sink)) { why = "Bookshelf read failed: " + aux[0].string(); return false; }
    }
    if (!sink.error.empty()) { why = sink.error; return false; }
    sink.design.maximum_utilization = read_maximum_utilization(dir);
    return true;
}

} // namespace sw_flat
