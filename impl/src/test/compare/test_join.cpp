#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include "../../app/data_structures/types.h"
#include "../../app/io/table_io.h"
#include "../../app/crypto/crypto_utils.h"
#include "sgx_urts.h"
#include "../../app/Enclave_u.h"
#include "../../common/debug_util.h"

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
    
    // Enclave initialized
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

/* Structure to hold phase timing information */
struct PhaseTimings {
    double bottom_up;
    double top_down;
    double distribute_expand;
    double align_concat;
    double total;
    bool valid;
    
    PhaseTimings() : bottom_up(0), top_down(0), distribute_expand(0), 
                     align_concat(0), total(0), valid(false) {}
};

/* Structure to hold phase ecall counts */
struct PhaseEcalls {
    size_t bottom_up;
    size_t top_down;
    size_t distribute_expand;
    size_t align_concat;
    size_t total;
    bool valid;
    
    PhaseEcalls() : bottom_up(0), top_down(0), distribute_expand(0),
                    align_concat(0), total(0), valid(false) {}
};

/* Structure to hold phase sizes */
struct PhaseSizes {
    size_t bottom_up;
    size_t top_down;
    size_t distribute_expand;
    size_t align_concat;
    bool valid;
    
    PhaseSizes() : bottom_up(0), top_down(0), distribute_expand(0),
                   align_concat(0), valid(false) {}
};

/* Structure to hold align-concat sorting metrics */
struct AlignConcatSortMetrics {
    double total_time;
    size_t total_ecalls;
    double accumulator_time;
    size_t accumulator_ecalls;
    double child_time;
    size_t child_ecalls;
    bool valid;
    
    AlignConcatSortMetrics() : total_time(0), total_ecalls(0),
                               accumulator_time(0), accumulator_ecalls(0),
                               child_time(0), child_ecalls(0), valid(false) {}
};

/* Run a command and capture output with timing */
struct CommandResult {
    double wall_time;
    PhaseTimings phase_timings;
    PhaseEcalls phase_ecalls;
    PhaseSizes phase_sizes;
    AlignConcatSortMetrics sort_metrics;
    std::string output;
};

CommandResult run_command_with_output(const std::string& command) {
    CommandResult result;
    
    // Use popen to capture output
    auto start = std::chrono::high_resolution_clock::now();
    
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to run command: " + command);
    }
    
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.output += buffer;
        
        // Parse phase timing if present
        if (strncmp(buffer, "PHASE_TIMING:", 13) == 0) {
            // Parse the timing line
            double bu, td, de, ac, tot;
            if (sscanf(buffer + 13, " Bottom-Up=%lf Top-Down=%lf Distribute-Expand=%lf Align-Concat=%lf Total=%lf",
                       &bu, &td, &de, &ac, &tot) == 5) {
                result.phase_timings.bottom_up = bu;
                result.phase_timings.top_down = td;
                result.phase_timings.distribute_expand = de;
                result.phase_timings.align_concat = ac;
                result.phase_timings.total = tot;
                result.phase_timings.valid = true;
            }
        }
        // Parse phase ecalls if present
        else if (strncmp(buffer, "PHASE_ECALLS:", 13) == 0) {
            size_t bu, td, de, ac, tot;
            if (sscanf(buffer + 13, " Bottom-Up=%zu Top-Down=%zu Distribute-Expand=%zu Align-Concat=%zu Total=%zu",
                       &bu, &td, &de, &ac, &tot) == 5) {
                result.phase_ecalls.bottom_up = bu;
                result.phase_ecalls.top_down = td;
                result.phase_ecalls.distribute_expand = de;
                result.phase_ecalls.align_concat = ac;
                result.phase_ecalls.total = tot;
                result.phase_ecalls.valid = true;
            }
        }
        // Parse phase sizes if present
        else if (strncmp(buffer, "PHASE_SIZES:", 12) == 0) {
            size_t bu, td, de, ac;
            if (sscanf(buffer + 12, " Bottom-Up=%zu Top-Down=%zu Distribute-Expand=%zu Align-Concat=%zu",
                       &bu, &td, &de, &ac) == 4) {
                result.phase_sizes.bottom_up = bu;
                result.phase_sizes.top_down = td;
                result.phase_sizes.distribute_expand = de;
                result.phase_sizes.align_concat = ac;
                result.phase_sizes.valid = true;
            }
        } else if (strncmp(buffer, "ALIGN_CONCAT_SORTS:", 19) == 0) {
            // Parse sorting metrics
            double total_time;
            size_t total_ecalls;
            double acc_time;
            size_t acc_ecalls;
            double child_time;
            size_t child_ecalls;
            
            // Look for pattern: Total=XXXs (YYY ecalls), Accumulator=XXXs (YYY ecalls), Child=XXXs (YYY ecalls)
            if (sscanf(buffer + 19, " Total=%lfs (%zu ecalls), Accumulator=%lfs (%zu ecalls), Child=%lfs (%zu ecalls)",
                       &total_time, &total_ecalls, &acc_time, &acc_ecalls, &child_time, &child_ecalls) == 6) {
                result.sort_metrics.total_time = total_time;
                result.sort_metrics.total_ecalls = total_ecalls;
                result.sort_metrics.accumulator_time = acc_time;
                result.sort_metrics.accumulator_ecalls = acc_ecalls;
                result.sort_metrics.child_time = child_time;
                result.sort_metrics.child_ecalls = child_ecalls;
                result.sort_metrics.valid = true;
            }
        }
    }
    
    int ret = pclose(pipe);
    auto end = std::chrono::high_resolution_clock::now();
    
    if (ret != 0) {
        throw std::runtime_error("Command failed: " + command);
    }
    
    std::chrono::duration<double> diff = end - start;
    result.wall_time = diff.count();
    
    return result;
}

/* Run a command and measure time */
double run_timed_command(const std::string& command) {
    // Executing command
    
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
    
    // Starting test comparison
    
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
        // Running SGX
        std::string sgx_cmd = "/home/r33wei/omwj/memory_const/impl/src/sgx_app " + sql_file + " " + data_dir + " " + sgx_output + " 2>&1";
        CommandResult sgx_result = run_command_with_output(sgx_cmd);
        // SGX done
        
        // Run SQLite baseline
        // Running SQLite
        std::string sqlite_cmd = "/home/r33wei/omwj/memory_const/impl/src/test/sqlite_baseline " + sql_file + " " + data_dir + " " + sqlite_output;
        double sqlite_time = run_timed_command(sqlite_cmd);
        // SQLite done
        
        // Load and decrypt results
        // Comparing results
        Table sgx_encrypted = TableIO::load_csv(sgx_output);
        Table sgx_decrypted = decrypt_table(sgx_encrypted);
        
        Table sqlite_encrypted = TableIO::load_csv(sqlite_output);
        Table sqlite_result = decrypt_table(sqlite_encrypted);
        
        // Compare results
        ComparisonResult comparison = compare_tables(sgx_decrypted, sqlite_result);
        
        // Print minimal output
        printf("Output: SGX=%zu rows, SQLite=%zu rows\n", comparison.sgx_rows, comparison.sqlite_rows);
        
        printf("Match: %s\n", comparison.are_equivalent ? "YES" : "NO");
        
        // Performance comparison
        printf("Time: SGX=%.6fs, SQLite=%.6fs\n", sgx_result.wall_time, sqlite_time);
        
        // Print phase timings if available
        if (sgx_result.phase_timings.valid) {
            printf("Phase Timings:\n");
            printf("  Bottom-Up: %.6fs\n", sgx_result.phase_timings.bottom_up);
            printf("  Top-Down: %.6fs\n", sgx_result.phase_timings.top_down);
            printf("  Distribute-Expand: %.6fs\n", sgx_result.phase_timings.distribute_expand);
            printf("  Align-Concat: %.6fs\n", sgx_result.phase_timings.align_concat);
            printf("  Total (phases): %.6fs\n", sgx_result.phase_timings.total);
        }
        
        // Print phase ecalls if available
        if (sgx_result.phase_ecalls.valid) {
            printf("Phase Ecalls:\n");
            printf("  Bottom-Up: %zu ecalls\n", sgx_result.phase_ecalls.bottom_up);
            printf("  Top-Down: %zu ecalls\n", sgx_result.phase_ecalls.top_down);
            printf("  Distribute-Expand: %zu ecalls\n", sgx_result.phase_ecalls.distribute_expand);
            printf("  Align-Concat: %zu ecalls\n", sgx_result.phase_ecalls.align_concat);
            printf("  Total: %zu ecalls\n", sgx_result.phase_ecalls.total);
        }
        
        // Print phase sizes if available
        if (sgx_result.phase_sizes.valid) {
            printf("Phase Sizes (total rows in tree):\n");
            printf("  Bottom-Up: %zu rows\n", sgx_result.phase_sizes.bottom_up);
            printf("  Top-Down: %zu rows\n", sgx_result.phase_sizes.top_down);
            printf("  Distribute-Expand: %zu rows\n", sgx_result.phase_sizes.distribute_expand);
            printf("  Align-Concat (result): %zu rows\n", sgx_result.phase_sizes.align_concat);
        }
        
        // Display sorting metrics if available
        if (sgx_result.sort_metrics.valid) {
            printf("Align-Concat Sorting Details:\n");
            printf("  Total sorting: %.6fs (%zu ecalls)\n", 
                   sgx_result.sort_metrics.total_time, sgx_result.sort_metrics.total_ecalls);
            printf("    - Accumulator sorts: %.6fs (%zu ecalls)\n",
                   sgx_result.sort_metrics.accumulator_time, sgx_result.sort_metrics.accumulator_ecalls);
            printf("    - Child sorts: %.6fs (%zu ecalls)\n",
                   sgx_result.sort_metrics.child_time, sgx_result.sort_metrics.child_ecalls);
        }
        
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
            summary_file << "SGX Time: " << sgx_result.wall_time << " seconds" << std::endl;
            summary_file << "SQLite Time: " << sqlite_time << " seconds" << std::endl;
            
            if (sgx_result.phase_timings.valid) {
                summary_file << "\n=== SGX Phase Timings ===" << std::endl;
                summary_file << "Bottom-Up: " << sgx_result.phase_timings.bottom_up << " seconds" << std::endl;
                summary_file << "Top-Down: " << sgx_result.phase_timings.top_down << " seconds" << std::endl;
                summary_file << "Distribute-Expand: " << sgx_result.phase_timings.distribute_expand << " seconds" << std::endl;
                summary_file << "Align-Concat: " << sgx_result.phase_timings.align_concat << " seconds" << std::endl;
                summary_file << "Total (phases): " << sgx_result.phase_timings.total << " seconds" << std::endl;
            }
            
            if (sgx_result.phase_ecalls.valid) {
                summary_file << "\n=== SGX Phase Ecalls ===" << std::endl;
                summary_file << "Bottom-Up: " << sgx_result.phase_ecalls.bottom_up << " ecalls" << std::endl;
                summary_file << "Top-Down: " << sgx_result.phase_ecalls.top_down << " ecalls" << std::endl;
                summary_file << "Distribute-Expand: " << sgx_result.phase_ecalls.distribute_expand << " ecalls" << std::endl;
                summary_file << "Align-Concat: " << sgx_result.phase_ecalls.align_concat << " ecalls" << std::endl;
                summary_file << "Total: " << sgx_result.phase_ecalls.total << " ecalls" << std::endl;
            }
            
            if (sgx_result.phase_sizes.valid) {
                summary_file << "\n=== SGX Phase Sizes ===" << std::endl;
                summary_file << "Bottom-Up: " << sgx_result.phase_sizes.bottom_up << " rows in tree" << std::endl;
                summary_file << "Top-Down: " << sgx_result.phase_sizes.top_down << " rows in tree" << std::endl;
                summary_file << "Distribute-Expand: " << sgx_result.phase_sizes.distribute_expand << " rows in tree" << std::endl;
                summary_file << "Align-Concat (result): " << sgx_result.phase_sizes.align_concat << " rows" << std::endl;
            }
            
            if (sgx_result.sort_metrics.valid) {
                summary_file << "\n=== Align-Concat Sorting Details ===" << std::endl;
                summary_file << "Total sorting: " << sgx_result.sort_metrics.total_time << " seconds (" 
                            << sgx_result.sort_metrics.total_ecalls << " ecalls)" << std::endl;
                summary_file << "  - Accumulator sorts: " << sgx_result.sort_metrics.accumulator_time << " seconds (" 
                            << sgx_result.sort_metrics.accumulator_ecalls << " ecalls)" << std::endl;
                summary_file << "  - Child sorts: " << sgx_result.sort_metrics.child_time << " seconds (" 
                            << sgx_result.sort_metrics.child_ecalls << " ecalls)" << std::endl;
            }
            
            summary_file.close();
            // Summary written
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