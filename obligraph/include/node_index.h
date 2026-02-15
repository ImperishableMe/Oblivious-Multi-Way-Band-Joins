#pragma once

#include <memory>
#include <cstring>
#include <bit>
#include <vector>

#include "definitions.h"
#include "ohash_bin.hpp"

namespace obligraph {

    // Block size for ObliviousBin: key_t (8 bytes) + Row payload
    constexpr size_t ROW_BLOCK_SIZE = sizeof(key_t) + sizeof(Row);
    using RowBlock = ORAM::Block<key_t, ROW_BLOCK_SIZE>;

    // Dummy marker: MSB set indicates a dummy block
    constexpr key_t DUMMY_KEY_MSB = 1ULL << 63;

    // Pre-built oblivious hash index over a node table
    using NodeIndex = ORAM::ObliviousBin<key_t, ROW_BLOCK_SIZE>;

    // triple32 hash function: https://github.com/skeeto/hash-prospector
    // exact bias: 0.020888578919738908
    inline uint32_t triple32(uint32_t x) {
        x ^= x >> 17;
        x *= 0xed5ad4bb;
        x ^= x >> 11;
        x *= 0xac4c1b51;
        x ^= x >> 15;
        x *= 0x31848bab;
        x ^= x >> 14;
        return x;
    }

    // Build an ObliviousBin index from a node table (offline build phase).
    // Returns a unique_ptr because ObliviousBin has no move constructor.
    std::unique_ptr<NodeIndex> buildNodeIndex(const Table& table);

    // oneHop overload that accepts pre-built indexes for probe-only execution.
    // Both indexes are consumed (probing is destructive).
    Table oneHop(Catalog& catalog, OneHopQuery& query, ThreadPool& pool,
                 std::unique_ptr<NodeIndex> srcIndex,
                 std::unique_ptr<NodeIndex> dstIndex);

} // namespace obligraph
