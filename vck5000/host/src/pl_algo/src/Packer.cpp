// Packer.cpp -- DataBase -> v0 host->PL buffers. See Packer.hpp / host_interface.hpp.

#include "Packer.hpp"
#include <unordered_map>
#include <algorithm>
#include <cstdio>

namespace plalgo {

using AIEplace::Node;
using AIEplace::Component;
using AIEplace::IOPad;
using AIEplace::Net;
using AIEplace::NetPin;

PackedDesign packDesign(AIEplace::DataBase& db) {
    PackedDesign pk;
    std::unordered_map<Node*, int32_t> idx;

    auto push_node = [&](Node* n) {
        idx[n] = (int32_t)pk.node_pos.size();
        pk.node_pos.push_back({n->getX(), n->getY()});
    };

    // Pass 1: movable components -> [0, M)
    for (const auto& kv : db.getComponents())
        if (kv.second->getStatus() != AIEplace::FIXED)
            push_node(kv.second);
    const int32_t M = (int32_t)pk.node_pos.size();

    // Pass 2: fixed nodes -> [M, N): FIXED components, then IOPads
    for (const auto& kv : db.getComponents())
        if (kv.second->getStatus() == AIEplace::FIXED)
            push_node(kv.second);
    for (const auto& kv : db.getIOPads())
        push_node(kv.second);
    const int32_t N = (int32_t)pk.node_pos.size();

    // Nets -> CSR (net_ptr) + flattened pin records.
    const std::vector<Net*>& nets = db.getNetsVector();
    pk.net_ptr.reserve(nets.size() + 1);
    pk.net_ptr.push_back(0);
    int unresolved = 0;
    for (Net* net : nets) {
        for (const NetPin& pin : net->getPins()) {
            auto it = idx.find(pin.node);
            if (it == idx.end()) { ++unresolved; continue; } // not in v0 index space
            pk.pins.push_back(PinRecord{ it->second, pin.offset.x, pin.offset.y, 0 });
        }
        pk.net_ptr.push_back((int32_t)pk.pins.size());
    }
    if (unresolved)
        fprintf(stderr, "[pack] WARNING: %d pin(s) referenced nodes outside the "
                        "v0 index space (skipped)\n", unresolved);

    pk.header = DesignHeader{ M, N, (int32_t)nets.size(), (int32_t)pk.pins.size() };
    return pk;
}

double hpwlFromPacked(const PackedDesign& pk) {
    double total = 0.0;
    for (int n = 0; n < pk.header.num_nets; ++n) {
        const int beg = pk.net_ptr[n], end = pk.net_ptr[n + 1];
        if (beg == end) continue;
        const PinRecord& f = pk.pins[beg];
        float min_x = pk.node_pos[f.node_idx].x + f.off_x, max_x = min_x;
        float min_y = pk.node_pos[f.node_idx].y + f.off_y, max_y = min_y;
        for (int p = beg + 1; p < end; ++p) {
            const PinRecord& r = pk.pins[p];
            const float x = pk.node_pos[r.node_idx].x + r.off_x;
            const float y = pk.node_pos[r.node_idx].y + r.off_y;
            min_x = std::min(min_x, x); max_x = std::max(max_x, x);
            min_y = std::min(min_y, y); max_y = std::max(max_y, y);
        }
        total += (double)((max_x - min_x) + (max_y - min_y));
    }
    return total;
}

} // namespace plalgo
