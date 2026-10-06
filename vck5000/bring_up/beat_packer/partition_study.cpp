// partition_study.cpp -- #42: the chunk partitioners side by side, scored end to end.
//
// For each design and method: the smallest K whose partition build_chunks_from_owner accepts (the
// real chunk builder, so "fits" is exact), its external slots, the partition's host time, and the
// cycle_model ledger: start-up + 1000 x 2 axes x cycles / 300 MHz, with the 1-float and the
// widened mailbox. Methods (partition.hpp): baseline, improved, fm (from baseline), fm+ (from
// improved), multilevel, and with --kahypar DIR the oracle's partitions (kahypar_partition.py) raw
// and FM-refined (kahypar+fm).
//
//   partition_study [--capacity SLOTS] [--methods a,b,...] [--export-hgr DIR] [--kahypar DIR]
//                   (--bookshelf DIR NAME | --def FILE NAME)...
//
// --export-hgr writes DIR/<design>.hgr (level-0 units; weight = slots) and exits that design. Meow.

#include "partition.hpp"
#include "cycle_model.hpp"

#include <chrono>
#include <fstream>
#include <sstream>

using namespace packer;

static double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

static std::string file_name(std::string name) {
    for (char& c : name) if (c == '/') c = '_';
    return name;
}

static void export_hgr(const PartitionGraph& G, const Units& U, const std::string& path) {
    std::vector<std::vector<int>> nets;
    std::vector<int> mark(U.size(), -1);
    for (int n = 0; n < (int)G.homed.size(); n++) {
        std::vector<int> pins;
        for (int p = G.net_ptr[n]; p < G.net_ptr[n + 1]; p++) {
            const int u = U.of_node[G.net_node[p]];
            if (u >= 0 && mark[u] != n) { mark[u] = n; pins.push_back(u); }
        }
        if (pins.size() >= 2) nets.push_back(std::move(pins));
    }
    std::ofstream out(path);
    out << nets.size() << " " << U.size() << " 11\n";
    for (const auto& pins : nets) {
        out << 1;
        for (int u : pins) out << " " << u + 1;
        out << "\n";
    }
    for (int u = 0; u < U.size(); u++) out << U.weight(u) << "\n";
}

static bool read_unit_partition(const std::string& path, const Units& U, int G_nodes, std::vector<int>& owner) {
    std::ifstream in(path);
    if (!in) return false;
    owner.assign(G_nodes, -1);
    for (int u = 0; u < U.size(); u++) {
        int block;
        if (!(in >> block)) return false;
        for (int i = U.ptr[u]; i < U.ptr[u + 1]; i++) owner[U.node[i]] = block;
    }
    return true;
}

struct Result {
    std::string method;
    int K = 0;
    bool fits = false;
    long externals = 0, max_slots = 0;
    double partition_s = 0, build_s = 0, layout_s = 0;
    Phases one_float, widened;
    std::string note;
};

int main(int argc, char** argv) {
    Config cfg;
    long capacity = 1L << 20;
    std::string methods = "baseline,improved,fm,fm+,multilevel", export_dir, kahypar_dir;
    MultilevelOptions ml_options;
    FmOptions fm_defaults;
    std::vector<std::pair<std::string, std::string>> designs;
    std::vector<char> is_def;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto next = [&]() { if (i + 1 >= argc) exit(2); return std::string(argv[++i]); };
        if      (arg == "--capacity")   capacity = std::stol(next());
        else if (arg == "--methods")    methods = next();
        else if (arg == "--export-hgr") export_dir = next();
        else if (arg == "--kahypar")    kahypar_dir = next();
        else if (arg == "--ml-passes")  ml_options.passes_per_level = std::stoi(next());
        else if (arg == "--ml-coarsest") ml_options.coarsest_units = std::stoi(next());
        else if (arg == "--ml-maxw")    ml_options.max_weight_frac = std::stod(next());
        else if (arg == "--ml-min-refine") ml_options.min_refine_units = std::stol(next());
        else if (arg == "--fm-passes")  fm_defaults.max_passes = std::stoi(next());
        else if (arg == "--fm-stall")   fm_defaults.stall_fraction = std::stod(next());
        else if (arg == "--bookshelf" || arg == "--def") {
            std::string path = next(), name = next();
            designs.emplace_back(path, name); is_def.push_back(arg == "--def");
        } else { fprintf(stderr, "unknown argument %s\n", arg.c_str()); return 2; }
    }
    if (!kahypar_dir.empty()) methods += ",kahypar,kahypar+fm";
    std::vector<std::string> method_list;
    { std::stringstream ss(methods); std::string m; while (std::getline(ss, m, ',')) method_list.push_back(m); }

    // The model has no bank-row rounding; leave room for it (a failed build retries tighter, then K+1). Meow.
    const double MODEL_MARGINS[] = {0.995, 0.98};
    printf("#42 partition_study: capacity=%ld, %.0f MHz, %d iterations x %d axes\n", capacity, CLOCK_HZ / 1e6, ITERATIONS, GRADIENT_AXES);

    for (size_t d = 0; d < designs.size(); d++) {
        auto t = std::chrono::steady_clock::now();
        const Netlist nl = is_def[d] ? read_def(designs[d].first, designs[d].second) : read_bookshelf(designs[d].first, designs[d].second);
        const double parse_s = seconds_since(t);
        Chunked base;
        base.capacity = capacity;
        t = std::chrono::steady_clock::now();
        resolve_pin_nodes(nl, base.global);
        const double resolve_s = seconds_since(t);
        t = std::chrono::steady_clock::now();
        const PartitionGraph G = make_partition_graph(base.global, cfg);
        const Units U0 = level0_units(G);
        const double graph_s = seconds_since(t);
        long slot_owners = 0, movable = 0;
        for (NodeKind kind : base.global.kind) slot_owners += needs_slot(kind);
        for (char m : nl.movable) movable += m;
        const int k_min = (int)std::max<long>(1, (slot_owners + capacity - 1) / capacity);

        printf("\n%s  movable=%ld units=%d homed_nets=%zu K_min=%d | parse %.1fs resolve %.1fs graph %.1fs\n", nl.name.c_str(), movable,
               U0.size(), G.homed.size(), k_min, parse_s, resolve_s, graph_s);
        if (!export_dir.empty()) {
            export_hgr(G, U0, export_dir + "/" + file_name(nl.name) + ".hgr");
            printf("  exported %s.hgr\n", file_name(nl.name).c_str());
            continue;
        }
        if (k_min == 1) { printf("  fits in one chunk: no partition\n"); continue; }

        t = std::chrono::steady_clock::now();
        const std::vector<int> bfs_order = locality_order(base.global);
        const double bfs_s = seconds_since(t);
        t = std::chrono::steady_clock::now();
        const std::vector<int> improved_order = unit_bfs_order(G, U0);
        const double improved_order_s = seconds_since(t);

        std::vector<Result> results;
        for (const std::string& method : method_list) {
            Result r;
            r.method = method;
            for (int K = k_min; K <= k_min + 2 && !r.fits; K++) {
                for (double margin : MODEL_MARGINS) {
                    const long model_capacity = (long)(margin * capacity);
                    std::vector<int> owner;
                    double extra_s = 0;   // shared inputs this method needs, charged to it
                    t = std::chrono::steady_clock::now();
                    FmOptions fm_opt = fm_defaults;
                    fm_opt.capacity = model_capacity;
                    if (method == "baseline") {
                        owner = order_owner(base.global, bfs_order, K);
                        extra_s = bfs_s;
                    } else if (method == "improved") {
                        owner = balanced_cut(G, U0, improved_order, K, model_capacity);
                        extra_s = graph_s + improved_order_s;
                    } else if (method == "baseline-bal") {   // ablation: baseline order, balanced cut. Meow.
                        owner = balanced_cut(G, U0, bfs_order, K, model_capacity);
                        extra_s = graph_s + bfs_s;
                    } else if (method == "improved-eq") {    // ablation: improved order, equal cut. Meow.
                        owner = order_owner(base.global, improved_order, K);
                        extra_s = improved_order_s;
                    } else if (method == "fm" || method == "fm+") {
                        PartitionState s;
                        s.init(G, K, method == "fm" ? order_owner(base.global, bfs_order, K)
                                                    : balanced_cut(G, U0, improved_order, K, model_capacity));
                        const FmStats st = fm_refine(s, U0, fm_opt);
                        owner = s.owner;
                        extra_s = graph_s + (method == "fm" ? bfs_s : improved_order_s);
                        r.note = std::to_string(st.passes) + " passes, " + std::to_string(st.kept) + "/" + std::to_string(st.moves) + " moves kept | externals by pass:";
                        for (size_t p = 0; p < st.pass_externals.size(); p++) {
                            char buf[48];
                            snprintf(buf, sizeof buf, " %ld@%.1fs", st.pass_externals[p], st.pass_seconds[p]);
                            r.note += buf;
                        }
                    } else if (method == "multilevel") {
                        MultilevelStats ms;
                        owner = multilevel_partition(G, K, model_capacity, fm_opt, ml_options, ms);
                        extra_s = graph_s;
                        char buf[160];
                        snprintf(buf, sizeof buf, "%d levels, coarsest %ld | coarsen %.1fs initial %.1fs refine %.1fs | externals coarsest->finest %ld -> %ld",
                                 ms.levels, ms.coarsest, ms.coarsen_s, ms.initial_s, ms.refine_s,
                                 ms.externals_after_level.front(), ms.externals_after_level.back());
                        r.note = buf;
                        r.note += " | per level (units:s:externals)";
                        for (size_t l = 0; l < ms.seconds_per_level.size(); l++) {
                            snprintf(buf, sizeof buf, " %ld:%.1f:%ld", ms.units_per_level[l], ms.seconds_per_level[l], ms.externals_after_level[l]);
                            r.note += buf;
                        }
                    } else if (method == "kahypar" || method == "kahypar+fm") {
                        const std::string path = kahypar_dir + "/" + file_name(nl.name) + ".k" + std::to_string(K) + ".part";
                        if (!read_unit_partition(path, U0, G.num_nodes, owner)) { r.note = "no " + path; break; }
                        if (method == "kahypar+fm") {
                            PartitionState s;
                            s.init(G, K, owner);
                            fm_refine(s, U0, fm_opt);
                            owner = s.owner;
                        }
                        extra_s = graph_s;   // + KaHyPar's own time, printed by kahypar_partition.py. Meow.
                    } else { fprintf(stderr, "unknown method %s\n", method.c_str()); return 2; }
                    r.partition_s = seconds_since(t) + extra_s;

                    Chunked ch;
                    ch.global = base.global;
                    ch.capacity = capacity;
                    t = std::chrono::steady_clock::now();
                    r.fits = build_chunks_from_owner(ch, owner, K, cfg);
                    r.build_s = seconds_since(t);
                    r.K = K;
                    if (!r.fits) continue;
                    ch.fits = true;
                    r.externals = ch.externals;
                    r.max_slots = 0;
                    for (const Chunk& c : ch.chunks) r.max_slots = std::max(r.max_slots, c.enc.num_slots);
                    r.one_float = model_cycles(ch, nullptr);
                    t = std::chrono::steady_clock::now();
                    const WideMailbox wide = build_wide_mailbox(ch, cfg);
                    r.layout_s = seconds_since(t);
                    r.widened = model_cycles(ch, &wide);
                    if (check_chunked(nl, ch, cfg)) { r.note += " CHECK_CHUNKED FAIL"; r.fits = false; }
                    break;
                }
                if (!r.note.empty() && r.note.rfind("no ", 0) == 0) break;
            }
            results.push_back(r);
            const double startup = parse_s + resolve_s + r.partition_s + r.build_s;
            if (!r.fits) { printf("  %-11s does not fit up to K=%d %s\n", method.c_str(), r.K, r.note.c_str()); continue; }
            printf("  %-11s K=%d external=%8ld (%5.1f%%) max_slots=%7ld | partition %6.1fs build %5.1fs | cyc/axis 1-float %8ld (mailbox %4.1f%%) "
                   "widened %8ld (mailbox %4.1f%%) | end-to-end 1-float %6.1fs widened %6.1fs | %s\n",
                   method.c_str(), r.K, r.externals, 100.0 * r.externals / movable, r.max_slots, r.partition_s, r.build_s,
                   r.one_float.total(), 100.0 * r.one_float.mailbox() / r.one_float.total(), r.widened.total(),
                   100.0 * r.widened.mailbox() / r.widened.total(), startup + device_seconds(r.one_float),
                   startup + r.layout_s + device_seconds(r.widened), r.note.c_str());
            fflush(stdout);
        }
    }
    return 0;
}
