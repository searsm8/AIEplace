// tail_layout.cpp -- #47 M1: can each chunk's borrowed nodes form a tail that iteration_update
// writes as a plain sequential stream? (.claude/1_REVIEW/reports/_NEW_PLAN_47_mailbox_redesign_20261006.md)
//
// Emission order into borrower k's tail: owners j ascending, each owner's borrowed nodes in its own slot
// order under the tail layout (owned movable rows only, per bank in node order -- the order the
// update streams them). Layout (M0's): each borrowed node takes the next tail row OF ITS BANK (the bank
// encode_chunked's colouring chose), so the tail is max_b(borrowed nodes in bank b) rows. The update writes
// the tail row by row, holding rows in an on-chip assembly buffer until they are final (every bank
// has filled the row, or will put nothing more there -- known statically). Reported: tail size / E
// (1.00 = no padding), the buffer depth in rows, the most entries for one bank among 16 consecutive
// emissions, and the update's stalls / FIFO depth at W buffer writes per cycle per borrower.
// Colouring hints toward the emission order were measured in ed888f9 and did not pay. Meow.

#include "cycle_model.hpp"

using namespace packer;

constexpr int WRITE_RATES[] = {1, 2, 4, 8};   // assembly-buffer entries per cycle, per borrower stream
constexpr int NUM_RATES = 4;

struct TailCost { long tail_slots = 0, buffer_rows = 0, bank_ports = 0; };

static TailCost tail_cost(const std::vector<int>& order_banks) {
    TailCost t;
    std::vector<long> final_count(BANKS, 0), count(BANKS, 0);
    for (int b : order_banks) final_count[b]++;
    const long rows = *std::max_element(final_count.begin(), final_count.end());
    t.tail_slots = rows * BANKS;
    long flushed = 0, highest = 0;   // rows already written; rows any bank has started
    auto row_final = [&](long r) {
        for (int k = 0; k < BANKS; k++) if (count[k] <= r && final_count[k] > r) return false;
        return true;
    };
    for (size_t i = 0; i < order_banks.size(); i++) {
        highest = std::max(highest, ++count[order_banks[i]]);
        while (flushed < rows && row_final(flushed)) flushed++;
        t.buffer_rows = std::max(t.buffer_rows, highest - flushed);
        if (i % LANES == LANES - 1 || i + 1 == order_banks.size()) {
            std::vector<int> group(BANKS, 0);
            for (size_t g = i - i % LANES; g <= i; g++) t.bank_ports = std::max<long>(t.bank_ports, ++group[order_banks[g]]);
        }
    }
    return t;
}

int main(int argc, char** argv) {
    Config cfg;
    long capacity = 1L << 20;
    std::vector<std::string> designs;   // netlist files: make -C host/sw_flat netlists
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        auto next = [&]() { if (i + 1 >= argc) exit(2); return std::string(argv[++i]); };
        if (arg == "--capacity") capacity = std::stol(next());
        else if (arg == "--netlist") designs.push_back(next());
        else { fprintf(stderr, "usage: tail_layout [--capacity SLOTS] (--netlist FILE)...\n"); exit(2); }
    }
    printf("#47 tail_layout: capacity=%ld; tail size as a multiple of E (1.00 = no padding)\n", capacity);

    for (size_t d = 0; d < designs.size(); d++) {
        const Netlist nl = read_netlist(designs[d]);
        const Chunked ch = encode_chunked(nl, cfg, capacity);
        if (!ch.fits) { printf("%s: no K fits\n", nl.name.c_str()); continue; }
        if (ch.num_chunks == 1) continue;
        const int K = ch.num_chunks;

        // Owner-side slot of every owned movable node under the tail layout. Meow.
        std::vector<long> owner_slot(ch.global.kind.size(), -1);
        std::vector<long> owned_rows(K, 0);
        for (int j = 0; j < K; j++) {
            const Chunk& c = ch.chunks[j];
            std::vector<long> next_row(BANKS, 0);
            for (size_t l = 0; l < c.work_node.size(); l++)
                if (!c.borrowed[l] && c.enc.bank[l] != NO_BANK && slot_class(c.enc.kind[l]) == 1)
                    owner_slot[c.work_node[l]] = next_row[c.enc.bank[l]]++ * BANKS + c.enc.bank[l];
            owned_rows[j] = *std::max_element(next_row.begin(), next_row.end());
        }

        long E = 0, max_slots = 0;
        TailCost cost;
        std::vector<long> max_fifo(NUM_RATES, 0);
        std::vector<std::vector<long>> stall(NUM_RATES, std::vector<long>(K, 0));
        for (int k = 0; k < K; k++) {
            const Chunk& c = ch.chunks[k];
            std::vector<int> order;   // k's borrowed locals in emission order
            for (size_t l = 0; l < c.work_node.size(); l++) if (c.borrowed[l]) order.push_back((int)l);
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                const int ja = ch.owner[c.work_node[a]], jb = ch.owner[c.work_node[b]];
                return ja != jb ? ja < jb : owner_slot[c.work_node[a]] < owner_slot[c.work_node[b]]; });
            E += (long)order.size();

            // Write rate: owner j's update reaches its slot s at beat s / LANES and hands borrower k's
            // assembly buffer W entries per cycle. An entry waits (FIFO) behind pending writes; owner
            // j's update stalls by however far k's last write runs past its final beat. Meow.
            for (int w = 0; w < NUM_RATES; w++)
                for (size_t i = 0; i < order.size();) {
                    const int j = ch.owner[c.work_node[order[i]]];
                    double write = -1;
                    for (; i < order.size() && ch.owner[c.work_node[order[i]]] == j; i++) {
                        const double beat = (double)(owner_slot[c.work_node[order[i]]] / LANES);
                        write = std::max(beat, write + 1.0 / WRITE_RATES[w]);
                        max_fifo[w] = std::max(max_fifo[w], (long)((write - beat) * WRITE_RATES[w]));
                    }
                    stall[w][j] = std::max(stall[w][j], (long)std::ceil(write) - (owned_rows[j] * BANKS / LANES - 1));
                }

            std::vector<int> banks;
            for (int l : order) banks.push_back(c.enc.bank[l]);
            const TailCost t = tail_cost(banks);
            cost.tail_slots += t.tail_slots;
            cost.buffer_rows = std::max(cost.buffer_rows, t.buffer_rows);
            cost.bank_ports = std::max(cost.bank_ports, t.bank_ports);
            const long fixed_rows = (c.enc.num_slots - c.enc.first_fixed_slot) / BANKS;
            max_slots = std::max(max_slots, (owned_rows[k] + t.tail_slots / BANKS + fixed_rows) * BANKS);
        }
        long update_beats = 0;
        for (int j = 0; j < K; j++) update_beats += owned_rows[j] * BANKS / LANES;
        printf("\n%s  K=%d borrowed=%ld  (update: %ld owned beats)\n", nl.name.c_str(), K, E, update_beats);
        for (int w = 0; w < NUM_RATES; w++) {
            long stall_total = 0;
            for (int j = 0; j < K; j++) stall_total += std::max(0L, stall[w][j]);
            printf("  write rate %d/cycle per borrower: update stalls %7ld cycles (%6.2f%%), FIFO %6ld entries\n",
                   WRITE_RATES[w], stall_total, 100.0 * stall_total / update_beats, max_fifo[w]);
        }
        printf("  tail/E %5.3f  assembly buffer %4ld rows  bank ports %ld  max chunk %ld slots with tail (%s capacity)\n",
               (double)cost.tail_slots / E, cost.buffer_rows, cost.bank_ports, max_slots, max_slots <= capacity ? "within" : "OVER");
        fflush(stdout);
    }
    return 0;
}
