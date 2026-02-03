#include <set>
#include <unordered_map>
#include <iostream>
#include <future>
#include <thread>
#include <cassert>
#include <bit>
#include <cstring>

#include "xxhash.h"
#include "definitions.h"
#include "obl_primitives.h"
#include "obl_building_blocks.h"
#include "timer.h"
#include "config.h"

// ObliviousBin headers
#include "ohash_bin.hpp"
#include "hash_planner.hpp"

using namespace std;

namespace obligraph {
    // Block size for ObliviousBin: key_t (8 bytes) + Row payload
    // The Block template stores: id (key_t) + value[BlockSize - sizeof(key_t)]
    // We need value to hold a Row, so BlockSize = sizeof(key_t) + sizeof(Row)
    constexpr size_t ROW_BLOCK_SIZE = sizeof(key_t) + sizeof(Row);
    using RowBlock = ORAM::Block<key_t, ROW_BLOCK_SIZE>;

    // Dummy marker: MSB set indicates a dummy block
    constexpr key_t DUMMY_KEY_MSB = 1ULL << 63;

    void build_and_probe(Table &buildT, Table &probeT, ThreadPool &pool) {
        ScopedTimer timer("Build and Probe (ObliviousBin)");

        int n_build = buildT.rowCount;
        int n_probe = probeT.rowCount;

        // Pad to next power of 2 (required by ObliviousBin)
        size_t n = std::max(static_cast<size_t>(n_build), static_cast<size_t>(n_probe));
        n = std::bit_ceil(n);

        // Create blocks from build table
        std::vector<RowBlock> blocks(n);

        for (int i = 0; i < n_build; i++) {
            // Clear MSB from key (MSB is reserved for dummy marking in ObliviousBin)
            key_t key = buildT.rows[i].key.first & ~DUMMY_KEY_MSB;
            blocks[i].id = key;
            std::memcpy(blocks[i].value, &buildT.rows[i], sizeof(Row));
        }

        // Fill remaining with dummy blocks (MSB set in id)
        for (size_t i = n_build; i < n; i++) {
            blocks[i].id = static_cast<key_t>(i) | DUMMY_KEY_MSB;
        }

        // Build ObliviousBin (fully oblivious)
        // Algorithm selection is automatic based on n:
        //   - n < 128: OLinearScan (simple linear scan)
        //   - n >= 128: Benchmarks OHashBucket, OCuckooHash, OTwoTierHash
        //               and selects fastest (cached in hash_map.bin{BlockSize})
        ORAM::ObliviousBin<key_t, ROW_BLOCK_SIZE> obin(n);
        obin.build(blocks.data());

        // Probe phase (oblivious lookups)
        Row dummyRow;
        dummyRow.isDummy = true;

        for (int i = 0; i < n_probe; i++) {
            // Clear MSB from probe key (MSB is reserved for dummy marking in ObliviousBin)
            key_t srcId = probeT.rows[i].key.first & ~DUMMY_KEY_MSB;
            bool dummy = probeT.rows[i].isDummy;

            RowBlock result = obin[srcId];

            Row matchedRow;
            std::memcpy(&matchedRow, result.value, sizeof(Row));

            // Use ObliviousChoose for final selection
            // If probe row is dummy OR result block is dummy, use dummyRow
            probeT.rows[i] = ObliviousChoose(
                dummy || result.dummy(),
                dummyRow,
                matchedRow
            );
        }
    }

    void deduplicateRows(Table& table) {
        key_t lastKey = -1;
        key_t dummy = 1e9;

        for (size_t i = 0; i < table.rows.size(); ++i) {
            key_t currentKey = table.rows[i].key.first;
            // Generate dummy key without MSB set (MSB reserved for ObliviousBin dummy marking)
            dummy = static_cast<key_t>(random()) & ~DUMMY_KEY_MSB;
            table.rows[i].key.first = ObliviousChoose(lastKey == currentKey, dummy, currentKey);
            table.rows[i].isDummy = (lastKey == currentKey);
            lastKey = currentKey;
        }
    }

    void reduplicateRows(Table& table) {
        Row lastRow;
        for (size_t i = 0; i < table.rows.size(); ++i) {
            auto secKey = table.rows[i].key.second;
            table.rows[i] = ObliviousChoose(table.rows[i].isDummy, lastRow, table.rows[i]);
            table.rows[i].key.second = secKey; // Restore the second key
            lastRow = table.rows[i];
        }
    }

    Table buildSourceAndEdgeTables(Catalog& catalog, const OneHopQuery& query, ThreadPool &pool) {
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
        for (int i = 0; i < edgeProjectedFwd.rowCount; i++) {
            srcSide.addRow(Row());
            srcSide.rows[i].key = edgeProjectedFwd.rows[i].key; // Copy keys from edge table
        }
        deduplicateRows(srcSide);
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

    Table buildDestinationTable(Catalog& catalog, const OneHopQuery& query, ThreadPool& pool) {
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
            for (int i = 0; i < edgeTableRev.rowCount; i++) {
                dstSide.addRow(Row());
                dstSide.rows[i].key = edgeTableRev.rows[i].key; // Copy keys from edge table
            }
            deduplicateRows(dstSide);
        }

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
                                  std::ref(catalog), std::ref(query), std::ref(pool));
        auto futureRev = std::async(std::launch::async, buildDestinationTable,
                                  std::ref(catalog), std::ref(query), std::ref(pool));
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
} // namespace obligraph
