#ifndef SW_FLAT_TAIL_FUSION_HPP
#define SW_FLAT_TAIL_FUSION_HPP

// tail_fusion.hpp -- #47 M2, the software golden of option A (Mark, 2026-10-09): the chunk exchange
// done inside iteration_update's stream, not in hpwl_gradient_CU's send and fold passes.
//
// iteration_update streams each owner chunk's owned slots once, ascending. Every consumer's tail
// block for that owner is sorted by owner slot (packer rule C7), so each consumer is a cursor that
// the stream advances:
//   fused_send   (R1)  at owned slot s, every consumer whose cursor is at s gets pos[s] in its tail slot
//   fused_merge  (R4)  at owned slot s, grad[s] = partial + each consumer's tail gradient for s, in
//                      ASCENDING consumer order -- the order B3's fold pass adds them in, so the two are
//                      bit-identical (pl/test/tail_exchange_test.cpp holds them so)
// Arrays are the concatenated chunk slot images (packer::chunked_device_arrays' layout: chunk k starts
// at the sum of the earlier chunks' num_slots), one axis. Meow.

#include "packer/beat_packer.hpp"

#include <cstdio>
#include <vector>

namespace sw_flat {

// One owner -> consumer block as a sorted list, in concatenated-image slots. Meow.
struct TailParcel {
    int consumer = 0;
    std::vector<long> owner_slot;   // ascending
    std::vector<long> tail_slot;    // the consumer's copy of owner_slot[i]
};

// Per owner chunk, its parcels, consumers ascending. Exits if a block is not in owner-slot order. Meow.
inline std::vector<std::vector<TailParcel>> tail_parcels(const packer::Chunked& ch)
{
    std::vector<long> chunk_base(ch.num_chunks + 1, 0);
    for (int k = 0; k < ch.num_chunks; k++) chunk_base[k + 1] = chunk_base[k] + ch.chunks[k].enc.num_slots;
    const std::vector<long> owner_local = packer::owner_slots_of(ch);

    std::vector<std::vector<TailParcel>> parcels(ch.num_chunks);
    for (int consumer = 0; consumer < ch.num_chunks; consumer++) {
        const packer::Chunk& c = ch.chunks[consumer];
        for (const packer::Chunk::TailBlock& block : c.tail_blocks) {
            TailParcel parcel;
            parcel.consumer = consumer;
            for (long i = block.first; i < block.first + block.count; i++) {
                const int local = c.tail_order[i];
                parcel.owner_slot.push_back(chunk_base[block.owner] + owner_local[c.work_node[local]]);
                parcel.tail_slot.push_back(chunk_base[consumer] + c.enc.node_slot[local]);
                if (parcel.owner_slot.size() > 1 && parcel.owner_slot.back() <= parcel.owner_slot[parcel.owner_slot.size() - 2]) {
                    fprintf(stderr, "tail block %d -> %d is not in owner-slot order (C7)\n", block.owner, consumer);
                    exit(2);
                }
            }
            parcels[block.owner].push_back(std::move(parcel));   // consumers visited ascending
        }
    }
    return parcels;
}

// The owned slots of chunk j in the concatenated image: [first, end). Meow.
inline std::pair<long, long> owned_range(const packer::Chunked& ch, int j)
{
    long first = 0;
    for (int k = 0; k < j; k++) first += ch.chunks[k].enc.num_slots;
    return {first, first + ch.chunks[j].enc.first_tail_slot};
}

// R1 for owner j. Meow.
inline void fused_send(const packer::Chunked& ch, int j, const std::vector<TailParcel>& parcels, std::vector<float>& pos)
{
    const auto [first, end] = owned_range(ch, j);
    std::vector<size_t> cursor(parcels.size(), 0);
    for (long slot = first; slot < end; slot++)
        for (size_t p = 0; p < parcels.size(); p++)
            if (cursor[p] < parcels[p].owner_slot.size() && parcels[p].owner_slot[cursor[p]] == slot) {
                pos[parcels[p].tail_slot[cursor[p]]] = pos[slot];
                cursor[p]++;
            }
    for (size_t p = 0; p < parcels.size(); p++)
        if (cursor[p] != parcels[p].owner_slot.size()) { fprintf(stderr, "fused_send: parcel %d -> %d not consumed\n", j, parcels[p].consumer); exit(2); }
}

// R4 for owner j: the K-1-way sorted merge-add. Meow.
inline void fused_merge(const packer::Chunked& ch, int j, const std::vector<TailParcel>& parcels, std::vector<float>& grad)
{
    const auto [first, end] = owned_range(ch, j);
    std::vector<size_t> cursor(parcels.size(), 0);
    for (long slot = first; slot < end; slot++) {
        float sum = grad[slot];
        for (size_t p = 0; p < parcels.size(); p++)
            if (cursor[p] < parcels[p].owner_slot.size() && parcels[p].owner_slot[cursor[p]] == slot) {
                sum += grad[parcels[p].tail_slot[cursor[p]]];
                cursor[p]++;
            }
        grad[slot] = sum;
    }
    for (size_t p = 0; p < parcels.size(); p++)
        if (cursor[p] != parcels[p].owner_slot.size()) { fprintf(stderr, "fused_merge: parcel %d -> %d not consumed\n", j, parcels[p].consumer); exit(2); }
}

} // namespace sw_flat

#endif // SW_FLAT_TAIL_FUSION_HPP
