#include <set>
#include <unordered_map>
#include <iostream>
#include <future>
#include <thread>
#include <cassert>
#include <bit>
#include <cstring>

#include "xxhash.h"
#include "node_index.h"
#include "obl_primitives.h"
#include "obl_building_blocks.h"
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

    void probe_with_index(NodeIndex& obin, Table& probeT, ThreadPool& pool,
                          const std::string& label) {
        TimedScope ts("probe " + label, "ONLINE");

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
        TimedScope ts("build_and_probe", "ONLINE");
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

    Table buildSourceAndEdgeTables(Catalog& catalog, const OneHopQuery& query,
                                    ThreadPool &pool, NodeIndex* srcIndex) {
        TimedScope ts_total("src branch (total)", "ONLINE");

        string srcPrefix = query.sourceNodeTableName;
        if (query.sourceNodeTableName == query.destNodeTableName)
            srcPrefix += "_src";

        if (query.projectionColumns.empty()) {
            const Table& srcRef = catalog.getTable(query.sourceNodeTableName);
            Table edgeTableFwd = catalog.getTable(query.edgeTableName + "_fwd");

            Table srcSide;
            srcSide.init(srcRef);
            srcSide.rows.reserve(edgeTableFwd.rowCount);
            {
                TimedScope ts("deduplicateRows (src)", "ONLINE");
                for (size_t i = 0; i < edgeTableFwd.rowCount; i++) {
                    srcSide.addRow(Row());
                    srcSide.rows[i].key = edgeTableFwd.rows[i].key;
                }
                deduplicateRows(srcSide);
            }

            if (srcIndex)
                probe_with_index(*srcIndex, srcSide, pool, "src");
            else
                build_and_probe(srcRef, srcSide, pool);

            {
                TimedScope ts("reduplicateRows (src)", "ONLINE");
                reduplicateRows(srcSide);
            }
            {
                TimedScope ts("unionWith (src)", "ONLINE");
                edgeTableFwd.unionWith(srcSide, pool, srcPrefix);
            }
            return edgeTableFwd;
        }

        set<string> srcColumns;
        set<string> edgeColumns;

        Table srcTable = catalog.getTable(query.sourceNodeTableName);
        Table edgeTableFwd = catalog.getTable(query.edgeTableName + "_fwd");

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
        srcSide.rows.reserve(edgeProjectedFwd.rowCount);
        {
            TimedScope ts("deduplicateRows (src)", "ONLINE");
            for (size_t i = 0; i < edgeProjectedFwd.rowCount; i++) {
                srcSide.addRow(Row());
                srcSide.rows[i].key = edgeProjectedFwd.rows[i].key;
            }
            deduplicateRows(srcSide);
        }

        if (srcIndex)
            probe_with_index(*srcIndex, srcSide, pool, "src");
        else
            build_and_probe(srcProjected, srcSide, pool);

        {
            TimedScope ts("reduplicateRows (src)", "ONLINE");
            reduplicateRows(srcSide);
        }
        {
            TimedScope ts("unionWith (src)", "ONLINE");
            edgeProjectedFwd.unionWith(srcSide, pool, srcPrefix);
        }
        return edgeProjectedFwd;
    }

    Table buildDestinationTable(Catalog& catalog, const OneHopQuery& query,
                                ThreadPool& pool, NodeIndex* dstIndex) {
        TimedScope ts_total("dst branch (total)", "ONLINE");

        string dstPrefix = query.destNodeTableName;
        if (query.sourceNodeTableName == query.destNodeTableName)
            dstPrefix += "_dest";

        Table edgeTableRev = catalog.getTable(query.edgeTableName + "_rev");

        auto doSortAndReturn = [&]() -> Table {
            TimedScope ts("parallel_sort (dst)", "ONLINE");
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
            return edgeTableRev;
        };

        if (query.projectionColumns.empty()) {
            const Table& dstRef = catalog.getTable(query.destNodeTableName);

            Table dstSide;
            {
                TimedScope ts("deduplicateRows (dst)", "ONLINE");
                dstSide.init(dstRef);
                dstSide.rows.reserve(edgeTableRev.rowCount);
                for (size_t i = 0; i < edgeTableRev.rowCount; i++) {
                    dstSide.addRow(Row());
                    dstSide.rows[i].key = edgeTableRev.rows[i].key;
                }
                deduplicateRows(dstSide);
            }

            if (dstIndex)
                probe_with_index(*dstIndex, dstSide, pool, "dst");
            else
                build_and_probe(dstRef, dstSide, pool);

            {
                TimedScope ts("reduplicateRows (dst)", "ONLINE");
                reduplicateRows(dstSide);
            }
            {
                TimedScope ts("unionWith (dst)", "ONLINE");
                edgeTableRev.unionWith(dstSide, pool, dstPrefix);
            }

            return doSortAndReturn();
        }

        set<string> dstColumns;
        Table dstTable = catalog.getTable(query.destNodeTableName);

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
        {
            TimedScope ts("deduplicateRows (dst)", "ONLINE");
            dstSide.init(dstProjected);
            dstSide.rows.reserve(edgeTableRev.rowCount);
            for (size_t i = 0; i < edgeTableRev.rowCount; i++) {
                dstSide.addRow(Row());
                dstSide.rows[i].key = edgeTableRev.rows[i].key;
            }
            deduplicateRows(dstSide);
        }

        if (dstIndex)
            probe_with_index(*dstIndex, dstSide, pool, "dst");
        else
            build_and_probe(dstProjected, dstSide, pool);

        {
            TimedScope ts("reduplicateRows (dst)", "ONLINE");
            reduplicateRows(dstSide);
        }
        {
            TimedScope ts("unionWith (dst)", "ONLINE");
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
