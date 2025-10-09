#include "table_io.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <climits>  // For INT32_MAX, INT32_MIN
#include "converters.h"
#include "io_entry.h"  // For IO_Entry
#include "types_common.h"  // For NULL_VALUE and type constants
#include "debug_util.h"

// For directory operations
#include <dirent.h>
#include <sys/stat.h>

Table TableIO::load_csv(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open CSV file: " + filepath);
    }

    std::string line;
    std::vector<std::string> headers;

    // Read first line to get headers
    if (!std::getline(file, line)) {
        throw std::runtime_error("CSV file is empty: " + filepath);
    }

    // Parse headers from first line
    headers = parse_csv_line(line);

    // Create table with schema
    Table table(extract_table_name(filepath), headers);
    table.set_num_columns(headers.size());

    // Process data lines
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        auto values = parse_csv_line(line);

        // Data line - create IO_Entry for dynamic size handling
        IO_Entry io_entry;

        // Build column names
        io_entry.column_names = headers;

        // Parse values
        for (size_t i = 0; i < values.size() && i < headers.size(); ++i) {
            int32_t val = parse_value(values[i]);
            io_entry.attributes.push_back(val);

            // Set join_attr to the first data column value
            if (io_entry.attributes.size() == 1) {
                io_entry.join_attr = val;
            }
        }

        // Convert IO_Entry to regular Entry with fixed MAX_ATTRIBUTES
        Entry entry = io_entry.to_entry();

        // Metadata is already initialized to 0 by IO_Entry::to_entry()

        table.add_entry(entry);
    }

    file.close();
    return table;
}

void TableIO::save_csv(const Table& table, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot create CSV file: " + filepath);
    }

    // Write headers
    if (table.size() > 0) {
        std::vector<std::string> headers = table.get_schema();

        // Table should always have schema set
        if (headers.empty()) {
            throw std::runtime_error("Table has no schema set");
        }

        for (size_t i = 0; i < headers.size(); ++i) {
            if (i > 0) file << ",";
            file << headers[i];
        }
        file << "\n";

        // Write data
        for (size_t row = 0; row < table.size(); ++row) {
            const auto& entry = table.get_entry(row);
            // Write only non-empty columns
            bool first = true;
            for (size_t col = 0; col < headers.size(); ++col) {
                if (!first) file << ",";
                file << static_cast<int64_t>(entry.attributes[col]);
                first = false;
            }
            file << "\n";
        }
    }

    file.close();
}


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
                Table loaded_table = load_csv(full_path);
                std::cout << "Loaded table: " << table_name 
                         << " (" << loaded_table.size() << " rows)" << std::endl;
                tables.emplace(table_name, std::move(loaded_table));
            }
        }
    }
    
    closedir(dir);
    return tables;
}

std::unordered_map<std::string, Table>
TableIO::load_tables_from_directory(const std::string& dir_path) {
    // This function is now just an alias for load_csv_directory
    return load_csv_directory(dir_path);
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