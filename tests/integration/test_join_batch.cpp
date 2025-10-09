/**
 * test_join_batch.cpp
 * 
 * Batch test runner that reads test configurations from a file and generates
 * a summary table with runtime, ecalls, ocalls, and correctness metrics.
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <algorithm>
#include <unistd.h>
#include "app/batch/ecall_wrapper.h"
#include "sgx_compat/sgx_urts.h"
#include "file_io/table_io.h"
#include "crypto/crypto_utils.h"
#include "sgx_compat/Enclave_u.h"

struct TestConfig {
    std::string query_file;
    std::string data_dir;
    std::string query_name;  // Extracted from query_file
    std::string scale_factor; // Extracted from data_dir
};

struct TestResult {
    bool success;
    bool correct;
    size_t output_size;
    double runtime_seconds;
    size_t total_ecalls;
    size_t total_ocalls;
    std::string error_message;
};

/* Initialize the enclave */
sgx_enclave_id_t global_eid = 0;

int initialize_enclave() {
    sgx_status_t ret = sgx_create_enclave("enclave.signed.so", SGX_DEBUG_FLAG, NULL, NULL, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
        std::cerr << "Failed to create enclave, error code: 0x" << std::hex << ret << std::endl;
        return -1;
    }
    return 0;
}

void destroy_enclave() {
    if (global_eid != 0) {
        sgx_destroy_enclave(global_eid);
    }
}

/* Parse configuration file */
std::vector<TestConfig> parse_config_file(const std::string& config_file) {
    std::vector<TestConfig> configs;
    std::ifstream file(config_file);
    
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + config_file);
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;
        
        // Parse comma-separated values
        std::stringstream ss(line);
        std::string query_file, data_dir;
        
        if (std::getline(ss, query_file, ',') && std::getline(ss, data_dir)) {
            TestConfig config;
            config.query_file = query_file;
            config.data_dir = data_dir;
            
            // Extract query name (e.g., "tpch_tb1" from "input/queries/tpch_tb1.sql")
            size_t last_slash = query_file.find_last_of("/\\");
            std::string basename = (last_slash != std::string::npos) ? 
                query_file.substr(last_slash + 1) : query_file;
            if (basename.size() > 4 && basename.substr(basename.size() - 4) == ".sql") {
                config.query_name = basename.substr(0, basename.size() - 4);
            } else {
                config.query_name = basename;
            }
            
            // Extract scale factor (e.g., "0.001" from "input/encrypted/data_0_001")
            last_slash = data_dir.find_last_of("/\\");
            std::string data_basename = (last_slash != std::string::npos) ? 
                data_dir.substr(last_slash + 1) : data_dir;
            
            // Convert data_0_001 to 0.001, data_0_01 to 0.01, etc.
            if (data_basename.find("data_") == 0) {
                std::string scale = data_basename.substr(5); // Remove "data_"
                std::replace(scale.begin(), scale.end(), '_', '.');
                config.scale_factor = scale;
            } else {
                config.scale_factor = data_basename;
            }
            
            configs.push_back(config);
        }
    }
    
    file.close();
    return configs;
}

/* Convert table to comparable format (sorted rows as strings) */
std::multiset<std::string> table_to_multiset(const Table& table) {
    std::multiset<std::string> result;
    
    // Get table schema for column names
    std::vector<std::string> schema = table.get_schema();
    if (schema.empty()) {
        throw std::runtime_error("Table has no schema set - cannot compare tables");
    }
    
    for (const auto& entry : table) {
        // Create pairs of (column_name, value) and sort by column name
        std::vector<std::pair<std::string, int32_t>> column_value_pairs;
        
        for (size_t i = 0; i < schema.size() && i < MAX_ATTRIBUTES; i++) {
            column_value_pairs.emplace_back(schema[i], entry.attributes[i]);
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
bool compare_tables(const Table& sgx_table, const Table& sqlite_table) {
    // Convert to multisets for comparison
    auto sgx_set = table_to_multiset(sgx_table);
    auto sqlite_set = table_to_multiset(sqlite_table);
    
    // Check equivalence
    return sgx_set == sqlite_set;
}

/* Run a single test */
TestResult run_test(const TestConfig& config) {
    TestResult result;
    result.success = false;
    result.correct = false;
    result.output_size = 0;
    result.runtime_seconds = 0.0;
    result.total_ecalls = 0;
    result.total_ocalls = 0;
    
    try {
        // Prepare temporary output files
        std::string sgx_output = "/tmp/test_sgx_" + std::to_string(getpid()) + ".csv";
        std::string sqlite_output = "/tmp/test_sqlite_" + std::to_string(getpid()) + ".csv";
        
        // Run SGX implementation and capture output
        std::string sgx_cmd = "./sgx_app " + config.query_file + " " + config.data_dir + " " + sgx_output + " 2>&1";
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Use popen to capture output
        FILE* pipe = popen(sgx_cmd.c_str(), "r");
        if (!pipe) {
            result.error_message = "Failed to run SGX command";
            std::remove(sgx_output.c_str());
            return result;
        }
        
        // Read output and parse ecall/ocall counts
        char buffer[256];
        std::string sgx_output_str;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            sgx_output_str += buffer;
            
            // Parse ECALL_COUNT
            if (strncmp(buffer, "ECALL_COUNT: ", 13) == 0) {
                result.total_ecalls = std::stoul(buffer + 13);
            }
            // Parse OCALL_COUNT
            else if (strncmp(buffer, "OCALL_COUNT: ", 13) == 0) {
                result.total_ocalls = std::stoul(buffer + 13);
            }
        }
        
        int ret = pclose(pipe);
        auto end_time = std::chrono::high_resolution_clock::now();
        
        if (ret != 0) {
            result.error_message = "SGX execution failed";
            std::remove(sgx_output.c_str());
            std::remove(sqlite_output.c_str());
            return result;
        }
        
        std::chrono::duration<double> diff = end_time - start_time;
        result.runtime_seconds = diff.count();
        
        // Load SGX output table
        Table sgx_table = TableIO::load_csv(sgx_output);
        result.output_size = sgx_table.size();
        
        // Run SQLite baseline for correctness check
        std::string sqlite_cmd = "./sqlite_baseline " + config.query_file + " " + 
                                config.data_dir + " " + sqlite_output + " 2>&1";
        ret = std::system(sqlite_cmd.c_str());
        
        if (ret != 0) {
            result.error_message = "SQLite execution failed";
            result.success = true; // SGX succeeded
            std::remove(sgx_output.c_str());
            std::remove(sqlite_output.c_str());
            return result;
        }
        
        // Load SQLite output table and compare
        Table sqlite_table = TableIO::load_csv(sqlite_output);
        
        // Decrypt both tables for comparison
        for (auto& entry : sgx_table) {
            if (entry.is_encrypted) {
                CryptoUtils::decrypt_entry(const_cast<Entry&>(entry), global_eid);
            }
        }
        for (auto& entry : sqlite_table) {
            if (entry.is_encrypted) {
                CryptoUtils::decrypt_entry(const_cast<Entry&>(entry), global_eid);
            }
        }
        
        // Compare tables as multisets
        result.correct = compare_tables(sgx_table, sqlite_table);
        
        // Cleanup
        std::remove(sgx_output.c_str());
        std::remove(sqlite_output.c_str());
        
        result.success = true;
        
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }
    
    return result;
}

/* Format number with commas for readability */
std::string format_number(size_t n) {
    std::string num = std::to_string(n);
    std::string formatted;
    
    int count = 0;
    for (int i = static_cast<int>(num.length()) - 1; i >= 0; i--) {
        if (count == 3) {
            formatted = "," + formatted;
            count = 0;
        }
        formatted = num[i] + formatted;
        count++;
    }
    
    return formatted;
}

/* Main function */
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        std::cerr << "Config file format: query_file,data_dir (one per line)" << std::endl;
        std::cerr << "Example: input/queries/tpch_tb1.sql,input/encrypted/data_0_001" << std::endl;
        return 1;
    }
    
    try {
        // Parse configuration
        std::string config_file = argv[1];
        std::vector<TestConfig> configs = parse_config_file(config_file);
        if (configs.empty()) {
            std::cerr << "No test configurations found in " << config_file << std::endl;
            return 1;
        }
        
        std::cout << "Found " << configs.size() << " test configurations" << std::endl;
        
        // Generate output filename with timestamp
        // Extract base name from config file
        size_t last_slash = config_file.find_last_of("/\\");
        std::string config_basename = (last_slash != std::string::npos) ? 
            config_file.substr(last_slash + 1) : config_file;
        if (config_basename.find(".txt") != std::string::npos) {
            config_basename = config_basename.substr(0, config_basename.find(".txt"));
        }
        
        // Get current timestamp
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        char timestamp[100];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", std::localtime(&now_time_t));
        
        // Create output filename
        std::string output_dir = "output";
        std::string output_filename = output_dir + "/" + config_basename + "_" + timestamp + ".csv";
        
        // Open output file and write header
        std::ofstream output_file(output_filename);
        if (!output_file.is_open()) {
            std::cerr << "Failed to create output file: " << output_filename << std::endl;
            return 1;
        }
        
        // Write CSV header
        output_file << "Query,Scale Factor,Output Size,Runtime (s),#Ecalls,#Ocalls,Correctness" << std::endl;
        std::cout << "Writing results to: " << output_filename << std::endl;
        
        // Open markdown file and write header
        std::string md_file = output_dir + "/" + config_basename + "_" + timestamp + ".md";
        std::ofstream md_output(md_file);
        if (!md_output.is_open()) {
            std::cerr << "Failed to create markdown file: " << md_file << std::endl;
            return 1;
        }
        md_output << "# Test Results\n\n";
        md_output << "Generated by test_join_batch\n\n";
        md_output << "| Query | Scale Factor | Output Size | Runtime (s) | #Ecalls | #Ocalls | Correctness |\n";
        md_output << "|-------|--------------|-------------|-------------|---------|---------|-------------|\n";
        md_output.flush();
        std::cout << "Writing markdown to: " << md_file << std::endl;
        
        // Initialize enclave once
        if (initialize_enclave() != 0) {
            std::cerr << "Failed to initialize enclave" << std::endl;
            return 1;
        }
        
        // Run all tests
        std::vector<TestResult> results;
        for (size_t i = 0; i < configs.size(); i++) {
            const auto& config = configs[i];
            std::cout << "\nRunning test " << (i + 1) << "/" << configs.size() 
                     << ": " << config.query_name << " @ " << config.scale_factor << std::endl;
            
            TestResult result = run_test(config);
            results.push_back(result);
            
            // Write result immediately to CSV file
            output_file << config.query_name << ","
                       << config.scale_factor << ",";
            
            if (result.success) {
                output_file << result.output_size << ","
                           << std::fixed << std::setprecision(2) << result.runtime_seconds << ","
                           << result.total_ecalls << ","
                           << result.total_ocalls << ","
                           << (result.correct ? "YES" : "NO");
            } else {
                output_file << "FAILED,FAILED,0,0,FAILED";
            }
            output_file << std::endl;
            output_file.flush(); // Ensure data is written immediately
            
            // Also write to markdown file immediately
            std::string query_upper = config.query_name;
            std::transform(query_upper.begin(), query_upper.end(), query_upper.begin(), ::toupper);
            
            md_output << "| " << query_upper << " | " << config.scale_factor << " | ";
            
            if (result.success) {
                md_output << format_number(result.output_size) << " | "
                         << std::fixed << std::setprecision(2) << result.runtime_seconds << " | "
                         << result.total_ecalls << " | "
                         << result.total_ocalls << " | "
                         << (result.correct ? "✓" : "✗") << " |";
            } else {
                md_output << "- | - | - | - | FAILED |";
            }
            md_output << "\n";
            md_output.flush(); // Ensure markdown is written immediately
            
            if (!result.success) {
                std::cerr << "  FAILED: " << result.error_message << std::endl;
            } else {
                std::cout << "  Output: " << format_number(result.output_size) << " rows" << std::endl;
                std::cout << "  Runtime: " << std::fixed << std::setprecision(2) 
                         << result.runtime_seconds << " s" << std::endl;
                std::cout << "  Correct: " << (result.correct ? "YES" : "NO") << std::endl;
            }
        }
        
        // Close files
        md_output << "\n";
        md_output.close();
        output_file.close();
        
        std::cout << "\nResults saved to: " << output_filename << std::endl;
        std::cout << "Markdown saved to: " << md_file << std::endl;
        
        // Generate summary table
        std::cout << "\n" << std::string(100, '=') << std::endl;
        std::cout << "SUMMARY TABLE" << std::endl;
        std::cout << std::string(100, '=') << std::endl;
        
        // Console output (formatted)
        std::cout << std::left << std::setw(15) << "Query" 
                  << std::setw(15) << "Scale Factor"
                  << std::setw(15) << "Output Size"
                  << std::setw(12) << "Runtime (s)"
                  << std::setw(10) << "#Ecalls"
                  << std::setw(10) << "#Ocalls"
                  << std::setw(12) << "Correctness" << std::endl;
        std::cout << std::string(100, '-') << std::endl;
        
        for (size_t i = 0; i < configs.size(); i++) {
            const auto& config = configs[i];
            const auto& result = results[i];
            
            std::cout << std::left << std::setw(15) << config.query_name
                     << std::setw(15) << config.scale_factor;
            
            if (result.success) {
                std::cout << std::setw(15) << format_number(result.output_size)
                         << std::setw(12) << std::fixed << std::setprecision(2) << result.runtime_seconds
                         << std::setw(10) << result.total_ecalls
                         << std::setw(10) << result.total_ocalls
                         << std::setw(12) << (result.correct ? "YES" : "NO");
            } else {
                std::cout << std::setw(15) << "-"
                         << std::setw(12) << "-"
                         << std::setw(10) << "-"
                         << std::setw(10) << "-"
                         << std::setw(12) << "FAILED";
            }
            std::cout << std::endl;
        }
        
        
        // Cleanup
        destroy_enclave();
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        destroy_enclave();
        return 1;
    }
}