#include "table_io.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <climits>  // For INT32_MAX, INT32_MIN
#include "../converters.h"
#include "../Enclave_u.h"

// For directory operations
#include <dirent.h>
#include <sys/stat.h>

Table TableIO::load_csv(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open CSV file: " + filepath);
    }
    
    Table table(extract_table_name(filepath));
    std::string line;
    std::vector<std::string> headers;
    bool first_line = true;
    int nonce_column_index = -1;  // -1 means no nonce column
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        auto values = parse_csv_line(line);
        
        if (first_line) {
            // First line contains column headers
            headers = values;
            
            // Check if any column is "nonce"
            for (size_t i = 0; i < headers.size(); ++i) {
                if (headers[i] == "nonce") {
                    nonce_column_index = i;
                    break;
                }
            }
            
            // Set num_columns (excluding nonce column if present)
            size_t data_columns = (nonce_column_index >= 0) ? headers.size() - 1 : headers.size();
            table.set_num_columns(data_columns);
            first_line = false;
        } else {
            // Data line - create Entry
            Entry entry;
            
            // Build column names (excluding nonce if present)
            for (size_t i = 0; i < headers.size(); ++i) {
                if (static_cast<int>(i) != nonce_column_index) {
                    entry.column_names.push_back(headers[i]);
                }
            }
            
            // Parse values
            uint64_t nonce_value = 0;
            for (size_t i = 0; i < values.size() && i < headers.size(); ++i) {
                if (static_cast<int>(i) == nonce_column_index) {
                    // This is the nonce column - parse as uint64_t
                    nonce_value = std::stoull(values[i]);
                } else {
                    // Regular data column
                    int32_t val = parse_value(values[i]);
                    entry.attributes.push_back(val);
                    
                    // Set join_attr to the first data column value
                    if (entry.attributes.size() == 1) {
                        entry.join_attr = val;
                    }
                }
            }
            
            // Initialize metadata
            entry.field_type = SOURCE;  // Default type
            entry.equality_type = NONE;
            entry.is_encrypted = (nonce_column_index >= 0);  // Encrypted if nonce column exists
            entry.nonce = nonce_value;
            entry.original_index = table.size();
            entry.local_mult = 1;  // Will be computed during algorithm
            entry.final_mult = 0;
            entry.foreign_sum = 0;
            entry.local_cumsum = 0;
            entry.local_interval = 0;
            entry.foreign_cumsum = 0;
            entry.foreign_interval = 0;
            entry.local_weight = 0;
            entry.copy_index = 0;
            entry.alignment_key = 0;
            
            table.add_entry(entry);
        }
    }
    
    file.close();
    return table;
}

void TableIO::save_csv(const Table& table, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot create CSV file: " + filepath);
    }
    
    // Write headers (from first entry if available)
    if (table.size() > 0) {
        const auto& first_entry = table.get_entry(0);
        for (size_t i = 0; i < first_entry.column_names.size(); ++i) {
            if (i > 0) file << ",";
            file << first_entry.column_names[i];
        }
        file << "\n";
        
        // Write data
        for (size_t row = 0; row < table.size(); ++row) {
            const auto& entry = table.get_entry(row);
            for (size_t col = 0; col < entry.attributes.size(); ++col) {
                if (col > 0) file << ",";
                file << static_cast<int64_t>(entry.attributes[col]);
            }
            file << "\n";
        }
    }
    
    file.close();
}

void TableIO::save_encrypted_csv(const Table& table, 
                                 const std::string& filepath,
                                 sgx_enclave_id_t eid) {
    // Work with a copy of the table to allow encryption if needed
    Table table_copy = table;
    
    // Only encrypt entries that aren't already encrypted
    for (size_t i = 0; i < table_copy.size(); i++) {
        Entry& entry = table_copy.get_entry(i);
        if (!entry.is_encrypted) {
            crypto_status_t ret = CryptoUtils::encrypt_entry(entry, eid);
            if (ret != CRYPTO_SUCCESS) {
                throw std::runtime_error("Encryption failed at entry " + std::to_string(i));
            }
        }
    }
    
    // Now convert to entry_t vector for writing
    std::vector<entry_t> entries = table_copy.to_entry_t_vector();
    
    // Now write as CSV with encrypted values
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot create encrypted CSV file: " + filepath);
    }
    
    // Write headers (from first entry if available)
    if (entries.size() > 0) {
        // Column names are not encrypted
        for (size_t i = 0; i < MAX_ATTRIBUTES; ++i) {
            if (entries[0].column_names[i][0] != '\0') {
                if (i > 0) file << ",";
                file << entries[0].column_names[i];
            } else {
                break;
            }
        }
        // Add nonce column header
        file << ",nonce\n";
        
        // Write encrypted data as integers with nonce
        for (const auto& entry : entries) {
            bool first = true;
            for (size_t i = 0; i < MAX_ATTRIBUTES; ++i) {
                if (entry.column_names[i][0] != '\0') {
                    if (!first) file << ",";
                    // Write the encrypted integer value directly
                    // Cast to int32_t since that's what the encrypted values are
                    file << static_cast<int32_t>(entry.attributes[i]);
                    first = false;
                } else {
                    break;
                }
            }
            // Add the nonce value
            file << "," << entry.nonce << "\n";
        }
    }
    
    file.close();
}

// load_encrypted_csv has been deprecated
// Use load_csv() instead - it auto-detects encryption by checking for nonce column

std::unordered_map<std::string, Table> 
TableIO::load_csv_directory(const std::string& dir_path) {
    std::unordered_map<std::string, Table> tables;
    
    DIR* dir = opendir(dir_path.c_str());
    if (dir == nullptr) {
        throw std::runtime_error("Cannot open directory: " + dir_path);
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        
        // Skip . and ..
        if (filename == "." || filename == "..") continue;
        
        // Check if it's a regular file and CSV
        std::string full_path = dir_path + "/" + filename;
        struct stat file_stat;
        if (stat(full_path.c_str(), &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
            if (is_csv_file(filename)) {
                std::string table_name = extract_table_name(filename);
                tables[table_name] = load_csv(full_path);
                std::cout << "Loaded table: " << table_name 
                         << " (" << tables[table_name].size() << " rows)" << std::endl;
            }
        }
    }
    
    closedir(dir);
    return tables;
}

std::unordered_map<std::string, Table> 
TableIO::load_tables_from_directory(const std::string& dir_path,
                                   bool encrypted) {
    if (encrypted) {
        std::unordered_map<std::string, Table> tables;
        
        DIR* dir = opendir(dir_path.c_str());
        if (dir == nullptr) {
            throw std::runtime_error("Cannot open directory: " + dir_path);
        }
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            
            // Skip . and ..
            if (filename == "." || filename == "..") continue;
            
            // Check if it's a regular file and CSV
            std::string full_path = dir_path + "/" + filename;
            struct stat file_stat;
            if (stat(full_path.c_str(), &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
                if (is_csv_file(filename)) {
                    std::string table_name = extract_table_name(filename);
                    // load_csv auto-detects encryption by checking for nonce column
                    tables[table_name] = load_csv(full_path);
                    std::cout << "Loaded CSV table: " << table_name 
                             << " (" << tables[table_name].size() << " rows)" << std::endl;
                }
            }
        }
        
        closedir(dir);
        return tables;
    } else {
        return load_csv_directory(dir_path);
    }
}

bool TableIO::file_exists(const std::string& filepath) {
    struct stat buffer;
    return (stat(filepath.c_str(), &buffer) == 0);
}

std::string TableIO::extract_table_name(const std::string& filepath) {
    // Extract filename from path
    size_t last_slash = filepath.find_last_of("/\\");
    std::string filename = (last_slash != std::string::npos) 
                          ? filepath.substr(last_slash + 1)
                          : filepath;
    
    // Remove extension
    size_t last_dot = filename.find_last_of('.');
    if (last_dot != std::string::npos) {
        filename = filename.substr(0, last_dot);
    }
    
    // Remove any numeric suffixes (e.g., "supplier1" -> "supplier1")
    // Keep them for now as they might be intentional
    return filename;
}

std::vector<std::string> TableIO::parse_csv_line(const std::string& line) {
    std::vector<std::string> values;
    std::stringstream ss(line);
    std::string value;
    
    while (std::getline(ss, value, ',')) {
        // Trim whitespace
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        values.push_back(value);
    }
    
    return values;
}

int32_t TableIO::parse_value(const std::string& str) {
    try {
        // Parse as integer (our data is all integers)
        long long val = std::stoll(str);
        // Clamp to int32_t range
        if (val > INT32_MAX) return INT32_MAX;
        if (val < INT32_MIN) return INT32_MIN;
        return static_cast<int32_t>(val);
    } catch (const std::exception& e) {
        std::cerr << "Warning: Cannot parse value '" << str << "', using 0" << std::endl;
        return 0;
    }
}

bool TableIO::is_csv_file(const std::string& filename) {
    return filename.size() >= 4 && 
           filename.substr(filename.size() - 4) == ".csv";
}