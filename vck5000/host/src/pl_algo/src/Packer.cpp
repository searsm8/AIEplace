// Packer.cpp -- DataBase -> v0 host->PL buffers. See Packer.hpp / host_interface.hpp.

#include "Packer.hpp"      // -> host_interface.hpp: plalgo::IGNORE_NET_DEGREE (XPlace net_mask)
#include <unordered_map>
#include <algorithm>
#include <numeric>
#include <random>
#include <cstdio>

namespace plalgo {

using AIEplace::Node;
using AIEplace::Component;
using AIEplace::IOPad;
using AIEplace::Net;
using AIEplace::NetPin;

// Deterministic seed for filler initial positions (see packDesign). Fixed so a run is
// reproducible; NOT an attempt to reproduce sw_only's interleaved std::rand() stream (see Packer.hpp).
static constexpr unsigned FILLER_SEED = 20260829u;

// Port of sw_only Placer::tagMovableMacros (Setup.cpp:106) -- XPlace's is_mov_macro rule
// (database.py:621-632). A MOVABLE node is a macro iff it is taller than ~two std-cell rows, its
// area exceeds 10x the mean of the smallest 99.9% of movable areas, and both dims are non-degenerate.
// Sets Node::m_is_movable_macro, read by packDesign (to bucket the movable range) and by the density
// deposit-weight override (#11b). MUST run before db.addFillers (the filler math is std-cell-only).
void tagMovableMacros(AIEplace::DataBase& db) {
    std::vector<float> movable_areas;
    float height_sum = 0.0f;
    for (const auto& item : db.getComponents()) {
        if (item.second->getStatus() == AIEplace::FIXED) continue;
        movable_areas.push_back(item.second->getArea());
        height_sum += item.second->getYsize();
    }
    if (movable_areas.empty()) return;
    const float row_height = height_sum / movable_areas.size();

    // Threshold from the smallest 99.9% of movable areas, so a few huge macros can't drag the mean up.
    std::vector<float> ascending = movable_areas;
    std::sort(ascending.begin(), ascending.end());
    const size_t small_count = std::max<size_t>(1, (size_t)(ascending.size() * 0.999));
    const float mean_small = std::accumulate(ascending.begin(), ascending.begin() + small_count, 0.0f)
                           / small_count;
    const float macro_area_threshold = 10.0f * mean_small;

    for (const auto& item : db.getComponents()) {
        Component* c = item.second;
        if (c->getStatus() == AIEplace::FIXED) continue;
        const bool is_tall  = c->getYsize() > 2.01f * row_height;
        const bool is_large = c->getArea() > macro_area_threshold;
        const bool is_sized = c->getXsize() > 1e-4f && c->getYsize() > 1e-4f;
        c->setMovableMacro(is_tall && is_large && is_sized);
    }
}

PackedDesign packDesign(AIEplace::DataBase& db) {
    PackedDesign pk;
    std::unordered_map<Node*, int32_t> idx;

    auto push_node = [&](Node* n) {
        idx[n] = (int32_t)pk.node_pos.size();
        pk.node_pos.push_back({n->getX(), n->getY()});
        // Geometry for density binning (same index order as node_pos).
        pk.node_box.push_back(NodeBox{n->getX(), n->getY(), n->getXsize(), n->getYsize()});
    };

    // Movable prefix is NESTED by kind (host_interface.hpp): std | macro | filler. The two
    // boundaries let a per-node consumer classify by index alone. A bring-up pack (no macros
    // tagged, getFillers() empty) collapses to all-std, first_macro == first_filler == M, and is
    // byte-identical to the old single movable pass.
    // Pass 1a: movable std-cells -> [0, first_macro)
    for (const auto& kv : db.getComponents())
        if (kv.second->getStatus() != AIEplace::FIXED && !kv.second->isMovableMacro())
            push_node(kv.second);
    const int32_t first_macro = (int32_t)pk.node_pos.size();

    // Pass 1b: movable macros -> [first_macro, first_filler)
    for (const auto& kv : db.getComponents())
        if (kv.second->getStatus() != AIEplace::FIXED && kv.second->isMovableMacro())
            push_node(kv.second);
    const int32_t first_filler = (int32_t)pk.node_pos.size();

    // Pass 1c: fillers -> [first_filler, M). On no nets (no idx entry), density force only.
    // Placed uniform-random across the die (XPlace get_filler_pos / sw_only initializePlacement):
    // fillers represent whitespace everywhere, so unlike real cells they are NOT centre-clustered.
    {
        AIEplace::Box die = db.getDieArea();
        const float ll_x = die.getPosBottomLeft().x, ll_y = die.getPosBottomLeft().y;
        const float die_w = die.getXsize(), die_h = die.getYsize();
        std::mt19937 rng(FILLER_SEED);
        std::uniform_real_distribution<float> u01(0.0f, 1.0f);
        for (Component* filler_p : db.getFillers()) {
            const float fx = ll_x + u01(rng) * die_w, fy = ll_y + u01(rng) * die_h;
            pk.node_pos.push_back({fx, fy});
            pk.node_box.push_back(NodeBox{fx, fy, filler_p->getXsize(), filler_p->getYsize()});
        }
    }
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
    int32_t net_id = 0;
    for (Net* net : nets) {
        const int32_t beg = (int32_t)pk.pins.size();
        for (const NetPin& pin : net->getPins()) {
            auto it = idx.find(pin.node_p);
            if (it == idx.end()) { ++unresolved; continue; } // not in v0 index space
            // NodePin carries the ABSOLUTE position (P2); the constant offset goes to pin_off
            // and the position is filled in by refresh_pin_pos on the device each iteration.
            // Seeded here from the initial node positions so a caller that reads pk before the
            // first refresh sees a consistent design rather than zeros.
            const coord_t np = pk.node_pos[it->second];
            pk.pins.push_back(NodePin{ it->second, np.x + pin.offset.x, np.y + pin.offset.y, net_id });
            pk.pin_off.push_back(PinOffset{ pin.offset.x, pin.offset.y });
        }
        // Tag pins of masked nets (net=-1) so the PL gradient/metrics skip them: degree <= 1
        // (no gradient) OR degree > IGNORE_NET_DEGREE (XPlace net_mask, high-degree clock/reset
        // nets excluded from both the WA gradient and reported HPWL). Mirrors sw_only.
        const int32_t deg = (int32_t)pk.pins.size() - beg;
        if (deg <= 1 || deg > plalgo::IGNORE_NET_DEGREE)
            for (int32_t p = beg; p < (int32_t)pk.pins.size(); ++p) pk.pins[p].net = -1;
        pk.net_ptr.push_back((int32_t)pk.pins.size());
        ++net_id;
    }
    if (unresolved)
        fprintf(stderr, "[pack] WARNING: %d pin(s) referenced nodes outside the "
                        "v0 index space (skipped)\n", unresolved);

    // Node-major pin stream for the gradient's segmented reduction (pass 3): the
    // movable, gradient-bearing pins, sorted ascending by node so each node's pins
    // are contiguous (and node_grad writes come out in node order -> sequential).
    // The offsets are carried through the same permutation so npin_off stays parallel to npins;
    // sorting the records alone would silently decouple them (P2).
    std::vector<int32_t> order;
    order.reserve(pk.pins.size());
    for (int32_t p = 0; p < (int32_t)pk.pins.size(); ++p)
        if (pk.pins[p].net >= 0 && pk.pins[p].node_idx < M) order.push_back(p);
    std::stable_sort(order.begin(), order.end(),
                     [&](int32_t a, int32_t b){ return pk.pins[a].node_idx < pk.pins[b].node_idx; });
    pk.npins.reserve(order.size());
    pk.npin_off.reserve(order.size());
    for (int32_t p : order) { pk.npins.push_back(pk.pins[p]); pk.npin_off.push_back(pk.pin_off[p]); }

    pk.header = DesignHeader{ M, N, (int32_t)nets.size(), (int32_t)pk.pins.size(),
                              first_macro, first_filler };
    return pk;
}

double hpwlFromPacked(const PackedDesign& pk) {
    double total = 0.0;
    for (int n = 0; n < pk.header.num_nets; ++n) {
        const int beg = pk.net_ptr[n], end = pk.net_ptr[n + 1];
        if (beg == end) continue;
        const NodePin& f = pk.pins[beg];
        if (f.net < 0) continue;   // masked net (degree<=1 or >IGNORE_NET_DEGREE); skip like the PL kernels
        float min_x = f.x, max_x = min_x;   // NodePin carries the absolute position (P2)
        float min_y = f.y, max_y = min_y;
        for (int p = beg + 1; p < end; ++p) {
            const NodePin& r = pk.pins[p];
            const float x = r.x;
            const float y = r.y;
            min_x = std::min(min_x, x); max_x = std::max(max_x, x);
            min_y = std::min(min_y, y); max_y = std::max(max_y, y);
        }
        total += (double)((max_x - min_x) + (max_y - min_y));
    }
    return total;
}

} // namespace plalgo
