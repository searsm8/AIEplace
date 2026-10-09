// export_netlist -- one design -> the packer's Netlist file (packer/beat_packer.hpp, read_netlist).
//
//   export_netlist <design_dir> <out_file>
//
// FlatDatabase's one parse, then sw_only's macro rule and the net-degree mask (flat_netlist.hpp).
// This is how header-only consumers -- pl/test's tier-1 harnesses, the #47 models -- get a real
// design without linking the host parser (`make netlists` writes all 44). The file is read back
// and compared before exit, so a format bug fails here, not in a harness. Meow.

#include "flat_database.hpp"
#include "flat_netlist.hpp"
#include "Logger.h"

static bool same_netlist(const packer::Netlist& a, const packer::Netlist& b)
{
    if (a.name != b.name || a.movable != b.movable || a.is_macro != b.is_macro || a.nets.size() != b.nets.size()) return false;
    for (int axis = 0; axis < 2; axis++)
        if (a.offset_value[axis].size() != b.offset_value[axis].size() ||
            std::memcmp(a.offset_value[axis].data(), b.offset_value[axis].data(), 4 * a.offset_value[axis].size()) != 0)
            return false;
    for (size_t n = 0; n < a.nets.size(); n++) {
        if (a.nets[n].size() != b.nets[n].size()) return false;
        for (size_t k = 0; k < a.nets[n].size(); k++) {
            const packer::Pin &p = a.nets[n][k], &q = b.nets[n][k];
            if (p.node != q.node || p.offset_key[0] != q.offset_key[0] || p.offset_key[1] != q.offset_key[1]) return false;
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    if (argc != 3) { fprintf(stderr, "usage: export_netlist <design_dir> <out_file>\n"); return 2; }
    using namespace AIEplace;
    Logger::setup_logging(LogLevel::WARNING);
    std::string dir = argv[1];
    while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
    const std::string suite = dir.substr(0, dir.find_last_of('/'));
    const std::string name = suite.substr(suite.find_last_of('/') + 1) + "/" + dir.substr(dir.find_last_of('/') + 1);

    sw_flat::FlatDatabase flat_db;
    std::string why;
    if (!sw_flat::read_flat_design(dir, flat_db, why)) { fprintf(stderr, "%s: %s\n", dir.c_str(), why.c_str()); return 1; }
    const packer::Netlist nl = sw_flat::to_netlist(flat_db.design, sw_flat::tag_movable_macros(flat_db.design), name);
    packer::write_netlist(nl, argv[2]);
    if (!same_netlist(nl, packer::read_netlist(argv[2]))) { fprintf(stderr, "%s: read-back differs\n", argv[2]); return 1; }
    printf("%s: %zu nodes, %zu nets -> %s\n", name.c_str(), nl.movable.size(), nl.nets.size(), argv[2]);
    return 0;
}
