#ifndef PARTITION_HPP
#define PARTITION_HPP

// partition.hpp -- chunk partitioners for encode_chunked (#42). Each returns an owner per work node
// for build_chunks_from_owner; the four candidates:
//   0  baseline     locality_order + K equal contiguous runs (what encode_chunked does)
//   1  improved     BFS over every homed net (large ones too) from a pseudo-peripheral unit, cut where
//                   the chunks' exact slot loads balance instead of at equal node counts
//   2  FM           k-way Fiduccia-Mattheyses refinement of a starting partition
//   3  multilevel   heavy-edge coarsening, an improved-style cut of the coarsest level, FM at every level
//
// The objective is EXACT, not a proxy: PartitionState tracks build_chunks' own rules -- a net is
// homed in the chunk owning most of its distinct movable nodes (ties to the lowest chunk), a node
// is external in every non-owner chunk that homes a net containing it -- so `externals` equals
// Chunked::externals and `load[k]` equals chunk k's slot-node count (before bank-row rounding).
// tier 1: test/partition_test.cpp. Meow.

#include "beat_packer.hpp"

#include <chrono>
#include <climits>
#include <queue>
#include <random>

namespace packer {

constexpr int PARTITION_MAX_K = 16;

// The work netlist as build_chunks sees it, in CSR form. Meow.
struct PartitionGraph {
    int num_nodes = 0;
    std::vector<int>  homed;                 // homed net ids, build_chunks' order
    std::vector<int>  net_ptr, net_node;     // per homed index: distinct work nodes
    std::vector<int>  node_ptr, node_net;    // per node: homed indices containing it
    std::vector<char> movable, slot;         // slot: needs a slot in a chunk that holds it
    std::vector<int>  root;                  // per node: its move unit's root (a macro for its pins), -1 fixed
};

inline PartitionGraph make_partition_graph(const Encoded& g, const Config& cfg) {
    PartitionGraph G;
    G.num_nodes = (int)g.kind.size();
    G.homed = g.in_scope_nets;
    if (cfg.large_nets)
        for (int net = 0; net < (int)g.work.nets.size(); net++) if (is_large(g.work.nets[net])) G.homed.push_back(net);
    G.net_ptr = {0};
    std::vector<int> degree(G.num_nodes, 0);
    for (int net : G.homed) {
        for (int node : g.unique_nodes[net]) { G.net_node.push_back(node); degree[node]++; }
        G.net_ptr.push_back((int)G.net_node.size());
    }
    G.node_ptr.assign(G.num_nodes + 1, 0);
    for (int v = 0; v < G.num_nodes; v++) G.node_ptr[v + 1] = G.node_ptr[v] + degree[v];
    G.node_net.resize(G.node_ptr.back());
    std::vector<int> fill(G.node_ptr.begin(), G.node_ptr.end() - 1);
    for (int n = 0; n < (int)G.homed.size(); n++)
        for (int i = G.net_ptr[n]; i < G.net_ptr[n + 1]; i++) G.node_net[fill[G.net_node[i]]++] = n;
    G.movable.resize(G.num_nodes);
    G.slot.resize(G.num_nodes);
    G.root.assign(G.num_nodes, -1);
    for (int v = 0; v < G.num_nodes; v++) {
        G.movable[v] = g.work.movable[v];
        G.slot[v] = needs_slot(g.kind[v]);
        if (G.movable[v]) G.root[v] = g.kind[v] == MACRO_PIN ? g.base_node[v] : v;
    }
    return G;
}

// A grouping of movable nodes into move units (CSR). Level 0: a cell, or a macro with its pins. Meow.
struct Units {
    std::vector<int> ptr = {0}, node;
    std::vector<int> of_node;                // per work node, -1 fixed
    int size() const { return (int)ptr.size() - 1; }
    int weight(int u) const { return ptr[u + 1] - ptr[u]; }   // every movable node needs a slot
};

inline Units level0_units(const PartitionGraph& G) {
    std::vector<std::vector<int>> members(G.num_nodes);
    for (int v = 0; v < G.num_nodes; v++) if (G.root[v] >= 0) members[G.root[v]].push_back(v);
    Units U;
    U.of_node.assign(G.num_nodes, -1);
    for (int r = 0; r < G.num_nodes; r++) {
        if (members[r].empty()) continue;
        for (int v : members[r]) { U.of_node[v] = U.size(); U.node.push_back(v); }
        U.ptr.push_back((int)U.node.size());
    }
    return U;
}

struct MoveDelta { long externals = 0; long load[PARTITION_MAX_K] = {}; };

// The exact build_chunks model for one owner vector, with O(local) move evaluation. Meow.
struct PartitionState {
    const PartitionGraph* G = nullptr;
    int K = 0;
    std::vector<int>  owner;                 // per node, -1 fixed
    std::vector<int>  count;                 // [net * K + k] distinct movable nodes of the net owned by k
    std::vector<int>  home;                  // per homed index
    std::vector<int>  presence;              // [node * K + k] homed-in-k nets containing the node
    std::vector<long> load;                  // per chunk: slot nodes held (owned + external + fixed copies)
    long externals = 0;

    // scratch for evaluate()
    std::vector<int> mult, touched_nets, delta, touched_nodes;
    std::vector<char> in_unit, node_touched;

    int argmax_home(int n, int a, int ca, int b, int cb) const {
        int best = 0, best_count = -1;
        for (int k = 0; k < K; k++) {
            const int c = k == a ? ca : k == b ? cb : count[(size_t)n * K + k];
            if (c > best_count) { best = k; best_count = c; }
        }
        return best;
    }

    void init(const PartitionGraph& graph, int num_chunks, const std::vector<int>& owners) {
        G = &graph; K = num_chunks;
        if (K > PARTITION_MAX_K) { fprintf(stderr, "partition: K=%d > %d\n", K, PARTITION_MAX_K); exit(2); }
        owner = owners;
        const int num_nets = (int)G->homed.size();
        count.assign((size_t)num_nets * K, 0);
        home.assign(num_nets, 0);
        presence.assign((size_t)G->num_nodes * K, 0);
        load.assign(K, 0);
        externals = 0;
        for (int n = 0; n < num_nets; n++) {
            for (int i = G->net_ptr[n]; i < G->net_ptr[n + 1]; i++) {
                const int v = G->net_node[i];
                if (owner[v] >= 0) count[(size_t)n * K + owner[v]]++;
            }
            home[n] = argmax_home(n, -1, 0, -1, 0);
            for (int i = G->net_ptr[n]; i < G->net_ptr[n + 1]; i++) presence[(size_t)G->net_node[i] * K + home[n]]++;
        }
        for (int v = 0; v < G->num_nodes; v++) {
            if (!G->slot[v]) continue;
            if (owner[v] >= 0) load[owner[v]]++;
            for (int k = 0; k < K; k++)
                if (presence[(size_t)v * K + k] > 0 && owner[v] != k) { load[k]++; externals += G->movable[v]; }
        }
        mult.assign(num_nets, 0);
        delta.assign((size_t)G->num_nodes * K, 0);
        in_unit.assign(G->num_nodes, 0);
        node_touched.assign(G->num_nodes, 0);
    }

    // Effect of moving unit u to chunk b; applies it when `apply`. Meow.
    MoveDelta evaluate(const Units& U, int u, int b, bool apply) {
        MoveDelta d;
        const int a = owner[U.node[U.ptr[u]]];
        if (a == b) return d;
        auto touch = [&](int v) { if (!node_touched[v]) { node_touched[v] = 1; touched_nodes.push_back(v); } };
        for (int i = U.ptr[u]; i < U.ptr[u + 1]; i++) {
            const int v = U.node[i];
            in_unit[v] = 1;
            touch(v);
            for (int j = G->node_ptr[v]; j < G->node_ptr[v + 1]; j++) {
                const int n = G->node_net[j];
                if (mult[n]++ == 0) touched_nets.push_back(n);
            }
        }
        for (int n : touched_nets) {
            const int m = mult[n];
            const int ca = count[(size_t)n * K + a] - m, cb = count[(size_t)n * K + b] + m;
            const int new_home = argmax_home(n, a, ca, b, cb);
            if (new_home != home[n])
                for (int i = G->net_ptr[n]; i < G->net_ptr[n + 1]; i++) {
                    const int v = G->net_node[i];
                    touch(v);
                    delta[(size_t)v * K + home[n]]--;
                    delta[(size_t)v * K + new_home]++;
                }
            if (apply) { count[(size_t)n * K + a] = ca; count[(size_t)n * K + b] = cb; home[n] = new_home; }
            mult[n] = 0;
        }
        touched_nets.clear();
        for (int v : touched_nodes) {
            const int old_owner = owner[v], new_owner = in_unit[v] ? b : old_owner;
            for (int k = 0; k < K; k++) {
                int& dk = delta[(size_t)v * K + k];
                const int before_count = presence[(size_t)v * K + k], after_count = before_count + dk;
                if (G->slot[v]) {
                    const int before = before_count > 0 && old_owner != k, after = after_count > 0 && new_owner != k;
                    d.load[k] += after - before;
                    if (G->movable[v]) d.externals += after - before;
                }
                if (apply) presence[(size_t)v * K + k] = after_count;
                dk = 0;
            }
            if (apply) owner[v] = new_owner;
            node_touched[v] = 0;
            in_unit[v] = 0;
        }
        touched_nodes.clear();
        d.load[a] -= U.weight(u);
        d.load[b] += U.weight(u);
        if (apply) {
            for (int k = 0; k < K; k++) load[k] += d.load[k];
            externals += d.externals;
        }
        return d;
    }
};

inline long overflow_of(const std::vector<long>& load, long capacity) {
    long over = 0;
    for (long l : load) over += std::max(0L, l - capacity);
    return over;
}

// ---- candidate 1: improved order + load-balanced cuts ----

// BFS over move units through every homed net, each component started from a pseudo-peripheral
// unit (the last one reached by a BFS from its lowest unit), so runs of the order are deep, not
// wide. Returns work nodes, a unit's nodes contiguous. Meow.
inline std::vector<int> unit_bfs_order(const PartitionGraph& G, const Units& U) {
    const int num_units = U.size();
    std::vector<int> mark(num_units, -1), order;
    order.reserve(G.num_nodes);
    std::deque<int> queue;
    auto bfs = [&](int start, int stamp, std::vector<int>* visit) {
        int last = start;
        mark[start] = stamp; queue.push_back(start);
        while (!queue.empty()) {
            const int u = queue.front(); queue.pop_front();
            last = u;
            if (visit) visit->push_back(u);
            for (int i = U.ptr[u]; i < U.ptr[u + 1]; i++)
                for (int j = G.node_ptr[U.node[i]]; j < G.node_ptr[U.node[i] + 1]; j++) {
                    const int n = G.node_net[j];
                    for (int p = G.net_ptr[n]; p < G.net_ptr[n + 1]; p++) {
                        const int w = U.of_node[G.net_node[p]];
                        if (w >= 0 && mark[w] != stamp) { mark[w] = stamp; queue.push_back(w); }
                    }
                }
        }
        return last;
    };
    std::vector<char> done(num_units, 0);
    std::vector<int> component;
    int stamp = 0;
    for (int start = 0; start < num_units; start++) {
        if (done[start]) continue;
        const int far = bfs(start, stamp++, nullptr);
        component.clear();
        bfs(far, stamp++, &component);
        for (int u : component) {
            done[u] = 1;
            for (int i = U.ptr[u]; i < U.ptr[u + 1]; i++) order.push_back(U.node[i]);
        }
    }
    return order;
}

// Cut `order` into K contiguous runs whose exact loads balance: start equal, then shift each
// boundary by half the load difference of its two chunks (damped), keeping the best max-load.
// A cut never splits a unit (the order keeps units contiguous; a boundary snaps to a unit start). Meow.
inline std::vector<int> balanced_cut(const PartitionGraph& G, const Units& U, const std::vector<int>& order, int K,
                                     long capacity, int iterations = 24) {
    const long total = (long)order.size();
    std::vector<long> cut(K + 1);
    for (int k = 0; k <= K; k++) cut[k] = total * k / K;
    auto snap = [&](long position) {
        position = std::clamp(position, 0L, total);
        while (position > 0 && position < total && U.of_node[order[position]] == U.of_node[order[position - 1]]) position--;
        return position;
    };
    auto owners_of = [&](const std::vector<long>& c) {
        std::vector<int> owner(G.num_nodes, -1);
        for (int k = 0; k < K; k++) for (long i = c[k]; i < c[k + 1]; i++) owner[order[i]] = k;
        return owner;
    };
    PartitionState s;
    std::vector<long> best_cut = cut;
    long best_max = LONG_MAX;
    for (int it = 0; it < iterations; it++) {
        for (int k = 1; k < K; k++) cut[k] = snap(cut[k]);
        s.init(G, K, owners_of(cut));
        const long max_load = *std::max_element(s.load.begin(), s.load.end());
        if (max_load < best_max) { best_max = max_load; best_cut = cut; }
        if (max_load <= capacity) break;
        const double damping = 0.5 / (1 + it / 8);
        std::vector<long> next = cut;
        for (int k = 1; k < K; k++) next[k] = cut[k] - (long)(damping * (s.load[k - 1] - s.load[k]));
        for (int k = 1; k < K; k++) next[k] = std::clamp(next[k], next[k - 1] + 1, total - (K - k));
        cut = next;
    }
    return owners_of(best_cut);
}

// ---- candidate 2: k-way FM ----

struct FmOptions {
    long   capacity = 0;          // per-chunk slot budget the model must meet
    double overflow_weight = 100; // while over capacity: cost of one overflowing slot, in externals
    int    max_passes = 8;
    double min_pass_gain = 0.001; // stop when a pass improves externals by less than this fraction
    double stall_fraction = 0.02; // a pass stops after this fraction of units moved without a new best
    bool   boundary_only = true;  // seed a pass with units on a cut net only
};

struct FmStats { int passes = 0; long moves = 0, kept = 0, evaluations = 0; long best_externals = 0, best_over = 0; std::vector<long> pass_externals; std::vector<double> pass_seconds; };   // best_*: the last pass's best prefix. Meow.

// One unit's best target and its key (higher is better); key = LONG_MIN if no legal target. Meow.
inline std::pair<long, int> best_target(PartitionState& s, const Units& U, int u, const FmOptions& opt, FmStats& st) {
    const long over = overflow_of(s.load, opt.capacity);
    const int a = s.owner[U.node[U.ptr[u]]];
    long best_key = LONG_MIN; int best = -1;
    for (int b = 0; b < s.K; b++) {
        if (b == a) continue;
        const MoveDelta d = s.evaluate(U, u, b, false);
        st.evaluations++;
        long key;
        if (over == 0) {
            bool legal = true;
            for (int k = 0; k < s.K; k++) legal &= s.load[k] + d.load[k] <= opt.capacity;
            if (!legal) continue;
            key = -d.externals;
        } else {
            long over_after = 0;
            for (int k = 0; k < s.K; k++) over_after += std::max(0L, s.load[k] + d.load[k] - opt.capacity);
            key = (long)(-(d.externals + opt.overflow_weight * (over_after - over)));
        }
        if (key > best_key) { best_key = key; best = b; }
    }
    return {best_key, best};
}

inline bool on_cut(const PartitionState& s, const Units& U, int u) {
    const PartitionGraph& G = *s.G;
    const int a = s.owner[U.node[U.ptr[u]]];
    for (int i = U.ptr[u]; i < U.ptr[u + 1]; i++) {
        const int v = U.node[i];
        for (int k = 0; k < s.K; k++) if (k != a && s.presence[(size_t)v * s.K + k] > 0) return true;
        for (int j = G.node_ptr[v]; j < G.node_ptr[v + 1]; j++) if (s.home[G.node_net[j]] != a) return true;
    }
    return false;
}

// Passes of k-way FM over units U: best move first (lazy max-heap, gains re-checked when popped),
// each unit moved at most once per pass, negative moves allowed, then roll back to the best
// prefix -- best = least overflow, then fewest externals. Meow.
inline FmStats fm_refine(PartitionState& s, const Units& U, const FmOptions& opt) {
    FmStats st;
    const PartitionGraph& G = *s.G;
    const int num_units = U.size();
    std::vector<int> stamp(num_units, 0), seen(num_units, -1);
    std::vector<char> locked(num_units, 0);
    for (int pass = 0; pass < opt.max_passes; pass++) {
        st.passes++;
        const auto pass_start = std::chrono::steady_clock::now();
        const long start_externals = s.externals, start_over = overflow_of(s.load, opt.capacity);
        std::fill(locked.begin(), locked.end(), 0);
        std::priority_queue<std::tuple<long, int, int, int>> heap;   // key, -unit, target, stamp
        for (int u = 0; u < num_units; u++) {
            if (opt.boundary_only && start_over == 0 && !on_cut(s, U, u)) continue;
            const auto [key, b] = best_target(s, U, u, opt, st);
            if (b >= 0) heap.emplace(key, -u, b, ++stamp[u]);
        }
        std::vector<std::pair<int, int>> moves;   // unit, from
        long best_over = start_over, best_externals = start_externals;
        size_t best_prefix = 0, since_best = 0;
        const size_t stall = std::max<size_t>(500, (size_t)(opt.stall_fraction * num_units));
        int visit = 0;
        while (!heap.empty() && since_best < stall) {
            const auto [key, neg_u, b_popped, popped_stamp] = heap.top(); heap.pop();
            const int u = -neg_u;
            if (locked[u] || popped_stamp != stamp[u]) continue;
            const auto [now_key, b] = best_target(s, U, u, opt, st);
            if (b < 0) continue;
            if (now_key < key) { heap.emplace(now_key, -u, b, ++stamp[u]); continue; }
            const int from = s.owner[U.node[U.ptr[u]]];
            s.evaluate(U, u, b, true);
            locked[u] = 1;
            moves.emplace_back(u, from);
            st.moves++;
            const long over = overflow_of(s.load, opt.capacity);
            if (over < best_over || (over == best_over && s.externals < best_externals)) {
                best_over = over; best_externals = s.externals; best_prefix = moves.size(); since_best = 0;
            } else {
                since_best++;
            }
            visit++;
            for (int i = U.ptr[u]; i < U.ptr[u + 1]; i++)
                for (int j = G.node_ptr[U.node[i]]; j < G.node_ptr[U.node[i] + 1]; j++) {
                    const int n = G.node_net[j];
                    for (int p = G.net_ptr[n]; p < G.net_ptr[n + 1]; p++) {
                        const int w = U.of_node[G.net_node[p]];
                        if (w < 0 || locked[w] || seen[w] == visit) continue;
                        seen[w] = visit;
                        const auto [wkey, wb] = best_target(s, U, w, opt, st);
                        ++stamp[w];
                        if (wb >= 0) heap.emplace(wkey, -w, wb, stamp[w]);
                    }
                }
        }
        for (size_t i = moves.size(); i > best_prefix; i--) s.evaluate(U, moves[i - 1].first, moves[i - 1].second, true);
        st.kept += best_prefix;
        st.best_externals = best_externals; st.best_over = best_over;
        st.pass_externals.push_back(s.externals);
        st.pass_seconds.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - pass_start).count());
        const bool improved_over = best_over < start_over;
        const long gain = start_externals - s.externals;
        if (!improved_over && (best_prefix == 0 || gain < opt.min_pass_gain * std::max(1L, start_externals))) break;
    }
    return st;
}

// ---- candidate 3: multilevel ----

// Heavy-edge matching: each unmatched unit (random order) pairs with the unmatched neighbor it
// shares the most net weight with, 1/(distinct nodes - 1) per shared net, if their weights fit
// `max_weight`. Nets wider than `max_net` are skipped for rating (they say little, cost a lot). Meow.
inline Units coarsen(const PartitionGraph& G, const Units& fine, long max_weight, std::mt19937& rng, int max_net = 32) {
    const int num_units = fine.size();
    std::vector<int> visit_order(num_units);
    for (int u = 0; u < num_units; u++) visit_order[u] = u;
    std::shuffle(visit_order.begin(), visit_order.end(), rng);
    std::vector<int> match(num_units, -1);
    std::vector<double> rating(num_units, 0.0);
    std::vector<int> rated;
    for (int u : visit_order) {
        if (match[u] >= 0) continue;
        for (int i = fine.ptr[u]; i < fine.ptr[u + 1]; i++)
            for (int j = G.node_ptr[fine.node[i]]; j < G.node_ptr[fine.node[i] + 1]; j++) {
                const int n = G.node_net[j];
                const int size = G.net_ptr[n + 1] - G.net_ptr[n];
                if (size < 2 || size > max_net) continue;
                const double w = 1.0 / (size - 1);
                for (int p = G.net_ptr[n]; p < G.net_ptr[n + 1]; p++) {
                    const int x = fine.of_node[G.net_node[p]];
                    if (x < 0 || x == u || match[x] >= 0) continue;
                    if (rating[x] == 0.0) rated.push_back(x);
                    rating[x] += w;
                }
            }
        int best = -1; double best_rating = 0.0;
        for (int x : rated) {
            if (rating[x] > best_rating && fine.weight(u) + fine.weight(x) <= max_weight) { best = x; best_rating = rating[x]; }
            rating[x] = 0.0;
        }
        rated.clear();
        match[u] = best >= 0 ? best : u;
        if (best >= 0) match[best] = u;
    }
    Units coarse;
    coarse.of_node.assign(G.num_nodes, -1);
    for (int u = 0; u < num_units; u++) {
        if (match[u] < u) continue;   // the pair is emitted once, from its lower unit
        const int cluster = coarse.size();
        auto append = [&](int x) {
            for (int i = fine.ptr[x]; i < fine.ptr[x + 1]; i++) { coarse.of_node[fine.node[i]] = cluster; coarse.node.push_back(fine.node[i]); }
        };
        append(u);
        if (match[u] != u) append(match[u]);
        coarse.ptr.push_back((int)coarse.node.size());
    }
    return coarse;
}

struct MultilevelOptions {
    int    coarsest_units = 2000;   // stop coarsening below K x this many units
    double min_shrink     = 0.92;   // ...or when a level shrinks by less than this
    double max_weight_frac = 0.02;  // a cluster holds at most this fraction of the capacity
    int    passes_per_level = 4;
    unsigned seed = 42;
    long   min_refine_units = 0;    // skip FM on levels with fewer units: a coarse move re-evaluates the flat netlist of every member. Meow.
};

struct MultilevelStats {
    int levels = 0; long coarsest = 0; FmStats fm;
    double coarsen_s = 0, initial_s = 0, refine_s = 0;
    std::vector<long> externals_after_level;   // coarsest first
    std::vector<double> seconds_per_level;      // coarsest first
    std::vector<long> units_per_level;          // coarsest first
};

inline std::vector<int> multilevel_partition(const PartitionGraph& G, int K, long capacity, const FmOptions& fm_opt,
                                             const MultilevelOptions& opt, MultilevelStats& stats) {
    auto seconds_since = [](std::chrono::steady_clock::time_point t) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
    };
    auto t = std::chrono::steady_clock::now();
    std::mt19937 rng(opt.seed);
    std::vector<Units> levels = {level0_units(G)};
    const long max_weight = std::max(2L, (long)(opt.max_weight_frac * capacity));
    while (levels.back().size() > (long)opt.coarsest_units * K) {
        Units next = coarsen(G, levels.back(), max_weight, rng);
        if (next.size() > opt.min_shrink * levels.back().size()) break;
        levels.push_back(std::move(next));
    }
    stats.levels = (int)levels.size();
    stats.coarsest = levels.back().size();
    stats.coarsen_s = seconds_since(t);
    t = std::chrono::steady_clock::now();
    PartitionState s;
    s.init(G, K, balanced_cut(G, levels.back(), unit_bfs_order(G, levels.back()), K, capacity));
    stats.initial_s = seconds_since(t);
    t = std::chrono::steady_clock::now();
    FmOptions level_opt = fm_opt;
    level_opt.max_passes = opt.passes_per_level;
    for (int l = (int)levels.size() - 1; l >= 0; l--) {
        if (levels[l].size() < opt.min_refine_units) { stats.seconds_per_level.push_back(0); stats.units_per_level.push_back(levels[l].size()); stats.externals_after_level.push_back(s.externals); continue; }
        const auto level_start = std::chrono::steady_clock::now();
        const FmStats st = fm_refine(s, levels[l], level_opt);
        stats.seconds_per_level.push_back(seconds_since(level_start));
        stats.units_per_level.push_back(levels[l].size());
        stats.fm.passes += st.passes; stats.fm.moves += st.moves; stats.fm.kept += st.kept; stats.fm.evaluations += st.evaluations;
        stats.externals_after_level.push_back(s.externals);
    }
    stats.refine_s = seconds_since(t);
    return s.owner;
}

} // namespace packer

#endif // PARTITION_HPP
