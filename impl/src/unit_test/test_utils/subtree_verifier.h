#ifndef SUBTREE_VERIFIER_H
#define SUBTREE_VERIFIER_H

#include <map>
#include <memory>
#include "../../app/types.h"
#include "../../app/utils/join_tree_node.h"
#include "../../app/crypto_utils.h"
#include "simple_join_executor.h"
#include "sgx_urts.h"

/**
 * SubtreeVerifier Class
 * 
 * Verifies the correctness of the bottom-up phase by comparing
 * computed local multiplicities against ground truth values.
 * 
 * The verifier uses SimpleJoinExecutor to compute the actual join
 * result for each subtree, then counts how many times each original
 * tuple appears in that result. This count should match the local_mult
 * value computed by the bottom-up phase.
 * 
 * Note: This is test code, so it's allowed to decrypt entries to
 * verify correctness. The production algorithm never decrypts.
 */
class SubtreeVerifier {
public:
    /**
     * Verify the entire tree recursively
     * Checks that local_mult values are correct for all nodes
     * 
     * @param node Root of tree/subtree to verify
     * @param eid SGX enclave ID for decryption
     * @return true if all multiplicities are correct
     */
    static bool VerifyFullTree(JoinTreeNodePtr node, sgx_enclave_id_t eid);
    
    /**
     * Compute expected local multiplicities for a node
     * Uses SimpleJoinExecutor to get ground truth
     * 
     * @param node Node to compute multiplicities for
     * @param eid SGX enclave ID
     * @return Map from original_index to expected multiplicity
     */
    static std::map<uint32_t, uint32_t> ComputeExpectedMultiplicities(
        JoinTreeNodePtr node,
        sgx_enclave_id_t eid);
    
    /**
     * Verify local_mult values for a single node
     * 
     * @param node Node to verify
     * @param expected Map of expected multiplicities
     * @param eid SGX enclave ID
     * @param verbose Print detailed output
     * @return true if all values match
     */
    static bool VerifyLocalMultiplicities(
        JoinTreeNodePtr node,
        const std::map<uint32_t, uint32_t>& expected,
        sgx_enclave_id_t eid,
        bool verbose = true);

private:
    /**
     * Get local_mult value from an entry
     * Decrypts if necessary (OK in test code)
     * 
     * @param entry Entry to read from
     * @param eid SGX enclave ID
     * @return local_mult value
     */
    static uint32_t GetLocalMult(Entry entry, sgx_enclave_id_t eid);
    
    /**
     * Get original_index from an entry
     * Decrypts if necessary
     * 
     * @param entry Entry to read from
     * @param eid SGX enclave ID
     * @return original_index value
     */
    static uint32_t GetOriginalIndex(Entry entry, sgx_enclave_id_t eid);
    
    /**
     * Check if a result row matches an original tuple
     * Based on the actual data attributes
     * 
     * @param result_row Row from join result
     * @param original Original tuple from node's table
     * @param table_name Name of the table (for column prefix)
     * @return true if they match
     */
    static bool RowMatchesOriginal(
        const Entry& result_row,
        const Entry& original,
        const std::string& table_name);
};

#endif // SUBTREE_VERIFIER_H