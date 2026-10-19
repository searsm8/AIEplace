// window_model.cpp -- #47 R, sliding window: how big an on-chip shared region must be if each shared
// node is resident only from the first chunk that uses it to the last (its live interval), not for the
// whole call. Slots freed when a node dies are reused, so the window needs the PEAK live set over the
// chunk sequence (intervals: the peak is exactly achievable). Compared against today's per-chunk
// tails and the global region (every shared node, all call long). Chunk order matters, so the peak is
// reported in today's order and in the best order (all permutations, K <= 8).
// Node counts, not rows: bank rounding (1-9% on the global region) is not charged. Meow.

#include "cycle_model.hpp"

#include <numeric>

using namespace packer;

static long peak_live(const std::vector<uint64_t>& users, const std::vector<int>& position, int num_chunks) {
    std::vector<long> delta(num_chunks + 1, 0);
    for (uint64_t mask : users) {
        int first = num_chunks, last = -1;
        for (int k = 0; k < num_chunks; k++)
            if (mask >> k & 1) { first = std::min(first, position[k]); last = std::max(last, position[k]); }
        delta[first]++;
        delta[last + 1]--;
    }
    long live = 0, peak = 0;
    for (int p = 0; p < num_chunks; p++) peak = std::max(peak, live += delta[p]);
    return peak;
}

int main(int argc, char** argv) {
    Config cfg;
    long capacity = 1L << 20;
    std::vector<std::string> designs;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--capacity" && i + 1 < argc) capacity = std::stol(argv[++i]);
        else if (arg == "--netlist" && i + 1 < argc) designs.push_back(argv[++i]);
        else { fprintf(stderr, "usage: window_model [--capacity SLOTS] (--netlist FILE)...\n"); return 2; }
    }
    printf("#47 window_model: capacity=%ld (node counts; bank rounding not charged)\n", capacity);
    for (const std::string& path : designs) {
        const Netlist nl = read_netlist(path);
        const Chunked ch = encode_chunked(nl, cfg, capacity);
        if (!ch.fits || ch.num_chunks < 2) continue;
        const int K = ch.num_chunks;

        std::vector<uint64_t> users_of(ch.global.kind.size(), 0);   // per global node: chunks that hold it
        for (int k = 0; k < K; k++) {
            const Chunk& c = ch.chunks[k];
            for (size_t l = 0; l < c.work_node.size(); l++)
                if (c.external[l]) users_of[c.work_node[l]] |= (1ull << k) | (1ull << ch.owner[c.work_node[l]]);
        }
        std::vector<uint64_t> users;
        for (uint64_t mask : users_of) if (mask) users.push_back(mask);
        const long global_region = (long)users.size();

        // Today, per chunk: its owned shared nodes + its tail. Window layout: private | window | fixed. Meow.
        long max_today_shared = 0, max_tail = 0, max_private = 0, max_fixed = 0, max_today_slots = 0;
        for (int k = 0; k < K; k++) {
            const Chunk& c = ch.chunks[k];
            long tail = 0, owned_shared = 0, private_nodes = 0;
            for (size_t l = 0; l < c.work_node.size(); l++) {
                if (c.enc.bank[l] == NO_BANK || slot_class(c.enc.kind[l]) == 0) continue;
                if (c.external[l]) tail++;
                else if (users_of[c.work_node[l]]) owned_shared++;
                else private_nodes++;
            }
            max_tail = std::max(max_tail, tail);
            max_today_shared = std::max(max_today_shared, owned_shared + tail);
            max_private = std::max(max_private, private_nodes);
            max_fixed = std::max(max_fixed, c.enc.num_slots - c.enc.first_fixed_slot);
            max_today_slots = std::max(max_today_slots, c.enc.num_slots);
        }

        std::vector<int> order(K);
        std::iota(order.begin(), order.end(), 0);
        const long today_order_peak = peak_live(users, order, K);
        long best_peak = today_order_peak;
        std::vector<int> best_order = order;
        if (K <= 8)
            while (std::next_permutation(order.begin(), order.end())) {
                const long peak = peak_live(users, order, K);
                if (peak < best_peak) { best_peak = peak; best_order = order; }
            }
        std::vector<int> run_order(K);   // best_order holds positions; print the chunk run order
        for (int k = 0; k < K; k++) run_order[best_order[k]] = k;

        const long window_slots = max_private + best_peak + max_fixed;
        printf("\n%-26s K=%d  global region %ld (%.1f%% cap) | today per chunk: tail max %ld, owned shared + tail max %ld | "
               "window peak: today order %ld, best order %ld (%.1f%% cap, %.2fx global) run order",
               nl.name.c_str(), K, global_region, 100.0 * global_region / capacity, max_tail, max_today_shared,
               today_order_peak, best_peak, 100.0 * best_peak / capacity, (double)best_peak / global_region);
        for (int k : run_order) printf(" %d", k);
        printf(" | slots ~ private %ld + window %ld + fixed %ld = %ld (today max %ld) %s\n", max_private, best_peak,
               max_fixed, window_slots, max_today_slots, window_slots <= capacity ? "fits" : "DOES NOT FIT");
        fflush(stdout);
    }
    return 0;
}
