#ifndef TABLE_IO_H
#define TABLE_IO_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <fstream>
#include "../data_structures/data_structures.h"

/**
 * TableIO Class
 *
 * Handles loading and saving tables in CSV format:
 * - CSV: Plain text format for data (TPC-H tables)
 *
 * CSV Format:
 * - First row contains column headers
 * - Subsequent rows contain data values
 * - All values are numeric (as seen in the TPC-H data)
 */

class TableIO {
public:
    // CSV Operations
    /**
     * Load a CSV file into a Table object
     * @param filepath Path to the CSV file
     * @return Loaded table with data
     */
    static Table load_csv(const std::string& filepath);
    
    /**
     * Save a Table to CSV format
     * @param table Table to save
     * @param filepath Output CSV file path
     */
    static void save_csv(const Table& table, const std::string& filepath);
    
    // Batch Operations
    /**
     * Load all CSV files from a directory
     * @param dir_path Directory containing CSV files
     * @return Map of table name to Table object
     */
    static std::unordered_map<std::string, Table> 
        load_csv_directory(const std::string& dir_path);
    
    /**
     * Load all tables from a directory
     * @param dir_path Directory path
     * @return Map of table name to Table object
     */
    static std::unordered_map<std::string, Table>
        load_tables_from_directory(const std::string& dir_path);
    
    // Utility Functions
    /**
     * Check if a file exists
     * @param filepath File path to check
     * @return true if file exists
     */
    static bool file_exists(const std::string& filepath);
    
    /**
     * Get table name from file path (removes path and extension)
     * @param filepath File path
     * @return Table name
     */
    static std::string extract_table_name(const std::string& filepath);
    
private:
    // Helper functions
    static std::vector<std::string> parse_csv_line(const std::string& line);
    static int32_t parse_value(const std::string& str);
    static bool is_csv_file(const std::string& filename);
};

#endif // TABLE_IO_H