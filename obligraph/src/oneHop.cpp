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

using namespace std;

namespace obligraph {
    std::unique_ptr<NodeIndex> buildNodeIndex(const Table& table, size_t op_num) {
        ScopedTimer timer("buildNodeIndex");

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

    void probe_with_index(NodeIndex& obin, Table& probeT, ThreadPool& pool) {
        ScopedTimer timer("Probe with Pre-built Index");

        auto thread_chunk = [&](int start, int end) {
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

        int num_threads = obligraph::number_of_threads.load();
        std::vector<std::future<void>> futures;

        for (int i = 0; i < num_threads; i++) {
            auto chunks = obligraph::get_cutoffs_for_thread(i, probeT.rowCount, num_threads);
            if (chunks.first == chunks.second) continue;
            if (i == num_threads - 1) {
                thread_chunk(chunks.first, chunks.second);
            } else {
                futures.push_back(pool.submit(thread_chunk, chunks.first, chunks.second));
            }
        }

        for (auto& fut : futures) {
            fut.get();
        }
    }

    void build_and_probe(Table &buildT, Table &probeT, ThreadPool& pool) {
        ScopedTimer timer("Build and Probe (ObliviousBin)");
        auto index = buildNodeIndex(buildT, probeT.rowCount);
        probe_with_index(*index, probeT, pool);
    }

    void deduplicateRows(Table& table) {
        key_t lastKey = -1;
        key_t dummy = 1e9;

        for (size_t i = 0; i < table.rows.size(); ++i) {
            key_t currentKey = table.rows[i].key.first;
            // Generate dummy key without MSB set (MSB reserved for ObliviousBin dummy marking)
            dummy = static_cast<key_t>(random()) & ~DUMMY_KEY_MSB;
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
            table.rows[i].key.second = secKey; // Restore the second key
            lastRow = table.rows[i];
        }
    }

    Table buildSourceAndEdgeTables(Catalog& catalog, const OneHopQuery& query,
                                    ThreadPool &pool, NodeIndex* srcIndex = nullptr) {
        ScopedTimer timer("Building Source and Edge Tables");

        set <string> srcColumns;
        set <string> edgeColumns;

        Table srcTable = catalog.getTable(query.sourceNodeTableName);
        Table edgeTableFwd = catalog.getTable(query.edgeTableName + "_fwd");

        // If projectionColumns is empty, include ALL columns from source and edge tables
        if (query.projectionColumns.empty()) {
            for (const auto& meta : srcTable.schema.columnMetas) {
                srcColumns.insert(meta.name);
            }
            for (const auto& meta : edgeTableFwd.schema.columnMetas) {
                edgeColumns.insert(meta.name);
            }
        } else {
            for (const auto& col : query.projectionColumns) {
                if (col.first == query.sourceNodeTableName ||
                    (col.first.length() > 4 && col.first.substr(col.first.length() - 4) == "_src")) {
                    srcColumns.insert(col.second);
                } else if (col.first == query.edgeTableName) {
                    edgeColumns.insert(col.second);
                }
            }
        }

        // Also include columns needed for predicates
        for (const auto& tablePred : query.tablePredicates) {
            if (tablePred.first == query.sourceNodeTableName) {
                for (const auto& pred : tablePred.second) {
                    srcColumns.insert(pred.column);
                }
            } else if (tablePred.first == query.edgeTableName) {
                for (const auto& pred : tablePred.second) {
                    edgeColumns.insert(pred.column);
                }
            }
        }

        Table srcProjected = srcTable.project(vector<string>(srcColumns.begin(), srcColumns.end()), pool);
        Table edgeProjectedFwd = edgeTableFwd.project(vector<string>(edgeColumns.begin(), edgeColumns.end()), pool);

        Table srcSide;
        srcSide.init(srcProjected);
        srcSide.rows.reserve(edgeProjectedFwd.rowCount);
        for (size_t i = 0; i < edgeProjectedFwd.rowCount; i++) {
            srcSide.addRow(Row());
            srcSide.rows[i].key = edgeProjectedFwd.rows[i].key; // Copy keys from edge table
        }
        deduplicateRows(srcSide);

        if (srcIndex)
            probe_with_index(*srcIndex, srcSide, pool);
        else
            build_and_probe(srcProjected, srcSide, pool);

        reduplicateRows(srcSide);

        // For self-referential joins, use "_src" suffix to distinguish from destination
        string srcPrefix = query.sourceNodeTableName;
        if (query.sourceNodeTableName == query.destNodeTableName) {
            srcPrefix += "_src";
        }
        edgeProjectedFwd.unionWith(srcSide, pool, srcPrefix);
        return edgeProjectedFwd;
    }

    Table buildDestinationTable(Catalog& catalog, const OneHopQuery& query,
                                ThreadPool& pool, NodeIndex* dstIndex = nullptr) {
        ScopedTimer timer("Building Destination Table");
        set <string> dstColumns;

        Table dstTable = catalog.getTable(query.destNodeTableName);

        // If projectionColumns is empty, include ALL columns from destination table
        if (query.projectionColumns.empty()) {
            for (const auto& meta : dstTable.schema.columnMetas) {
                dstColumns.insert(meta.name);
            }
        } else {
            for (const auto& col : query.projectionColumns) {
                if (col.first == query.destNodeTableName ||
                    (col.first.length() > 5 && col.first.substr(col.first.length() - 5) == "_dest")) {
                    dstColumns.insert(col.second);
                }
            }
        }

        // Also include columns needed for predicates
        for (const auto& tablePred : query.tablePredicates) {
            if (tablePred.first == query.destNodeTableName) {
                for (const auto& pred : tablePred.second) {
                    dstColumns.insert(pred.column);
                }
            }
        }

        Table dstProjected = dstTable.project(vector<string>(dstColumns.begin(), dstColumns.end()), pool);
        Table edgeTableRev = catalog.getTable(query.edgeTableName + "_rev");
        Table dstSide;
        {
            ScopedTimer timer("Building Destination Side Table");
            dstSide.init(dstProjected);
            dstSide.rows.reserve(edgeTableRev.rowCount);
            for (size_t i = 0; i < edgeTableRev.rowCount; i++) {
                dstSide.addRow(Row());
                dstSide.rows[i].key = edgeTableRev.rows[i].key; // Copy keys from edge table
            }
            deduplicateRows(dstSide);
        }

        if (dstIndex)
            probe_with_index(*dstIndex, dstSide, pool);
        else
            build_and_probe(dstProjected, dstSide, pool);
        {
            ScopedTimer timerRedup("Reduplicating Destination Rows");
            reduplicateRows(dstSide);
        }
        {
            ScopedTimer timerUnion("Union with Edge Table");
            // For self-referential joins, use "_dest" suffix to distinguish from source
            string dstPrefix = query.destNodeTableName;
            if (query.sourceNodeTableName == query.destNodeTableName) {
                dstPrefix += "_dest";
            }
            edgeTableRev.unionWith(dstSide, pool, dstPrefix);
        }

        {
            ScopedTimer timerSort("Sorting Edge Table");
            parallel_sort(edgeTableRev.rows.begin(), edgeTableRev.rows.end(),
                pool,
                // TODO: Make the comparator oblivious
                [](const Row& a, const Row& b) {
                    if (a.key.second == b.key.second) {
                        return a.key.first < b.key.first;
                    }
                    return a.key.second < b.key.second;
                },
                pool.size()
            );
        }

        return edgeTableRev;
    }

    Table oneHop(Catalog& catalog, OneHopQuery& query, ThreadPool& pool) {
        // Execute the two function calls in parallel
        auto futureFwd = std::async(std::launch::async, buildSourceAndEdgeTables,
                                  std::ref(catalog), std::ref(query), std::ref(pool), nullptr);
        auto futureRev = std::async(std::launch::async, buildDestinationTable,
                                  std::ref(catalog), std::ref(query), std::ref(pool), nullptr);
        //
        //// Wait for both results
        Table edgeProjectedFwd = futureFwd.get();
        Table edgeProjectedRev = futureRev.get();

        // Table edgeProjectedFwd = buildSourceAndEdgeTables(catalog, query);
        // Table edgeProjectedRev = buildDestinationTable(catalog, query, pool);
        edgeProjectedFwd.unionWith(edgeProjectedRev, pool);

        // Detect self-referential join
        bool isSelfReferential = (query.sourceNodeTableName == query.destNodeTableName);

        vector<Predicate> allPredicates;
        for (const auto& tablePred : query.tablePredicates) {
            // Qualify column names in predicates with table names
            for (const auto& pred : tablePred.second) {
                Predicate qualifiedPred = pred;
                string tablePrefix = tablePred.first;

                // In self-referential joins, predicates on the table refer to source
                if (isSelfReferential && tablePred.first == query.sourceNodeTableName) {
                    tablePrefix += "_src";
                }

                qualifiedPred.column = tablePrefix + "_" + pred.column;
                allPredicates.push_back(qualifiedPred);
            }
        }
        edgeProjectedFwd.filter(allPredicates, pool);

        // If no projection columns specified, return all columns (skip projection)
        if (query.projectionColumns.empty()) {
            return edgeProjectedFwd;
        }

        vector <string> projectionColumns;
        for (const auto& col : query.projectionColumns) {
            string columnName;

            // Check if this is an edge column or a node column
            if (col.first == query.edgeTableName) {
                // Edge columns are not prefixed - they're already in the schema as-is
                columnName = col.second;
            } else {
                // Node columns need table-qualified names
                string tablePrefix = col.first;

                // In self-referential joins, check for explicit _src or _dest suffix
                if (isSelfReferential) {
                    // Check if user explicitly specified source or destination
                    if (col.first.length() > 4 && col.first.substr(col.first.length() - 4) == "_src") {
                        // User wants source columns - use the base table name + _src
                        tablePrefix = col.first;  // Already has _src suffix
                    } else if (col.first.length() > 5 && col.first.substr(col.first.length() - 5) == "_dest") {
                        // User wants dest columns - use the base table name + _dest
                        tablePrefix = col.first;  // Already has _dest suffix
                    } else if (col.first == query.destNodeTableName) {
                        // Default: destination columns for backward compatibility
                        tablePrefix += "_dest";
                    }
                }

                columnName = tablePrefix + "_" + col.second;
            }

            projectionColumns.push_back(columnName);
        }

        edgeProjectedFwd = edgeProjectedFwd.project(projectionColumns, pool);
        return edgeProjectedFwd;
    }

    Table oneHop(Catalog& catalog, OneHopQuery& query, ThreadPool& pool,
                 std::unique_ptr<NodeIndex> srcIndex,
                 std::unique_ptr<NodeIndex> dstIndex) {
        // Release ownership — raw pointers needed for std::async (no move-capture in C++14-style)
        NodeIndex* srcPtr = srcIndex.release();
        NodeIndex* dstPtr = dstIndex.release();

        auto futureFwd = std::async(std::launch::async, buildSourceAndEdgeTables,
                                    std::ref(catalog), std::ref(query), std::ref(pool), srcPtr);
        auto futureRev = std::async(std::launch::async, buildDestinationTable,
                                    std::ref(catalog), std::ref(query), std::ref(pool), dstPtr);

        Table edgeProjectedFwd = futureFwd.get();
        delete srcPtr;
        Table edgeProjectedRev = futureRev.get();
        delete dstPtr;

        edgeProjectedFwd.unionWith(edgeProjectedRev, pool);

        // --- Same filter/project logic as the original oneHop ---
        bool isSelfReferential = (query.sourceNodeTableName == query.destNodeTableName);

        vector<Predicate> allPredicates;
        for (const auto& tablePred : query.tablePredicates) {
            for (const auto& pred : tablePred.second) {
                Predicate qualifiedPred = pred;
                string tablePrefix = tablePred.first;
                if (isSelfReferential && tablePred.first == query.sourceNodeTableName) {
                    tablePrefix += "_src";
                }
                qualifiedPred.column = tablePrefix + "_" + pred.column;
                allPredicates.push_back(qualifiedPred);
            }
        }
        edgeProjectedFwd.filter(allPredicates, pool);

        if (query.projectionColumns.empty()) {
            return edgeProjectedFwd;
        }

        vector<string> projectionColumns;
        for (const auto& col : query.projectionColumns) {
            string columnName;
            if (col.first == query.edgeTableName) {
                columnName = col.second;
            } else {
                string tablePrefix = col.first;
                if (isSelfReferential) {
                    if (col.first.length() > 4 && col.first.substr(col.first.length() - 4) == "_src") {
                        tablePrefix = col.first;
                    } else if (col.first.length() > 5 && col.first.substr(col.first.length() - 5) == "_dest") {
                        tablePrefix = col.first;
                    } else if (col.first == query.destNodeTableName) {
                        tablePrefix += "_dest";
                    }
                }
                columnName = tablePrefix + "_" + col.second;
            }
            projectionColumns.push_back(columnName);
        }

        edgeProjectedFwd = edgeProjectedFwd.project(projectionColumns, pool);
        return edgeProjectedFwd;
    }
} // namespace obligraph
