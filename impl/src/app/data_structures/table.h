#ifndef APP_TABLE_H
#define APP_TABLE_H

#include <vector>
#include <string>
#include <functional>
#include "entry.h"
#include "sgx_urts.h"
#include "../../common/batch_types.h"

/**
 * Table Class - Manages collections of entries for oblivious multi-way band join
 * 
 * Provides oblivious primitives (map, linear_pass, parallel_pass, oblivious_sort)
 * that maintain fixed access patterns for secure computation in SGX enclaves.
 */
class Table {
private:
    std::vector<Entry> entries;
    std::string table_name;
    size_t num_columns;
    
public:
    // Constructors
    Table();
    Table(const std::string& name);
    
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
    
    // Conversion for SGX processing
    std::vector<entry_t> to_entry_t_vector() const;
    void from_entry_t_vector(const std::vector<entry_t>& c_entries);
    
    // Table metadata
    void set_table_name(const std::string& name);
    std::string get_table_name() const;
    void set_num_columns(size_t n);
    size_t get_num_columns() const;
    
    // Iterator support
    std::vector<Entry>::iterator begin();
    std::vector<Entry>::iterator end();
    std::vector<Entry>::const_iterator begin() const;
    std::vector<Entry>::const_iterator end() const;
    
    // Encryption status
    enum EncryptionStatus {
        UNENCRYPTED,  // All entries have is_encrypted = false
        ENCRYPTED,    // All entries have is_encrypted = true
        MIXED         // Entries have different encryption states
    };
    EncryptionStatus get_encryption_status() const;
    
    // Oblivious operations (from thesis Section 4.1.4)
    // These maintain fixed access patterns for security
    
    /**
     * Map: Apply transformation to each entry independently
     * @param eid SGX enclave ID
     * @param transform_func Ecall that transforms one entry
     * @return New table with transformed entries
     */
    Table map(sgx_enclave_id_t eid,
             std::function<sgx_status_t(sgx_enclave_id_t, entry_t*)> transform_func) const;
    
    /**
     * LinearPass: Apply window function to sliding window of size 2
     * @param eid SGX enclave ID
     * @param window_func Ecall that processes window of 2 entries
     */
    void linear_pass(sgx_enclave_id_t eid,
                    std::function<sgx_status_t(sgx_enclave_id_t, entry_t*, entry_t*)> window_func);
    
    /**
     * ParallelPass: Apply function to aligned pairs from two tables
     * @param other Second table (must have same size)
     * @param eid SGX enclave ID
     * @param pair_func Ecall that processes aligned pair
     */
    void parallel_pass(Table& other, sgx_enclave_id_t eid,
                      std::function<sgx_status_t(sgx_enclave_id_t, entry_t*, entry_t*)> pair_func);
    
    /**
     * Distribution pass for variable-distance operations
     * Applies function to pairs of entries at specified distance apart
     * @param eid Enclave ID
     * @param distance Distance between entries to process
     * @param func Function to apply to each pair
     */
    void distribute_pass(sgx_enclave_id_t eid, size_t distance,
                        std::function<void(sgx_enclave_id_t, entry_t*, entry_t*, size_t)> func);
    
    /**
     * ObliviousSort: Sort using bitonic sorting network
     * @param eid SGX enclave ID
     * @param compare_swap_func Ecall that obliviously swaps if needed
     */
    void oblivious_sort(sgx_enclave_id_t eid,
                       std::function<sgx_status_t(sgx_enclave_id_t, entry_t*, entry_t*)> compare_swap_func);
    
    /**
     * ObliviousExpand: Duplicate tuples by multiplicity
     * @param eid SGX enclave ID
     * @return Expanded table
     */
    Table oblivious_expand(sgx_enclave_id_t eid) const;
    
    // ========================================================================
    // Batched Operations - Reduce SGX overhead by batching ecalls
    // ========================================================================
    
    /**
     * BatchedMap: Apply transformation to each entry with batched ecalls
     * @param eid SGX enclave ID
     * @param op_type Operation type for batch dispatcher
     * @param params Optional parameters for operations (can be nullptr)
     * @return New table with transformed entries
     */
    Table batched_map(sgx_enclave_id_t eid, OpEcall op_type, int32_t* params = nullptr) const;
    
    /**
     * BatchedLinearPass: Apply window function with batched ecalls
     * @param eid SGX enclave ID
     * @param op_type Operation type for batch dispatcher
     * @param params Optional parameters for operations (can be nullptr)
     */
    void batched_linear_pass(sgx_enclave_id_t eid, OpEcall op_type, int32_t* params = nullptr);
    
    /**
     * BatchedParallelPass: Apply function to aligned pairs with batched ecalls
     * @param other Second table (must have same size)
     * @param eid SGX enclave ID
     * @param op_type Operation type for batch dispatcher
     * @param params Optional parameters for operations (can be nullptr)
     */
    void batched_parallel_pass(Table& other, sgx_enclave_id_t eid, OpEcall op_type, int32_t* params = nullptr);
    
    /**
     * BatchedObliviousSort: Sort using bitonic network with batched ecalls
     * @param eid SGX enclave ID
     * @param op_type Comparator operation type for batch dispatcher
     */
    void batched_oblivious_sort(sgx_enclave_id_t eid, OpEcall op_type);
    
private:
    // Helper methods for oblivious operations
    static void check_sgx_status(sgx_status_t status, const std::string& operation);
    void compare_and_swap(size_t i, size_t j, sgx_enclave_id_t eid,
                         std::function<sgx_status_t(sgx_enclave_id_t, entry_t*, entry_t*)> compare_swap_func);
    static bool is_power_of_two(size_t n);
    static size_t next_power_of_two(size_t n);
};

#endif // APP_TABLE_H