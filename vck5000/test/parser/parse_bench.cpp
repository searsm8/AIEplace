// parse_bench -- parse one design directory into a DataBase, time it, and optionally dump every
// parse-derived field in a canonical text form (TODO #43).
//
//   parse_bench [--dump <file>] <design_dir>
//
// Built exactly like the host. compare_parsers.sh checks each dump's sha256 against the frozen
// Limbo hashes in limbo_dump_hashes.txt. The --limbo bridge that produced them was deleted with
// Limbo; it is in git history (see README.md). Meow.
// Prints:  <design_dir> <seconds> <components> <nets> <iopads> <macros>
#include "DataBase.h"
#include <chrono>
#include <cstdio>
#include <iostream>
#include "sampler.hpp"

using namespace AIEplace;

namespace {

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
    string dump_path, dir;
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--dump" && i + 1 < argc) dump_path = argv[++i];
        else dir = arg;
    }
    if (dir.empty()) { std::cerr << "usage: parse_bench [--dump <file>] <design_dir>\n"; return 2; }
    Logger::setup_logging(LogLevel::WARNING);

    auto start = std::chrono::steady_clock::now();
    DataBase* db = new DataBase(dir);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << dir << " " << seconds << " " << db->getComponents().size() << " " << db->getNetsVector().size()
              << " " << db->getIOPads().size() << " " << db->getMacros().size() << "\n";
    if (!dump_path.empty()) dump(*db, dump_path);
    std::cout.flush();
    exit(0);   // exit, not return: no teardown of millions of nodes, but gprof still writes gmon.out. Meow.
}
