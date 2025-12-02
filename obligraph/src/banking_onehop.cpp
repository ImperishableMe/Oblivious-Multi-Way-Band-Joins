/**
 * banking_onehop.cpp
 *
 * Driver program to run one-hop join on banking dataset (account -> txn -> account)
 * and output results in Multi-Way Band Joins compatible format.
 *
 * Usage: ./banking_onehop <data_dir> <output_csv>
 *   data_dir: Directory containing account.csv and account_txn_account.csv in ObliGraph format
 *   output_csv: Output file path for the hop result table
 */

#include <iostream>
#include <fstream>
#include <chrono>

#include "definitions.h"
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
        if (row.isDummy) continue;  // Skip dummy rows

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
        cerr << "  data_dir: Directory with account.csv and account_txn_account.csv" << endl;
        cerr << "  output_csv: Output file path for hop result" << endl;
        return 1;
    }

    string dataDir = argv[1];
    string outputPath = argv[2];

    try {
        // Set thread count
        obligraph::number_of_threads.store(thread::hardware_concurrency());
        cout << "Using " << obligraph::number_of_threads.load() << " threads" << endl;

        auto startTotal = chrono::high_resolution_clock::now();

        // Import data
        cout << "Importing data from " << dataDir << "..." << endl;
        Catalog catalog;
        catalog.importNodeFromCSV(dataDir + "/account.csv");
        catalog.importEdgeFromCSV(dataDir + "/account_txn_account.csv");

        cout << "Imported " << catalog.tables.size() << " tables" << endl;

        // Create one-hop query with empty projection (returns all columns)
        // For self-referential join: account -> txn -> account
        OneHopQuery query(
            "account",  // source node table
            "txn",      // edge table
            "account",  // destination node table
            {},         // no predicates (filters applied in final join)
            {}          // empty projection = return all columns
        );

        // Execute one-hop
        cout << "Executing one-hop join..." << endl;
        ThreadPool pool(obligraph::number_of_threads.load());

        auto startOneHop = chrono::high_resolution_clock::now();
        Table result = oneHop(catalog, query, pool);
        auto endOneHop = chrono::high_resolution_clock::now();

        auto oneHopMs = chrono::duration_cast<chrono::milliseconds>(endOneHop - startOneHop).count();
        cout << "One-hop completed in " << oneHopMs << " ms" << endl;
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
        cout << "One-hop execution: " << oneHopMs << " ms" << endl;
        cout << "Total (with I/O):  " << totalMs << " ms" << endl;

    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}
