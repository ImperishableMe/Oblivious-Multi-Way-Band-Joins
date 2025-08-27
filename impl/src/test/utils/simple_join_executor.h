#ifndef SIMPLE_JOIN_EXECUTOR_H
#define SIMPLE_JOIN_EXECUTOR_H

#include <memory>
#include <vector>
#include <string>
#include "../../app/types.h"
#include "../../app/utils/join_tree_node.h"
#include "../../app/utils/join_constraint.h"
#include "../../app/crypto_utils.h"
#include "sgx_urts.h"

/**
 * SimpleJoinExecutor - Non-oblivious join executor for testing
 * 
 * Implements a straightforward nested-loop join algorithm that:
 * - Traverses the join tree bottom-up
 * - Joins tables based on constraints
 * - Decrypts data freely (for testing only)
 * - Produces the correct join result
 * 
 * This is used as a reference implementation to validate the
 * correctness of the oblivious join algorithm.
 */
class SimpleJoinExecutor {
private:
    /**
     * Check if two entries satisfy a join constraint
     * Handles both equality and band joins
     * 
     * @param left Entry from left/parent table
     * @param right Entry from right/child table
     * @param constraint Join constraint (from child's perspective)
     * @param left_col Column name in left table
     * @param right_col Column name in right table
     * @return true if entries satisfy the constraint
     */
    bool satisfies_constraint(
        const Entry& left,
        const Entry& right,
        const JoinConstraint& constraint,
        const std::string& left_col,
        const std::string& right_col);
    
    /**
     * Join two tables using nested loop join
     * 
     * @param left Left/parent table
     * @param right Right/child table  
     * @param constraint Join constraint
     * @return Joined result table
     */
    Table join_tables(
        const Table& left,
        const Table& right,
        const JoinConstraint& constraint);
    
    /**
     * Concatenate two entries into a single joined entry
     * 
     * @param left Left entry
     * @param right Right entry
     * @param left_table_name Name of left table (for prefixing columns)
     * @param right_table_name Name of right table (for prefixing columns)
     * @return Combined entry
     */
    Entry concatenate_entries(
        const Entry& left,
        const Entry& right,
        const std::string& left_table_name,
        const std::string& right_table_name);
    
    /**
     * Get value of a specific column from an entry
     * Returns 0 if column not found
     */
    int32_t get_column_value(const Entry& entry, const std::string& column_name);
    
    /**
     * Decrypt an entry if encrypted (for testing)
     */
    Entry decrypt_if_needed(const Entry& entry);
    
    sgx_enclave_id_t enclave_id;  // SGX enclave ID for decryption
    
public:
    /**
     * Constructor with optional enclave ID
     */
    SimpleJoinExecutor(sgx_enclave_id_t eid = 0) : enclave_id(eid), should_decrypt(true) {}
    
    /**
     * Execute join tree and return result table
     * 
     * @param root Root node of join tree
     * @return Final joined table
     */
    Table execute_join_tree(JoinTreeNodePtr root);
    
    /**
     * Execute join for a subtree rooted at node
     * 
     * @param node Root of subtree to join
     * @return Joined result for subtree
     */
    Table join_subtree(JoinTreeNodePtr node);
    
    /**
     * Set whether to decrypt entries before joining
     * Default is true for testing
     */
    void set_decrypt_mode(bool decrypt) { should_decrypt = decrypt; }
    
    /**
     * Set enclave ID for decryption
     */
    void set_enclave_id(sgx_enclave_id_t eid) { enclave_id = eid; }
    
private:
    bool should_decrypt;  // Decrypt by default for testing (set in constructor)
};

#endif // SIMPLE_JOIN_EXECUTOR_H