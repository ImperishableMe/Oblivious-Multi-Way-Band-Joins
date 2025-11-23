#include <set>
#include <unordered_map>
#include <iostream>
#include <future>
#include <thread>
#include <cassert>
#include <bit>

#include "xxhash.h"
#include "definitions.h"
#include "obl_primitives.h"
#include "obl_building_blocks.h"
#include "timer.h"
#include "config.h"

using namespace std;

namespace obligraph {
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

    inline key_t hash_key(key_t key, int bucket_size) {
        // Ensure bucket_size is a power of two
        // auto hash = XXH64(&key, sizeof(key), 0); 
        // auto hash = XXH32(&key, sizeof(key), 0); 
        auto hash = triple32(key);
        return hash & (bucket_size - 1);
    }

    void build_and_probe(Table &buildT, Table &probeT, ThreadPool &pool) {
        ScopedTimer timer("Build and Probe");

        int n_build = buildT.rowCount;
        int n_probe = probeT.rowCount;
        
        // TODO: fix the bucket_size
        int bucket_size = max(2.0, n_probe * 0.8);
        bucket_size = bit_floor(static_cast<uint64_t>(bucket_size));
        

        vector <int> next(n_build, 0), bucket(bucket_size, 0);

        // build the hashmap first
        // It is not parallelized yet
        for (int i = 0; i < n_build; i++) {
            int idx = hash_key(buildT.rows[i].key.first, bucket_size);
            next[i] = bucket[idx];
            bucket[idx] = i + 1; // Store 1-based index
        }

        Row dummyRow;
        dummyRow.isDummy = true;
        // now probe the probe table, we can do it parallelly
        auto thread_chunk = [&] (int start, int end) {
            for (int i = start; i < end; i++) {
                key_t srcId = probeT.rows[i].key.first;
                auto idx = hash_key(srcId, bucket_size);

                int match = 0;
                bool dummy = probeT.rows[i].isDummy;
                for (int hit = bucket[idx]; hit != 0; hit = next[hit - 1]) {
                    // Probe the build table
                    match = ObliviousChoose(buildT.rows[hit - 1].key.first == srcId, hit - 1, match);
                }
                probeT.rows[i] = ObliviousChoose(dummy, dummyRow, buildT.rows[match]);
            }
        };

        int num_threads = obligraph::number_of_threads.load();
        std::vector <std::future<void>> futures;

        for (int i = 0; i < num_threads; i++) {
            auto chunks = obligraph::get_cutoffs_for_thread(i, probeT.rowCount, num_threads);
            if (chunks.first == chunks.second) continue; // no work for this thread
            if (i == num_threads - 1) {
                // Last thread takes any remaining rows
                thread_chunk(chunks.first, chunks.second);
            }
            else {
                futures.push_back(pool.submit(thread_chunk, chunks.first, chunks.second));
            }
        }

        for (auto& fut : futures) {
            fut.get();
        }
    }

    void deduplicateRows(Table& table) {
        key_t lastKey = -1;
        key_t dummy = 1e9;

        for (size_t i = 0; i < table.rows.size(); ++i) {
            key_t currentKey = table.rows[i].key.first;
            // TODO: use cpp style random-ness instead?
            dummy = random();
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

        for (const auto& col : query.projectionColumns) {
            if (col.first == query.sourceNodeTableName) {
                srcColumns.insert(col.second);
            } else if (col.first == query.edgeTableName) {
                edgeColumns.insert(col.second);
            }
        }
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

        Table srcTable = catalog.getTable(query.sourceNodeTableName );
        Table srcProjected = srcTable.project(vector<string>(srcColumns.begin(), srcColumns.end()), pool);

        Table edgeTableFwd = catalog.getTable(query.edgeTableName + "_fwd");
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

        for (const auto& col : query.projectionColumns) {
           if(col.first == query.destNodeTableName) {
                dstColumns.insert(col.second);
           }
        }

        for (const auto& tablePred : query.tablePredicates) {
            if (tablePred.first == query.destNodeTableName) {
                for (const auto& pred : tablePred.second) {
                    dstColumns.insert(pred.column);
                }
            }
        }

        Table dstTable = catalog.getTable(query.destNodeTableName);
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

                // In self-referential joins, determine if referring to source or dest
                if (isSelfReferential) {
                    // Projections on source table refer to destination by convention
                    if (col.first == query.destNodeTableName) {
                        tablePrefix += "_dest";
                    }
                    // If user explicitly wants source columns, they would need to use sourceNodeTableName
                    // For now, default projection to destination
                }

                columnName = tablePrefix + "_" + col.second;
            }

            projectionColumns.push_back(columnName);
        }

        edgeProjectedFwd = edgeProjectedFwd.project(projectionColumns, pool);
        return edgeProjectedFwd;
    }
} // namespace obligraph
