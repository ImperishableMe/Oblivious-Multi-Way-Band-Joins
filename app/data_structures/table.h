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
     * Map: Apply transformation to each entry, returning a new table.
     * Implemented as (Table copy; copy.map_inplace(...)) - one bulk
     * vector deep-copy followed by in-place transform. Prefer
     * map_inplace() at call sites where the source table is no longer
     * needed afterwards.
     */
    Table map(OpEcall op_type, int32_t* params = nullptr) const;

    /**
     * MapInPlace: Apply transformation to each entry in-place.
     * No allocation, no per-entry copy - one pass over entries[].
     */
    void map_inplace(OpEcall op_type, int32_t* params = nullptr);

    /**
     * Reserve capacity for n entries (forwards to underlying vector).
     * Use before a sequence of add_entry / append_transformed to avoid
     * incremental reallocation cost on large builds.
     */
    void reserve(size_t n);

    /**
     * AppendTransformed: Bulk-append src.entries to this table's tail
     * (one vector insert: single allocation + single memcpy), then
     * apply the per-entry transform in-place over the appended range
     * only. Used by CombineTable to fuse three "copy + transform"
     * passes into one without materialising intermediate Tables.
     */
    void append_transformed(const Table& src, OpEcall op_type,
                            int32_t* params = nullptr);

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
     * Phase 1: Pad to next power of two, oblivious Waksman shuffle over the
     *          whole table (switch bits depend only on public n).
     * Phase 2: In-place heap_sort over the shuffled table (info-theoretically
     *          oblivious because input is now a uniformly random permutation
     *          conditioned on public n).
     * Phase 3: Truncate padding (sort_padding entries with JOIN_ATTR_POS_INF
     *          sort to the high end under the join-attr-asc comparators).
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