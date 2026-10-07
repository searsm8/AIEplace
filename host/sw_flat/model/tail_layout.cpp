// tail_layout.cpp -- #47 M1: can each chunk's external slots form a tail that iteration_update
// writes as a plain sequential stream? (.claude/1_REVIEW/reports/_NEW_PLAN_47_mailbox_redesign_20261006.md)
//
// Emission order into consumer k's tail: owners j ascending, each owner's externals in its own slot
// order under the tail layout (owned movable rows only, per bank in node order -- the order the
// update streams them). Tail position p has bank p mod 32, so an external lands in order only if its
// bank, set by k's colouring, is p mod 32. Two colourings:
//   today   : the banks encode_chunked chose;
//   hinted  : k re-coloured with bank_hint = p mod 32 for its externals and its owned banks frozen
//             (so every owner's slot order, and with it every hint, stays valid).
// Layout (M0's): each external takes the next tail row OF ITS BANK, so the tail is
// max_b(externals in bank b) rows. The update writes the tail row by row, so it holds rows in an
// on-chip assembly buffer until they are final (every bank has filled the row, or will put nothing
// more there -- known statically). Costs: tail size / E (1.00 = no padding), the buffer depth in rows,
// and the most entries for one bank among 16 consecutive emissions (buffer write ports per bank). Meow.

#include "cycle_model.hpp"

using namespace packer;

// The colouring inputs build_chunks_from_owner gives a chunk, with none of encode_resolved's outputs. Meow.
static Encoded fresh_inputs(const Encoded& e) {
    Encoded f;
    f.work = e.work;
    f.kind = e.kind;
    f.base_node = e.base_node;
    f.pinned_offset = e.pinned_offset;
    f.macro_pins = e.macro_pins;
    f.unique_nodes = e.unique_nodes;
    f.in_scope_nets = e.in_scope_nets;
    for (int axis = 0; axis < 2; axis++) f.offset_table[axis] = e.offset_table[axis];
    return f;
}

constexpr int WRITE_RATES[] = {1, 2, 4, 8};   // assembly-buffer entries per cycle, per consumer stream
constexpr int NUM_RATES = 4;

struct TailCost { long hits = 0, tail_slots = 0, buffer_rows = 0, bank_ports = 0; };

static TailCost tail_cost(const std::vector<int>& order_banks) {
    TailCost t;
    std::vector<long> final_count(BANKS, 0), count(BANKS, 0);
    for (int b : order_banks) final_count[b]++;
    const long rows = *std::max_element(final_count.begin(), final_count.end());
    t.tail_slots = rows * BANKS;
    long flushed = 0, highest = 0;   // rows already written; rows any bank has started
    for (size_t i = 0; i < order_banks.size(); i++) {
        const int b = order_banks[i];
        if (b == (int)(i % BANKS)) t.hits++;
        highest = std::max(highest, ++count[b]);
        auto row_final = [&](long r) {
            for (int k = 0; k < BANKS; k++) if (count[k] <= r && final_count[k] > r) return false;
            return true;
        };
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
    std::vector<std::pair<std::string, std::string>> designs;
    std::vector<char> is_def;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        auto next = [&]() { if (i + 1 >= argc) exit(2); return std::string(argv[++i]); };
        if (arg == "--capacity") capacity = std::stol(next());
        else if (arg == "--bookshelf" || arg == "--def") {
            const std::string path = next(), name = next();
            designs.emplace_back(path, name); is_def.push_back(arg == "--def");
        } else { fprintf(stderr, "usage: tail_layout [--capacity SLOTS] (--bookshelf DIR NAME | --def FILE NAME)...\n"); exit(2); }
    }
    printf("#47 tail_layout: capacity=%ld; tail size as a multiple of E (1.00 = no padding)\n", capacity);
    int failures = 0;

    for (size_t d = 0; d < designs.size(); d++) {
        const Netlist nl = is_def[d] ? read_def(designs[d].first, designs[d].second)
                                     : read_bookshelf(designs[d].first, designs[d].second);
        const Chunked ch = encode_chunked(nl, cfg, capacity);
        if (!ch.fits) { printf("%s: no K fits\n", nl.name.c_str()); continue; }
        if (ch.num_chunks == 1) continue;
        const int K = ch.num_chunks;
        const size_t num_global = ch.global.kind.size();

        // Owner-side slot of every owned movable node under the tail layout. Meow.
        std::vector<long> owner_slot(num_global, -1);
        std::vector<long> owned_rows(K, 0);
        for (int j = 0; j < K; j++) {
            const Chunk& c = ch.chunks[j];
            std::vector<long> next_row(BANKS, 0);
            for (size_t l = 0; l < c.work_node.size(); l++)
                if (!c.external[l] && c.enc.bank[l] != NO_BANK && slot_class(c.enc.kind[l]) == 1)
                    owner_slot[c.work_node[l]] = next_row[c.enc.bank[l]]++ * BANKS + c.enc.bank[l];
            owned_rows[j] = *std::max_element(next_row.begin(), next_row.end());
        }

        long E = 0, beats_today = 0, beats_hinted = 0, collide_today = 0, collide_hinted = 0, max_slots_hinted = 0;
        TailCost today_cost, hinted_cost;
        std::vector<long> max_fifo(NUM_RATES, 0);
        std::vector<std::vector<long>> stall(NUM_RATES, std::vector<long>(K, 0));
        for (int k = 0; k < K; k++) {
            const Chunk& c = ch.chunks[k];
            std::vector<int> order;   // k's external locals in emission order
            for (size_t l = 0; l < c.work_node.size(); l++) if (c.external[l]) order.push_back((int)l);
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                const int ja = ch.owner[c.work_node[a]], jb = ch.owner[c.work_node[b]];
                return ja != jb ? ja < jb : owner_slot[c.work_node[a]] < owner_slot[c.work_node[b]]; });
            E += (long)order.size();

            // Write rate: owner j's update reaches its slot s at beat s / LANES and hands consumer k's
            // assembly buffer one entry per cycle. An entry waits (FIFO) if the previous write is
            // still pending; owner j's update stalls by however far k's last write runs past its
            // final beat. Meow.
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

            Encoded hinted = fresh_inputs(c.enc);
            {   // Self-check: the copied inputs, unhinted, reproduce the chunk exactly. Meow.
                Encoded again = fresh_inputs(c.enc);
                encode_resolved(again, cfg);
                if (again.node_slot != c.enc.node_slot || again.records[0] != c.enc.records[0]) {
                    printf("FAIL %s chunk %d: re-encoding the copied inputs does not reproduce the chunk\n", nl.name.c_str(), k);
                    failures++;
                }
            }
            hinted.bank_hint.assign(c.work_node.size(), NO_BANK);
            hinted.bank_frozen.assign(c.work_node.size(), 0);
            for (size_t l = 0; l < c.work_node.size(); l++)
                if (!c.external[l] && c.enc.bank[l] != NO_BANK && slot_class(c.enc.kind[l]) == 1) {
                    hinted.bank_hint[l] = c.enc.bank[l];
                    hinted.bank_frozen[l] = 1;
                }
            for (size_t i = 0; i < order.size(); i++) hinted.bank_hint[order[i]] = (int)(i % BANKS);
            encode_resolved(hinted, cfg);

            std::vector<long> check_rows(BANKS, 0);
            for (size_t l = 0; l < c.work_node.size(); l++)
                if (hinted.bank_frozen[l] && hinted.bank[l] != c.enc.bank[l]) { printf("FAIL %s chunk %d: a frozen bank moved\n", nl.name.c_str(), k); failures++; break; }

            std::vector<int> banks_today, banks_hinted;
            for (int l : order) { banks_today.push_back(c.enc.bank[l]); banks_hinted.push_back(hinted.bank[l]); }
            const TailCost t0 = tail_cost(banks_today), t1 = tail_cost(banks_hinted);
            for (TailCost* sum : {&today_cost, &hinted_cost}) {
                const TailCost& t = sum == &today_cost ? t0 : t1;
                sum->hits += t.hits; sum->tail_slots += t.tail_slots;
                sum->buffer_rows = std::max(sum->buffer_rows, t.buffer_rows); sum->bank_ports = std::max(sum->bank_ports, t.bank_ports);
            }
            beats_today += (long)c.enc.issue.size(); beats_hinted += (long)hinted.issue.size();
            collide_today += c.enc.colliding_nets; collide_hinted += hinted.colliding_nets;
            const long fixed_rows = (hinted.num_slots - hinted.first_fixed_slot) / BANKS;
            max_slots_hinted = std::max(max_slots_hinted, (owned_rows[k] + t1.tail_slots / BANKS + fixed_rows) * BANKS);
        }
        auto row = [&](const char* label, const TailCost& t) {
            printf("  %-7s in-order %5.1f%% | tail/E %5.3f  assembly buffer %6ld rows  bank ports %2ld\n", label,
                   100.0 * t.hits / E, (double)t.tail_slots / E, t.buffer_rows, t.bank_ports);
        };
        long update_beats = 0;
        for (int j = 0; j < K; j++) update_beats += owned_rows[j] * BANKS / LANES;
        printf("\n%s  K=%d external=%ld  (update: %ld owned beats)\n", nl.name.c_str(), K, E, update_beats);
        for (int w = 0; w < NUM_RATES; w++) {
            long stall_total = 0;
            for (int j = 0; j < K; j++) stall_total += std::max(0L, stall[w][j]);
            printf("  write rate %d/cycle per consumer: update stalls %7ld cycles (%6.2f%%), FIFO %6ld entries\n",
                   WRITE_RATES[w], stall_total, 100.0 * stall_total / update_beats, max_fifo[w]);
        }
        row("today", today_cost);
        row("hinted", hinted_cost);
        printf("  hinted: record beats %+ld (%+.2f%%), colliding nets %ld (today %ld), max chunk %ld slots with tail (%s capacity)\n",
               beats_hinted - beats_today, 100.0 * (beats_hinted - beats_today) / beats_today, collide_hinted, collide_today,
               max_slots_hinted, max_slots_hinted <= capacity ? "within" : "OVER");
        if (collide_hinted > collide_today) { printf("FAIL %s: hinting created colliding nets\n", nl.name.c_str()); failures++; }
        fflush(stdout);
    }
    if (failures) { printf("\nFAIL: %d check(s)\n", failures); return 1; }
    return 0;
}
