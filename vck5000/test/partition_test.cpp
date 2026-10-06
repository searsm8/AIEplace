// Verify the chunk partitioners (bring_up/beat_packer/partition.hpp, #42).
//
// Golden: build_chunks_from_owner itself -- the code that builds the chunks the device runs. The
// partitioners' objective is meant to BE its external-slot count, so every check is exact:
//
//   [1] MODEL       PartitionState(owner) vs build_chunks_from_owner(owner), for the baseline cut and
//                   for random owners: externals equal; per chunk, load == slot nodes it holds
//   [2] MOVES       3000 random unit moves: each predicted delta (externals, every load) equals the
//                   applied change, and the final state equals a fresh init of the final owners
//   [3] FM          fm_refine ends at its last pass's best prefix (rollback) and no worse than it
//                   started (overflow, then externals), its state
//                   equals a fresh init, build_chunks_from_owner agrees and fits
//   [4] MULTILEVEL  the same, and every macro pin is in its macro's chunk
//   [5] COVERAGE    macros and large nets exist; K >= 3; FM and multilevel both strictly beat the
//                   baseline's externals (else [3]/[4] could pass vacuously)
//   [6] DEFAULT     encode_chunked with the default Config IS one FM pass on the baseline cut at the K
//                   it chose (same owners, same externals); that K is no larger than the bare cut's and
//                   the externals are strictly fewer (here FM fits K=4 where the bare cut needs 5) Meow.

#include "record_design.hpp"
#include "partition.hpp"

#include <cstdio>

using namespace packer;

static int failures = 0;
static void expect(bool ok, const char* what) {
    if (!ok) { printf("FAIL %s\n", what); failures++; }
}

static bool same_state(const PartitionState& a, const PartitionState& b) {
    return a.owner == b.owner && a.count == b.count && a.home == b.home && a.presence == b.presence &&
           a.load == b.load && a.externals == b.externals;
}

// [1]: the model against the real chunk builder. Meow.
static bool model_matches(const Chunked& base, const PartitionGraph& G, int K, const std::vector<int>& owner, const Config& cfg,
                          long* externals_out = nullptr) {
    Chunked ch;
    ch.global = base.global;
    ch.capacity = LONG_MAX;
    if (!build_chunks_from_owner(ch, owner, K, cfg)) return false;
    PartitionState s;
    s.init(G, K, ch.owner);   // build_chunks_from_owner forces macro pins to their macro; use its owners
    bool ok = s.externals == ch.externals;
    for (int k = 0; k < K; k++) {
        long slot_nodes = 0;
        for (int node : ch.chunks[k].work_node) slot_nodes += needs_slot(ch.global.kind[node]);
        ok &= s.load[k] == slot_nodes;
    }
    if (externals_out) *externals_out = ch.externals;
    return ok;
}

int main() {
    const Config cfg;
    fixture::SyntheticSpec spec;
    spec.cells = 3000; spec.macros = 6; spec.fixed = 40; spec.nets = 2600;
    const Netlist nl = fixture::build_synthetic(20261005u, spec);
    const long capacity = 3000;
    Config bare_cfg = cfg;
    bare_cfg.partition_fm_passes = 0;   // the bare BFS cut, the reference everything is measured against
    const Chunked base = encode_chunked(nl, bare_cfg, capacity);
    const int K = base.num_chunks;
    const PartitionGraph G = make_partition_graph(base.global, cfg);
    const Units U = level0_units(G);
    std::mt19937 rng(7);

    // [1]
    bool model_ok = model_matches(base, G, K, base.owner, cfg);
    for (int trial = 0; trial < 3; trial++) {
        std::vector<int> owner(G.num_nodes, -1);
        for (int u = 0; u < U.size(); u++) {
            const int k = (int)(rng() % K);
            for (int i = U.ptr[u]; i < U.ptr[u + 1]; i++) owner[U.node[i]] = k;
        }
        model_ok &= model_matches(base, G, K, owner, cfg);
    }
    printf("%s [1] model vs build_chunks_from_owner (baseline + 3 random owners, K=%d)\n", model_ok ? "ok  " : "FAIL", K);
    expect(model_ok, "[1] model");

    // [2]
    PartitionState s;
    s.init(G, K, base.owner);
    bool moves_ok = true;
    for (int m = 0; m < 3000; m++) {
        const int u = (int)(rng() % U.size()), b = (int)(rng() % K);
        const MoveDelta predicted = s.evaluate(U, u, b, false);
        const long externals_before = s.externals;
        const std::vector<long> load_before = s.load;
        const MoveDelta applied = s.evaluate(U, u, b, true);
        moves_ok &= predicted.externals == applied.externals && s.externals - externals_before == predicted.externals;
        for (int k = 0; k < K; k++) moves_ok &= s.load[k] - load_before[k] == predicted.load[k] && predicted.load[k] == applied.load[k];
    }
    PartitionState fresh;
    fresh.init(G, K, s.owner);
    moves_ok &= same_state(s, fresh);
    printf("%s [2] 3000 random moves: deltas exact, final state == fresh init\n", moves_ok ? "ok  " : "FAIL");
    expect(moves_ok, "[2] moves");

    // [3]
    PartitionState fm;
    fm.init(G, K, base.owner);
    const long base_externals = fm.externals, base_over = overflow_of(fm.load, capacity);
    FmOptions opt;
    opt.capacity = capacity;
    const FmStats st = fm_refine(fm, U, opt);
    fresh.init(G, K, fm.owner);
    const long fm_over = overflow_of(fm.load, capacity);
    long fm_built = -1;
    const bool rolled_back = fm.externals == st.best_externals && fm_over == st.best_over;   // ends AT the best prefix
    const bool fm_ok = same_state(fm, fresh) && rolled_back && (fm_over < base_over || (fm_over == base_over && fm.externals <= base_externals)) &&
                       model_matches(base, G, K, fm.owner, cfg, &fm_built) && fm_built == fm.externals;
    printf("%s [3] FM: externals %ld -> %ld, overflow %ld -> %ld (%d passes, %ld moves, %ld kept)\n", fm_ok ? "ok  " : "FAIL",
           base_externals, fm.externals, base_over, fm_over, st.passes, st.moves, st.kept);
    expect(fm_ok, "[3] FM");

    // [4]
    MultilevelStats ml_stats;
    MultilevelOptions ml_opt;
    ml_opt.coarsest_units = 50;
    const std::vector<int> ml_owner = multilevel_partition(G, K, capacity, opt, ml_opt, ml_stats);
    PartitionState ml;
    ml.init(G, K, ml_owner);
    bool macros_together = true;
    for (const MacroPin& mp : base.global.macro_pins) macros_together &= ml_owner[mp.pin_node] == ml_owner[mp.macro_node];
    long ml_built = -1;
    const bool ml_ok = macros_together && model_matches(base, G, K, ml_owner, cfg, &ml_built) && ml_built == ml.externals &&
                       overflow_of(ml.load, capacity) <= base_over;
    printf("%s [4] multilevel: externals %ld, overflow %ld (%d levels, coarsest %ld units)\n", ml_ok ? "ok  " : "FAIL",
           ml.externals, overflow_of(ml.load, capacity), ml_stats.levels, ml_stats.coarsest);
    expect(ml_ok, "[4] multilevel");

    // [5]
    long large = 0;
    for (int net : G.homed) large += is_large(base.global.work.nets[net]);
    const bool coverage = !base.global.macro_pins.empty() && large > 0 && K >= 3 && fm.externals < base_externals &&
                          ml.externals < base_externals;
    printf("%s [5] coverage: %zu macro pins, %ld large nets, K=%d, baseline %ld > FM %ld, multilevel %ld\n", coverage ? "ok  " : "FAIL",
           base.global.macro_pins.size(), large, K, base_externals, fm.externals, ml.externals);
    expect(coverage, "[5] coverage");

    // [6]
    const Chunked chosen = encode_chunked(nl, cfg, capacity);
    const int chosen_k = chosen.num_chunks;
    PartitionState one_pass;
    one_pass.init(G, chosen_k, order_owner(base.global, locality_order(base.global), chosen_k));
    FmOptions one_opt;
    one_opt.capacity = chosen.fm_target;   // the target encode_chunked settled on (it tightens after a rounding miss)
    one_opt.max_passes = 1;
    fm_refine(one_pass, U, one_opt);
    const bool default_ok = cfg.partition_fm_passes == 1 && chosen.fits && chosen.fm_target > 0 && chosen_k <= K &&
                            chosen.owner == one_pass.owner && chosen.externals == one_pass.externals &&
                            chosen.externals < base.externals;
    printf("%s [6] default encode_chunked == 1 FM pass on the baseline: K=%d, externals %ld (bare cut K=%d, %ld); "
           "target %ld, re-run %ld externals, owners %s\n",
           default_ok ? "ok  " : "FAIL", chosen_k, chosen.externals, K, base.externals, chosen.fm_target, one_pass.externals,
           chosen.owner == one_pass.owner ? "equal" : "differ");
    expect(default_ok, "[6] default");

    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
