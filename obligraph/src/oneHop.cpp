#include <set>
#include <unordered_map>
#include <iostream>
#include <future>
#include <thread>
#include <cassert>
#include <bit>
#include <cstring>
#include <random>

#include "xxhash.h"
#include "node_index.h"
#include "obl_primitives.h"
#include "obl_building_blocks.h"
#include "slice_utils.h"
#include "timer.h"
#include "config.h"

// ObliviousBin headers
#include "ohash_bin.hpp"
#include "hash_planner.hpp"
#include "ohash_tiers.hpp"

using namespace std;

namespace obligraph {
    std::unique_ptr<NodeIndex> buildNodeIndex(const Table& table, size_t op_num) {
        TimedScope ts("buildNodeIndex", "OFFLINE");

        std::cout << "[INFO] Row size: " << sizeof(Row)
                  << " bytes, RowBlock size: " << ROW_BLOCK_SIZE
                  << " bytes" << std::endl;

        size_t n = std::bit_ceil(static_cast<size_t>(table.rowCount));
        std::vector<RowBlock> blocks(n);

        for (size_t i = 0; i < table.rowCount; i++) {
            blocks[i].id = triple32(table.rows[i].key.first) & ~DUMMY_KEY_MSB;
            std::memcpy(blocks[i].value, &table.rows[i], sizeof(Row));
        }
        for (size_t i = table.rowCount; i < n; i++) {
            blocks[i].id = static_cast<key_t>(i) | DUMMY_KEY_MSB;
        }

        auto index = std::make_unique<NodeIndex>(n, op_num);
        index->build(blocks.data());
        return index;
    }

    // Resize `side` to n rows and copy each edge row's key in parallel.
    // Replaces the serial loop `for (i=0..n) { addRow(Row()); rows[i].key = edge[i].key; }`,
    // which on banking_10M ran 50M single-threaded push_backs before dedup could start.
    // rows.resize(n) is a single bulk zero-init (~memset); the key copy is O(n/p).
    // Row payload is overwritten later by probe, so zero-init of data[48] is wasted work
    // — tolerated here; elimination would require a custom allocator.
    static void initProbeSide(Table& side, const Table& edge, ThreadPool& pool) {
        const size_t n = edge.rowCount;
        side.rows.resize(n);
        side.rowCount = n;

        auto chunk = [&](int start, int end) {
            for (int i = start; i < end; i++)
                side.rows[i].key = edge.rows[i].key;
        };

        int num_threads = obligraph::number_of_threads.load();
        std::vector<std::future<void>> futures;
        for (int t = 0; t < num_threads; t++) {
            auto c = obligraph::get_cutoffs_for_thread(t, static_cast<int>(n), num_threads);
            if (c.first == c.second) continue;
            if (t == num_threads - 1) chunk(c.first, c.second);
            else futures.push_back(pool.submit(chunk, c.first, c.second));
        }
        for (auto& f : futures) f.get();
    }

    void probe_with_index(NodeIndex& obin, Table& probeT, ThreadPool& pool,
                          const std::string& label) {
        TimedScope ts("probe " + label, "ONLINE", /*contributes_to_total=*/false);

        key_t d = 2 * probeT.rowCount;
        int num_threads = std::max(1, obligraph::number_of_threads.load() / 2);

        auto thread_chunk = [&](int thread_id, int start, int end) {
            using TwoTier = ORAM::OTwoTierHash<key_t, ROW_BLOCK_SIZE>;
            TwoTier::init_dummy_range(key_t(0) - key_t(1) - key_t(thread_id) * d);

            Row dummyRow;
            dummyRow.setDummy(true);

            for (int i = start; i < end; i++) {
                key_t srcId = triple32(probeT.rows[i].key.first) & ~DUMMY_KEY_MSB;
                bool dummy = probeT.rows[i].isDummy();

                RowBlock result = obin[srcId];

                Row matchedRow;
                std::memcpy(&matchedRow, result.value, sizeof(Row));

                probeT.rows[i] = ObliviousChoose(
                    dummy || result.dummy(),
                    dummyRow,
                    matchedRow
                );
            }
        };

        std::vector<std::future<void>> futures;
        for (int i = 0; i < num_threads; i++) {
            auto chunks = obligraph::get_cutoffs_for_thread(i, probeT.rowCount, num_threads);
            if (chunks.first == chunks.second) continue;
            if (i == num_threads - 1)
                thread_chunk(i, chunks.first, chunks.second);
            else
                futures.push_back(pool.submit(thread_chunk, i, chunks.first, chunks.second));
        }
        for (auto& fut : futures) fut.get();
    }

    // Legacy overload (no label) — used by build_and_probe.
    void probe_with_index(NodeIndex& obin, Table& probeT, ThreadPool& pool) {
        probe_with_index(obin, probeT, pool, "");
    }

    void build_and_probe(const Table& buildT, Table &probeT, ThreadPool& pool) {
        TimedScope ts("build_and_probe", "ONLINE", /*contributes_to_total=*/false);
        auto index = buildNodeIndex(buildT, probeT.rowCount);
        probe_with_index(*index, probeT, pool, "");
    }

    void deduplicateRows(Table& table) {
        key_t lastKey = -1;
        key_t dummy = 1e9;

        for (size_t i = 0; i < table.rows.size(); ++i) {
            key_t currentKey = table.rows[i].key.first;
            // Generate dummy key without MSB set (MSB reserved for ObliviousBin dummy marking).
            // Offset by 2^31 to ensure lower 32 bits are in [2^31, 2^32-1], which is safely
            // above any valid account ID (small positive integers), preventing accidental
            // ORAM entry consumption for real account IDs.
            dummy = (static_cast<key_t>(random()) + (key_t(1) << 31)) & ~DUMMY_KEY_MSB;
            table.rows[i].key.first = ObliviousChoose(lastKey == currentKey, dummy, currentKey);
            table.rows[i].setDummy(lastKey == currentKey);
            lastKey = currentKey;
        }
    }

    void reduplicateRows(Table& table) {
        Row lastRow;
        for (size_t i = 0; i < table.rows.size(); ++i) {
            auto secKey = table.rows[i].key.second;
            table.rows[i] = ObliviousChoose(table.rows[i].isDummy(), lastRow, table.rows[i]);
            table.rows[i].key.second = secKey;
            lastRow = table.rows[i];
        }
    }

    // Parallel dedup: 1 pre-pass (O(P), serial, boundary reads) + 1 parallel scan.
    // The pre-pass captures each slice's entering lastKey BEFORE any writes, so each
    // worker can run the original loop independently on its slice with the correct seed.
    void deduplicateRowsParallel(Table& table, ThreadPool& pool) {
        const size_t N = table.rows.size();
        if (N == 0) return;
        const size_t P = std::min<size_t>(pool.size(), N);
        const auto slices = buildSlices(N, P);
        if (slices.empty()) return;

        // Pre-pass: seed[t] = key entering slice t (pre-dedup). Must run before any writes.
        std::vector<key_t> seed(slices.size());
        seed[0] = static_cast<key_t>(-1);  // sentinel: idx 0 is always real
        for (size_t t = 1; t < slices.size(); ++t) {
            seed[t] = table.rows[slices[t-1].end - 1].key.first;
        }

        // Phase 3: original loop, per slice, with correct seed.
        std::vector<std::future<void>> fs;
        fs.reserve(slices.size());
        for (size_t t = 0; t < slices.size(); ++t) {
            fs.push_back(pool.submit([&, t] {
                // random() is not thread-safe; use a thread-local PRNG instead.
                // Mask rng() to 31 bits so dummy lands in [2^31, 2^32-1]; serial
                // random() returns a 31-bit positive long, giving that same range.
                // This matters because triple32() hashes the low 32 bits of key.first:
                // if dummy's low-32-bits collide with a real account id (small positive
                // int), the dummy probe consumes the real ORAM entry and the later real
                // probe returns a dummy, corrupting results (off-by-one neighbor rows).
                thread_local std::mt19937_64 rng{std::random_device{}()};
                key_t lastKey = seed[t];
                for (size_t i = slices[t].begin; i < slices[t].end; ++i) {
                    key_t currentKey = table.rows[i].key.first;
                    key_t dummy = ((static_cast<key_t>(rng()) & 0x7FFFFFFFULL)
                                   + (key_t(1) << 31));
                    table.rows[i].key.first =
                        ObliviousChoose(lastKey == currentKey, dummy, currentKey);
                    table.rows[i].setDummy(lastKey == currentKey);
                    lastKey = currentKey;
                }
            }));
        }
        for (auto& f : fs) f.get();
    }

    // Parallel redup: canonical 3-phase carry-forward scan.
    // Phase 1 (parallel): each thread records its slice's tail real row.
    // Phase 2 (serial,   O(P)): compute seed[t] = real row in effect entering slice t.
    // Phase 3 (parallel): original redup loop per slice, seeded.
    void reduplicateRowsParallel(Table& table, ThreadPool& pool) {
        const size_t N = table.rows.size();
        if (N == 0) return;
        const size_t P = std::min<size_t>(pool.size(), N);
        const auto slices = buildSlices(N, P);
        if (slices.empty()) return;

        // Phase 1: find "tail real row" per slice.
        std::vector<Row>     tail(slices.size());
        std::vector<uint8_t> tailReal(slices.size(), 0);

        std::vector<std::future<void>> fs;
        fs.reserve(slices.size());
        for (size_t t = 0; t < slices.size(); ++t) {
            fs.push_back(pool.submit([&, t] {
                Row cur;
                uint8_t have = 0;
                for (size_t i = slices[t].begin; i < slices[t].end; ++i) {
                    bool real = !table.rows[i].isDummy();
                    cur  = ObliviousChoose(real, table.rows[i], cur);
                    have = ObliviousChoose(real, uint8_t{1}, have);
                }
                tail[t]     = cur;
                tailReal[t] = have;
            }));
        }
        for (auto& f : fs) f.get();
        fs.clear();

        // Phase 2: serial walk, seed[t] = running, then fold tail[t] into running.
        std::vector<Row> seed(slices.size());
        Row running;  // default-constructed; matches serial reduplicateRows's initial state
        for (size_t t = 0; t < slices.size(); ++t) {
            seed[t] = running;
            running = ObliviousChoose(tailReal[t] != 0, tail[t], running);
        }

        // Phase 3: original redup loop, per slice, with seeded lastRow.
        for (size_t t = 0; t < slices.size(); ++t) {
            fs.push_back(pool.submit([&, t] {
                Row lastRow = seed[t];
                for (size_t i = slices[t].begin; i < slices[t].end; ++i) {
                    auto secKey = table.rows[i].key.second;
                    table.rows[i] = ObliviousChoose(
                        table.rows[i].isDummy(), lastRow, table.rows[i]);
                    table.rows[i].key.second = secKey;
                    lastRow = table.rows[i];
                }
            }));
        }
        for (auto& f : fs) f.get();
    }

    Table buildSourceAndEdgeTables(Catalog& catalog, const OneHopQuery& query,
                                    ThreadPool &pool, NodeIndex* srcIndex) {
        TimedScope ts_total("src branch (total)", "ONLINE", /*contributes_to_total=*/false);

        string srcPrefix = query.sourceNodeTableName;
        if (query.sourceNodeTableName == query.destNodeTableName)
            srcPrefix += "_src";

        if (query.projectionColumns.empty()) {
            const Table& srcRef = catalog.getTable(query.sourceNodeTableName);
            // Move out of the catalog: oneHop's driver is single-use, the fwd edge table
            // is not read again after this call. Avoids a 50M-row copy on banking_10M.
            Table edgeTableFwd = std::move(catalog.getTable(query.edgeTableName + "_fwd"));

            Table srcSide;
            srcSide.init(srcRef);
            {
                TimedScope ts("initProbeSide (src)", "ONLINE", /*contributes_to_total=*/false);
                initProbeSide(srcSide, edgeTableFwd, pool);
            }
            {
                TimedScope ts("deduplicateRows (src)", "ONLINE", /*contributes_to_total=*/false);
                deduplicateRowsParallel(srcSide, pool);
            }

            if (srcIndex)
                probe_with_index(*srcIndex, srcSide, pool, "src");
            else
                build_and_probe(srcRef, srcSide, pool);

            {
                TimedScope ts("reduplicateRows (src)", "ONLINE", /*contributes_to_total=*/false);
                reduplicateRowsParallel(srcSide, pool);
            }
            {
                TimedScope ts("unionWith (src)", "ONLINE", /*contributes_to_total=*/false);
                edgeTableFwd.unionWith(srcSide, pool, srcPrefix);
            }
            return edgeTableFwd;
        }

        set<string> srcColumns;
        set<string> edgeColumns;

        // srcTable is only read by .project() (const method), so bind by reference —
        // avoids copying the full node table (10M rows on banking_10M).
        const Table& srcTable = catalog.getTable(query.sourceNodeTableName);
        // Move out of the catalog: single-use driver, fwd edge table not read again.
        Table edgeTableFwd = std::move(catalog.getTable(query.edgeTableName + "_fwd"));

        for (const auto& col : query.projectionColumns) {
            if (col.first == query.sourceNodeTableName ||
                (col.first.length() > 4 && col.first.substr(col.first.length() - 4) == "_src"))
                srcColumns.insert(col.second);
            else if (col.first == query.edgeTableName)
                edgeColumns.insert(col.second);
        }
        for (const auto& tablePred : query.tablePredicates) {
            if (tablePred.first == query.sourceNodeTableName)
                for (const auto& pred : tablePred.second) srcColumns.insert(pred.column);
            else if (tablePred.first == query.edgeTableName)
                for (const auto& pred : tablePred.second) edgeColumns.insert(pred.column);
        }

        Table srcProjected = srcTable.project(vector<string>(srcColumns.begin(), srcColumns.end()), pool);
        Table edgeProjectedFwd = edgeTableFwd.project(vector<string>(edgeColumns.begin(), edgeColumns.end()), pool);

        Table srcSide;
        srcSide.init(srcProjected);
        {
            TimedScope ts("initProbeSide (src)", "ONLINE", /*contributes_to_total=*/false);
            initProbeSide(srcSide, edgeProjectedFwd, pool);
        }
        {
            TimedScope ts("deduplicateRows (src)", "ONLINE", /*contributes_to_total=*/false);
            deduplicateRowsParallel(srcSide, pool);
        }

        if (srcIndex)
            probe_with_index(*srcIndex, srcSide, pool, "src");
        else
            build_and_probe(srcProjected, srcSide, pool);

        {
            TimedScope ts("reduplicateRows (src)", "ONLINE", /*contributes_to_total=*/false);
            reduplicateRowsParallel(srcSide, pool);
        }
        {
            TimedScope ts("unionWith (src)", "ONLINE", /*contributes_to_total=*/false);
            edgeProjectedFwd.unionWith(srcSide, pool, srcPrefix);
        }
        return edgeProjectedFwd;
    }

    Table buildDestinationTable(Catalog& catalog, const OneHopQuery& query,
                                ThreadPool& pool, NodeIndex* dstIndex) {
        TimedScope ts_total("dst branch (total)", "ONLINE", /*contributes_to_total=*/false);

        string dstPrefix = query.destNodeTableName;
        if (query.sourceNodeTableName == query.destNodeTableName)
            dstPrefix += "_dest";

        // Move out of the catalog: single-use driver, rev edge table not read again.
        Table edgeTableRev = std::move(catalog.getTable(query.edgeTableName + "_rev"));

        auto doSortAndReturn = [&]() -> Table {
            TimedScope ts("parallel_sort (dst)", "ONLINE", /*contributes_to_total=*/false);
            parallel_sort(edgeTableRev.rows.begin(), edgeTableRev.rows.end(),
                pool,
                [](const Row& a, const Row& b) {
                    bool eq  = (a.key.second == b.key.second);
                    bool lt2 = (a.key.second <  b.key.second);
                    bool lt1 = (a.key.first  <  b.key.first);
                    return ObliviousChoose(eq, lt1, lt2);
                },
                pool.size()
            );
            // edgeTableRev is captured by reference; return-by-value would copy. Move.
            return std::move(edgeTableRev);
        };

        if (query.projectionColumns.empty()) {
            const Table& dstRef = catalog.getTable(query.destNodeTableName);

            Table dstSide;
            dstSide.init(dstRef);
            {
                TimedScope ts("initProbeSide (dst)", "ONLINE", /*contributes_to_total=*/false);
                initProbeSide(dstSide, edgeTableRev, pool);
            }
            {
                TimedScope ts("deduplicateRows (dst)", "ONLINE", /*contributes_to_total=*/false);
                deduplicateRowsParallel(dstSide, pool);
            }

            if (dstIndex)
                probe_with_index(*dstIndex, dstSide, pool, "dst");
            else
                build_and_probe(dstRef, dstSide, pool);

            {
                TimedScope ts("reduplicateRows (dst)", "ONLINE", /*contributes_to_total=*/false);
                reduplicateRowsParallel(dstSide, pool);
            }
            {
                TimedScope ts("unionWith (dst)", "ONLINE", /*contributes_to_total=*/false);
                edgeTableRev.unionWith(dstSide, pool, dstPrefix);
            }

            return doSortAndReturn();
        }

        set<string> dstColumns;
        // dstTable is only read by .project() (const method), so bind by reference —
        // avoids copying the full node table.
        const Table& dstTable = catalog.getTable(query.destNodeTableName);

        for (const auto& col : query.projectionColumns) {
            if (col.first == query.destNodeTableName ||
                (col.first.length() > 5 && col.first.substr(col.first.length() - 5) == "_dest"))
                dstColumns.insert(col.second);
        }
        for (const auto& tablePred : query.tablePredicates) {
            if (tablePred.first == query.destNodeTableName)
                for (const auto& pred : tablePred.second) dstColumns.insert(pred.column);
        }

        Table dstProjected = dstTable.project(vector<string>(dstColumns.begin(), dstColumns.end()), pool);

        Table dstSide;
        dstSide.init(dstProjected);
        {
            TimedScope ts("initProbeSide (dst)", "ONLINE", /*contributes_to_total=*/false);
            initProbeSide(dstSide, edgeTableRev, pool);
        }
        {
            TimedScope ts("deduplicateRows (dst)", "ONLINE", /*contributes_to_total=*/false);
            deduplicateRowsParallel(dstSide, pool);
        }

        if (dstIndex)
            probe_with_index(*dstIndex, dstSide, pool, "dst");
        else
            build_and_probe(dstProjected, dstSide, pool);

        {
            TimedScope ts("reduplicateRows (dst)", "ONLINE", /*contributes_to_total=*/false);
            reduplicateRowsParallel(dstSide, pool);
        }
        {
            TimedScope ts("unionWith (dst)", "ONLINE", /*contributes_to_total=*/false);
            edgeTableRev.unionWith(dstSide, pool, dstPrefix);
        }

        return doSortAndReturn();
    }

    // ---------------------------------------------------------------------------
    // Shared filter + project logic after the parallel branches complete.
    // ---------------------------------------------------------------------------
    static void applyFilterAndProject(Table& result, OneHopQuery& query, ThreadPool& pool) {
        bool isSelfReferential = (query.sourceNodeTableName == query.destNodeTableName);

        vector<Predicate> allPredicates;
        for (const auto& tablePred : query.tablePredicates) {
            for (const auto& pred : tablePred.second) {
                Predicate qualifiedPred = pred;
                string tablePrefix = tablePred.first;
                if (isSelfReferential && tablePred.first == query.sourceNodeTableName)
                    tablePrefix += "_src";
                qualifiedPred.column = tablePrefix + "_" + pred.column;
                allPredicates.push_back(qualifiedPred);
            }
        }
        if (!allPredicates.empty()) {
            TimedScope ts("filter", "ONLINE");
            result.filter(allPredicates, pool);
        }

        if (query.projectionColumns.empty())
            return;

        vector<string> projectionColumns;
        for (const auto& col : query.projectionColumns) {
            string columnName;
            if (col.first == query.edgeTableName) {
                columnName = col.second;
            } else {
                string tablePrefix = col.first;
                if (isSelfReferential) {
                    if (col.first.length() > 4 && col.first.substr(col.first.length() - 4) == "_src")
                        tablePrefix = col.first;
                    else if (col.first.length() > 5 && col.first.substr(col.first.length() - 5) == "_dest")
                        tablePrefix = col.first;
                    else if (col.first == query.destNodeTableName)
                        tablePrefix += "_dest";
                }
                columnName = tablePrefix + "_" + col.second;
            }
            projectionColumns.push_back(columnName);
        }
        TimedScope ts("project", "ONLINE");
        result = result.project(projectionColumns, pool);
    }

    Table oneHop(Catalog& catalog, OneHopQuery& query, ThreadPool& pool) {
        Table edgeProjectedFwd, edgeProjectedRev;
        {
            TimedScope ts("parallel branches (wall)", "ONLINE");
            auto futureFwd = std::async(std::launch::async, buildSourceAndEdgeTables,
                                        std::ref(catalog), std::ref(query), std::ref(pool), nullptr);
            auto futureRev = std::async(std::launch::async, buildDestinationTable,
                                        std::ref(catalog), std::ref(query), std::ref(pool), nullptr);
            edgeProjectedFwd = futureFwd.get();
            edgeProjectedRev = futureRev.get();
        }
        {
            TimedScope ts("unionWith (final)", "ONLINE");
            edgeProjectedFwd.unionWith(edgeProjectedRev, pool);
        }
        applyFilterAndProject(edgeProjectedFwd, query, pool);
        return edgeProjectedFwd;
    }

    Table oneHop(Catalog& catalog, OneHopQuery& query, ThreadPool& pool,
                 std::unique_ptr<NodeIndex> srcIndex,
                 std::unique_ptr<NodeIndex> dstIndex) {
        NodeIndex* srcPtr = srcIndex.release();
        NodeIndex* dstPtr = dstIndex.release();

        Table edgeProjectedFwd, edgeProjectedRev;
        {
            TimedScope ts("parallel branches (wall)", "ONLINE");
            auto futureFwd = std::async(std::launch::async, buildSourceAndEdgeTables,
                                        std::ref(catalog), std::ref(query), std::ref(pool), srcPtr);
            auto futureRev = std::async(std::launch::async, buildDestinationTable,
                                        std::ref(catalog), std::ref(query), std::ref(pool), dstPtr);
            edgeProjectedFwd = futureFwd.get();
            delete srcPtr;
            edgeProjectedRev = futureRev.get();
            delete dstPtr;
        }
        {
            TimedScope ts("unionWith (final)", "ONLINE");
            edgeProjectedFwd.unionWith(edgeProjectedRev, pool);
        }
        applyFilterAndProject(edgeProjectedFwd, query, pool);
        return edgeProjectedFwd;
    }
} // namespace obligraph
