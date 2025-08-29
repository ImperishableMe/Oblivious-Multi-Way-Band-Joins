#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include "../../app/data_structures/types.h"
#include "../../app/io/table_io.h"
#include "../../app/crypto/crypto_utils.h"
#include "sgx_urts.h"
#include "../../app/Enclave_u.h"

/* Global enclave ID for decryption */
sgx_enclave_id_t global_eid = 0;

/* Initialize the enclave */
int initialize_enclave() {
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    
    ret = sgx_create_enclave("/home/r33wei/omwj/memory_const/impl/src/enclave.signed.so", SGX_DEBUG_FLAG, NULL, NULL, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error code: 0x" << std::hex << ret << std::endl;
        return -1;
    }
    
    std::cout << "SGX Enclave initialized for result comparison" << std::endl;
    return 0;
}

/* Destroy the enclave */
void destroy_enclave() {
    if (global_eid != 0) {
        sgx_destroy_enclave(global_eid);
    }
}

/* Decrypt a table */
Table decrypt_table(const Table& encrypted_table) {
    Table decrypted = encrypted_table;
    
    for (size_t i = 0; i < decrypted.size(); i++) {
        Entry& entry = decrypted[i];
        if (entry.is_encrypted) {
            CryptoUtils::decrypt_entry(entry, global_eid);
        }
    }
    
    return decrypted;
}

/* Convert table to comparable format (sorted rows as strings) */
std::multiset<std::string> table_to_multiset(const Table& table) {
    std::multiset<std::string> result;
    
    for (const auto& entry : table) {
        // Create pairs of (column_name, value) and sort by column name
        std::vector<std::pair<std::string, int32_t>> column_value_pairs;
        
        for (size_t i = 0; i < entry.attributes.size() && i < entry.column_names.size(); i++) {
            column_value_pairs.emplace_back(entry.column_names[i], entry.attributes[i]);
        }
        
        // Sort by column name alphabetically
        std::sort(column_value_pairs.begin(), column_value_pairs.end(),
                  [](const std::pair<std::string, int32_t>& a, 
                     const std::pair<std::string, int32_t>& b) { 
                      return a.first < b.first; 
                  });
        
        // Create the row string from sorted columns
        std::string row;
        for (size_t i = 0; i < column_value_pairs.size(); i++) {
            if (i > 0) row += ",";
            row += std::to_string(column_value_pairs[i].second);
        }
        result.insert(row);
    }
    
    return result;
}

/* Compare two tables for equivalence */
struct ComparisonResult {
    bool are_equivalent;
    size_t sgx_rows;
    size_t sqlite_rows;
    size_t matching_rows;
    std::vector<std::string> sgx_only;
    std::vector<std::string> sqlite_only;
};

ComparisonResult compare_tables(const Table& sgx_table, const Table& sqlite_table) {
    ComparisonResult result;
    result.sgx_rows = sgx_table.size();
    result.sqlite_rows = sqlite_table.size();
    
    // Convert to multisets for comparison
    auto sgx_set = table_to_multiset(sgx_table);
    auto sqlite_set = table_to_multiset(sqlite_table);
    
    // Find differences
    std::set_difference(sgx_set.begin(), sgx_set.end(),
                        sqlite_set.begin(), sqlite_set.end(),
                        std::back_inserter(result.sgx_only));
    
    std::set_difference(sqlite_set.begin(), sqlite_set.end(),
                        sgx_set.begin(), sgx_set.end(),
                        std::back_inserter(result.sqlite_only));
    
    // Count matching rows
    result.matching_rows = sgx_set.size() - result.sgx_only.size();
    
    // Check equivalence
    result.are_equivalent = (result.sgx_only.empty() && result.sqlite_only.empty());
    
    return result;
}

/* Run a command and measure time */
double run_timed_command(const std::string& command) {
    std::cout << "Executing: " << command << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    int ret = std::system(command.c_str());
    auto end = std::chrono::high_resolution_clock::now();
    
    if (ret != 0) {
        throw std::runtime_error("Command failed: " + command);
    }
    
    std::chrono::duration<double> diff = end - start;
    return diff.count();
}

/* Print usage information */
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <sql_file> <data_dir>" << std::endl;
    std::cout << "  sql_file : SQL file containing the query" << std::endl;
    std::cout << "  data_dir : Directory containing encrypted input tables" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string sql_file = argv[1];
    std::string data_dir = argv[2];
    
    std::cout << "\n=== Join Test Comparator ===" << std::endl;
    std::cout << "SQL file: " << sql_file << std::endl;
    std::cout << "Data directory: " << data_dir << std::endl;
    
    try {
        // Initialize enclave for decryption
        if (initialize_enclave() < 0) {
            std::cerr << "Enclave initialization failed!" << std::endl;
            return -1;
        }
        
        // Create temporary directory for outputs
        std::string output_dir = "/tmp/join_compare_" + std::to_string(time(nullptr));
        mkdir(output_dir.c_str(), 0755);
        
        // Define output files
        std::string sgx_output = output_dir + "/sgx_result.csv";
        std::string sqlite_output = output_dir + "/sqlite_result.csv";
        
        // Check if SQL file exists
        struct stat buffer;
        if (stat(sql_file.c_str(), &buffer) != 0) {
            std::cerr << "SQL file not found: " << sql_file << std::endl;
            return 1;
        }
        
        // Run SGX oblivious join
        std::cout << "\n--- Running SGX Oblivious Join ---" << std::endl;
        std::string sgx_cmd = "/home/r33wei/omwj/memory_const/impl/src/sgx_app " + sql_file + " " + data_dir + " " + sgx_output;
        double sgx_time = run_timed_command(sgx_cmd);
        std::cout << "SGX join completed in " << sgx_time << " seconds" << std::endl;
        
        // Run SQLite baseline
        std::cout << "\n--- Running SQLite Baseline ---" << std::endl;
        std::string sqlite_cmd = "/home/r33wei/omwj/memory_const/impl/src/test/sqlite_baseline " + sql_file + " " + data_dir + " " + sqlite_output;
        double sqlite_time = run_timed_command(sqlite_cmd);
        std::cout << "SQLite join completed in " << sqlite_time << " seconds" << std::endl;
        
        // Load and decrypt results
        std::cout << "\n--- Comparing Results ---" << std::endl;
        
        std::cout << "Loading SGX result..." << std::endl;
        Table sgx_encrypted = TableIO::load_csv(sgx_output);
        std::cout << "  Loaded " << sgx_encrypted.size() << " encrypted rows" << std::endl;
        std::cout << "  First entry encrypted: " << sgx_encrypted[0].is_encrypted << std::endl;
        Table sgx_result = decrypt_table(sgx_encrypted);
        std::cout << "  After decrypt, first entry encrypted: " << sgx_result[0].is_encrypted << std::endl;
        
        std::cout << "Loading SQLite result..." << std::endl;
        Table sqlite_encrypted = TableIO::load_csv(sqlite_output);
        std::cout << "  Loaded " << sqlite_encrypted.size() << " encrypted rows" << std::endl;
        std::cout << "  First entry encrypted: " << sqlite_encrypted[0].is_encrypted << std::endl;
        Table sqlite_result = decrypt_table(sqlite_encrypted);
        std::cout << "  After decrypt, first entry encrypted: " << sqlite_result[0].is_encrypted << std::endl;
        
        // Compare results
        ComparisonResult comparison = compare_tables(sgx_result, sqlite_result);
        
        // Print comparison results
        std::cout << "\n=== Comparison Results ===" << std::endl;
        std::cout << "SGX rows: " << comparison.sgx_rows << std::endl;
        std::cout << "SQLite rows: " << comparison.sqlite_rows << std::endl;
        std::cout << "Matching rows: " << comparison.matching_rows << std::endl;
        
        if (comparison.are_equivalent) {
            std::cout << "\n✓ PASS: Results are equivalent!" << std::endl;
        } else {
            std::cout << "\n✗ FAIL: Results differ!" << std::endl;
            
            if (!comparison.sgx_only.empty()) {
                std::cout << "\nRows only in SGX result (" << comparison.sgx_only.size() << "):" << std::endl;
                for (size_t i = 0; i < comparison.sgx_only.size(); i++) {
                    std::cout << "  " << comparison.sgx_only[i] << std::endl;
                }
            }
            
            if (!comparison.sqlite_only.empty()) {
                std::cout << "\nRows only in SQLite result (" << comparison.sqlite_only.size() << "):" << std::endl;
                for (size_t i = 0; i < comparison.sqlite_only.size(); i++) {
                    std::cout << "  " << comparison.sqlite_only[i] << std::endl;
                }
            }
        }
        
        // Performance comparison
        std::cout << "\n=== Performance ===" << std::endl;
        std::cout << "SGX time: " << sgx_time << " seconds" << std::endl;
        std::cout << "SQLite time: " << sqlite_time << " seconds" << std::endl;
        
        // Write summary to file
        // Extract base names from paths
        std::string query_basename = sql_file.substr(sql_file.find_last_of("/\\") + 1);
        if (query_basename.size() > 4 && query_basename.substr(query_basename.size() - 4) == ".sql") {
            query_basename = query_basename.substr(0, query_basename.size() - 4);
        }
        std::string data_basename = data_dir.substr(data_dir.find_last_of("/\\") + 1);
        if (data_basename.empty()) {
            // Handle case where path ends with /
            std::string temp = data_dir.substr(0, data_dir.find_last_of("/\\"));
            data_basename = temp.substr(temp.find_last_of("/\\") + 1);
        }
        
        // Create output directory if it doesn't exist
        std::string summary_dir = "/home/r33wei/omwj/memory_const/output";
        mkdir(summary_dir.c_str(), 0755);
        
        std::string summary_filename = summary_dir + "/" + query_basename + "_" + data_basename + "_summary.txt";
        std::ofstream summary_file(summary_filename);
        if (summary_file.is_open()) {
            // Count input table sizes
            std::map<std::string, size_t> table_sizes;
            DIR* dir = opendir(data_dir.c_str());
            if (dir) {
                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    std::string filename = entry->d_name;
                    if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".csv") {
                        std::string filepath = data_dir + "/" + filename;
                        std::string table_name = filename.substr(0, filename.size() - 4);
                        Table temp_table = TableIO::load_csv(filepath);
                        table_sizes[table_name] = temp_table.size();
                    }
                }
                closedir(dir);
            }
            
            // Write summary
            summary_file << "=== Test Summary ===" << std::endl;
            summary_file << "Query File: " << query_basename << ".sql" << std::endl;
            summary_file << "Dataset: " << data_basename << std::endl;
            
            summary_file << "\n=== Input Table Sizes ===" << std::endl;
            for (const auto& pair : table_sizes) {
                summary_file << pair.first << ": " << pair.second << " rows" << std::endl;
            }
            
            summary_file << "\n=== Output ===" << std::endl;
            summary_file << "SGX Output Size: " << comparison.sgx_rows << " rows" << std::endl;
            summary_file << "SQLite Output Size: " << comparison.sqlite_rows << " rows" << std::endl;
            
            summary_file << "\n=== Results ===" << std::endl;
            summary_file << "Match: " << (comparison.are_equivalent ? "YES" : "NO") << std::endl;
            if (!comparison.are_equivalent) {
                summary_file << "  Matching rows: " << comparison.matching_rows << std::endl;
                summary_file << "  SGX-only rows: " << comparison.sgx_only.size() << std::endl;
                summary_file << "  SQLite-only rows: " << comparison.sqlite_only.size() << std::endl;
                
                // Output actual mismatched rows
                if (!comparison.sgx_only.empty()) {
                    summary_file << "\n  SGX-only row values (all " << comparison.sgx_only.size() << " rows):" << std::endl;
                    for (size_t i = 0; i < comparison.sgx_only.size(); i++) {
                        summary_file << "    " << comparison.sgx_only[i] << std::endl;
                    }
                }
                if (!comparison.sqlite_only.empty()) {
                    summary_file << "\n  SQLite-only row values (all " << comparison.sqlite_only.size() << " rows):" << std::endl;
                    for (size_t i = 0; i < comparison.sqlite_only.size(); i++) {
                        summary_file << "    " << comparison.sqlite_only[i] << std::endl;
                    }
                }
            }
            
            summary_file << "\n=== Performance ===" << std::endl;
            summary_file << "SGX Time: " << sgx_time << " seconds" << std::endl;
            summary_file << "SQLite Time: " << sqlite_time << " seconds" << std::endl;
            
            summary_file.close();
            std::cout << "\nSummary written to: " << summary_filename << std::endl;
        } else {
            std::cerr << "Warning: Could not write summary file: " << summary_filename << std::endl;
        }
        
        // Cleanup
        destroy_enclave();
        
        return comparison.are_equivalent ? 0 : 1;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        destroy_enclave();
        return 1;
    }
}