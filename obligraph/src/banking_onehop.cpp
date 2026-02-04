/**
 * banking_onehop.cpp
 *
 * Driver program to run one-hop join on banking dataset (account -> txn -> account)
 * with filter on source account's owner_id.
 *
 * Usage: ./banking_onehop <data_dir> <output_csv> [src_owner_id]
 *   data_dir: Directory containing account.csv and txn.csv (comma-delimited)
 *   output_csv: Output file path for the hop result table
 *   src_owner_id: (optional) Filter source accounts by owner_id
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <filesystem>

#include "definitions.h"
#include "timer.h"
#include "config.h"

using namespace std;
using namespace obligraph;

/**
 * Convert comma-delimited CSV to pipe-delimited format with type headers.
 * ObliGraph expects: col1|col2|col3 (header), type1|type2|type3 (types), data...
 */
void convertNodeCSV(const string& inputPath, const string& outputPath,
                    const vector<pair<string, string>>& columnTypes) {
    ifstream inFile(inputPath);
    if (!inFile.is_open()) {
        throw runtime_error("Cannot open input file: " + inputPath);
    }

    ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        throw runtime_error("Cannot open output file: " + outputPath);
    }

    string line;

    // Read and convert header line
    if (getline(inFile, line)) {
        // Parse comma-delimited header
        stringstream ss(line);
        string col;
        vector<string> columns;
        while (getline(ss, col, ',')) {
            columns.push_back(col);
        }

        // Write pipe-delimited header with "id" as first column
        outFile << "id";
        for (const auto& c : columns) {
            outFile << "|" << c;
        }
        outFile << "\n";

        // Write type line
        outFile << "int64";  // id is always int64
        for (const auto& ct : columnTypes) {
            outFile << "|" << ct.second;
        }
        outFile << "\n";
    }

    // Convert data lines (add row number as id)
    int64_t rowId = 1;
    while (getline(inFile, line)) {
        if (line.empty()) continue;

        // Replace commas with pipes and prepend id
        outFile << rowId;
        stringstream ss(line);
        string field;
        while (getline(ss, field, ',')) {
            outFile << "|" << field;
        }
        outFile << "\n";
        rowId++;
    }

    inFile.close();
    outFile.close();
}

/**
 * Convert transaction CSV to edge format for self-join.
 * Edge format: account1Id|account2Id|edge_columns...
 */
void convertEdgeCSV(const string& inputPath, const string& outputPath,
                    const vector<pair<string, string>>& columnTypes) {
    ifstream inFile(inputPath);
    if (!inFile.is_open()) {
        throw runtime_error("Cannot open input file: " + inputPath);
    }

    ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        throw runtime_error("Cannot open output file: " + outputPath);
    }

    string line;

    // Read header line to understand column positions
    vector<string> headers;
    if (getline(inFile, line)) {
        stringstream ss(line);
        string col;
        while (getline(ss, col, ',')) {
            headers.push_back(col);
        }
    }

    // Write edge header: account1Id|account2Id|amount|txn_time
    outFile << "account1Id|account2Id";
    for (size_t i = 2; i < headers.size(); i++) {
        outFile << "|" << headers[i];
    }
    outFile << "\n";

    // Write types
    outFile << "int64|int64";
    for (size_t i = 2; i < columnTypes.size(); i++) {
        outFile << "|" << columnTypes[i].second;
    }
    outFile << "\n";

    // Convert data lines
    while (getline(inFile, line)) {
        if (line.empty()) continue;

        // Replace commas with pipes
        stringstream ss(line);
        string field;
        bool first = true;
        while (getline(ss, field, ',')) {
            if (!first) outFile << "|";
            outFile << field;
            first = false;
        }
        outFile << "\n";
    }

    inFile.close();
    outFile.close();
}

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
    if (argc < 3 || argc > 4) {
        cerr << "Usage: " << argv[0] << " <data_dir> <output_csv> [src_owner_id]" << endl;
        cerr << "  data_dir: Directory with account.csv and txn.csv" << endl;
        cerr << "  output_csv: Output file path for hop result" << endl;
        cerr << "  src_owner_id: (optional) Filter source accounts by owner_id" << endl;
        return 1;
    }

    string dataDir = argv[1];
    string outputPath = argv[2];

    // Optional owner_id filter for source accounts
    bool hasSrcFilter = (argc >= 4);
    int32_t srcOwnerId = hasSrcFilter ? stoi(argv[3]) : 0;

    try {
        // Set thread count
        // Note: Using 1 thread due to a bug in parallel_o_compact with multi-threading
        obligraph::number_of_threads.store(1);
        cout << "Using " << obligraph::number_of_threads.load() << " threads" << endl;

        auto startTotal = chrono::high_resolution_clock::now();

        // Create temp directory for converted files
        string tempDir = "/tmp/banking_onehop_" + to_string(getpid());
        filesystem::create_directories(tempDir);

        // Convert CSV files from comma-delimited to pipe-delimited with types
        cout << "Converting CSV files..." << endl;

        // Account columns: account_id, balance, owner_id
        vector<pair<string, string>> accountTypes = {
            {"account_id", "int32"},
            {"balance", "int32"},
            {"owner_id", "int32"}
        };
        convertNodeCSV(dataDir + "/account.csv", tempDir + "/account.csv", accountTypes);

        // Transaction columns: acc_from, acc_to, amount, txn_time
        vector<pair<string, string>> txnTypes = {
            {"acc_from", "int64"},
            {"acc_to", "int64"},
            {"amount", "int32"},
            {"txn_time", "int32"}
        };
        convertEdgeCSV(dataDir + "/txn.csv", tempDir + "/account_txn_account.csv", txnTypes);

        // Import data from converted files
        cout << "Importing data from " << tempDir << "..." << endl;
        Catalog catalog;
        catalog.importNodeFromCSV(tempDir + "/account.csv");
        catalog.importEdgeFromCSV(tempDir + "/account_txn_account.csv");

        cout << "Imported " << catalog.tables.size() << " tables" << endl;

        // Build predicates for source filter
        vector<pair<string, vector<Predicate>>> tablePredicates;

        if (hasSrcFilter) {
            Predicate srcFilter;
            srcFilter.column = "owner_id";
            srcFilter.op = Predicate::Cmp::EQ;
            srcFilter.constant = srcOwnerId;
            // Use base table name - oneHop internally adds "_src" suffix for self-joins
            tablePredicates.push_back({"account", {srcFilter}});
            cout << "Filtering source accounts: owner_id == " << srcOwnerId << endl;
        }

        // Define projection columns (both source and destination info)
        vector<pair<string, string>> projectionColumns = {
            {"account_src", "account_id"},
            {"account_src", "balance"},
            {"account_src", "owner_id"},
            {"txn", "amount"},
            {"txn", "txn_time"},
            {"account_dest", "account_id"},
            {"account_dest", "balance"},
            {"account_dest", "owner_id"}
        };

        // Create one-hop query for self-referential join: account -> txn -> account
        OneHopQuery query(
            "account",          // source node table
            "txn",              // edge table
            "account",          // destination node table
            tablePredicates,    // filter on source owner_id
            projectionColumns   // projection columns
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

        // Cleanup temp directory
        filesystem::remove_all(tempDir);

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
