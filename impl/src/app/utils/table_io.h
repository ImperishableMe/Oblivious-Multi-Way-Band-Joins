#ifndef TABLE_IO_H
#define TABLE_IO_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <fstream>
#include "../types.h"
#include "../crypto_utils.h"
#include "sgx_urts.h"

/**
 * TableIO Class
 * 
 * Handles loading and saving tables in various formats:
 * - CSV: Plain text format for initial data (TPC-H tables)
 * - Encrypted binary: Pre-encrypted format for production use
 * 
 * CSV Format:
 * - First row contains column headers
 * - Subsequent rows contain data values
 * - All values are numeric (as seen in the TPC-H data)
 * 
 * Encrypted Binary Format:
 * - Header with metadata (magic, version, counts, table name)
 * - Array of encrypted entry_t structures
 * - Can be loaded directly without re-encryption
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
    
    // Encrypted CSV Operations
    
    /**
     * Save a table as encrypted CSV using secure enclave key
     * The encryption key is stored securely inside the enclave
     * @param table Table to encrypt and save
     * @param filepath Output CSV file path
     * @param eid Enclave ID for encryption
     */
    static void save_encrypted_csv_secure(const Table& table, 
                                         const std::string& filepath,
                                         sgx_enclave_id_t eid);
    
    // Legacy XOR encryption with key parameter has been removed.
    // Use save_encrypted_csv_secure() instead.
    
    /**
     * Load a table from encrypted CSV format
     * Just loads the encrypted integer values and sets is_encrypted flag
     * No decryption happens here - that's done later when needed
     * @param filepath Input CSV file path
     * @return Table with encrypted entries (is_encrypted = true)
     */
    static Table load_encrypted_csv(const std::string& filepath);
    
    // Batch Operations
    /**
     * Load all CSV files from a directory
     * @param dir_path Directory containing CSV files
     * @return Map of table name to Table object
     */
    static std::unordered_map<std::string, Table> 
        load_csv_directory(const std::string& dir_path);
    
    /**
     * Load all tables from a directory (plain CSV or encrypted CSV)
     * @param dir_path Directory path
     * @param encrypted Whether files are encrypted CSVs
     * @return Map of table name to Table object
     */
    static std::unordered_map<std::string, Table> 
        load_tables_from_directory(const std::string& dir_path,
                                  bool encrypted = false);
    
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