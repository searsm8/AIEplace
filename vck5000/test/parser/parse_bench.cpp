// parse_bench -- parse one design directory into a DataBase, time it, and optionally dump every
// parse-derived field in a canonical text form (TODO #43).
//
//   parse_bench [--limbo] [--dump <file>] <design_dir>
//
// --limbo  read through the Limbo parsers instead of the native DesignReader, via a bridge that
//          converts Limbo's structs into the same ParseRecords DataBase consumes. This is what
//          lets compare_parsers.sh demand that the two dumps be byte-identical. Only in the
//          parse_bench_limbo build (-DWITH_LIMBO), which must use Limbo's old string ABI; the
//          plain parse_bench is built exactly like the host.
// Prints:  <design_dir> <seconds> <components> <nets> <iopads> <macros>
#include "DataBase.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <unistd.h>
#include "sampler.hpp"

#ifdef WITH_LIMBO
#include <limbo/parsers/lef/adapt/LefDriver.h>
#include <limbo/parsers/def/adapt/DefDriver.h>
#include <limbo/parsers/bookshelf/bison/BookshelfDriver.h>
#endif

using namespace AIEplace;

namespace {

#ifdef WITH_LIMBO
// Limbo prints progress to stdout; the host used to run it with stdout pointed at /dev/null. Meow.
bool runSilenced(const std::function<bool()>& parse_fn)
{
    fflush(stdout);
    int saved_stdout = dup(STDOUT_FILENO);
    if (!freopen("/dev/null", "w", stdout)) {}
    bool success = parse_fn();
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    clearerr(stdout);
    return success;
}

// ---- Limbo -> ParseRecords bridges. Each conversion is exactly what DataBase's old Limbo
// ---- callbacks read out of the Limbo struct, nothing more. Meow.

struct LefBridge : LefParser::LefDataBase {
    DataBase& db;
    explicit LefBridge(DataBase& d) : db(d) {}
    void lef_version_cbk(std::string const&) override {}
    void lef_version_cbk(double) override {}
    void lef_casesensitive_cbk(int) override {}
    void lef_dividerchar_cbk(std::string const&) override {}
    void lef_units_cbk(LefParser::lefiUnits const&) override {}
    void lef_manufacturing_cbk(double) override {}
    void lef_useminspacing_cbk(LefParser::lefiUseMinSpacing const&) override {}
    void lef_clearancemeasure_cbk(std::string const&) override {}
    void lef_busbitchars_cbk(std::string const&) override {}
    void lef_layer_cbk(LefParser::lefiLayer const&) override {}
    void lef_via_cbk(LefParser::lefiVia const&) override {}
    void lef_viarule_cbk(LefParser::lefiViaRule const&) override {}
    void lef_spacing_cbk(LefParser::lefiSpacing const&) override {}
    void lef_obstruction_cbk(LefParser::lefiObstruction const&) override {}
    void lef_prop_cbk(LefParser::lefiProp const&) override {}
    void lef_maxstackvia_cbk(LefParser::lefiMaxStackVia const&) override {}
    // NOT overridden by the old host DataBase: Limbo's default calls exit(0), so the old host
    // silently quit -- status 0, nothing placed -- on any LEF with a NONDEFAULTRULE (all 20
    // ispd2015_fix designs). Overridden here only so their DEFs can be compared at all. Meow.
    void lef_nondefault_cbk(LefParser::lefiNonDefault const&) override {}
    void lef_site_cbk(LefParser::lefiSite const& s) override {
        LefSite site;
        site.has_size = s.hasSize();
        if (site.has_size) { site.size_x = s.sizeX(); site.size_y = s.sizeY(); }
        site.has_class = s.hasClass();
        if (site.has_class) site.site_class = s.siteClass();
        db.lef_site_cbk(site);
    }
    void lef_macrobegin_cbk(std::string const& n) override { db.lef_macrobegin_cbk(n); }
    void lef_macro_cbk(LefParser::lefiMacro const& m) override {
        LefMacro macro;
        macro.size_x = m.sizeX();
        macro.size_y = m.sizeY();
        macro.has_class = m.hasClass();
        if (macro.has_class) macro.macro_class = m.macroClass();
        db.lef_macro_cbk(macro);
    }
    void lef_pin_cbk(LefParser::lefiPin const& p) override {
        LefPin pin;
        pin.name = p.name();
        if (p.hasUse()) pin.use = p.use();
        if (p.numPorts() >= 1) {
            LefParser::lefiGeometries* geom = p.port(0);
            for (int gi = 0; gi < geom->numItems(); gi++) {
                if ((int)geom->itemType(gi) == (int)LefParser::lefiGeomRectE) {
                    LefParser::lefiGeomRect* rect = geom->getRect(gi);
                    pin.has_rect = true;
                    pin.rect_xl = rect->xl; pin.rect_yl = rect->yl;
                    pin.rect_xh = rect->xh; pin.rect_yh = rect->yh;
                    break;
                }
            }
        }
        db.lef_pin_cbk(pin);
    }
};

struct DefBridge : DefParser::DefDataBase {
    DataBase& db;
    explicit DefBridge(DataBase& d) : db(d) {}
    void set_def_busbitchars(std::string const&) override {}
    void set_def_dividerchar(std::string const&) override {}
    void set_def_version(std::string const&) override {}
    void set_def_unit(int u) override { db.set_def_unit(u); }
    void set_def_design(std::string const& d) override { db.set_def_design(d); }
    void set_def_diearea(int xl, int yl, int xh, int yh) override { db.set_def_diearea(xl, yl, xh, yh); }
    void add_def_row(DefParser::Row const&) override {}
    void resize_def_component(int) override {}
    void add_def_component(DefParser::Component const& c) override {
        DefComponent comp;
        comp.name = c.comp_name;
        comp.macro_name = c.macro_name;
        comp.status = c.status;
        comp.x = c.origin[0];
        comp.y = c.origin[1];
        db.add_def_components({comp});
    }
    void resize_def_pin(int) override {}
    void add_def_pin(DefParser::Pin const& p) override {
        DefPin pin;
        pin.name = p.pin_name;
        pin.direction = p.direct;
        pin.status = p.status;
        pin.x = p.origin[0];
        pin.y = p.origin[1];
        pin.has_layer = !p.vBbox.empty();
        if (pin.has_layer) std::copy(p.vBbox.front().begin(), p.vBbox.front().end(), pin.bbox);
        db.add_def_pin(pin);
    }
    void resize_def_net(int) override {}
    void add_def_net(DefParser::Net const& n) override {
        std::vector<DefNetPin> pins(n.vNetPin.begin(), n.vNetPin.end());
        db.add_def_nets({DefNet{n.net_name, pins.data(), pins.size()}});
    }
    void resize_def_blockage(int) override {}
    void add_def_placement_blockage(std::vector<std::vector<int>> const&) override {}
    void resize_def_region(int n) override { db.resize_def_region(n); }
    void add_def_region(DefParser::Region const&) override {}
    void resize_def_group(int n) override { db.resize_def_group(n); }
    void add_def_group(DefParser::Group const&) override {}
    void end_def_design() override {}
};

struct BookshelfBridge : BookshelfParser::BookshelfDataBase {
    DataBase& db;
    explicit BookshelfBridge(DataBase& d) : db(d) {}
    void resize_bookshelf_node_terminals(int, int) override {}
    void resize_bookshelf_net(int) override {}
    void resize_bookshelf_pin(int) override {}
    void resize_bookshelf_row(int) override {}
    void add_bookshelf_terminal(std::string& name, int w, int h) override { db.add_bookshelf_nodes({{name, w, h, true}}); }
    void add_bookshelf_node(std::string& name, int w, int h, bool) override { db.add_bookshelf_nodes({{name, w, h, false}}); }
    void add_bookshelf_net(BookshelfParser::Net const& n) override {
        std::vector<BookshelfNetPin> pins;
        for (const BookshelfParser::NetPin& p : n.vNetPin) {
            BookshelfNetPin pin;
            pin.node_name = p.node_name;
            pin.pin_name = p.pin_name;
            pin.offset_x = p.offset[0];
            pin.offset_y = p.offset[1];
            pins.push_back(pin);
        }
        db.add_bookshelf_nets({BookshelfNet{n.net_name, pins.data(), pins.size()}});
    }
    void add_bookshelf_row(BookshelfParser::Row const& r) override {
        BookshelfRow row;
        row.origin_x = r.origin[0];
        row.origin_y = r.origin[1];
        row.height = r.height;
        row.site_num = r.site_num;
        row.site_width = r.site_width;
        row.site_spacing = r.site_spacing;
        db.add_bookshelf_row(row);
    }
    void set_bookshelf_node_position(std::string const& name, double x, double y, std::string const& orient,
                                     std::string const& status, bool) override {
        db.set_bookshelf_node_positions({BookshelfPlacement{name, x, y, orient, status}});
    }
    void set_bookshelf_design(std::string& name) override { db.set_bookshelf_design(name); }
    void bookshelf_end() override { db.bookshelf_end(); }
};

class LimboDataBase : public DataBase {
public:
    explicit LimboDataBase(fs::path dir) : DataBase(dir, DeferRead{}) { readInput(); }
protected:
    bool parseLefFile(const fs::path& f) override {
        LefBridge bridge(*this);
        return runSilenced([&]() { return LefParser::read(bridge, f.string()); });
    }
    bool parseDefFile(const fs::path& f) override {
        DefBridge bridge(*this);
        return runSilenced([&]() { return DefParser::read(bridge, f.string()); });
    }
    bool parseBookshelfAux(const fs::path& f) override {
        BookshelfBridge bridge(*this);
        return runSilenced([&]() { return BookshelfParser::read(bridge, f.string()); });
    }
};

#endif // WITH_LIMBO

// ---- canonical dump: every field the parse sets, floats in %a so equality is bit equality. Meow.

string hexf(double v) { char buf[64]; std::snprintf(buf, sizeof buf, "%a", v); return buf; }
string pos(const Position& p) { return hexf(p.x) + " " + hexf(p.y); }

void dumpNode(FILE* out, Node* node)
{
    std::fprintf(out, " status %d movable_macro %d orient %s next %s %s current %s %s nets %zu\n",
                 (int)node->getStatus(), (int)node->isMovableMacro(), node->getOrientation().c_str(),
                 pos(node->next.node_pos).c_str(), pos(node->next.probe_pos).c_str(),
                 pos(node->current.node_pos).c_str(), pos(node->current.probe_pos).c_str(), node->getNets().size());
}

void dump(DataBase& db, const string& path)
{
    FILE* out = std::fopen(path.c_str(), "w");
    if (!out) { std::cerr << "cannot write " << path << "\n"; std::exit(2); }
    Box& die = db.getDieArea();
    std::fprintf(out, "design %s units %d regions %d groups %d\n", db.getDesignName().c_str(),
                 db.getUnitsPerMicron(), db.getNumDefRegions(), db.getNumDefGroups());
    std::fprintf(out, "die %s %s shift %s row_height %s site_width %s\n", pos(die.getPosBottomLeft()).c_str(),
                 pos(die.getPosTopRight()).c_str(), pos(db.getDieShift()).c_str(),
                 hexf(db.getRowHeight()).c_str(), hexf(db.getSiteWidth()).c_str());
    std::fprintf(out, "area fixed %s movable %s total %s max_util %s net_degree %d\n",
                 hexf(db.getTotalFixedArea()).c_str(), hexf(db.getTotalMovableArea()).c_str(),
                 hexf(db.computeTotalComponentArea()).c_str(), hexf(db.getMaximumUtilization()).c_str(),
                 db.getTotalNetDegree());
    for (const auto& [name, macro] : db.getMacros()) {
        std::fprintf(out, "macro %s name %s class '%s' size %s %s area %s pins %zu\n", name.c_str(),
                     macro->getName().c_str(), macro->getClass().c_str(), hexf(macro->getXsize()).c_str(),
                     hexf(macro->getYsize()).c_str(), hexf(macro->getArea()).c_str(), macro->getPinOffsets().size());
        for (const auto& [pin, offset] : macro->getPinOffsets())
            std::fprintf(out, "  pin %s %s\n", pin.c_str(), pos(offset).c_str());
    }
    for (const auto& [name, comp] : db.getComponents()) {
        std::fprintf(out, "comp %s macro %s", name.c_str(), comp->getMacro() ? comp->getMacro()->getName().c_str() : "(null)");
        dumpNode(out, comp);
    }
    for (const auto& [name, pad] : db.getIOPads()) {
        Box& bb = pad->getBoundingBox();
        std::fprintf(out, "iopad %s dir %s bbox %s %s", name.c_str(), pad->getDirection().c_str(),
                     pos(bb.getPosBottomLeft()).c_str(), pos(bb.getPosTopRight()).c_str());
        dumpNode(out, pad);
    }
    std::fprintf(out, "nets_by_name %zu\n", db.getNets().size());
    for (Net* net : db.getNetsVector()) {
        std::fprintf(out, "net %s degree %d\n", net->getName().c_str(), net->getDegree());
        for (const NetPin& pin : net->getPins())
            std::fprintf(out, "  %s %s '%s'\n", pin.node_p->getName().c_str(), pos(pin.offset).c_str(), pin.pin_name.c_str());
    }
    for (const auto& [degree, nets] : db.getNetsByDegree())
        std::fprintf(out, "degree %d nets %zu\n", degree, nets.size());
    std::fclose(out);
}

} // namespace

int main(int argc, char** argv)
{
    sampler::start();
    bool use_limbo = false;
    string dump_path, dir;
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--limbo") use_limbo = true;
        else if (arg == "--dump" && i + 1 < argc) dump_path = argv[++i];
        else dir = arg;
    }
    if (dir.empty()) { std::cerr << "usage: parse_bench [--limbo] [--dump <file>] <design_dir>\n"; return 2; }
    Logger::setup_logging(LogLevel::WARNING);

    auto start = std::chrono::steady_clock::now();
#ifdef WITH_LIMBO
    DataBase* db = use_limbo ? new LimboDataBase(dir) : new DataBase(dir);
#else
    if (use_limbo) { std::cerr << "--limbo needs the parse_bench_limbo build\n"; return 2; }
    DataBase* db = new DataBase(dir);
#endif
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << dir << " " << seconds << " " << db->getComponents().size() << " " << db->getNetsVector().size()
              << " " << db->getIOPads().size() << " " << db->getMacros().size() << "\n";
    if (!dump_path.empty()) dump(*db, dump_path);
    std::cout.flush();
    exit(0);   // exit, not return: no teardown of millions of nodes, but gprof still writes gmon.out. Meow.
}
