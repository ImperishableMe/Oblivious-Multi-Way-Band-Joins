/**
 * banking_onehop.cpp
 *
 * Driver program to run one-hop join on banking dataset (account -> txn -> account).
 *
 * Usage: ./banking_onehop <data_dir> <output_csv>
 *   data_dir: Directory containing account.csv and txn.csv (comma-delimited)
 *   output_csv: Output file path for the hop result table
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>

#include "definitions.h"
#include "node_index.h"
#include "timer.h"
#include "config.h"

using namespace std;
using namespace obligraph;

void writeTableToCSV(const Table& table, const string& filePath) {
    ofstream file(filePath);
    if (!file.is_open()) {
        throw runtime_error("Cannot open output file: " + filePath);
    }

    // Write header row (comma-delimited for Multi-Way Band Joins format)
    for (size_t i = 0; i < table.schema.columnMetas.size(); i++) {
        file << table.schema.columnMetas[i].name;
        if (i < table.schema.columnMetas.size() - 1) file << ",";
    }
    file << "\n";

    // Write data rows
    for (const auto& row : table.rows) {
        if (row.isDummy()) continue;  // Skip dummy rows

        for (size_t i = 0; i < table.schema.columnMetas.size(); i++) {
            ColumnValue val = row.getColumnValue(
                table.schema.columnMetas[i].name, table.schema);

            visit([&file](const auto& v) { file << v; }, val);

            if (i < table.schema.columnMetas.size() - 1) file << ",";
        }
        file << "\n";
    }

    file.close();
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <data_dir> <output_csv>" << endl;
        cerr << "  data_dir: Directory with account.csv and txn.csv" << endl;
        cerr << "  output_csv: Output file path for hop result" << endl;
        return 1;
    }

    string dataDir = argv[1];
    string outputPath = argv[2];

    try {
        // Set thread count to all available hardware threads
        obligraph::number_of_threads.store(std::thread::hardware_concurrency());
        cout << "Using " << obligraph::number_of_threads.load() << " threads" << endl;

        auto startTotal = chrono::high_resolution_clock::now();

        // Import data directly from comma-delimited CSVs (no file conversion needed)
        cout << "Importing data from " << dataDir << "..." << endl;
        Catalog catalog;

        catalog.importNodeFromCSV(
            dataDir + "/account.csv", ',',
            {{"account_id", "int32"}, {"balance", "int32"}, {"owner_id", "int32"}},
            "account_id"
        );

        catalog.importEdgeFromCSV(
            dataDir + "/txn.csv", ',',
            {{"acc_from", "int64"}, {"acc_to", "int64"}, {"amount", "int32"}, {"txn_time", "int32"}},
            "account", "txn", "account",
            "acc_from", "acc_to"
        );

        cout << "Imported " << catalog.tables.size() << " tables" << endl;

        // No filtering — return all one-hop results
        vector<pair<string, vector<Predicate>>> tablePredicates;

        // Create one-hop query for self-referential join: account -> txn -> account
        // Empty projectionColumns = select all columns
        OneHopQuery query(
            "account",          // source node table
            "txn",              // edge table
            "account",          // destination node table
            tablePredicates     // no filters
        );

        // Execute one-hop with offline index build
        cout << "Executing one-hop join..." << endl;
        ThreadPool pool(obligraph::number_of_threads.load());

        // --- Offline build phase (timed separately) ---
        auto startBuild = chrono::high_resolution_clock::now();
        Table& accountTable = catalog.getTable("account");
        size_t edgeCount = catalog.getTable("txn_fwd").rowCount;
        auto nodeIndex = buildNodeIndex(accountTable, edgeCount);
        auto srcIndex = std::make_unique<NodeIndex>(*nodeIndex);  // Deep copy (probing is destructive)
        auto dstIndex = std::move(nodeIndex);
        auto endBuild = chrono::high_resolution_clock::now();
        auto buildMs = chrono::duration_cast<chrono::milliseconds>(endBuild - startBuild).count();
        cout << "Index build (offline) completed in " << buildMs << " ms" << endl;

        // --- Online probe phase (timed separately) ---
        auto startOneHop = chrono::high_resolution_clock::now();
        Table result = oneHop(catalog, query, pool, std::move(srcIndex), std::move(dstIndex));
        auto endOneHop = chrono::high_resolution_clock::now();

        auto oneHopMs = chrono::duration_cast<chrono::milliseconds>(endOneHop - startOneHop).count();
        cout << "One-hop probe (online) completed in " << oneHopMs << " ms" << endl;
        cout << "Result: " << result.rowCount << " rows, "
             << result.schema.columnMetas.size() << " columns" << endl;

        // Print schema for debugging
        cout << "Schema: ";
        for (size_t i = 0; i < result.schema.columnMetas.size(); i++) {
            cout << result.schema.columnMetas[i].name;
            if (i < result.schema.columnMetas.size() - 1) cout << ", ";
        }
        cout << endl;

        // Write result to CSV
        cout << "Writing result to " << outputPath << "..." << endl;
        writeTableToCSV(result, outputPath);

        auto endTotal = chrono::high_resolution_clock::now();
        auto totalMs = chrono::duration_cast<chrono::milliseconds>(endTotal - startTotal).count();

        cout << "\n=== TIMING ===" << endl;
        cout << "Index build (offline):  " << buildMs << " ms" << endl;
        cout << "One-hop probe (online): " << oneHopMs << " ms" << endl;
        cout << "Total (with I/O):       " << totalMs << " ms" << endl;

    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}
