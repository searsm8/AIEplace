#ifndef SW_FLAT_BORROWED_FUSION_HPP
#define SW_FLAT_BORROWED_FUSION_HPP

// borrowed_fusion.hpp -- #47 M2, the software golden of option A (Mark, 2026-10-09): the chunk exchange
// done inside iteration_update's stream, not in hpwl_gradient_CU's send_borrowed_pass and merge_borrowed_pass.
//
// iteration_update streams each owner chunk's owned slots once, ascending. Every borrower's borrowed
// block for that owner is sorted by owner slot (packer rule C7), so each borrower is a cursor that
// the stream advances:
//   fused_send   (R1)  at owned slot s, every borrower whose cursor is at s gets pos[s] in its borrowed slot
//   fused_merge  (R4)  at owned slot s, grad[s] = partial + each borrower's borrowed gradient for s, in
//                      ASCENDING borrower order -- the order B3's merge_borrowed_pass adds them in, so the two are
//                      bit-identical (pl/test/borrowed_copies_test.cpp holds them so)
// Arrays are the concatenated chunk slot images (packer::chunked_device_arrays' layout: chunk k starts
// at the sum of the earlier chunks' num_slots), one axis. Meow.

#include "packer/beat_packer.hpp"

#include <cstdio>
#include <vector>

namespace sw_flat {

// One owner -> borrower block as a sorted list, in concatenated-image slots. Meow.
struct BorrowedParcel {
    int borrower = 0;
    std::vector<long> owner_slot;   // ascending
    std::vector<long> borrowed_slot;  // the borrower's copy of owner_slot[i]
};

// Per owner chunk, its parcels, borrowers ascending. Exits if a block is not in owner-slot order. Meow.
inline std::vector<std::vector<BorrowedParcel>> borrowed_parcels(const packer::Chunked& ch)
{
    std::vector<long> chunk_base(ch.num_chunks + 1, 0);
    for (int k = 0; k < ch.num_chunks; k++) chunk_base[k + 1] = chunk_base[k] + ch.chunks[k].enc.num_slots;
    const std::vector<long> owner_local = packer::owner_slots_of(ch);

    std::vector<std::vector<BorrowedParcel>> parcels(ch.num_chunks);
    for (int borrower = 0; borrower < ch.num_chunks; borrower++) {
        const packer::Chunk& c = ch.chunks[borrower];
        for (const packer::Chunk::BorrowedBlock& block : c.borrowed_blocks) {
            BorrowedParcel parcel;
            parcel.borrower = borrower;
            for (long i = block.first; i < block.first + block.count; i++) {
                const int local = c.borrowed_order[i];
                parcel.owner_slot.push_back(chunk_base[block.owner] + owner_local[c.work_node[local]]);
                parcel.borrowed_slot.push_back(chunk_base[borrower] + c.enc.node_slot[local]);
                if (parcel.owner_slot.size() > 1 && parcel.owner_slot.back() <= parcel.owner_slot[parcel.owner_slot.size() - 2]) {
                    fprintf(stderr, "borrowed block %d -> %d is not in owner-slot order (C7)\n", block.owner, borrower);
                    exit(2);
                }
            }
            parcels[block.owner].push_back(std::move(parcel));   // borrowers visited ascending
        }
    }
    return parcels;
}

// The owned slots of chunk j in the concatenated image: [first, end). Meow.
inline std::pair<long, long> owned_range(const packer::Chunked& ch, int j)
{
    long first = 0;
    for (int k = 0; k < j; k++) first += ch.chunks[k].enc.num_slots;
    return {first, first + ch.chunks[j].enc.first_borrowed_slot};
}

// R1 for owner j. Meow.
inline void fused_send(const packer::Chunked& ch, int j, const std::vector<BorrowedParcel>& parcels, std::vector<float>& pos)
{
    const auto [first, end] = owned_range(ch, j);
    std::vector<size_t> cursor(parcels.size(), 0);
    for (long slot = first; slot < end; slot++)
        for (size_t p = 0; p < parcels.size(); p++)
            if (cursor[p] < parcels[p].owner_slot.size() && parcels[p].owner_slot[cursor[p]] == slot) {
                pos[parcels[p].borrowed_slot[cursor[p]]] = pos[slot];
                cursor[p]++;
            }
    for (size_t p = 0; p < parcels.size(); p++)
        if (cursor[p] != parcels[p].owner_slot.size()) { fprintf(stderr, "fused_send: parcel %d -> %d not consumed\n", j, parcels[p].borrower); exit(2); }
}

// R4 for owner j: the K-1-way sorted merge-add. Meow.
inline void fused_merge(const packer::Chunked& ch, int j, const std::vector<BorrowedParcel>& parcels, std::vector<float>& grad)
{
    const auto [first, end] = owned_range(ch, j);
    std::vector<size_t> cursor(parcels.size(), 0);
    for (long slot = first; slot < end; slot++) {
        float sum = grad[slot];
        for (size_t p = 0; p < parcels.size(); p++)
            if (cursor[p] < parcels[p].owner_slot.size() && parcels[p].owner_slot[cursor[p]] == slot) {
                sum += grad[parcels[p].borrowed_slot[cursor[p]]];
                cursor[p]++;
            }
        grad[slot] = sum;
    }
    for (size_t p = 0; p < parcels.size(); p++)
        if (cursor[p] != parcels[p].owner_slot.size()) { fprintf(stderr, "fused_merge: parcel %d -> %d not consumed\n", j, parcels[p].borrower); exit(2); }
}

// ---- The beat-level form (#47 item C): the op lists iteration_update_CU replays, and a replay of them on
// floats with the device's buffers, held bit-identical to fused_send / fused_merge above. The op format is
// pin_record.hpp's FUSED_*. Meow.

struct FusedOwnerOps {
    std::vector<uint64_t> merge, send;
    std::vector<long> block_first_beat;   // per borrower rank s, in the concatenated slot images
    bool feasible = true;                 // every block fits its BORROWED_BUFFER_ROWS ring
    long shared_beats = 0;                // the owner beats the lists span (first_private_slot / LANES)
};

namespace fused_detail {
struct Entry { int beat, lane, bank; long row; };   // row: within the block

// Parcel entries in owner order, cut into GROUPs: one owner beat, at most FUSED_GROUP_ENTRIES, distinct banks. Meow.
inline std::vector<std::vector<Entry>> groups_of(const std::vector<Entry>& entries, int beat, size_t& cursor)
{
    std::vector<std::vector<Entry>> groups;
    while (cursor < entries.size() && entries[cursor].beat == beat) {
        std::vector<Entry> group;
        unsigned banks_used = 0;
        while (cursor < entries.size() && entries[cursor].beat == beat && (int)group.size() < pinrec::FUSED_GROUP_ENTRIES
               && !(banks_used >> entries[cursor].bank & 1u)) {
            banks_used |= 1u << entries[cursor].bank;
            group.push_back(entries[cursor++]);
        }
        groups.push_back(group);
    }
    return groups;
}

inline uint64_t group_op(int s, const std::vector<Entry>& group)
{
    uint64_t op = (uint64_t)s << pinrec::FUSED_BORROWER_SHIFT;
    for (size_t e = 0; e < group.size(); e++) op |= pinrec::fused_entry(group[e].lane, group[e].bank) << (e * pinrec::FUSED_ENTRY_BITS);
    return op;
}
} // namespace fused_detail

// Owner j's merge and send lists. Merge: per owner beat, each borrower's groups in rank order, each
// preceded by the half-rows its entries need (B3's merge rule); the span's last op carries BOUNDARY.
// Send: the span's first op carries BOUNDARY; each group is followed by the half-rows it made final
// (B3's send rule), and the rest of each block is written after the last beat. Meow.
inline FusedOwnerOps fused_owner_ops(const packer::Chunked& ch, int j, const std::vector<BorrowedParcel>& parcels)
{
    using fused_detail::Entry;
    constexpr long R = pinrec::BORROWED_BUFFER_ROWS;
    constexpr int HALVES = pinrec::BANKS / pinrec::LANES;
    FusedOwnerOps out;
    const long owner_first = owned_range(ch, j).first;
    out.shared_beats = ch.chunks[j].enc.first_private_slot / pinrec::LANES;
    const int nb = (int)parcels.size();
    if (nb > pinrec::FUSED_MAX_BORROWERS) { fprintf(stderr, "owner %d has %d borrowers, the device takes %d\n", j, nb, pinrec::FUSED_MAX_BORROWERS); exit(2); }

    std::vector<std::vector<Entry>> entries(nb);
    std::vector<long> rows(nb, 0);
    std::vector<std::vector<long>> total(nb, std::vector<long>(pinrec::BANKS, 0));
    for (int s = 0; s < nb; s++) {
        const long start_row = parcels[s].borrowed_slot[0] / pinrec::BANKS;   // the first entry is row 0 of its bank
        out.block_first_beat.push_back(start_row * HALVES);
        for (size_t i = 0; i < parcels[s].owner_slot.size(); i++) {
            const long local = parcels[s].owner_slot[i] - owner_first;
            const long slot = parcels[s].borrowed_slot[i];
            if (local >= out.shared_beats * pinrec::LANES) { fprintf(stderr, "owner %d: a borrowed node above first_private_slot (C8)\n", j); exit(2); }
            const Entry e{(int)(local / pinrec::LANES), (int)(local % pinrec::LANES), (int)(slot % pinrec::BANKS), slot / pinrec::BANKS - start_row};
            entries[s].push_back(e);
            total[s][e.bank]++;
            rows[s] = std::max(rows[s], e.row + 1);
        }
    }

    // Merge. Meow.
    {
        std::vector<size_t> cursor(nb, 0);
        std::vector<long> loaded(nb, 0);
        std::vector<std::vector<long>> used(nb, std::vector<long>(pinrec::BANKS, 0));   // entries consumed per bank
        for (int t = 0; t < out.shared_beats; t++) {
            const size_t span = out.merge.size();
            for (int s = 0; s < nb; s++)
                for (const std::vector<Entry>& group : fused_detail::groups_of(entries[s], t, cursor[s])) {
                    long need = -1;
                    for (const Entry& e : group) need = std::max(need, e.row * HALVES + e.bank / pinrec::LANES);
                    for (; loaded[s] <= need; loaded[s]++) {
                        const long row = loaded[s] / HALVES, half = loaded[s] % HALVES;
                        for (int b = (int)half * pinrec::LANES; b < (int)(half + 1) * pinrec::LANES; b++)
                            if (row >= R && used[s][b] <= row - R && total[s][b] > row - R) out.feasible = false;   // row - R not used yet
                        out.merge.push_back(pinrec::FUSED_BEAT | (uint64_t)s << pinrec::FUSED_BORROWER_SHIFT);
                    }
                    out.merge.push_back(fused_detail::group_op(s, group));
                    for (const Entry& e : group) used[s][e.bank]++;
                }
            if (out.merge.size() == span) out.merge.push_back(0);   // NOP
            out.merge.back() |= pinrec::FUSED_BOUNDARY;
        }
    }

    // Send. Meow.
    {
        std::vector<size_t> cursor(nb, 0);
        std::vector<long> written(nb, 0);
        std::vector<std::vector<long>> placed(nb, std::vector<long>(pinrec::BANKS, 0));
        auto half_final = [&](int s, long beat) {
            const long row = beat / HALVES, half = beat % HALVES;
            for (int b = (int)half * pinrec::LANES; b < (int)(half + 1) * pinrec::LANES; b++)
                if (placed[s][b] <= row && total[s][b] > row) return false;
            return true;
        };
        for (int t = 0; t < out.shared_beats; t++) {
            const size_t span = out.send.size();
            for (int s = 0; s < nb; s++)
                for (const std::vector<Entry>& group : fused_detail::groups_of(entries[s], t, cursor[s])) {
                    for (const Entry& e : group) if ((e.row - R + 1) * HALVES > written[s]) out.feasible = false;   // ring row still holds row - R
                    out.send.push_back(fused_detail::group_op(s, group));
                    for (const Entry& e : group) placed[s][e.bank]++;
                    while (written[s] < rows[s] * HALVES && half_final(s, written[s])) {
                        out.send.push_back(pinrec::FUSED_BEAT | (uint64_t)s << pinrec::FUSED_BORROWER_SHIFT);
                        written[s]++;
                    }
                }
            if (out.send.size() == span) out.send.push_back(0);
            out.send[span] |= pinrec::FUSED_BOUNDARY;
        }
        for (int s = 0; s < nb; s++)
            for (; written[s] < rows[s] * HALVES; written[s]++) out.send.push_back(pinrec::FUSED_BEAT | (uint64_t)s << pinrec::FUSED_BORROWER_SHIFT);
    }
    return out;
}

// Every owner's lists, concatenated, as iteration_update_CU reads them. Exits if a block overflows its ring. Meow.
struct FusedDevice {
    std::vector<pinrec::FusedOwnerDesc> owners;
    std::vector<uint64_t> ops;
    std::vector<FusedOwnerOps> per_owner;
};

inline FusedDevice fused_device_ops(const packer::Chunked& ch)
{
    FusedDevice dev;
    const auto parcels = borrowed_parcels(ch);
    for (int j = 0; j < ch.num_chunks; j++) {
        FusedOwnerOps ops = fused_owner_ops(ch, j, parcels[j]);
        if (!ops.feasible) { fprintf(stderr, "owner %d: a borrowed block needs more than %d ring rows\n", j, pinrec::BORROWED_BUFFER_ROWS); exit(2); }
        pinrec::FusedOwnerDesc d = {};
        d.merge_op_offset = (int32_t)dev.ops.size();
        d.num_merge_ops = (int32_t)ops.merge.size();
        dev.ops.insert(dev.ops.end(), ops.merge.begin(), ops.merge.end());
        d.send_op_offset = (int32_t)dev.ops.size();
        d.num_send_ops = (int32_t)ops.send.size();
        dev.ops.insert(dev.ops.end(), ops.send.begin(), ops.send.end());
        d.num_borrowers = (int32_t)parcels[j].size();
        for (size_t s = 0; s < parcels[j].size(); s++) d.block_beat_offset[s] = (int32_t)ops.block_first_beat[s];
        dev.owners.push_back(d);
        dev.per_owner.push_back(std::move(ops));
    }
    return dev;
}

// Replays owner j's merge list on floats as the device does (per-borrower ring of half-rows, per-bank row
// counters, the masked chain in rank order at each BOUNDARY): grad's owned shared slots += the borrowed
// gradients. Returns the cycles: one per op. Meow.
inline long replay_fused_merge(const packer::Chunked& ch, int j, const FusedOwnerOps& ops, std::vector<float>& grad)
{
    constexpr int R = pinrec::BORROWED_BUFFER_ROWS, NB = pinrec::FUSED_MAX_BORROWERS;
    const long owner_first = owned_range(ch, j).first;
    std::vector<float> row_buf((size_t)NB * pinrec::BANKS * R, 0.0f);
    std::vector<int> next_row((size_t)NB * pinrec::BANKS, 0), beat_count(NB, 0);
    float addend[NB][pinrec::LANES];
    bool held[NB][pinrec::LANES] = {};
    long t = 0;
    for (uint64_t op : ops.merge) {
        const int s = pinrec::fused_borrower(op);
        if (op & pinrec::FUSED_BEAT) {
            const int half = beat_count[s] & 1, row = (beat_count[s] >> 1) & (R - 1);
            const long first = (ops.block_first_beat[s] + beat_count[s]) * pinrec::LANES;
            for (int k = 0; k < pinrec::LANES; k++) row_buf[((size_t)s * pinrec::BANKS + half * pinrec::LANES + k) * R + row] = grad[first + k];
            beat_count[s]++;
            continue;
        }
        for (int e = 0; e < pinrec::FUSED_GROUP_ENTRIES; e++) {
            if (!pinrec::fused_entry_valid(op, e)) continue;
            const int bank = pinrec::fused_entry_bank(op, e), lane = pinrec::fused_entry_lane(op, e);
            int& row = next_row[(size_t)s * pinrec::BANKS + bank];
            addend[s][lane] = row_buf[((size_t)s * pinrec::BANKS + bank) * R + (row & (R - 1))];
            held[s][lane] = true;
            row++;
        }
        if (op & pinrec::FUSED_BOUNDARY) {
            for (int lane = 0; lane < pinrec::LANES; lane++) {
                float& g = grad[owner_first + t * pinrec::LANES + lane];
                for (int r = 0; r < NB; r++) if (held[r][lane]) g = g + addend[r][lane];
                for (int r = 0; r < NB; r++) held[r][lane] = false;
            }
            t++;
        }
    }
    return (long)ops.merge.size();
}

// Replays owner j's send list: each owned shared position into every borrower's block. Returns the cycles. Meow.
inline long replay_fused_send(const packer::Chunked& ch, int j, const FusedOwnerOps& ops, std::vector<float>& pos)
{
    constexpr int R = pinrec::BORROWED_BUFFER_ROWS, NB = pinrec::FUSED_MAX_BORROWERS;
    const long owner_first = owned_range(ch, j).first;
    std::vector<float> row_buf((size_t)NB * pinrec::BANKS * R, 0.0f);
    std::vector<int> next_row((size_t)NB * pinrec::BANKS, 0), beat_count(NB, 0);
    long t = -1;
    for (uint64_t op : ops.send) {
        const int s = pinrec::fused_borrower(op);
        if (op & pinrec::FUSED_BEAT) {
            const int half = beat_count[s] & 1, row = (beat_count[s] >> 1) & (R - 1);
            const long first = (ops.block_first_beat[s] + beat_count[s]) * pinrec::LANES;
            for (int k = 0; k < pinrec::LANES; k++) pos[first + k] = row_buf[((size_t)s * pinrec::BANKS + half * pinrec::LANES + k) * R + row];
            beat_count[s]++;
            continue;
        }
        if (op & pinrec::FUSED_BOUNDARY) t++;
        for (int e = 0; e < pinrec::FUSED_GROUP_ENTRIES; e++) {
            if (!pinrec::fused_entry_valid(op, e)) continue;
            const int bank = pinrec::fused_entry_bank(op, e), lane = pinrec::fused_entry_lane(op, e);
            int& row = next_row[(size_t)s * pinrec::BANKS + bank];
            row_buf[((size_t)s * pinrec::BANKS + bank) * R + (row & (R - 1))] = pos[owner_first + t * pinrec::LANES + lane];
            row++;
        }
    }
    return (long)ops.send.size();
}

} // namespace sw_flat

#endif // SW_FLAT_BORROWED_FUSION_HPP
