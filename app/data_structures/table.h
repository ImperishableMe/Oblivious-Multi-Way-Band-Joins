#ifndef APP_TABLE_H
#define APP_TABLE_H

#include <vector>
#include <string>
#include <functional>
#include "../../common/enclave_types.h"
#include "../../common/entry_utils.h"
#include "../../common/op_types.h"

/**
 * Table Class - Manages collections of entries for oblivious multi-way band join
 *
 * Provides batched operations for efficient TDX processing
 * that maintain fixed access patterns for secure computation.
 */
class Table {
private:
    std::vector<Entry> entries;
    std::string table_name;
    size_t num_columns;
    std::vector<std::string> schema_column_names;  // NEW: Table's schema for slim mode
    
public:
    // Constructor - requires schema
    Table(const std::string& name, const std::vector<std::string>& schema);

    // Copy constructor - deep copy for table aliasing
    Table(const Table& other);

    // Entry management
    void add_entry(const Entry& entry);
    Entry& get_entry(size_t index);
    const Entry& get_entry(size_t index) const;
    void set_entry(size_t index, const Entry& entry);
    Entry& operator[](size_t index);
    const Entry& operator[](size_t index) const;
    size_t size() const;
    void clear();
    
    // Batch operations
    void set_all_field_type(entry_type_t type);
    void initialize_original_indices();
    void initialize_leaf_multiplicities();
    
    // Table metadata
    void set_table_name(const std::string& name);
    std::string get_table_name() const;
    void set_num_columns(size_t n);
    size_t get_num_columns() const;
    
    // Schema management (for slim mode migration)
    void set_schema(const std::vector<std::string>& columns);
    std::vector<std::string> get_schema() const;
    size_t get_column_index(const std::string& col_name) const;
    bool has_column(const std::string& col_name) const;
    
    // Generate generic schema for combined tables
    static std::vector<std::string> generate_generic_schema(size_t num_columns);
    
    // Named attribute access using schema
    int32_t get_attribute(size_t row, const std::string& col_name) const;
    void set_attribute(size_t row, const std::string& col_name, int32_t value);
    
    // Iterator support
    std::vector<Entry>::iterator begin();
    std::vector<Entry>::iterator end();
    std::vector<Entry>::const_iterator begin() const;
    std::vector<Entry>::const_iterator end() const;
    
    // Encryption status (TDX: always unencrypted)
    enum EncryptionStatus { UNENCRYPTED, ENCRYPTED, MIXED };
    EncryptionStatus get_encryption_status() const;
    
    // Note: Non-batched operations have been removed in favor of batched versions
    // which are more efficient for SGX by reducing ecall overhead
    
    // ========================================================================
    // Direct Operations (TDX - no batching needed)
    // ========================================================================

    /**
     * Map: Apply transformation to each entry
     * @param op_type Operation type for core function
     * @param params Optional parameters for operations (can be nullptr)
     * @return New table with transformed entries
     */
    Table map(OpEcall op_type, int32_t* params = nullptr) const;

    /**
     * LinearPass: Apply window function to adjacent pairs
     * @param op_type Operation type for core function
     * @param params Optional parameters for operations (can be nullptr)
     */
    void linear_pass(OpEcall op_type, int32_t* params = nullptr);

    /**
     * ParallelPass: Apply function to aligned pairs from two tables
     * @param other Second table (must have same size)
     * @param op_type Operation type for core function
     * @param params Optional parameters for operations (can be nullptr)
     */
    void parallel_pass(Table& other, OpEcall op_type, int32_t* params = nullptr);


    /**
     * ShuffleMergeSort: Two-phase oblivious sort
     * Phase 1: Oblivious shuffle using ShuffleManager (Waksman network)
     * Phase 2: Non-oblivious merge sort using MergeSortManager
     * @param op_type Comparator operation type
     */
    void shuffle_merge_sort(OpEcall op_type);

    /**
     * DistributePass: Process pairs at given distance
     * @param distance Distance between pairs to process
     * @param op_type Operation type for core function
     * @param params Optional parameters for operations (can be nullptr)
     */
    void distribute_pass(size_t distance, OpEcall op_type, int32_t* params = nullptr);

    /**
     * AddPadding: Add multiple padding entries
     * @param count Number of padding entries to add
     * @param padding_op Operation type for padding creation
     */
    void add_padding(size_t count, OpEcall padding_op = OP_ECALL_TRANSFORM_CREATE_DIST_PADDING);

    /**
     * PadToShuffleSize: Pad table to 2^a * k^b format for shuffle operations
     */
    void pad_to_shuffle_size();
    
    /**
     * CalculateShufflePadding: Calculate target size for shuffle (2^a * k^b)
     * @param n Current size
     * @return Target padded size
     */
    static size_t calculate_shuffle_padding(size_t n);
    
    /**
     * IsValidShuffleSize: Check if size is valid 2^a * k^b format
     * @param n Size to check
     * @return true if valid shuffle size
     */
    static bool is_valid_shuffle_size(size_t n);
    
private:
    // Helper to calculate next power of 2
    static size_t next_power_of_two(size_t n) {
        size_t power = 1;
        while (power < n) power *= 2;
        return power;
    }
};

#endif // APP_TABLE_H